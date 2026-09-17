#pragma once
#include "inline_text.hpp"
#include <msctf.h>
#include <textstor.h>

namespace shot {
// STA object. stop() breaks the TSF context/store reference cycle before release.
class TextStore final:public ITextStoreACP,public ITfContextOwnerCompositionSink {
public:
    TextStore(InlineText& text,HWND window,Rect clip):text_(text),window_(window),clip_(clip){}
    void start();void stop();void focus(HWND window);bool key(MSG& message);
    void notify(const TextUpdate& update);
    void completeComposition(bool cancel=false);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--references_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID,IUnknown*,DWORD) override;
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown*) override;
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD,HRESULT*) override;
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS*) override;
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG,LONG,ULONG,LONG*,LONG*) override;
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG,ULONG,TS_SELECTION_ACP*,ULONG*) override;
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG,const TS_SELECTION_ACP*) override;
    HRESULT STDMETHODCALLTYPE GetText(LONG,LONG,WCHAR*,ULONG,ULONG*,TS_RUNINFO*,ULONG,ULONG*,LONG*) override;
    HRESULT STDMETHODCALLTYPE SetText(DWORD,LONG,LONG,const WCHAR*,ULONG,TS_TEXTCHANGE*) override;
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG,LONG,IDataObject** p) override {if(p)*p=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG,REFGUID,REFIID,IUnknown** p) override {if(p)*p=nullptr;return TS_E_NOOBJECT;}
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* p) override {if(!p)return E_INVALIDARG;*p=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD,const WCHAR*,ULONG,LONG*,LONG*,TS_TEXTCHANGE*) override;
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG,LONG,ULONG,const TS_ATTRID*,DWORD,LONG*,BOOL*,LONG*) override;
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* count) override {if(!count)return E_INVALIDARG;*count=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG*) override;
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* view) override {if(!view)return E_INVALIDARG;*view=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG*) override;
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie,LONG,LONG,RECT*,BOOL*) override;
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie,RECT*) override;
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie,HWND*) override;
    HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView*,BOOL*) override;
    HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*,ITfRange*) override;
    HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*) override;
private:
    bool readable() const {return (lock_&TS_LF_READ)!=0;}
    bool writable() const {return (lock_&TS_LF_READWRITE)==TS_LF_READWRITE;}
    bool valid(LONG a,LONG b) const {return a>=0 && b>=a && static_cast<size_t>(b)<=text_.annotation().text.size();}
    ULONG references_{1};InlineText& text_;HWND window_{};Rect clip_;
    ComPtr<ITextStoreACPSink> sink_;DWORD mask_{},lock_{},pending_{};
    ComPtr<ITfThreadMgr> manager_;ComPtr<ITfDocumentMgr> document_,previousFocus_;
    ComPtr<ITfContext> context_;ComPtr<ITfKeystrokeMgr> keys_;
    TfClientId client_{};bool activated_{},cancelComposition_{},layoutPending_{};
};
}
