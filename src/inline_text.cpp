#include "inline_text.hpp"
#include <cwctype>

namespace shot {
ComPtr<IDWriteTextLayout> textLayout(const Annotation& a) {
    ComPtr<IDWriteFactory> factory;
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf())),"Create text layout factory");
    ComPtr<IDWriteTextFormat> format;
    check(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,a.textSize,L"",&format),"Create text format");
    check(format->SetWordWrapping(a.textBounds?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP),"Set text wrapping");
    ComPtr<IDWriteTextLayout> layout;
    check(factory->CreateTextLayout(a.text.data(),static_cast<UINT32>(a.text.size()),format.Get(),a.textBounds?static_cast<float>(std::max(1,a.textBounds->width())):32768.f,10000000.f,&layout),"Lay out annotation text");
    return layout;
}
void InlineText::begin(Point p,Rect crop,Pixel color,float size) {
    finish(true);active_=true;state_={};state_.annotation.tool=Tool::Text;state_.annotation.points={p};
    state_.annotation.color=color;state_.annotation.textSize=size;
    state_.annotation.textBounds=Rect{p.x,p.y,p.x+std::max(1,std::min(300,crop.right-p.x)),p.y+static_cast<int>(std::ceil(size*1.4f))};rebuild();notify(TextChange::Geometry,{});
}
std::optional<Annotation> InlineText::finish(bool discard) {
    std::optional<Annotation> result;
    if(active_ && !discard && !state_.annotation.text.empty())result=state_.annotation;
    active_=false;layout_.Reset();undo_.clear();redo_.clear();gesture_.reset();composition_.reset();serviceEdit_.reset();boundaries_.clear();highSurrogate_=0;verticalX_.reset();return result;
}
void InlineText::rebuild() {
    layout_=textLayout(state_.annotation);
    DWRITE_TEXT_METRICS metrics{};check(layout_->GetMetrics(&metrics),"Measure text box");
    contentHeight_=static_cast<int>(std::ceil(metrics.height));
    DWRITE_OVERHANG_METRICS overhang{};check(layout_->GetOverhangMetrics(&overhang),"Measure text overhang");
    contentVisual_={static_cast<int>(std::floor(std::min(metrics.left,-overhang.left))),
        static_cast<int>(std::floor(std::min(metrics.top,-overhang.top))),
        static_cast<int>(std::ceil(std::max(metrics.left+metrics.widthIncludingTrailingWhitespace,layout_->GetMaxWidth()+overhang.right))),
        static_cast<int>(std::ceil(std::max(metrics.top+metrics.height,layout_->GetMaxHeight()+overhang.bottom)))};
    fitBounds();
    UINT32 count{};layout_->GetClusterMetrics(nullptr,0,&count);
    std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
    if(count)check(layout_->GetClusterMetrics(clusters.data(),count,&count),"Measure text clusters");
    boundaries_={0};UINT32 p=0;for(const auto& c:clusters){p+=c.length;boundaries_.push_back(p);}
}
void InlineText::fitBounds() {
    auto& b=*state_.annotation.textBounds;b.bottom=std::max(b.bottom,b.top+contentHeight_);
    state_.annotation.points.front()={b.left,b.top};
}
Rect InlineText::visualBounds() const {
    if(!active_)return {};
    const auto b=*annotation().textBounds;
    auto r=united(b,translated(contentVisual_,{b.left,b.top}));
    // Includes the caret outline, selection, composition underline, handle strokes and AA.
    return {r.left-5,r.top-5,r.right+5,r.bottom+5};
}
void InlineText::notify(TextChange flags,Rect previous,std::wstring_view old) {
    if(flags!=TextChange::None && changed)changed({flags,previous,old});
}
void InlineText::restore(State state,TextChange flags) {
    const auto previous=visualBounds();
    const bool content=state.annotation.text!=annotation().text;
    const bool reflow=content || state.annotation.textBounds->width()!=annotation().textBounds->width() || state.annotation.textSize!=annotation().textSize;
    if(content)flags=flags|TextChange::Content;
    if(state.anchor!=anchor() || state.caret!=caret() || state.trailing!=state_.trailing)flags=flags|TextChange::Selection;
    if(reflow || state.annotation.textBounds!=annotation().textBounds)flags=flags|TextChange::Geometry;
    if(state.annotation.color!=annotation().color || state.annotation.textSize!=annotation().textSize)flags=flags|TextChange::Formatting;
    auto old=std::move(state_);state_=std::move(state);
    if(reflow)rebuild();else fitBounds();
    notify(flags,previous,content?std::wstring_view(old.annotation.text):std::wstring_view{});
}
void InlineText::checkpoint() {
    if(composition_ || gesture_ || serviceEdit_)return;
    undo_.push_back(state_);if(undo_.size()>256)undo_.erase(undo_.begin());redo_.clear();
}
void InlineText::select(UINT32 a,UINT32 c,bool trailing) {
    const auto n=static_cast<UINT32>(state_.annotation.text.size());a=std::min(a,n);c=std::min(c,n);
    highSurrogate_=0;verticalX_.reset();
    if(anchor()==a && caret()==c && state_.trailing==trailing)return;
    const auto previous=visualBounds();state_.anchor=a;state_.caret=c;state_.trailing=trailing;
    notify(TextChange::Selection,previous);
}
void InlineText::replace(UINT32 a,UINT32 b,std::wstring_view value) {
    const auto old=state_.annotation.text;a=std::min(a,static_cast<UINT32>(old.size()));b=std::clamp(b,a,static_cast<UINT32>(old.size()));
    size_t count=std::min(value.size(),limit-(old.size()-(b-a)));
    if(count && value[count-1]>=0xd800 && value[count-1]<=0xdbff)--count;
    if(a==b && !count)return;
    if(std::wstring_view(old).substr(a,b-a)==value.substr(0,count)){select(a+static_cast<UINT32>(count),a+static_cast<UINT32>(count));return;}
    const auto previous=visualBounds();const bool selection=anchor()!=a+count || caret()!=a+count || state_.trailing;
    checkpoint();state_.annotation.text.replace(a,b-a,value.substr(0,count));state_.anchor=state_.caret=a+static_cast<UINT32>(count);state_.trailing=false;verticalX_.reset();rebuild();
    notify(TextChange::Content|TextChange::Geometry|(selection?TextChange::Selection:TextChange::None),previous,old);
}
void InlineText::character(wchar_t c) {
    if(c>=0xd800 && c<=0xdbff){highSurrogate_=c;return;}
    if(c>=0xdc00 && c<=0xdfff){if(highSurrogate_){wchar_t pair[]{highSurrogate_,c};highSurrogate_=0;insert({pair,2});}return;}
    highSurrogate_=0;if(c==L'\r')c=L'\n';if(c>=32 || c==L'\n' || c==L'\t')insert({&c,1});
}
UINT32 InlineText::previous(UINT32 p) const {auto i=std::lower_bound(boundaries_.begin(),boundaries_.end(),p);return i==boundaries_.begin()?0:*--i;}
UINT32 InlineText::next(UINT32 p) const {auto i=std::upper_bound(boundaries_.begin(),boundaries_.end(),p);return i==boundaries_.end()?static_cast<UINT32>(state_.annotation.text.size()):*i;}
UINT32 InlineText::word(UINT32 p,bool forward) const {
    const auto& t=state_.annotation.text;const auto n=static_cast<UINT32>(t.size());
    auto kind=[&](UINT32 i){return iswspace(t[i])?0:(iswalnum(t[i]) || t[i]==L'_' || t[i]>=0x80)?1:2;};
    if(forward){if(p<n){int k=kind(p);while(p<n && kind(p)==k)p=next(p);}while(p<n && kind(p)==0)p=next(p);}
    else {while(p && kind(previous(p))==0)p=previous(p);if(p){int k=kind(previous(p));while(p && kind(previous(p))==k)p=previous(p);}}
    return p;
}
void InlineText::erase(bool back,bool byWord) {
    if(start()!=end()){replace(start(),end(),L"");return;}
    const UINT32 p=byWord?word(caret(),!back):(back?previous(caret()):next(caret()));replace(std::min(p,caret()),std::max(p,caret()),L"");
}
void InlineText::navigate(UINT key,bool shift,bool control) {
    UINT32 p=caret();auto savedX=verticalX_;
    if(key==VK_LEFT)p=!shift && start()!=end()?start():(control?word(p,false):previous(p));
    if(key==VK_RIGHT)p=!shift && start()!=end()?end():(control?word(p,true):next(p));
    if(key==VK_HOME || key==VK_END) {
        if(control)p=key==VK_HOME?0:static_cast<UINT32>(state_.annotation.text.size());
        else {auto r=caretRect(p);auto b=*annotation().textBounds;placeCaret({key==VK_HOME?b.left-1:b.right+1,(r.top+r.bottom)/2},shift);return;}
    }
    if(key==VK_UP || key==VK_DOWN) {
        auto r=caretRect(p);int x=savedX.value_or(r.left);savedX=x;
        placeCaret({x,key==VK_UP?r.top-1:r.bottom+1},shift);verticalX_=savedX;return;
    }
    select(shift?anchor():p,p);if(key==VK_UP || key==VK_DOWN)verticalX_=savedX;
}
void InlineText::selectWord(UINT32 p) {
    const auto& t=state_.annotation.text;if(t.empty()){select(0,0);return;}
    p=std::min(p,static_cast<UINT32>(t.size()-1));p=previous(next(p));
    auto kind=[&](UINT32 i){return iswspace(t[i])?0:(iswalnum(t[i]) || t[i]==L'_' || t[i]>=0x80)?1:2;};
    int k=kind(p);UINT32 a=p,b=next(p);while(a && kind(previous(a))==k)a=previous(a);while(b<t.size() && kind(b)==k)b=next(b);select(a,b);
}
void InlineText::format(Pixel color,float size) {
    if(annotation().color==color && annotation().textSize==size)return;
    const auto previous=visualBounds();const bool reflow=annotation().textSize!=size;
    checkpoint();state_.annotation.color=color;state_.annotation.textSize=size;if(reflow)rebuild();
    notify(TextChange::Formatting|(reflow?TextChange::Geometry:TextChange::None),previous);
}
void InlineText::bounds(Rect b) {
    b.right=std::max(b.left+1,b.right);b.bottom=std::max(b.top+1,b.bottom);
    const bool reflow=b.width()!=annotation().textBounds->width();
    if(!reflow)b.bottom=std::max(b.bottom,b.top+contentHeight_);
    if(b==annotation().textBounds)return;
    const auto previous=visualBounds();checkpoint();state_.annotation.textBounds=b;
    if(reflow)rebuild();else fitBounds();
    notify(TextChange::Geometry,previous);
}
void InlineText::beginGesture(){gesture_=state_;}
void InlineText::endGesture(){if(gesture_){if(gesture_->annotation.textBounds!=annotation().textBounds){undo_.push_back(*gesture_);redo_.clear();}gesture_.reset();}}
void InlineText::beginServiceEdit(){serviceEdit_=state_;serviceComposition_=composing();}
void InlineText::endServiceEdit() {
    if(serviceEdit_ && !serviceComposition_ && serviceEdit_->annotation.text!=annotation().text){undo_.push_back(*serviceEdit_);redo_.clear();}
    serviceEdit_.reset();
}
void InlineText::beginComposition() {
    if(!composition_){composition_=serviceEdit_.value_or(state_);compositionRange_={start(),end()};notify(TextChange::Composition,visualBounds());}
    serviceComposition_=true;
}
void InlineText::compositionRange(UINT32 a,UINT32 b) {
    const auto n=static_cast<UINT32>(annotation().text.size());const auto range=std::pair{std::min(a,n),std::min(b,n)};
    if(compositionRange_==range)return;compositionRange_=range;notify(TextChange::Composition,visualBounds());
}
void InlineText::endComposition(bool cancel) {
    if(!composition_)return;
    auto original=std::move(*composition_);composition_.reset();
    if(cancel)restore(std::move(original),TextChange::Composition);
    else {if(annotation().text!=original.annotation.text){undo_.push_back(std::move(original));redo_.clear();}notify(TextChange::Composition,visualBounds());}
}
bool InlineText::undo(){if(undo_.empty() || composing())return false;redo_.push_back(state_);auto state=std::move(undo_.back());undo_.pop_back();restore(std::move(state));return true;}
bool InlineText::redo(){if(redo_.empty() || composing())return false;undo_.push_back(state_);auto state=std::move(redo_.back());redo_.pop_back();restore(std::move(state));return true;}

UINT32 InlineText::hit(Point p) const {
    const auto b=*annotation().textBounds;BOOL trailing{},inside{};DWRITE_HIT_TEST_METRICS m{};
    check(layout_->HitTestPoint(static_cast<float>(p.x-b.left),static_cast<float>(p.y-b.top),&trailing,&inside,&m),"Hit test text");return m.textPosition+(trailing?m.length:0);
}
void InlineText::placeCaret(Point p,bool extend) {
    const auto b=*annotation().textBounds;BOOL trailing{},inside{};DWRITE_HIT_TEST_METRICS m{};
    check(layout_->HitTestPoint(static_cast<float>(p.x-b.left),static_cast<float>(p.y-b.top),&trailing,&inside,&m),"Place text caret");
    const auto position=m.textPosition+(trailing?m.length:0);select(extend?anchor():position,position,trailing!=FALSE);
}
Handle InlineText::hitBorder(Point p) const {
    auto b=*annotation().textBounds;auto h=hitSelection(b,p,5);
    if(h==Handle::Move && p.x>b.left+5 && p.x<b.right-5 && p.y>b.top+5 && p.y<b.bottom-5)return Handle::None;
    return h;
}
Rect InlineText::caretRect(UINT32 p) const {
    float x{},y{};DWRITE_HIT_TEST_METRICS m{};const bool trailing=p==caret() && state_.trailing && p>0;check(layout_->HitTestTextPosition(trailing?previous(p):p,trailing,&x,&y,&m),"Locate text caret");
    const auto b=*annotation().textBounds;return {b.left+static_cast<int>(std::floor(x)),b.top+static_cast<int>(std::floor(y)),b.left+static_cast<int>(std::floor(x))+1,b.top+static_cast<int>(std::ceil(y+m.height))};
}
std::vector<D2D1_RECT_F> InlineText::rangeRects(UINT32 a,UINT32 b) const {
    std::vector<D2D1_RECT_F> result;if(a==b)return result;UINT32 count{};auto box=*annotation().textBounds;
    layout_->HitTestTextRange(a,b-a,static_cast<float>(box.left),static_cast<float>(box.top),nullptr,0,&count);
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    if(count)check(layout_->HitTestTextRange(a,b-a,static_cast<float>(box.left),static_cast<float>(box.top),metrics.data(),count,&count),"Locate text selection");
    for(const auto& m:metrics)result.push_back(D2D1::RectF(m.left,m.top,m.left+std::max(1.f,m.width),m.top+m.height));return result;
}
Rect InlineText::resized(Rect r,Handle h,Point d) {
    if(h==Handle::Move)return translated(r,d);
    if(h==Handle::NW || h==Handle::W || h==Handle::SW)r.left=std::min(r.right-1,r.left+d.x);
    if(h==Handle::NE || h==Handle::E || h==Handle::SE)r.right=std::max(r.left+1,r.right+d.x);
    if(h==Handle::NW || h==Handle::N || h==Handle::NE)r.top=std::min(r.bottom-1,r.top+d.y);
    if(h==Handle::SW || h==Handle::S || h==Handle::SE)r.bottom=std::max(r.top+1,r.bottom+d.y);
    return r;
}
}
