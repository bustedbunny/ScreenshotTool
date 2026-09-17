#include "graphics.hpp"
#include <DirectXPackedVector.h>
#include <cstring>

namespace shot {
Graphics::Graphics(std::optional<std::int64_t> luid,bool software) {
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)),"Create graphics factory");
    ComPtr<IDXGIAdapter1> adapter;
    if(luid) for(UINT i=0;;++i) {
        ComPtr<IDXGIAdapter1> candidate;HRESULT hr=factory_->EnumAdapters1(i,&candidate);
        if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"Find monitor graphics adapter");
        DXGI_ADAPTER_DESC1 desc{};check(candidate->GetDesc1(&desc),"Read graphics adapter");
        std::int64_t value{};std::memcpy(&value,&desc.AdapterLuid,sizeof(value));
        if(value==*luid) {adapter=candidate;break;}
    }
    if(luid && !adapter)throw std::runtime_error("The display adapter disconnected. Capture again.");
    HRESULT hr=D3D11CreateDevice(adapter.Get(),adapter?D3D_DRIVER_TYPE_UNKNOWN:(software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE),nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,&immediate_);
    if(FAILED(hr) && !adapter && !software)
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,&immediate_);
    check(hr,"Create Direct3D device");
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,IID_PPV_ARGS(&d2d_)),"Create Direct2D factory");
    ComPtr<IDXGIDevice> dxgiDevice;check(device_.As(&dxgiDevice),"Get Direct2D device");
    check(d2d_->CreateDevice(dxgiDevice.Get(),&d2dDevice_),"Create Direct2D device");
    check(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context_),"Create drawing context");
    context_->SetDpi(96,96);
    context_->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const D2D1_RENDERING_CONTROLS controls{D2D1_BUFFER_PRECISION_32BPC_FLOAT,{256,256}};
    context_->SetRenderingControls(&controls);
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.GetAddressOf())),"Create text renderer");
    auto style=D2D1::StrokeStyleProperties();style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;style.lineJoin=D2D1_LINE_JOIN_ROUND;
    check(d2d_->CreateStrokeStyle(style,nullptr,0,&roundedStroke_),"Create pen style");
    style=D2D1::StrokeStyleProperties();style.dashStyle=D2D1_DASH_STYLE_DASH;
    check(d2d_->CreateStrokeStyle(style,nullptr,0,&dashedStroke_),"Create dashed text outline");
    for(auto* brush:{std::addressof(annotationBrush_),std::addressof(dimBrush_),std::addressof(borderBrush_),std::addressof(inkBrush_),std::addressof(highlightBrush_),std::addressof(paperBrush_)})
        check(context_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),brush->GetAddressOf()),"Create reusable drawing brush");
    dimBrush_->SetColor(D2D1::ColorF(0,0,0,0.25f));
}
ComPtr<ID2D1Bitmap1> Graphics::upload(const Image& image) {
    ComPtr<ID2D1Bitmap1> bitmap;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,D2D1::PixelFormat(DXGI_FORMAT_R32G32B32A32_FLOAT,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    check(context_->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),image.width*sizeof(Pixel),props,&bitmap),"Upload floating-point image");return bitmap;
}
ComPtr<ID2D1Bitmap1> Graphics::target(int width,int height) {
    ComPtr<ID2D1Bitmap1> bitmap;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_R32G32B32A32_FLOAT,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    check(context_->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,props,&bitmap),"Allocate floating-point drawing surface");return bitmap;
}
Image Graphics::readback(ID2D1Bitmap1* bitmap) {
    const auto size=bitmap->GetPixelSize();const auto format=bitmap->GetPixelFormat();
    ComPtr<ID2D1Bitmap1> read;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format,96,96);
    check(context_->CreateBitmap(size,nullptr,0,props,&read),"Allocate readback image");
    check(read->CopyFromBitmap(nullptr,bitmap,nullptr),"Copy rendered pixels");
    D2D1_MAPPED_RECT mapped{};check(read->Map(D2D1_MAP_OPTIONS_READ,&mapped),"Read rendered pixels");
    struct Unmap{ID2D1Bitmap1* b;~Unmap(){b->Unmap();}}unmap{read.Get()};
    Image result(size.width,size.height);
    for(UINT y=0;y<size.height;++y) {
        const auto row=mapped.bits+static_cast<size_t>(y)*mapped.pitch;
        if(format.format==DXGI_FORMAT_R32G32B32A32_FLOAT)std::memcpy(&result.at(0,y),row,size.width*sizeof(Pixel));
        else if(format.format==DXGI_FORMAT_R16G16B16A16_FLOAT)for(UINT x=0;x<size.width;++x) {
            const auto p=reinterpret_cast<const DirectX::PackedVector::HALF*>(row)+x*4;
            result.at(x,y)={DirectX::PackedVector::XMConvertHalfToFloat(p[0]),DirectX::PackedVector::XMConvertHalfToFloat(p[1]),DirectX::PackedVector::XMConvertHalfToFloat(p[2]),1};
        } else throw std::runtime_error("Unexpected rendering pixel format.");
    }
    return result;
}
Image Graphics::toneMap(const Image& hdr,float white) {
    auto input=upload(hdr),output=target(hdr.width,hdr.height);
    ComPtr<ID2D1Effect> tone,adjust;
    check(context_->CreateEffect(CLSID_D2D1HdrToneMap,&tone),"Create Windows HDR tone mapper");
    check(context_->CreateEffect(CLSID_D2D1WhiteLevelAdjustment,&adjust),"Create white-level adjustment");
    float peak=white;
    for(const auto p:hdr.pixels)peak=std::max(peak,80.f*std::max({p.r,p.g,p.b}));
    // Normalize the captured Windows SDR-white level BEFORE tone mapping. The
    // tone mapper's SDR curve assumes 80-nit diffuse white. Without this step,
    // moving Windows' SDR-brightness slider changes the SDR export's contrast.
    adjust->SetInput(0,input.Get());
    check(adjust->SetValue(D2D1_WHITELEVELADJUSTMENT_PROP_INPUT_WHITE_LEVEL,80.f),"Set scene reference white");
    check(adjust->SetValue(D2D1_WHITELEVELADJUSTMENT_PROP_OUTPUT_WHITE_LEVEL,white),"Normalize captured SDR brightness");
    tone->SetInputEffect(0,adjust.Get());
    check(tone->SetValue(D2D1_HDRTONEMAP_PROP_INPUT_MAX_LUMINANCE,peak*80.f/white),"Set normalized HDR content luminance");
    check(tone->SetValue(D2D1_HDRTONEMAP_PROP_OUTPUT_MAX_LUMINANCE,80.f),"Set SDR output luminance");
    check(tone->SetValue(D2D1_HDRTONEMAP_PROP_DISPLAY_MODE,D2D1_HDRTONEMAP_DISPLAY_MODE_SDR),"Set SDR tone-map mode");
    context_->SetTarget(output.Get());context_->SetTransform(D2D1::Matrix3x2F::Identity());
    context_->BeginDraw();context_->Clear(D2D1::ColorF(0,0,0,1));context_->DrawImage(tone.Get());
    check(context_->EndDraw(),"Tone-map HDR image");context_->SetTarget(nullptr);return readback(output.Get());
}
void Graphics::annotation(const Annotation& a,float whiteScale,IDWriteTextLayout* sharedLayout) {
    if(a.points.empty())return;
    auto c=scaled({srgbToLinear(a.color.r),srgbToLinear(a.color.g),srgbToLinear(a.color.b),a.color.a},whiteScale);
    if(a.tool==Tool::Highlighter)c.a=0.32f;
    if(a.tool==Tool::Censor)c={0,0,0,1};
    auto* brush=annotationBrush_.Get();brush->SetColor(D2D1::ColorF(c.r,c.g,c.b,c.a));
    const Point first=a.points.front(),last=a.points.back();
    const auto box=normalized(first,last);
    switch(a.tool) {
    case Tool::Pen:case Tool::Highlighter: {
        const float width=a.tool==Tool::Highlighter?a.width*4.f:a.width;
        if(a.points.size()==1) {context_->FillEllipse(D2D1::Ellipse(dpoint(first),width/2,width/2),brush);break;}
        ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
        check(d2d_->CreatePathGeometry(&path),"Create pen path");check(path->Open(&sink),"Open pen path");
        sink->BeginFigure(dpoint(first),D2D1_FIGURE_BEGIN_HOLLOW);
        for(size_t i=1;i<a.points.size();++i)sink->AddLine(dpoint(a.points[i]));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);check(sink->Close(),"Finish pen path");
        context_->DrawGeometry(path.Get(),brush,width,roundedStroke_.Get());break;
    }
    case Tool::Rectangle:context_->DrawRectangle(drect(box),brush,a.width);break;
    case Tool::Ellipse:context_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((box.left+box.right)/2.f,(box.top+box.bottom)/2.f),box.width()/2.f,box.height()/2.f),brush,a.width);break;
    case Tool::Arrow:case Tool::Line: {
        context_->DrawLine(dpoint(first),dpoint(last),brush,a.width,roundedStroke_.Get());
        if(a.tool==Tool::Arrow) {
            float angle=std::atan2(static_cast<float>(last.y-first.y),static_cast<float>(last.x-first.x));
            float len=std::max(12.f,a.width*4);
            for(float offset:{-0.55f,0.55f})context_->DrawLine(dpoint(last),D2D1::Point2F(last.x-len*std::cos(angle+offset),last.y-len*std::sin(angle+offset)),brush,a.width,roundedStroke_.Get());
        }break;
    }
    case Tool::Text: {
        auto layout=sharedLayout?ComPtr<IDWriteTextLayout>(sharedLayout):textLayout(a);
        const auto origin=a.textBounds?Point{a.textBounds->left,a.textBounds->top}:first;
        context_->DrawTextLayout(dpoint(origin),layout.Get(),brush);break;
    }
    case Tool::Censor: {
        context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        context_->FillRectangle(drect(box),brush);context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);break;
    }
    default:break;
    }
}
void Graphics::pixelatedCensor(ID2D1Bitmap1* output,Rect bounds,Rect selection,const Annotation& a) {
    const auto cover=intersect(selection,normalized(a.points.front(),a.points.back()));
    if(intersect(cover,bounds).empty())return;
    auto pixels=readback(output);pixelate(pixels,bounds,normalized(a.points.front(),a.points.back()));
    auto replacement=upload(pixels);
    context_->SetTarget(output);context_->SetTransform(D2D1::Matrix3x2F::Identity());
    const auto visible=intersect(bounds,cover);
    const auto local=drect(translated(visible,{-bounds.left,-bounds.top}));
    context_->BeginDraw();context_->DrawBitmap(replacement.Get(),local,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,local);
    check(context_->EndDraw(),"Flatten pixelated censor");
}
void Graphics::document(ID2D1Bitmap1* output,Rect bounds,Rect selection,std::span<const Annotation> annotations,float white,IDWriteTextLayout* layout) {
    const auto visible=intersect(bounds,selection);if(visible.empty())return;
    auto begin=[&]{context_->SetTarget(output);context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-bounds.left),static_cast<float>(-bounds.top)));context_->BeginDraw();context_->PushAxisAlignedClip(drect(visible),D2D1_ANTIALIAS_MODE_ALIASED);};
    begin();
    for(const auto& a:annotations) {
        if(a.tool==Tool::Censor && a.pixelated && !a.points.empty()) {
            context_->PopAxisAlignedClip();check(context_->EndDraw(),"Render annotations before censor");
            pixelatedCensor(output,bounds,selection,a);begin();
        } else annotation(a,white,layout);
    }
    context_->PopAxisAlignedClip();check(context_->EndDraw(),"Render annotations");context_->SetTarget(nullptr);
}
Image Graphics::render(const DesktopImage& desktop,Rect crop,std::span<const Annotation> annotations,RenderDestination destination) {
    Image image=composite(desktop,crop,destination==RenderDestination::Hdr);
    if(destination==RenderDestination::Sdr) for(const auto& monitor:desktop.monitors) {
        const auto part=intersect(crop,monitor.bounds);if(!monitor.hdr || part.empty())continue;
        Image hdr(part.width(),part.height());
        for(int y=0;y<hdr.height;++y)for(int x=0;x<hdr.width;++x)hdr.at(x,y)=monitor.image.at(part.left-monitor.bounds.left+x,part.top-monitor.bounds.top+y);
        auto sdr=toneMap(hdr,monitor.sdrWhiteNits);
        for(int y=0;y<sdr.height;++y)for(int x=0;x<sdr.width;++x)image.at(part.left-crop.left+x,part.top-crop.top+y)=sdr.at(x,y);
    }
    if(annotations.empty())return image;
    auto background=upload(image),output=target(image.width,image.height);
    context_->SetTarget(output.Get());context_->SetTransform(D2D1::Matrix3x2F::Identity());context_->BeginDraw();
    context_->DrawBitmap(background.Get(),nullptr,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);check(context_->EndDraw(),"Prepare export image");
    document(output.Get(),crop,crop,annotations,destination==RenderDestination::Hdr?203.f/80.f:1.f);
    return readback(output.Get());
}
void Graphics::attach(HWND window,int width,int height) {
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    check(factory_->CreateSwapChainForHwnd(device_.Get(),window,&desc,nullptr,nullptr,&swapchain_),"Create HDR overlay swap chain");
    check(factory_->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"Configure overlay window");
    ComPtr<IDXGISwapChain3> swap3;check(swapchain_.As(&swap3),"Configure scRGB presentation");
    check(swap3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709),"Set scRGB overlay color space");
    ComPtr<IDXGISurface> surface;check(swapchain_->GetBuffer(0,IID_PPV_ARGS(&surface)),"Get overlay surface");
    // Swap-chain buffers have render-target binding, not shader-input binding.
    // CANNOT_DRAW is required for the D2D wrapper; CopyFromBitmap/readback is still allowed.
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(desc.Format,D2D1_ALPHA_MODE_IGNORE),96,96);
    check(context_->CreateBitmapFromDxgiSurface(surface.Get(),&props,&backbuffer_),"Create overlay drawing target");
}
void Graphics::present(ID2D1Bitmap1* background,Rect monitor,Rect selection,std::span<const Annotation> annotations,const Annotation* draft,float white,bool handles,const InlineText* text,bool caretVisible) {
    if(!factory_->IsCurrent())throw std::runtime_error("Display configuration changed. Capture again.");
    context_->SetTarget(backbuffer_.Get());context_->SetTransform(D2D1::Matrix3x2F::Identity());context_->BeginDraw();
    context_->DrawBitmap(background,nullptr,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    check(context_->EndDraw(),"Draw frozen desktop");
    document(backbuffer_.Get(),monitor,selection,annotations,white);
    if(draft)document(backbuffer_.Get(),monitor,selection,{draft,1},white,text?text->layout():nullptr);
    context_->SetTarget(backbuffer_.Get());context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-monitor.left),static_cast<float>(-monitor.top)));context_->BeginDraw();
    auto* dim=dimBrush_.Get();auto* border=borderBrush_.Get();
    const auto selected=intersect(monitor,selection);
    if(selected.empty())context_->FillRectangle(drect(monitor),dim);
    else {
        for(const auto r:{Rect{monitor.left,monitor.top,monitor.right,selected.top},Rect{monitor.left,selected.bottom,monitor.right,monitor.bottom},Rect{monitor.left,selected.top,selected.left,selected.bottom},Rect{selected.right,selected.top,monitor.right,selected.bottom}})
            if(!r.empty())context_->FillRectangle(drect(r),dim);
        border->SetColor(D2D1::ColorF(0.13f*white,0.6f*white,white,1));
        context_->DrawRectangle(drect(selection),border,1.f);
        if(handles) {
            const int mx=(selection.left+selection.right)/2,my=(selection.top+selection.bottom)/2;
            for(const Point p: {Point{selection.left,selection.top},Point{mx,selection.top},Point{selection.right,selection.top},Point{selection.right,my},Point{selection.right,selection.bottom},Point{mx,selection.bottom},Point{selection.left,selection.bottom},Point{selection.left,my}})
                context_->FillRectangle(drect({p.x-4,p.y-4,p.x+4,p.y+4}),border);
        }
    }
    if(text && text->active() && !selected.empty()) {
        context_->PushAxisAlignedClip(drect(selected),D2D1_ANTIALIAS_MODE_ALIASED);
        const auto box=*text->annotation().textBounds;
        auto* ink=inkBrush_.Get();auto* highlight=highlightBrush_.Get();auto* paper=paperBrush_.Get();
        ink->SetColor(D2D1::ColorF(0.05f*white,0.35f*white,0.8f*white,1));
        highlight->SetColor(D2D1::ColorF(0.1f*white,0.4f*white,white,0.3f));
        paper->SetColor(D2D1::ColorF(white,white,white,1));
        for(const auto r:text->rangeRects(text->start(),text->end()))context_->FillRectangle(r,highlight);
        context_->DrawRectangle(drect(box),paper,2.f);
        context_->DrawRectangle(drect(box),ink,1.f,dashedStroke_.Get());
        const int mx=(box.left+box.right)/2,my=(box.top+box.bottom)/2;
        for(const Point p:{Point{box.left,box.top},Point{mx,box.top},Point{box.right,box.top},Point{box.right,my},Point{box.right,box.bottom},Point{mx,box.bottom},Point{box.left,box.bottom},Point{box.left,my}}) {
            const auto handle=drect({p.x-3,p.y-3,p.x+3,p.y+3});context_->FillRectangle(handle,paper);context_->DrawRectangle(handle,ink,1.f);
        }
        if(text->composing())for(const auto r:text->rangeRects(text->compositionRange().first,text->compositionRange().second))
            context_->DrawLine(D2D1::Point2F(r.left,r.bottom-1),D2D1::Point2F(r.right,r.bottom-1),ink,1.f);
        if(caretVisible) {
            auto caret=text->caretRect(text->caret());auto outline=caret;--outline.left;++outline.right;
            context_->FillRectangle(drect(outline),paper);context_->FillRectangle(drect(caret),ink);
        }
        context_->PopAxisAlignedClip();
    }
    check(context_->EndDraw(),"Draw selection overlay");context_->SetTarget(nullptr);
    check(swapchain_->Present(1,0),"Present overlay. The graphics device may have disconnected; capture again");
}
}
