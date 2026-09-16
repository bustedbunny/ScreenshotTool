#include "core.hpp"
#include <iomanip>
#include <sstream>

namespace shot {
Rect normalized(Point a, Point b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)}; }
Rect intersect(Rect a, Rect b) {
    Rect r{std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
    return r.empty() ? Rect{} : r;
}
Rect united(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};
}
Rect translated(Rect r, Point d) { return {r.left+d.x,r.top+d.y,r.right+d.x,r.bottom+d.y}; }
Point clampPoint(Point p, Rect b) { return {std::clamp(p.x,b.left,b.right),std::clamp(p.y,b.top,b.bottom)}; }
Handle hitSelection(Rect r, Point p, int radius) {
    if (r.empty()) return Handle::None;
    const int midX = (r.left+r.right)/2, midY = (r.top+r.bottom)/2;
    const Point points[]{{r.left,r.top},{midX,r.top},{r.right,r.top},{r.right,midY},{r.right,r.bottom},{midX,r.bottom},{r.left,r.bottom},{r.left,midY}};
    const Handle handles[]{Handle::NW,Handle::N,Handle::NE,Handle::E,Handle::SE,Handle::S,Handle::SW,Handle::W};
    for (int i=0;i<8;++i) if (std::abs(p.x-points[i].x)<=radius && std::abs(p.y-points[i].y)<=radius) return handles[i];
    return r.contains(p) ? Handle::Move : Handle::None;
}
Rect adjustSelection(Rect r, Handle h, Point d, Rect desktop) {
    if (h == Handle::Move) {
        d.x = std::clamp(d.x, desktop.left-r.left, desktop.right-r.right);
        d.y = std::clamp(d.y, desktop.top-r.top, desktop.bottom-r.bottom);
        return translated(r,d);
    }
    if (h==Handle::NW || h==Handle::W || h==Handle::SW) r.left += d.x;
    if (h==Handle::NE || h==Handle::E || h==Handle::SE) r.right += d.x;
    if (h==Handle::NW || h==Handle::N || h==Handle::NE) r.top += d.y;
    if (h==Handle::SW || h==Handle::S || h==Handle::SE) r.bottom += d.y;
    return intersect(normalized({r.left,r.top},{r.right,r.bottom}),desktop);
}
Point sourcePixel(Point p, int w, int h, Rotation rotation) {
    switch (rotation) {
    case Rotation::Clockwise90: return {p.y,h-1-p.x};
    case Rotation::Clockwise180: return {w-1-p.x,h-1-p.y};
    case Rotation::Clockwise270: return {w-1-p.y,p.x};
    default: return p;
    }
}
Image::Image(int w, int h):width(w),height(h) {
    if (w<=0 || h<=0 || w>32768 || h>32768 || static_cast<uint64_t>(w)*h>268435456)
        throw std::runtime_error("The selected image is too large. Select a smaller region.");
    pixels.resize(static_cast<size_t>(w)*h);
}
float srgbToLinear(float v) { return v<=0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f); }
float linearToSrgb(float v) { return v<=0.0031308f ? v*12.92f : 1.055f*std::pow(v,1.f/2.4f)-0.055f; }
Pixel scaled(Pixel p, float f) { return {p.r*f,p.g*f,p.b*f,p.a}; }
Rect DesktopImage::bounds() const { Rect r; for (const auto& m:monitors) r=united(r,m.bounds); return r; }
bool DesktopImage::intersectsHdr(Rect crop) const {
    return std::any_of(monitors.begin(),monitors.end(),[&](const auto& m){return m.hdr && !intersect(crop,m.bounds).empty();});
}
Image composite(const DesktopImage& desktop, Rect crop, bool hdrExport) {
    Image output(crop.width(),crop.height()); // Uncovered desktop gaps are opaque black.
    for (const auto& m:desktop.monitors) {
        const Rect part=intersect(crop,m.bounds);
        const float factor=hdrExport && !m.hdr ? 203.f/80.f : 1.f;
        for (int y=part.top;y<part.bottom;++y) for (int x=part.left;x<part.right;++x)
            output.at(x-crop.left,y-crop.top)=scaled(m.image.at(x-m.bounds.left,y-m.bounds.top),factor);
    }
    return output;
}
void blackCover(Image& image, Rect bounds, Rect censor) {
    const auto r=intersect(bounds,censor);
    for (int y=r.top;y<r.bottom;++y) for(int x=r.left;x<r.right;++x) image.at(x-bounds.left,y-bounds.top)={0,0,0,1};
}
void pixelate(Image& image, Rect bounds, Rect censor, int block) {
    if(block<1) throw std::invalid_argument("Invalid pixelation block size");
    const auto r=intersect(bounds,censor);
    if(r.empty()) return;
    // The grid is anchored to the annotation, independent of crop and monitor origin.
    const int firstX=censor.left+(r.left-censor.left)/block*block;
    const int firstY=censor.top+(r.top-censor.top)/block*block;
    for(int by=firstY;by<r.bottom;by+=block) for(int bx=firstX;bx<r.right;bx+=block) {
        auto cell=intersect(r,{bx,by,bx+block,by+block}); Pixel sum{0,0,0,0}; int count=0;
        for(int y=cell.top;y<cell.bottom;++y) for(int x=cell.left;x<cell.right;++x) {
            auto p=image.at(x-bounds.left,y-bounds.top); sum.r+=p.r;sum.g+=p.g;sum.b+=p.b;++count;
        }
        if(!count) continue;
        sum=scaled(sum,1.f/count);sum.a=1;
        for(int y=cell.top;y<cell.bottom;++y) for(int x=cell.left;x<cell.right;++x) image.at(x-bounds.left,y-bounds.top)=sum;
    }
}
void History::add(Annotation a) { items_.resize(cursor_);items_.push_back(std::move(a));cursor_=items_.size(); }
bool History::undo() { if(!canUndo()) return false;--cursor_;return true; }
bool History::redo() { if(!canRedo()) return false;++cursor_;return true; }
bool SessionState::beginCapture() { if(state_!=State::Idle)return false;state_=State::Capturing;return true; }
void SessionState::captured() { if(state_!=State::Capturing)throw std::logic_error("Invalid capture transition");state_=State::Editing; }
bool SessionState::beginExport() { if(state_!=State::Editing)return false;state_=State::Exporting;return true; }
void SessionState::exportFailed() { if(state_!=State::Exporting)throw std::logic_error("Invalid export transition");state_=State::Editing; }
PrintScreenResult PrintScreenGate::key(bool down, bool modified) {
    if(!down) { bool suppress=suppressed_;held_=suppressed_=false;return {suppress,false}; }
    if(held_)return {suppressed_,false};
    held_=true;suppressed_=!modified;return {suppressed_,suppressed_};
}
std::wstring timestampStem(int year,int month,int day,int hour,int minute,int second,int ms) {
    std::wostringstream s;s<<L"Screenshot_"<<std::setfill(L'0')<<std::setw(4)<<year<<L'-'<<std::setw(2)<<month<<L'-'<<std::setw(2)<<day<<L'_'<<std::setw(2)<<hour<<L'-'<<std::setw(2)<<minute<<L'-'<<std::setw(2)<<second<<L'-'<<std::setw(3)<<ms;return s.str();
}
std::wstring availableStem(const std::wstring& stem,const std::function<bool(const std::wstring&)>& exists) {
    for(unsigned i=0;i<100000;++i) {
        auto candidate=stem+(i ? L"_"+std::to_wstring(i) : L"");
        if(!exists(candidate+L"_SDR.png") && !exists(candidate+L"_HDR.jxr") && !exists(candidate+L".lock"))return candidate;
    }
    throw std::runtime_error("Too many screenshots with this name.");
}
void publishFiles(std::span<const StagedFile> files,const FileOperations& ops) {
    size_t committed=0;
    try {
        for(const auto& f:files) f.write(f.temporary);
        for(const auto& f:files) {ops.moveNoReplace(f.temporary,f.final);++committed;}
    } catch(...) {
        // Never remove a target that this transaction did not create.
        for(size_t i=0;i<committed;++i)try{ops.remove(files[i].final);}catch(...){}
        for(const auto& f:files)try{ops.remove(f.temporary);}catch(...){}
        throw;
    }
}
}
