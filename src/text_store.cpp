#include "text_store.hpp"
#include <cstring>
#include <olectl.h>
namespace shot {
void TextStore::start() {
    try {
        check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager_)),"Create Windows text services");
        check(manager_->Activate(&client_),"Activate Windows text services");activated_=true;
        manager_->GetFocus(&previousFocus_);
        check(manager_->CreateDocumentMgr(&document_),"Create inline text document");TfEditCookie cookie{};
        check(document_->CreateContext(client_,0,static_cast<ITextStoreACP*>(this),&context_,&cookie),"Create inline text context");
        check(document_->Push(context_.Get()),"Register inline text context");
        check(manager_.As(&keys_),"Get text service keyboard routing");focus(window_);
    }catch(...){stop();throw;}
}
void TextStore::focus(HWND window) {window_=window;if(manager_)check(manager_->SetFocus(document_.Get()),"Focus inline text context");}
void TextStore::stop() {
    if(context_)completeComposition();
    if(manager_)manager_->SetFocus(previousFocus_.Get());
    if(document_)document_->Pop(TF_POPF_ALL);
    context_.Reset();document_.Reset();previousFocus_.Reset();keys_.Reset();sink_.Reset();
    if(activated_){manager_->Deactivate();activated_=false;}manager_.Reset();
}
bool TextStore::key(MSG& m) {
    if(!keys_)return false;BOOL eaten=FALSE;HRESULT hr=S_OK;
    if(m.message==WM_KEYDOWN || m.message==WM_SYSKEYDOWN) {
        hr=keys_->TestKeyDown(m.wParam,m.lParam,&eaten);if(SUCCEEDED(hr) && eaten)hr=keys_->KeyDown(m.wParam,m.lParam,&eaten);
    } else if(m.message==WM_KEYUP || m.message==WM_SYSKEYUP) {
        hr=keys_->TestKeyUp(m.wParam,m.lParam,&eaten);if(SUCCEEDED(hr) && eaten)hr=keys_->KeyUp(m.wParam,m.lParam,&eaten);
    }
    return SUCCEEDED(hr) && eaten;
}
void TextStore::completeComposition(bool cancel) {
    if(!text_.composing())return;cancelComposition_=cancel;
    ComPtr<ITfContextOwnerCompositionServices> services;
    if(context_ && SUCCEEDED(context_.As(&services)))services->TerminateComposition(nullptr);
    if(text_.composing())text_.endComposition(cancel);cancelComposition_=false;
}
void TextStore::notify(const TextUpdate& update) {
    const bool layout=has(update.flags,TextChange::Content|TextChange::Selection|TextChange::Geometry);
    if(!sink_)return;
    // Changes made by a service under its lock are reported by SetText's TS_TEXTCHANGE.
    if(lock_){layoutPending_=layoutPending_ || layout;return;}
    auto sink=sink_;
    if(has(update.flags,TextChange::Content) && (mask_&TS_AS_TEXT_CHANGE)) {
        const auto old=update.previousText;
        const auto& now=text_.annotation().text;size_t a=0,b=old.size(),c=now.size();
        while(a<b && a<c && old[a]==now[a])++a;
        while(b>a && c>a && old[b-1]==now[c-1]){--b;--c;}
        TS_TEXTCHANGE change{static_cast<LONG>(a),static_cast<LONG>(b),static_cast<LONG>(c)};sink->OnTextChange(0,&change);
    }
    if(has(update.flags,TextChange::Selection) && (mask_&TS_AS_SEL_CHANGE))sink->OnSelectionChange();
    if(layout && (mask_&TS_AS_LAYOUT_CHANGE))sink->OnLayoutChange(TS_LC_CHANGE,1);
}
HRESULT TextStore::QueryInterface(REFIID id,void** result) {
    if(!result)return E_INVALIDARG;*result=nullptr;
    if(id==IID_IUnknown || id==__uuidof(ITextStoreACP))*result=static_cast<ITextStoreACP*>(this);
    else if(id==__uuidof(ITfContextOwnerCompositionSink))*result=static_cast<ITfContextOwnerCompositionSink*>(this);
    else return E_NOINTERFACE;AddRef();return S_OK;
}
HRESULT TextStore::AdviseSink(REFIID id,IUnknown* object,DWORD mask) {
    if(id!=__uuidof(ITextStoreACPSink))return E_INVALIDARG;
    if(!object)return E_INVALIDARG;
    if(sink_){ComPtr<IUnknown> a,b;sink_.As(&a);object->QueryInterface(IID_PPV_ARGS(&b));if(a!=b)return CONNECT_E_ADVISELIMIT;}
    auto hr=object->QueryInterface(IID_PPV_ARGS(&sink_));if(SUCCEEDED(hr))mask_=mask;return hr;
}
HRESULT TextStore::UnadviseSink(IUnknown* object) {
    if(!object || !sink_)return CONNECT_E_NOCONNECTION;ComPtr<IUnknown> a,b;sink_.As(&a);object->QueryInterface(IID_PPV_ARGS(&b));
    if(a!=b)return CONNECT_E_NOCONNECTION;sink_.Reset();mask_=0;return S_OK;
}
HRESULT TextStore::RequestLock(DWORD flags,HRESULT* session) {
    if(!session)return E_INVALIDARG;if(!sink_)return E_UNEXPECTED;
    if(!(flags&TS_LF_READ))return E_INVALIDARG;
    if(lock_){if(flags&TS_LF_SYNC)*session=TS_E_SYNCHRONOUS;else{pending_|=flags&TS_LF_READWRITE;*session=TS_S_ASYNC;}return S_OK;}
    auto sink=sink_;
    auto grant=[&](DWORD requested) {
        lock_=requested;HRESULT result=E_FAIL;
        try {if(writable())text_.beginServiceEdit();result=sink->OnLockGranted(requested);if(writable())text_.endServiceEdit();}
        catch(...){result=E_FAIL;}
        lock_=0;return result;
    };
    *session=grant(flags);
    while(pending_){const auto next=pending_;pending_=0;grant(next);}
    if(layoutPending_){layoutPending_=false;if(mask_&TS_AS_LAYOUT_CHANGE)sink->OnLayoutChange(TS_LC_CHANGE,1);}return S_OK;
}
HRESULT TextStore::GetStatus(TS_STATUS* s){if(!s)return E_INVALIDARG;s->dwDynamicFlags=0;s->dwStaticFlags=TS_SS_NOHIDDENTEXT;return S_OK;}
HRESULT TextStore::QueryInsert(LONG a,LONG b,ULONG count,LONG* start,LONG* end) {
    if(!start || !end || !valid(a,b))return E_INVALIDARG;
    if(text_.annotation().text.size()-(b-a)+count>InlineText::limit)return TS_E_INVALIDPOS;*start=a;*end=b;return S_OK;
}
HRESULT TextStore::GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* selection,ULONG* fetched) {
    if(!readable())return TS_E_NOLOCK;if(!fetched || (count && !selection))return E_INVALIDARG;*fetched=0;
    if(index!=0 && index!=TS_DEFAULT_SELECTION)return TS_E_NOSELECTION;
    if(count){selection->acpStart=text_.start();selection->acpEnd=text_.end();selection->style={text_.caret()<text_.anchor()?TS_AE_START:TS_AE_END,FALSE};*fetched=1;}return S_OK;
}
HRESULT TextStore::SetSelection(ULONG count,const TS_SELECTION_ACP* s) {
    if(!writable())return TS_E_NOLOCK;if(count!=1 || !s || !valid(s->acpStart,s->acpEnd))return E_INVALIDARG;
    try {text_.select(s->style.ase==TS_AE_START?s->acpEnd:s->acpStart,s->style.ase==TS_AE_START?s->acpStart:s->acpEnd);return S_OK;}catch(...){return E_FAIL;}
}
HRESULT TextStore::GetText(LONG a,LONG b,WCHAR* plain,ULONG requested,ULONG* copied,TS_RUNINFO* runs,ULONG runCount,ULONG* fetched,LONG* next) {
    if(!readable())return TS_E_NOLOCK;
    if(b==-1)b=static_cast<LONG>(text_.annotation().text.size());
    if(!valid(a,b))return TS_E_INVALIDPOS;
    if(!copied || !fetched || !next || (requested && !plain) || (runCount && !runs))return E_INVALIDARG;
    auto count=std::min(requested,static_cast<ULONG>(b-a));*copied=count;*fetched=0;*next=a+count;
    if(count)std::memcpy(plain,text_.annotation().text.data()+a,count*sizeof(WCHAR));
    // A caller can request run information without a text buffer.
    if(runCount && b>a){runs[0]={requested?count:static_cast<ULONG>(b-a),TS_RT_PLAIN};*fetched=1;if(!requested)*next=b;}
    return S_OK;
}
HRESULT TextStore::SetText(DWORD,LONG a,LONG b,const WCHAR* value,ULONG count,TS_TEXTCHANGE* change) {
    if(!writable())return TS_E_NOLOCK;if(!valid(a,b))return TS_E_INVALIDPOS;
    if(!change || (count && !value))return E_INVALIDARG;
    if(text_.annotation().text.size()-(b-a)+count>InlineText::limit)return TS_E_INVALIDPOS;
    try {auto old=text_.annotation().text.size();text_.replace(a,b,{value?value:L"",count});*change={a,b,static_cast<LONG>(a+text_.annotation().text.size()-(old-(b-a)))};return S_OK;}catch(...){return E_FAIL;}
}
HRESULT TextStore::InsertTextAtSelection(DWORD flags,const WCHAR* value,ULONG count,LONG* a,LONG* b,TS_TEXTCHANGE* change) {
    if(!readable())return TS_E_NOLOCK;LONG start=text_.start(),end=text_.end();
    if(flags&TS_IAS_QUERYONLY){if(!a || !b)return E_INVALIDARG;return QueryInsert(start,end,count,a,b);}
    if(!writable())return TS_E_NOLOCK;TS_TEXTCHANGE result{};auto hr=SetText(0,start,end,value,count,&result);
    if(SUCCEEDED(hr)){if(a)*a=start;if(b)*b=result.acpNewEnd;if(change)*change=result;}return hr;
}
HRESULT TextStore::FindNextAttrTransition(LONG,LONG halt,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset) {
    if(!next || !found || !offset)return E_INVALIDARG;*next=halt;*found=FALSE;*offset=0;return S_OK;
}
HRESULT TextStore::GetEndACP(LONG* p){if(!readable())return TS_E_NOLOCK;if(!p)return E_INVALIDARG;*p=static_cast<LONG>(text_.annotation().text.size());return S_OK;}
HRESULT TextStore::GetACPFromPoint(TsViewCookie view,const POINT* point,DWORD flags,LONG* p) {
    if(!readable())return TS_E_NOLOCK;if(view!=1 || !point || !p)return E_INVALIDARG;
    if(!(flags&GXFPF_NEAREST) && !intersect(*text_.annotation().textBounds,clip_).contains({point->x,point->y}))return TS_E_INVALIDPOINT;
    try{*p=text_.hit({point->x,point->y});return S_OK;}catch(...){return E_FAIL;}
}
HRESULT TextStore::GetTextExt(TsViewCookie view,LONG a,LONG b,RECT* rect,BOOL* clipped) {
    if(!readable())return TS_E_NOLOCK;if(view!=1 || !rect || !clipped || !valid(a,b))return E_INVALIDARG;
    try {
        Rect r=a==b?text_.caretRect(a):Rect{};
        for(const auto& part:text_.rangeRects(a,b))r=united(r,{static_cast<int>(std::floor(part.left)),static_cast<int>(std::floor(part.top)),static_cast<int>(std::ceil(part.right)),static_cast<int>(std::ceil(part.bottom))});
        auto visible=intersect(r,clip_);*clipped=visible!=r;*rect=nativeRect(visible.empty()?Rect{}:visible);return S_OK;
    }catch(...){return E_FAIL;}
}
HRESULT TextStore::GetScreenExt(TsViewCookie view,RECT* rect){if(view!=1 || !rect)return E_INVALIDARG;*rect=nativeRect(intersect(*text_.annotation().textBounds,clip_));return S_OK;}
HRESULT TextStore::GetWnd(TsViewCookie view,HWND* window){if(view!=1 || !window)return E_INVALIDARG;*window=window_;return S_OK;}
HRESULT TextStore::OnStartComposition(ITfCompositionView*,BOOL* accepted) {
    if(!accepted)return E_INVALIDARG;try{*accepted=!text_.composing();if(*accepted)text_.beginComposition();return S_OK;}catch(...){*accepted=FALSE;return E_FAIL;}
}
HRESULT TextStore::OnUpdateComposition(ITfCompositionView* view,ITfRange* range) {
    try {
        ComPtr<ITfRange> current;
        if(!range && view){if(FAILED(view->GetRange(&current)))return E_FAIL;range=current.Get();}
        ComPtr<ITfRangeACP> acp;
        if(range && SUCCEEDED(range->QueryInterface(IID_PPV_ARGS(&acp)))){LONG start{},length{};if(SUCCEEDED(acp->GetExtent(&start,&length)))text_.compositionRange(start,start+length);}
        return S_OK;
    }catch(...){return E_FAIL;}
}
HRESULT TextStore::OnEndComposition(ITfCompositionView*){try{text_.endComposition(cancelComposition_);return S_OK;}catch(...){return E_FAIL;}}
}
