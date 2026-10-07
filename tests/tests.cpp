#include "capture.hpp"
#include "capture_frame.hpp"
#include "export.hpp"
#include "text_store.hpp"
#include "overlay.hpp"
#include <commctrl.h>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <fstream>
#include <cstring>
#include <DirectXPackedVector.h>

using namespace shot;
namespace {
int passed{},failed{};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void expectNear(float actual,float expected,float epsilon=0.002f){if(std::abs(actual-expected)>epsilon)throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
template<class F>void test(const char* name,F function){try{function();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
MonitorImage monitor(Rect bounds,bool hdr=false,float white=80) {
    MonitorImage m;m.bounds=bounds;m.hdr=hdr;m.sdrWhiteNits=white;m.image=Image(bounds.width(),bounds.height());return m;
}
ComPtr<IWICBitmapFrameDecode> decode(const std::vector<BYTE>& encoded) {
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create decoder factory");
    ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"Create decoder stream");check(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),static_cast<DWORD>(encoded.size())),"Read encoded memory");
    ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode image");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"Read decoded frame");return frame;
}
Image decodeFloat(const std::vector<BYTE>& encoded) {
    auto frame=decode(encoded);UINT w{},h{};check(frame->GetSize(&w,&h),"Read dimensions");Image image(w,h);
    ComPtr<IWICFormatConverter> converter;ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create converter factory");check(factory->CreateFormatConverter(&converter),"Create converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat128bppRGBAFloat,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Decode floating-point pixels");
    check(converter->CopyPixels(nullptr,w*sizeof(Pixel),static_cast<UINT>(image.pixels.size()*sizeof(Pixel)),reinterpret_cast<BYTE*>(image.pixels.data())),"Read float pixels");return image;
}
std::vector<BYTE> decodeBgra(const std::vector<BYTE>& encoded) {
    auto frame=decode(encoded);UINT w{},h{};frame->GetSize(&w,&h);std::vector<BYTE> pixels(static_cast<size_t>(w)*h*4);
    ComPtr<IWICBitmapSource> converted;check(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA,frame.Get(),&converted),"Decode sRGB pixels");
    check(converted->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()),"Read sRGB pixels");return pixels;
}
Annotation shape(Tool tool,Point a,Point b,Pixel color={1,0,0,1}) {Annotation annotation;annotation.tool=tool;annotation.points={a,b};annotation.color=color;annotation.width=4;return annotation;}
struct CaptureStep {
    HRESULT result{S_OK};
    LONGLONG presented{};
    Pixel pixel{0,0,0,1};
    bool cancel{},protectedContent{};
    std::chrono::milliseconds elapsed{100};
};
struct ScriptedCapture {
    std::vector<CaptureStep> steps;
    std::chrono::steady_clock::time_point time{};
    std::stop_source stop;
    std::vector<UINT> waits;
    size_t acquired{},released{},reads{};
    bool held{},invalidRelease{},failRead{};
    Pixel current{};
    Image capture() {
        Image image;
        detail::withDesktopFrame(stop.get_token(),
            [&](UINT wait,DXGI_OUTDUPL_FRAME_INFO& info) {
                require(!held,"Release the previous frame before acquiring another");
                require(wait>0 && wait<=100,"Acquisition waits remain bounded");
                require(waits.size()<steps.size(),"Unexpected acquisition attempt");
                const auto& step=steps[waits.size()];waits.push_back(wait);
                time+=step.elapsed;
                if(step.cancel)stop.request_stop();
                if(SUCCEEDED(step.result)) {
                    held=true;++acquired;current=step.pixel;
                    info.LastPresentTime.QuadPart=step.presented;
                    info.LastMouseUpdateTime.QuadPart=1;
                    info.ProtectedContentMaskedOut=step.protectedContent;
                }
                return step.result;
            },
            [&] {if(!held)invalidRelease=true;held=false;++released;},
            [&] {
                require(held,"Keep the frame acquired while copying pixels");++reads;
                if(failRead)throw std::runtime_error("Injected readback failure");
                image=Image(1,1);image.at(0,0)=current;
            },
            [&] {return time;});
        return image;
    }
    void requireReleased() const {
        require(!held && !invalidRelease && acquired==released,"Every acquired frame is released exactly once");
    }
};
void expectCaptureError(ScriptedCapture& source,std::string_view message) {
    bool threw=false;
    try{source.capture();}catch(const std::runtime_error& e){threw=true;require(std::string_view(e.what()).find(message)!=std::string_view::npos,"Expected capture error");}
    require(threw,"Capture must report failure");source.requireReleased();
}
void captureTests() {
    test("cursor-only startup frames cannot supply captured pixels",[]{
        ScriptedCapture source;
        source.steps={{S_OK,0,{0,0,0,1}},{DXGI_ERROR_WAIT_TIMEOUT},{S_OK,0,{1,0,1,1}},{S_OK,123,{-0.125f,3,12.5f,1}}};
        const auto image=source.capture();source.requireReleased();
        require(source.waits.size()==4 && source.acquired==3 && source.reads==1,"Skip cursor updates and wait timeouts");
        require(image.at(0,0)==Pixel{-0.125f,3,12.5f,1},"Only the desktop frame's HDR pixels reach the image");
    });
    test("cursor-only updates exhaust one four-second deadline",[]{
        ScriptedCapture source;source.steps.resize(40);
        expectCaptureError(source,"Timed out waiting for a desktop image");
        require(source.time.time_since_epoch()==std::chrono::seconds(4),"Retries do not reset the deadline");
        require(source.acquired==40 && source.reads==0,"Never read cursor-only pixels");
    });
    test("wait timeouts do not release unacquired frames",[]{
        ScriptedCapture source;source.steps.assign(40,CaptureStep{DXGI_ERROR_WAIT_TIMEOUT});
        expectCaptureError(source,"Timed out waiting for a desktop image");
        require(source.waits.size()==40 && source.released==0 && source.reads==0,"Timeouts own no frame");
    });
    test("last acquisition wait is capped to the remaining deadline",[]{
        ScriptedCapture source;source.steps={{S_OK,0,{0,0,0,1},false,false,std::chrono::milliseconds(3950)},
                                           {DXGI_ERROR_WAIT_TIMEOUT,0,{0,0,0,1},false,false,std::chrono::milliseconds(50)}};
        expectCaptureError(source,"Timed out waiting for a desktop image");
        require(source.waits==std::vector<UINT>{100,50},"Do not wait beyond the remaining capture budget");
    });
    test("capture cancellation before and during acquisition releases owned frames",[]{
        ScriptedCapture before;before.stop.request_stop();expectCaptureError(before,"Capture canceled");
        require(before.waits.empty(),"Canceled capture does not acquire");
        for(const auto step:{CaptureStep{DXGI_ERROR_WAIT_TIMEOUT,0,{0,0,0,1},true},
                             CaptureStep{S_OK,0,{0,0,0,1},true},CaptureStep{S_OK,1,{5,3,1,1},true}}) {
            ScriptedCapture source;source.steps={step};expectCaptureError(source,"Capture canceled");
            require(source.waits.size()==1 && source.reads==0,"Stop before copying or retrying");
        }
    });
    test("acquisition failure after a cursor update releases only owned frames",[]{
        ScriptedCapture source;source.steps={{S_OK,0},{DXGI_ERROR_ACCESS_LOST}};
        expectCaptureError(source,"Could not capture the desktop");
        require(source.acquired==1 && source.reads==0,"Propagate acquisition failure without reading pixels");
    });
    test("protected frames and readback failures release their frames",[]{
        ScriptedCapture protectedFrame;protectedFrame.steps={{S_OK,1,{0,0,0,1},false,true}};
        expectCaptureError(protectedFrame,"Windows hid protected content");require(protectedFrame.reads==0,"Do not read protected content");
        ScriptedCapture readFailure;readFailure.steps={{S_OK,1}};readFailure.failRead=true;
        expectCaptureError(readFailure,"Injected readback failure");require(readFailure.reads==1,"Readback failure was exercised");
    });
    test("genuinely black desktop frames remain valid captures",[]{
        ScriptedCapture source;source.steps={{S_OK,1,{0,0,0,1}}};
        auto image=source.capture();source.requireReleased();
        require(source.reads==1 && image.at(0,0)==Pixel{0,0,0,1},"Use metadata, not brightness, to accept a frame");
    });
}
void geometryTests() {
    test("negative coordinates and mixed-DPI physical geometry",[]{
        DesktopImage desktop;auto left=monitor({-1200,-200,0,1800});left.dpi=144;left.rotation=Rotation::Clockwise90;
        auto right=monitor({0,0,2560,1440});right.dpi=192;desktop.monitors={left,right};
        require(desktop.bounds()==Rect{-1200,-200,2560,1800},"Desktop union");
        auto selection=normalized({200,600},{-700,-100});require(selection==Rect{-700,-100,200,600},"Physical coordinate selection");
        require(intersect(selection,right.bounds)==Rect{0,0,200,600},"Physical crop clipping");
        require(selection.width()==900 && selection.height()==700,"DPI must not rescale pixels");
    });
    test("all rotation mappings",[]{
        require(sourcePixel({0,0},3,2,Rotation::Clockwise90)==Point{0,1},"90 top-left");
        require(sourcePixel({1,2},3,2,Rotation::Clockwise90)==Point{2,0},"90 bottom-right");
        require(sourcePixel({0,0},3,2,Rotation::Clockwise180)==Point{2,1},"180");
        require(sourcePixel({0,0},3,2,Rotation::Clockwise270)==Point{2,0},"270 top-left");
        require(sourcePixel({1,2},3,2,Rotation::Clockwise270)==Point{0,1},"270 bottom-right");
        for(auto rotation:{Rotation::Identity,Rotation::Clockwise90,Rotation::Clockwise180,Rotation::Clockwise270}) {
            bool swapped=rotation==Rotation::Clockwise90 || rotation==Rotation::Clockwise270;std::set<std::pair<int,int>> seen;
            for(int y=0;y<(swapped?3:2);++y)for(int x=0;x<(swapped?2:3);++x){auto p=sourcePixel({x,y},3,2,rotation);require(p.x>=0 && p.x<3 && p.y>=0 && p.y<2,"Rotation bounds");seen.emplace(p.x,p.y);}
            require(seen.size()==6,"Rotation bijection");
        }
    });
    test("move and resize selection with edge crossing",[]{
        Rect desktop{-1000,-1000,1000,1000},r{-200,-100,100,100};
        require(hitSelection(r,{-200,-100})==Handle::NW,"NW handle");require(hitSelection(r,{0,0})==Handle::Move,"Move handle");
        require(adjustSelection(r,Handle::Move,{3000,3000},desktop)==Rect{700,800,1000,1000},"Clamp moving crop");
        require(adjustSelection(r,Handle::NW,{400,300},desktop)==Rect{100,100,200,200},"Resize can cross opposite edge");
        require(hitSelection(r,{-600,0})==Handle::None,"Outside selection");
    });
    test("gaps, crop clipping, and SDR-to-HDR white",[]{
        DesktopImage desktop;auto a=monitor({-2,0,0,2});auto b=monitor({2,0,4,2},true,203);
        std::fill(a.image.pixels.begin(),a.image.pixels.end(),Pixel{1,1,1,1});std::fill(b.image.pixels.begin(),b.image.pixels.end(),Pixel{5,4,3,1});desktop.monitors={a,b};
        auto crop=composite(desktop,{-1,-1,3,2},true);require(crop.width==4 && crop.height==3,"Crop size");expectNear(crop.at(0,1).r,203.f/80);expectNear(crop.at(3,1).r,5);require(crop.at(1,1)==Pixel{0,0,0,1},"Gap black");require(crop.at(0,0)==Pixel{0,0,0,1},"Uncovered top black");
        require(!desktop.intersectsHdr({-2,0,2,2}),"Touching HDR edge does not intersect");require(desktop.intersectsHdr({-2,0,3,2}),"Crossing HDR edge intersects");
    });
    test("undo redo branching and immutable annotation anchors",[]{
        History history;auto a=shape(Tool::Line,{-10,2},{10,20});history.add(a);history.add(shape(Tool::Rectangle,{2,3},{9,8}));
        require(history.undo() && history.visible().size()==1,"Undo");require(history.redo() && history.visible().size()==2,"Redo");history.undo();history.add(shape(Tool::Text,{0,0},{0,0}));require(!history.canRedo(),"New action discards redo");
        require(history.visible().front().points.front()==a.points.front(),"Desktop anchor unchanged");history.undo();history.undo();require(!history.undo(),"Undo empty");
    });
    test("bare Print Screen, repeats, and modified shortcuts",[]{
        PrintScreenGate gate;auto first=gate.key(true,false);require(first.trigger && first.suppress,"Bare press");auto repeat=gate.key(true,false);require(!repeat.trigger && repeat.suppress,"Repeat suppressed");require(gate.key(false,true).suppress,"Paired release suppressed even if modifiers change");
        require(!gate.key(true,true).suppress,"Modified key preserved");require(!gate.key(true,false).trigger,"Modified hold does not trigger after modifier released");require(!gate.key(false,false).suppress,"Modified release preserved");require(gate.key(true,false).trigger,"Next capture");
    });
    test("state recovery after export failure and cancellation",[]{
        SessionState state;require(state.beginCapture(),"Start capture");require(!state.beginCapture(),"Capture cannot reenter");state.captured();require(state.beginExport(),"Start export");require(!state.beginExport(),"Export cannot reenter");state.exportFailed();require(state.get()==State::Editing,"Retain editing on failure");require(state.beginExport(),"Retry");state.reset();require(state.get()==State::Idle,"Complete or cancel");
    });
    test("timestamp and paired filename collisions",[]{
        auto base=timestampStem(2026,9,16,14,32,8,123);require(base==L"Screenshot_2026-09-16_14-32-08-123","Timestamp formatting");
        std::set<std::wstring> files{base+L"_HDR.jxr",base+L"_1_SDR.png",base+L"_2.lock"};require(availableStem(base,[&](const auto& f){return files.contains(f);})==base+L"_3","Check both pair names and reservation");
    });
    test("transaction rollback at every write and commit failure",[]{
        for(int failure=0;failure<4;++failure) {
            int step=0;std::set<std::filesystem::path> disk{L"existing.png"};
            std::vector<StagedFile> files;
            for(int i=0;i<2;++i)files.push_back({L"temp"+std::to_wstring(i),L"final"+std::to_wstring(i),[&](const auto& path){disk.insert(path);if(step++==failure)throw std::runtime_error("Injected write failure");}});
            FileOperations ops{[&](const auto& from,const auto& to){if(step++==failure)throw std::runtime_error("Injected commit failure");require(!disk.contains(to),"Cannot overwrite");disk.erase(from);disk.insert(to);},[&](const auto& path){disk.erase(path);}};
            bool threw=false;try{publishFiles(files,ops);}catch(...){threw=true;}
            require(threw,"Failure injection reached");require(disk==std::set<std::filesystem::path>{L"existing.png"},"Rollback removes only transaction artifacts");
        }
    });
    test("commit collision never deletes existing target",[]{
        std::set<std::filesystem::path> disk{L"final"};std::vector<StagedFile> files{{L"temporary",L"final",[&](const auto& p){disk.insert(p);}}};
        FileOperations ops{[](const auto&,const auto&){throw std::runtime_error("Target exists");},[&](const auto& p){disk.erase(p);}};
        try{publishFiles(files,ops);}catch(...){}require(disk==std::set<std::filesystem::path>{L"final"},"Pre-existing file must survive");
    });
    test("opaque censor and pixelation flatten actual pixels",[]{
        Image image(24,24);for(int y=0;y<24;++y)for(int x=0;x<24;++x)image.at(x,y)={x/24.f,y/24.f,5,1};
        blackCover(image,{-12,-12,12,12},{-20,-20,0,0});require(image.at(0,0)==Pixel{0,0,0,1},"Opaque black including HDR");require(image.at(11,11)==Pixel{0,0,0,1},"Clipped black bounds");expectNear(image.at(12,12).b,5);
        pixelate(image,{-12,-12,12,12},{0,0,12,12},6);require(image.at(12,12)==image.at(17,17),"Flattened pixel block");require(image.at(12,12)!=image.at(18,18),"Separate block average");
    });
}
void colorTests() {
    test("sRGB transfer functions and exact SDR byte round trip",[]{
        Image image(256,1);for(int i=0;i<256;++i)image.at(i,0)={srgbToLinear(i/255.f),srgbToLinear(i/255.f),srgbToLinear(i/255.f),1};
        const auto bytes=ExportService::toBgra(image);for(int i=0;i<256;++i)require(bytes[i*4]==i && bytes[i*4+3]==255,"sRGB byte mismatch");expectNear(srgbToLinear(0.5f),0.214041f,0.00001f);
    });
    test("PNG codec round trip and explicit sRGB metadata",[]{
        Image image(64,8);for(int y=0;y<8;++y)for(int x=0;x<64;++x)image.at(x,y)={srgbToLinear(x/63.f),0.18f,0,1};
        auto png=ExportService::encodePng(image);require(decodeBgra(png)==ExportService::toBgra(image),"PNG pixels match");auto frame=decode(png);ComPtr<IWICMetadataQueryReader> reader;check(frame->GetMetadataQueryReader(&reader),"Read PNG metadata");PROPVARIANT value{};check(reader->GetMetadataByName(L"/sRGB/RenderingIntent",&value),"Read sRGB tag");require(value.vt==VT_UI1,"PNG sRGB chunk");PropVariantClear(&value);
    });
    test("lossless JPEG XR retains signed scRGB and HDR highlights",[]{
        Image image(32,16);const Pixel patches[]{{0,0,0,1},{0.18f,0.25f,0.5f,1},{1,1,1,1},{2.5375f,2.5375f,2.5375f,1},{12.5f,6.25f,3.125f,1},{-0.125f,0.75f,2,1}};
        auto capturePrecision=[](float f){return DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(f));};
        for(size_t i=0;i<image.pixels.size();++i){auto p=patches[i%6];image.pixels[i]={capturePrecision(p.r),capturePrecision(p.g),capturePrecision(p.b),1};}auto encoded=ExportService::encodeJxr(image);auto decoded=decodeFloat(encoded);
        require(decoded.width==32 && decoded.height==16,"JPEG XR dimensions");
        for(size_t i=0;i<image.pixels.size();++i){expectNear(decoded.pixels[i].r,image.pixels[i].r,0.00001f);expectNear(decoded.pixels[i].g,image.pixels[i].g,0.00001f);expectNear(decoded.pixels[i].b,image.pixels[i].b,0.00001f);}
    });
    test("Direct2D HDR tone mapper and SDR brightness normalization",[]{
        Graphics graphics({},true);Image hdr(64,8);float values[]{0,0.05f,0.25f,1,2.5375f,5,8,12.5f};
        for(int y=0;y<8;++y)for(int x=0;x<64;++x)hdr.at(x,y)={values[x/8],values[x/8],values[x/8],1};
        auto sdr=graphics.toneMap(hdr,203);
        expectNear(sdr.at(0,0).r,0);for(int i=1;i<8;++i)require(sdr.at(i*8,0).r>=sdr.at((i-1)*8,0).r,"Tone map monotonic");
        require(sdr.at(56,0).r<=1.02f && sdr.at(56,0).r>0.7f,"Highlights mapped into SDR range");
        require(sdr.at(24,0).r<sdr.at(32,0).r,"Preserve distinct scene luminance");
        Image white(8,8);std::fill(white.pixels.begin(),white.pixels.end(),Pixel{2.5f,2.5f,2.5f,1});auto normalized=graphics.toneMap(white,200);
        std::fill(white.pixels.begin(),white.pixels.end(),Pixel{1,1,1,1});auto reference=graphics.toneMap(white,80);
        expectNear(normalized.at(0,0).r,reference.at(0,0).r,0.02f);
    });
    test("shared annotation renderer, crop clipping, mixed SDR/HDR exports",[]{
        Graphics graphics({},true);DesktopImage desktop;auto sdr=monitor({-32,0,0,32});auto hdr=monitor({0,0,32,32},true,203);
        std::fill(sdr.image.pixels.begin(),sdr.image.pixels.end(),Pixel{0.18f,0.18f,0.18f,1});std::fill(hdr.image.pixels.begin(),hdr.image.pixels.end(),Pixel{8,8,8,1});desktop.monitors={sdr,hdr};
        std::vector<Annotation> annotations{shape(Tool::Line,{-30,8},{30,8}),shape(Tool::Censor,{-10,16},{10,28})};
        auto s=graphics.render(desktop,{-32,0,32,32},annotations,RenderDestination::Sdr);auto h=graphics.render(desktop,{-32,0,32,32},annotations,RenderDestination::Hdr);
        expectNear(s.at(3,2).r,0.18f);expectNear(h.at(3,2).r,0.18f*203/80);expectNear(h.at(60,2).r,8);
        for(int x=22;x<42;++x)for(int y=16;y<28;++y){require(s.at(x,y)==Pixel{0,0,0,1},"SDR censor is opaque");require(h.at(x,y)==Pixel{0,0,0,1},"HDR censor is opaque");}
        expectNear(s.at(32,8).r,1);expectNear(h.at(32,8).r,203.f/80);expectNear(s.at(32,8).g,0);expectNear(h.at(32,8).g,0);
        auto cropped=graphics.render(desktop,{-8,0,8,32},annotations,RenderDestination::Hdr);expectNear(cropped.at(8,8).r,h.at(32,8).r);require(cropped.width==16,"Crop width");
        auto hRoundtrip=decodeFloat(ExportService::encodeJxr(h));auto sBytes=decodeBgra(ExportService::encodePng(s));expectNear(hRoundtrip.at(32,8).r,203.f/80);require(sBytes[(8*64+32)*4+2]==255,"PNG annotation matches JXR");
    });
    test("all drawing tools render, including text and pixelated censor",[]{
        Graphics graphics({},true);DesktopImage desktop;auto m=monitor({0,0,128,128});
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)m.image.at(x,y)={x/128.f,y/128.f,0.2f,1};desktop.monitors.push_back(m);
        std::vector<Annotation> annotations;
        int y=5;for(auto tool:{Tool::Pen,Tool::Highlighter,Tool::Rectangle,Tool::Ellipse,Tool::Line,Tool::Arrow}){annotations.push_back(shape(tool,{5,y},{60,y+10}));y+=16;}
        Annotation text; text.tool=Tool::Text;text.points={{65,5}};text.text=L"Test\n日本語";text.textSize=14;annotations.push_back(text);
        auto censor=shape(Tool::Censor,{64,64},{112,112});censor.pixelated=true;annotations.push_back(censor);
        auto output=graphics.render(desktop,desktop.bounds(),annotations,RenderDestination::Sdr);
        require(output.at(66,66)==output.at(70,70),"Rendered pixelated censor is flattened");require(output.at(20,5)!=m.image.at(20,5),"Pen drawn");
        auto bytes=ExportService::encodePng(output);require(decodeBgra(bytes)==ExportService::toBgra(output),"Flattened pixels survive encoding");
    });
    test("FP16 overlay swap chain and presentation",[]{
        HWND window=CreateWindowExW(0,L"STATIC",L"ScreenshotTool hidden rendering test",WS_POPUP,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"Create hidden render-test window");
        struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}}cleanup{window};
        Graphics graphics({},true);graphics.attach(window,128,128);Image image(128,128);
        std::fill(image.pixels.begin(),image.pixels.end(),Pixel{5,3,1,1});auto background=graphics.upload(image);
        auto line=shape(Tool::Arrow,{24,24},{100,100});
        graphics.present(background.Get(),{0,0,128,128},{16,16,112,112},{&line,1},nullptr,3,true);
        auto censor=shape(Tool::Censor,{32,32},{80,80});censor.pixelated=true;
        std::vector<Annotation> withCensor{line,censor};
        graphics.present(background.Get(),{0,0,128,128},{16,16,112,112},withCensor,nullptr,3,true);
        InlineText text;text.begin({24,24},{16,16,112,112},{1,0,0,1},16);text.insert(L"edit\ntext");text.select(0,4);text.beginComposition();
        for(float white:{1.f,3.f})graphics.present(background.Get(),{0,0,128,128},{16,16,112,112},withCensor,&text.annotation(),white,true,&text,true);
    });
    test("real file pair, collision avoidance, Save As replacement",[]{
        const auto folder=std::filesystem::temp_directory_path()/(L"ScreenshotTool-test-"+uniqueToken());std::filesystem::create_directories(folder);
        struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(p,ignored);}}cleanup{folder};
        Image input(8,8);std::fill(input.pixels.begin(),input.pixels.end(),Pixel{3,3,3,1});EncodedImage image;image.png=ExportService::encodePng(input);image.jxr=ExportService::encodeJxr(input);
        auto a=ExportService::quickSave(image,folder),b=ExportService::quickSave(image,folder);require(a.size()==2 && b.size()==2 && a[0]!=b[0],"Distinct export pairs");
        for(const auto& path:a)require(std::filesystem::file_size(path)>0,"Nonempty output");
        auto path=folder/L"replacement.png";ExportService::saveAs(image,path);ExportService::saveAs(image,path);require(std::filesystem::file_size(path)==image.png.size(),"Save As replacement");
        for(const auto& file:std::filesystem::directory_iterator(folder))require(file.path().extension()!=L".tmp" && file.path().extension()!=L".lock","No temporary artifacts");
    });
}
#include "text_tests.hpp"
}
#include "overlay_tests.hpp"
#include "crop_tests.hpp"
int main(int argc,char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ComApartment com;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};wincheck(InitCommonControlsEx(&controls),"Initialize test controls");
        if(argc>1 && std::string_view(argv[1])=="--capture-probe") {
            auto desktop=CaptureService{}.capture();std::cout<<"Captured "<<desktop->monitors.size()<<" monitors; no image written.\n";
            for(const auto& m:desktop->monitors)std::wcout<<m.deviceName<<L" "<<m.bounds.width()<<L"x"<<m.bounds.height()<<L" origin="<<m.bounds.left<<L","<<m.bounds.top<<L" HDR="<<m.hdr<<L" SDRWhite="<<m.sdrWhiteNits<<L" rotation="<<static_cast<int>(m.rotation)<<L" adapter="<<m.adapterLuid<<L'\n';return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--text-drag-benchmark"){OverlayTestAccess::benchmark();return 0;}
        captureTests();geometryTests();textTests();textStoreTests();OverlayTestAccess::tests();CropTestAccess::tests();colorTests();std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
