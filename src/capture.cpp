#include "capture.hpp"
#include "capture_frame.hpp"
#include <DirectXPackedVector.h>
#include <chrono>
#include <cstring>
#include <shellscalingapi.h>

namespace shot {
float querySdrWhite(const std::wstring& name,bool required) {
    UINT pathCount{},modeCount{};
    LONG result=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool queried=false;
    for(int attempt=0;result==ERROR_SUCCESS && attempt<3;++attempt) {
        paths.resize(pathCount);modes.resize(modeCount);
        result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_SUCCESS){queried=true;break;}
        if(result!=ERROR_INSUFFICIENT_BUFFER)break;
        result=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);
    }
    if(queried)for(UINT i=0;i<pathCount;++i) {
        const auto& p=paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),p.sourceInfo.adapterId,p.sourceInfo.id};
        if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || name!=source.viewGdiDeviceName)continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),p.targetInfo.adapterId,p.targetInfo.id};
        if(DisplayConfigGetDeviceInfo(&white.header)==ERROR_SUCCESS && white.SDRWhiteLevel>0)
            return 80.f*white.SDRWhiteLevel/1000.f;
    }
    if(required)throw std::runtime_error("Cannot read the HDR monitor's SDR brightness. Reconnect the display or update its graphics driver, then capture again.");
    return 80.f;
}
namespace {
struct OutputCapture {
    MonitorImage monitor;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
};
Rotation rotationOf(DXGI_MODE_ROTATION r) {
    switch(r) {
    case DXGI_MODE_ROTATION_ROTATE90:return Rotation::Clockwise90;
    case DXGI_MODE_ROTATION_ROTATE180:return Rotation::Clockwise180;
    case DXGI_MODE_ROTATION_ROTATE270:return Rotation::Clockwise270;
    default:return Rotation::Identity;
    }
}
void readTexture(OutputCapture& output,IDXGIResource* resource) {
    ComPtr<ID3D11Texture2D> texture;check(resource->QueryInterface(IID_PPV_ARGS(&texture)),"Get capture texture");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(output.monitor.hdr && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)
        throw std::runtime_error("The graphics driver returned an SDR surface for an HDR display. Update the driver or disable HDR explicitly before retrying; this capture cannot preserve HDR.");
    if(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)
        throw std::runtime_error("The graphics driver returned an unsupported desktop format. Update the driver and retry.");
    const int w=static_cast<int>(desc.Width),h=static_cast<int>(desc.Height);
    const bool swapped=output.monitor.rotation==Rotation::Clockwise90 || output.monitor.rotation==Rotation::Clockwise270;
    if(output.monitor.bounds.width()!=(swapped?h:w) || output.monitor.bounds.height()!=(swapped?w:h))
        throw std::runtime_error("The display layout changed during capture. Capture again.");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;check(output.device->CreateTexture2D(&desc,nullptr,&staging),"Allocate desktop staging image");
    output.context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(output.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read desktop staging image");
    struct Unmap { ID3D11DeviceContext* c;ID3D11Texture2D* t;~Unmap(){c->Unmap(t,0);} } unmap{output.context.Get(),staging.Get()};
    output.monitor.image=Image(output.monitor.bounds.width(),output.monitor.bounds.height());
    float table[256];for(int i=0;i<256;++i)table[i]=srgbToLinear(i/255.f);
    for(int y=0;y<output.monitor.image.height;++y)for(int x=0;x<output.monitor.image.width;++x) {
        const Point src=sourcePixel({x,y},w,h,output.monitor.rotation);
        const auto row=static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(src.y)*mapped.RowPitch;
        Pixel p;
        if(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            const auto* data=reinterpret_cast<const DirectX::PackedVector::HALF*>(row)+src.x*4;
            p={DirectX::PackedVector::XMConvertHalfToFloat(data[0]),DirectX::PackedVector::XMConvertHalfToFloat(data[1]),DirectX::PackedVector::XMConvertHalfToFloat(data[2]),1};
        } else { const BYTE* data=row+src.x*4;p={table[data[2]],table[data[1]],table[data[0]],1}; }
        output.monitor.image.at(x,y)=p;
    }
}
void readFrame(OutputCapture& output,std::stop_token stop) {
    ComPtr<IDXGIResource> resource;
    detail::withDesktopFrame(stop,
        [&](UINT wait,DXGI_OUTDUPL_FRAME_INFO& info) {
            return output.duplication->AcquireNextFrame(wait,&info,resource.ReleaseAndGetAddressOf());
        },
        [&] {resource.Reset();output.duplication->ReleaseFrame();},
        [&] {readTexture(output,resource.Get());},
        [] {return std::chrono::steady_clock::now();});
}
}
std::shared_ptr<const DesktopImage> CaptureService::capture(std::stop_token stop) const {
    ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Enumerate graphics adapters");
    std::vector<OutputCapture> outputs;
    for(UINT ai=0;;++ai) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr=factory->EnumAdapters1(ai,&adapter);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"Enumerate graphics adapter");
        DXGI_ADAPTER_DESC1 ad{};check(adapter->GetDesc1(&ad),"Read adapter information");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        for(UINT oi=0;;++oi) {
            ComPtr<IDXGIOutput> output;hr=adapter->EnumOutputs(oi,&output);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"Enumerate monitor");
            ComPtr<IDXGIOutput6> output6;check(output.As(&output6),"HDR desktop duplication requires Windows 11 and a current graphics driver");
            DXGI_OUTPUT_DESC1 desc{};check(output6->GetDesc1(&desc),"Read monitor information");
            if(!desc.AttachedToDesktop)continue;
            if(!device)check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Create capture device");
            OutputCapture capture;capture.device=device;capture.context=context;
            capture.monitor.bounds={desc.DesktopCoordinates.left,desc.DesktopCoordinates.top,desc.DesktopCoordinates.right,desc.DesktopCoordinates.bottom};
            capture.monitor.deviceName=desc.DeviceName;
            capture.monitor.hdr=desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            capture.monitor.sdrWhiteNits=querySdrWhite(desc.DeviceName,capture.monitor.hdr);
            capture.monitor.maxLuminance=desc.MaxLuminance;
            capture.monitor.rotation=rotationOf(desc.Rotation);
            UINT dpiX=96,dpiY=96;
            if(SUCCEEDED(GetDpiForMonitor(desc.Monitor,MDT_EFFECTIVE_DPI,&dpiX,&dpiY)))capture.monitor.dpi=dpiX;
            static_assert(sizeof(ad.AdapterLuid)==sizeof(capture.monitor.adapterLuid));
            std::memcpy(&capture.monitor.adapterLuid,&ad.AdapterLuid,sizeof(ad.AdapterLuid));
            // Required BGRA fallback is accepted only for SDR; HDR must stay FP16.
            const DXGI_FORMAT formats[]{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_B8G8R8A8_UNORM};
            check(output6->DuplicateOutput1(device.Get(),0,2,formats,&capture.duplication),"Desktop capture unavailable. Unlock Windows or close other capture software and retry");
            outputs.push_back(std::move(capture));
        }
    }
    if(outputs.empty())throw std::runtime_error("No active displays are available for capture.");
    auto desktop=std::make_shared<DesktopImage>();
    // Each adapter owns its device. CPU staging bridges adapters without shared-resource assumptions.
    for(auto& output:outputs) {readFrame(output,stop);desktop->monitors.push_back(std::move(output.monitor));}
    if(!factory->IsCurrent())throw std::runtime_error("Display settings changed during capture. Capture again.");
    return desktop;
}
}
