#pragma once
#include "inline_text.hpp"

namespace shot {
enum class RenderDestination { Preview, Sdr, Hdr };
class Graphics {
public:
    explicit Graphics(std::optional<std::int64_t> adapter = {}, bool software = false);
    ComPtr<ID2D1Bitmap1> upload(const Image& image);
    ComPtr<ID2D1Bitmap1> target(int width,int height);
    Image readback(ID2D1Bitmap1* bitmap);
    Image toneMap(const Image& hdr,float sdrWhiteNits);
    // This single document renderer is used by both exports and every monitor overlay.
    void document(ID2D1Bitmap1* target,Rect targetBounds,Rect selection,std::span<const Annotation> annotations,float whiteScale,IDWriteTextLayout* layout=nullptr);
    Image render(const DesktopImage& desktop,Rect crop,std::span<const Annotation> annotations,RenderDestination destination);
    void attach(HWND window,int width,int height);
    void present(ID2D1Bitmap1* background,Rect monitor,Rect selection,std::span<const Annotation> annotations,const Annotation* draft,float whiteScale,bool handles,const InlineText* text=nullptr,bool caretVisible=false);
    ID2D1DeviceContext* context() const {return context_.Get();}
    bool isCurrent() const {return factory_->IsCurrent()!=FALSE;}
private:
    void annotation(const Annotation& annotation,float whiteScale,IDWriteTextLayout* layout=nullptr);
    void pixelatedCensor(ID2D1Bitmap1* output,Rect targetBounds,Rect selection,const Annotation& annotation);
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> immediate_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<ID2D1Factory1> d2d_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> context_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1StrokeStyle> roundedStroke_,dashedStroke_;
    ComPtr<ID2D1SolidColorBrush> annotationBrush_,dimBrush_,borderBrush_,inkBrush_,highlightBrush_,paperBrush_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<ID2D1Bitmap1> backbuffer_;
};
}
