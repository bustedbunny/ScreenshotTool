#include "export.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <cstring>
#include <DirectXPackedVector.h>

namespace shot {
namespace {
ComPtr<IWICImagingFactory> wicFactory() {ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Create Windows image codec");return wic;}
std::vector<BYTE> encode(const Image& image,bool hdr) {
    auto wic=wicFactory();ComPtr<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"Create image stream");
    ComPtr<IWICBitmapEncoder> encoder;check(wic->CreateEncoder(hdr?GUID_ContainerFormatWmp:GUID_ContainerFormatPng,nullptr,&encoder),"Create image encoder");
    check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize image encoder");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame,&properties),"Create encoded frame");
    if(hdr) {
        PROPBAG2 option{};option.pstrName=const_cast<wchar_t*>(L"Lossless");VARIANT value{};value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;
        check(properties->Write(1,&option,&value),"Enable lossless JPEG XR");
    }
    check(frame->Initialize(properties.Get()),"Initialize encoded frame");
    check(frame->SetSize(image.width,image.height),"Set image dimensions");check(frame->SetResolution(96,96),"Set image resolution");
    // JPEG XR's 32-bit float representation is not generally bit-lossless. FP16
    // matches desktop duplication and is losslessly supported by the Windows codec.
    WICPixelFormatGUID requested=hdr?GUID_WICPixelFormat64bppRGBAHalf:GUID_WICPixelFormat32bppBGRA;
    auto actual=requested;check(frame->SetPixelFormat(&actual),"Set image pixel format");
    if(actual!=requested)throw std::runtime_error("The Windows codec cannot preserve the requested image format. Repair the Windows image codecs and retry.");
    if(!hdr) {
        ComPtr<IWICMetadataQueryWriter> metadata;check(frame->GetMetadataQueryWriter(&metadata),"Tag PNG as sRGB");
        PROPVARIANT intent{};intent.vt=VT_UI1;intent.bVal=0;
        check(metadata->SetMetadataByName(L"/sRGB/RenderingIntent",&intent),"Write PNG sRGB color space");
    }
    std::vector<BYTE> bgra;
    std::vector<DirectX::PackedVector::HALF> half;
    const BYTE* bytes=reinterpret_cast<const BYTE*>(image.pixels.data());
    UINT stride=image.width*sizeof(Pixel);
    if(!hdr) {bgra=ExportService::toBgra(image);bytes=bgra.data();stride=image.width*4;}
    else {
        half.resize(image.pixels.size()*4);
        for(size_t i=0;i<image.pixels.size();++i) {
            const auto p=image.pixels[i];
            half[i*4]=DirectX::PackedVector::XMConvertFloatToHalf(p.r);
            half[i*4+1]=DirectX::PackedVector::XMConvertFloatToHalf(p.g);
            half[i*4+2]=DirectX::PackedVector::XMConvertFloatToHalf(p.b);
            half[i*4+3]=DirectX::PackedVector::XMConvertFloatToHalf(1);
        }
        bytes=reinterpret_cast<const BYTE*>(half.data());stride=image.width*8;
    }
    const uint64_t byteCount=static_cast<uint64_t>(stride)*image.height;
    if(byteCount>MAXUINT)throw std::runtime_error("The selected image is too large for the Windows image codec.");
    check(frame->WritePixels(image.height,stride,static_cast<UINT>(byteCount),const_cast<BYTE*>(bytes)),"Encode selected pixels");
    check(frame->Commit(),"Finish image frame");check(encoder->Commit(),"Finish image encoding");
    STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME),"Read encoded image size");
    if(stat.cbSize.QuadPart>MAXUINT)throw std::runtime_error("Encoded image is too large.");
    std::vector<BYTE> result(static_cast<size_t>(stat.cbSize.QuadPart));LARGE_INTEGER start{};
    check(stream->Seek(start,STREAM_SEEK_SET,nullptr),"Read encoded image");ULONG read{};
    check(stream->Read(result.data(),static_cast<ULONG>(result.size()),&read),"Read encoded image");
    if(read!=result.size())throw std::runtime_error("The encoded image stream is incomplete.");return result;
}
struct GlobalDelete { void operator()(void* p) const {if(p)GlobalFree(p);} };
using GlobalMemory=std::unique_ptr<void,GlobalDelete>;
GlobalMemory globalCopy(const void* data,size_t size) {
    GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE,size));if(!memory)throw std::bad_alloc();
    void* ptr=GlobalLock(memory.get());if(!ptr)throw std::runtime_error("Cannot allocate clipboard image.");
    std::memcpy(ptr,data,size);GlobalUnlock(memory.get());return memory;
}
}
std::vector<BYTE> ExportService::toBgra(const Image& image) {
    std::vector<BYTE> bytes(image.pixels.size()*4);
    auto channel=[](float linear){return static_cast<BYTE>(std::lround(std::clamp(linearToSrgb(std::isfinite(linear)?linear:0.f),0.f,1.f)*255));};
    for(size_t i=0;i<image.pixels.size();++i) {const auto p=image.pixels[i];bytes[i*4]=channel(p.b);bytes[i*4+1]=channel(p.g);bytes[i*4+2]=channel(p.r);bytes[i*4+3]=255;}
    return bytes;
}
std::vector<BYTE> ExportService::encodePng(const Image& image) {return encode(image,false);}
std::vector<BYTE> ExportService::encodeJxr(const Image& image) {return encode(image,true);}
EncodedImage ExportService::prepare(const DesktopImage& desktop,Rect crop,std::span<const Annotation> annotations,bool includeHdr) {
    Graphics graphics;auto sdr=graphics.render(desktop,crop,annotations,RenderDestination::Sdr);
    EncodedImage result;result.width=sdr.width;result.height=sdr.height;result.bgra=toBgra(sdr);result.png=encodePng(sdr);
    if(includeHdr)result.jxr=encodeJxr(graphics.render(desktop,crop,annotations,RenderDestination::Hdr));return result;
}
std::filesystem::path picturesDirectory() {
    PWSTR raw{};check(SHGetKnownFolderPath(FOLDERID_Pictures,KF_FLAG_DEFAULT,nullptr,&raw),"Locate the Windows Pictures folder");
    std::filesystem::path result(raw);CoTaskMemFree(raw);return result/L"ScreenshotTool";
}
std::wstring currentStem() {SYSTEMTIME now{};GetLocalTime(&now);return timestampStem(now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);}
std::wstring uniqueToken() {GUID guid{};check(CoCreateGuid(&guid),"Generate temporary filename");wchar_t value[40]{};StringFromGUID2(guid,value,40);return value;}
void writeNewFile(const std::filesystem::path& path,std::span<const BYTE> bytes) {
    HANDLE raw=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(raw==INVALID_HANDLE_VALUE)check(HRESULT_FROM_WIN32(GetLastError()),"Create screenshot file. Check the folder's permissions and available space");
    UniqueHandle file(raw);size_t position=0;
    while(position<bytes.size()) {DWORD count{},chunk=static_cast<DWORD>(std::min<size_t>(bytes.size()-position,16*1024*1024));wincheck(WriteFile(file.get(),bytes.data()+position,chunk,&count,nullptr),"Write screenshot file");if(count==0)throw std::runtime_error("The screenshot file could not be fully written.");position+=count;}
    wincheck(FlushFileBuffers(file.get()),"Flush screenshot file to disk");
}
FileOperations nativeFileOperations() {
    return {[](const auto& from,const auto& to){wincheck(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH),"Finalize screenshot filename");},[](const auto& path){std::error_code error;std::filesystem::remove(path,error);if(error)throw std::system_error(error,"Clean up incomplete screenshot");}};
}
std::vector<std::filesystem::path> ExportService::quickSave(const EncodedImage& image,const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);const auto base=currentStem();
    UniqueHandle reservation;std::wstring stem;
    for(int retry=0;retry<100;++retry) {
        stem=availableStem(base,[&](const auto& filename){return std::filesystem::exists(directory/filename);});
        HANDLE file=CreateFileW((directory/(stem+L".lock")).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(file!=INVALID_HANDLE_VALUE){reservation.reset(file);break;}
        if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)check(HRESULT_FROM_WIN32(GetLastError()),"Reserve screenshot filename");
    }
    if(!reservation)throw std::runtime_error("Could not reserve a unique screenshot filename. Retry saving.");
    const auto token=uniqueToken();std::vector<StagedFile> files;
    auto add=[&](std::wstring suffix,const std::vector<BYTE>& bytes) {
        auto final=directory/(stem+suffix);auto temp=directory/(L"."+stem+token+suffix+L".tmp");
        files.push_back({temp,final,[&bytes](const auto& path){writeNewFile(path,bytes);}});
    };
    add(L"_SDR.png",image.png);if(!image.jxr.empty())add(L"_HDR.jxr",image.jxr);
    publishFiles(files,nativeFileOperations());std::vector<std::filesystem::path> saved;for(const auto& f:files)saved.push_back(f.final);return saved;
}
void ExportService::saveAs(const EncodedImage& image,const std::filesystem::path& path) {
    auto temporary=path.parent_path()/(L"."+path.filename().wstring()+uniqueToken()+L".tmp");
    try {
        writeNewFile(temporary,image.png);
        if(std::filesystem::exists(path))wincheck(ReplaceFileW(path.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr),"Replace screenshot file");
        else wincheck(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH),"Finalize screenshot file");
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
std::optional<std::filesystem::path> ExportService::chooseSavePath(HWND owner,const std::filesystem::path& folder) {
    ComPtr<IFileSaveDialog> dialog;check(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Open Save As dialog");
    COMDLG_FILTERSPEC filter{L"PNG image (*.png)",L"*.png"};check(dialog->SetFileTypes(1,&filter),"Set save file type");
    check(dialog->SetDefaultExtension(L"png"),"Set PNG extension");check(dialog->SetFileName((currentStem()+L"_SDR.png").c_str()),"Set screenshot filename");
    DWORD options{};check(dialog->GetOptions(&options),"Read Save As options");check(dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT|FOS_PATHMUSTEXIST|FOS_STRICTFILETYPES),"Configure Save As dialog");
    ComPtr<IShellItem> item;if(SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(),nullptr,IID_PPV_ARGS(&item))))dialog->SetDefaultFolder(item.Get());
    HRESULT hr=dialog->Show(owner);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};check(hr,"Choose screenshot destination");
    check(dialog->GetResult(&item),"Get save destination");PWSTR raw{};check(item->GetDisplayName(SIGDN_FILESYSPATH,&raw),"Get save filename");
    std::filesystem::path path(raw);CoTaskMemFree(raw);return path;
}
void ExportService::copy(HWND owner,const EncodedImage& image) {
    BITMAPV5HEADER header{};header.bV5Size=sizeof(header);header.bV5Width=image.width;header.bV5Height=-image.height;
    header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5SizeImage=static_cast<DWORD>(image.bgra.size());
    header.bV5RedMask=0x00ff0000;header.bV5GreenMask=0x0000ff00;header.bV5BlueMask=0x000000ff;header.bV5AlphaMask=0xff000000;
    header.bV5CSType=LCS_sRGB;header.bV5Intent=LCS_GM_IMAGES;
    std::vector<BYTE> dib5(sizeof(header)+image.bgra.size());std::memcpy(dib5.data(),&header,sizeof(header));std::memcpy(dib5.data()+sizeof(header),image.bgra.data(),image.bgra.size());
    BITMAPINFOHEADER basic{};basic.biSize=sizeof(basic);basic.biWidth=image.width;basic.biHeight=-image.height;basic.biPlanes=1;basic.biBitCount=32;basic.biCompression=BI_RGB;basic.biSizeImage=static_cast<DWORD>(image.bgra.size());
    std::vector<BYTE> dib(sizeof(basic)+image.bgra.size());std::memcpy(dib.data(),&basic,sizeof(basic));std::memcpy(dib.data()+sizeof(basic),image.bgra.data(),image.bgra.size());
    auto v5=globalCopy(dib5.data(),dib5.size()),v3=globalCopy(dib.data(),dib.size()),png=globalCopy(image.png.data(),image.png.size());
    const UINT pngFormat=RegisterClipboardFormatW(L"PNG");wincheck(pngFormat!=0,"Register PNG clipboard format");
    // DIB and DIBV5 are standard bitmap formats; Windows synthesizes CF_BITMAP from DIB.
    wincheck(OpenClipboard(owner),"Clipboard is busy. Close the app using it and retry Copy");
    struct Close{~Close(){CloseClipboard();}}close;
    wincheck(EmptyClipboard(),"Take clipboard ownership");
    auto transfer=[](UINT format,GlobalMemory& memory){if(!SetClipboardData(format,memory.get()))check(HRESULT_FROM_WIN32(GetLastError()),"Place image on clipboard");memory.release();};
    transfer(CF_DIBV5,v5);transfer(CF_DIB,v3);transfer(pngFormat,png);
    if(GetClipboardOwner()!=owner)throw std::runtime_error("Another application took the clipboard. Retry Copy.");
}
}
