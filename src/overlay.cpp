#include "overlay.hpp"
#include "capture.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

namespace shot {
namespace {
constexpr int ToolBase=1000,ColorId=1100,WidthId=1101,TextSizeId=1102,CensorModeId=1103,UndoId=1200,RedoId=1201,CopyId=1202,SaveId=1203,SaveAsId=1204,CancelId=1205;
constexpr wchar_t OverlayClass[]=L"ScreenshotTool.Overlay",ToolbarClass[]=L"ScreenshotTool.Toolbar";
Point cursorPoint(){POINT p{};GetCursorPos(&p);return {p.x,p.y};}
Tool toolAt(int id){return static_cast<Tool>(id-ToolBase);}
bool controlDown(){return (GetKeyState(VK_CONTROL)&0x8000)!=0;}
bool shiftDown(){return (GetKeyState(VK_SHIFT)&0x8000)!=0;}
struct ToolbarShortcut {int id;wchar_t key;};
constexpr ToolbarShortcut toolbarShortcuts[]{
    {ToolBase+static_cast<int>(Tool::Select),L'V'},
    {ToolBase+static_cast<int>(Tool::Pen),L'F'},
    {ToolBase+static_cast<int>(Tool::Highlighter),L'H'},
    {ToolBase+static_cast<int>(Tool::Rectangle),L'R'},
    {ToolBase+static_cast<int>(Tool::Ellipse),L'E'},
    {ToolBase+static_cast<int>(Tool::Line),L'L'},
    {ToolBase+static_cast<int>(Tool::Arrow),L'A'},
    {ToolBase+static_cast<int>(Tool::Text),L'T'},
    {ToolBase+static_cast<int>(Tool::Censor),L'B'},
    {ColorId,L'C'},{WidthId,L'W'},{TextSizeId,L'S'},{CensorModeId,L'P'}
};
const wchar_t* toolNames[]{L"Select",L"Pen",L"Highlight",L"Rectangle",L"Ellipse",L"Line",L"Arrow",L"Text",L"Censor"};
const wchar_t* toolTips[]{L"Select: drag outside the crop with any tool to replace it. Crop resize handles work with every tool.",L"Pen: start a freehand stroke inside the crop; continue anywhere on the captured desktop.",L"Highlighter: start a translucent wide stroke inside the crop; continue anywhere on the captured desktop.",L"Rectangle: start an outline inside the crop; drag anywhere on the captured desktop.",L"Ellipse: start an oval outline inside the crop; drag anywhere on the captured desktop.",L"Line: start inside the crop; drag to any desktop point.",L"Arrow: start inside the crop; point at any desktop detail.",L"Text: click or drag inside the crop, type, then Ctrl+Enter or click outside the box to finish. Active boxes can be edited outside the crop.",L"Censor: start an opaque black cover inside the crop; drag anywhere (or choose pixelation)."};
}
OverlaySession::OverlaySession(HINSTANCE instance,std::shared_ptr<const DesktopImage> desktop,Settings& settings,std::function<void(SessionAction)> action,std::function<void(std::wstring)> failure)
    :instance_(instance),desktop_(std::move(desktop)),settings_(settings),action_(std::move(action)),failure_(std::move(failure)) {
    WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_DBLCLKS;wc.hInstance=instance_;wc.lpfnWndProc=windowProc;wc.lpszClassName=OverlayClass;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)wincheck(FALSE,"Register overlay window");
    wc.lpfnWndProc=toolbarProc;wc.lpszClassName=ToolbarClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)wincheck(FALSE,"Register toolbar window");

    text_.changed=[this](const TextUpdate& update){textChanged(update);};
    darkBrush_=CreateSolidBrush(RGB(27,30,37));
}
OverlaySession::~OverlaySession() {
    closing_=true;ReleaseCapture();
    closeTextEditor();
    if(toolbar_)DestroyWindow(toolbar_);
    for(auto& window:windows_)if(window->hwnd)DestroyWindow(window->hwnd);
    if(toolbarFont_)DeleteObject(toolbarFont_);
    if(darkBrush_)DeleteObject(darkBrush_);
}
void OverlaySession::show() {
    for(size_t i=0;i<desktop_->monitors.size();++i) {
        const auto& monitor=desktop_->monitors[i];auto window=std::make_unique<MonitorWindow>();window->session=this;window->index=i;
        // Store before constructing resources so partial initialization cleans up every HWND.
        windows_.push_back(std::move(window));auto& view=*windows_.back();
        view.hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,OverlayClass,L"ScreenshotTool selection",WS_POPUP,monitor.bounds.left,monitor.bounds.top,monitor.bounds.width(),monitor.bounds.height(),nullptr,nullptr,instance_,&view);
        wincheck(view.hwnd!=nullptr,"Create monitor overlay");
        view.graphics=std::make_unique<Graphics>(monitor.adapterLuid);view.graphics->attach(view.hwnd,monitor.bounds.width(),monitor.bounds.height());
        view.background=view.graphics->upload(monitor.image);render(view);
    }
    createToolbar();
    const auto point=cursorPoint();HWND foreground=windows_.front()->hwnd;
    for(auto& window:windows_) {
        ShowWindow(window->hwnd,SW_SHOWNOACTIVATE);
        if(desktop_->monitors[window->index].bounds.contains(point))foreground=window->hwnd;
    }
    SetForegroundWindow(foreground);SetFocus(foreground);
}
HWND OverlaySession::owner() const {return windows_.empty()?nullptr:windows_.front()->hwnd;}
void OverlaySession::report(const std::exception& e) {if(!errorReported_ && !closing_){errorReported_=true;failure_(widen(e.what()));}}
LRESULT CALLBACK OverlaySession::windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto view=reinterpret_cast<MonitorWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){view=static_cast<MonitorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);view->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(view));}
    if(view)try{return view->session->monitorMessage(*view,msg,wp,lp);}catch(const std::exception& e){view->session->report(e);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK OverlaySession::toolbarProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto session=reinterpret_cast<OverlaySession*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){session=static_cast<OverlaySession*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);session->toolbar_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(session));}
    if(session)try{return session->toolbarMessage(msg,wp,lp);}catch(const std::exception& e){session->report(e);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT OverlaySession::monitorMessage(MonitorWindow& view,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: {PAINTSTRUCT paint{};BeginPaint(view.hwnd,&paint);EndPaint(view.hwnd,&paint);if(view.graphics && !closing_)render(view);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:if(!busy_)mouseDown(view.hwnd,cursorPoint());return 0;
    case WM_LBUTTONDBLCLK:if(!busy_)mouseDoubleClick(view.hwnd,cursorPoint());return 0;
    case WM_CHAR:
        if(!busy_ && text_.active())text_.character(static_cast<wchar_t>(wp));return 0;
    case WM_UNICHAR:
        if(wp==UNICODE_NOCHAR)return TRUE;
        if(!busy_ && text_.active() && wp<=0x10ffff) {
            if(wp>0xffff){const auto value=static_cast<UINT32>(wp)-0x10000;text_.character(static_cast<wchar_t>(0xd800+(value>>10)));text_.character(static_cast<wchar_t>(0xdc00+(value&1023)));}
            else text_.character(static_cast<wchar_t>(wp));
        }return 0;
    case WM_TIMER:
        if(wp==42 && text_.active()){caretVisible_=!caretVisible_;repaintText({},text_.visualBounds());}return 0;
    case WM_SETFOCUS:if(textStore_){textWindow_=view.hwnd;textStore_->focus(view.hwnd);repaintText({},text_.visualBounds());}return 0;
    case WM_KILLFOCUS:if(text_.active())repaintText({},text_.visualBounds());return 0;
    case WM_MOUSEMOVE:if(!busy_)mouseMove(cursorPoint());return 0;
    case WM_LBUTTONUP:if(!busy_)mouseUp(cursorPoint());return 0;
    case WM_CAPTURECHANGED:
        if(textDragging_ && reinterpret_cast<HWND>(lp)!=view.hwnd){textDragging_=textCreating_=textSelecting_=false;text_.endGesture();}
        if(dragging_ && reinterpret_cast<HWND>(lp)!=view.hwnd) {
            if(selecting_)selection_=original_;
            dragging_=false;draft_.reset();placeToolbar();refreshButtons();refreshStatus();repaint();
        }return 0;
    case WM_SETCURSOR: {
        if(LOWORD(lp)!=HTCLIENT)break;
        SetCursor(LoadCursorW(nullptr,cursorAt(cursorPoint())));return TRUE;
    }
    case WM_DPICHANGED: // Physical desktop bounds are immutable; a live layout/DPI change aborts the session.
        if(IsWindowVisible(view.hwnd) && !closing_)failure_(L"Display scaling changed. Press Print Screen to capture the new layout.");return 0;
    case WM_CLOSE:if(!busy_)action_(SessionAction::Cancel);return 0;
    }
    return DefWindowProcW(view.hwnd,msg,wp,lp);
}
void OverlaySession::render(MonitorWindow& view) {
    const auto& monitor=desktop_->monitors[view.index];
    const Annotation* preview=text_.active()?&text_.annotation():(draft_?&*draft_:nullptr);
    view.graphics->present(view.background.Get(),monitor.bounds,selection_,history_.visible(),preview,monitor.hdr?monitor.sdrWhiteNits/80.f:1.f,!selection_.empty(),text_.active()?&text_:nullptr,caretVisible_ && GetFocus()==textWindow_);
}
void OverlaySession::repaint() {for(auto& window:windows_)InvalidateRect(window->hwnd,nullptr,FALSE);}
void OverlaySession::repaintText(Rect previous,Rect current) {
    // Test each extent separately: a jump must not dirty the monitors in between.
    for(auto& window:windows_) {
        const auto monitor=desktop_->monitors[window->index].bounds;
        const auto dirty=united(intersect(previous,monitor),intersect(current,monitor));
        if(dirty.empty())continue;
        const auto local=nativeRect(translated(dirty,{-monitor.left,-monitor.top}));
        InvalidateRect(window->hwnd,&local,FALSE);
    }
}
Handle OverlaySession::cropHandle(Point p) const {
    const auto handle=hitSelection(selection_,p);
    return handle==Handle::Move?Handle::None:handle;
}
LPCWSTR OverlaySession::cursorAt(Point p) const {
    if(busy_)return IDC_WAIT;
    auto hit=cropHandle(p);LPCWSTR cursor=tool_==Tool::Select && selection_.contains(p)?IDC_ARROW:IDC_CROSS;
    if(hit==Handle::None && text_.active()) {
        hit=text_.hitBorder(p);
        if(hit==Handle::None && text_.annotation().textBounds->contains(p))cursor=IDC_IBEAM;
    }
    switch(hit) {
        case Handle::Move:return IDC_SIZEALL; // Only an active text-box border can move.
        case Handle::N:case Handle::S:return IDC_SIZENS;
        case Handle::E:case Handle::W:return IDC_SIZEWE;
        case Handle::NW:case Handle::SE:return IDC_SIZENWSE;
        case Handle::NE:case Handle::SW:return IDC_SIZENESW;
        default:return cursor;
    }
}
void OverlaySession::mouseDoubleClick(HWND hwnd,Point p) {
    if(cropHandle(p)==Handle::None && text_.active() && text_.annotation().textBounds->contains(p) && text_.hitBorder(p)==Handle::None) {
        if(textStore_)textStore_->completeComposition();text_.selectWord(text_.hit(p));
    }else mouseDown(hwnd,p);
}
void OverlaySession::mouseDown(HWND hwnd,Point p) {
    const auto crop=cropHandle(p);
    if(crop!=Handle::None) {
        commitText();SetFocus(hwnd);dragStart_=clampPoint(p,desktop_->bounds());original_=selection_;draft_.reset();handle_=crop;
        selecting_=dragging_=true;SetCapture(hwnd);ShowWindow(toolbar_,SW_HIDE);return;
    }
    if(text_.active() && (text_.hitBorder(p)!=Handle::None || text_.annotation().textBounds->contains(p))) {
        if(textStore_)textStore_->completeComposition();textWindow_=hwnd;SetFocus(hwnd);if(textStore_)textStore_->focus(hwnd);
        textStart_=p;textOriginal_=*text_.annotation().textBounds;textHandle_=text_.hitBorder(p);
        textSelecting_=textHandle_==Handle::None;textDragging_=true;textCreating_=false;
        if(textSelecting_)text_.placeCaret(p,shiftDown());else text_.beginGesture();
        SetCapture(hwnd);return;
    }
    commitText();SetFocus(hwnd);p=clampPoint(p,desktop_->bounds());
    if(tool_==Tool::Select && selection_.contains(p))return;
    dragStart_=p;original_=selection_;draft_.reset();
    handle_=Handle::None;selecting_=selection_.empty() || tool_==Tool::Select || !selection_.contains(p);
    if(selecting_) {
        selection_={};
    } else {
        if(tool_==Tool::Text){editText(hwnd,p);return;}
        draft_=Annotation{tool_,{p},settings_.color,settings_.strokeWidth,settings_.textSize,L"",pixelated_};
    }
    dragging_=true;SetCapture(hwnd);if(selecting_)ShowWindow(toolbar_,SW_HIDE);repaint();
}
void OverlaySession::mouseMove(Point p) {
    if(textDragging_) {
        const Point delta{p.x-textStart_.x,p.y-textStart_.y};
        if(textCreating_) {
            if(std::abs(delta.x)>3 || std::abs(delta.y)>3) {
                auto b=normalized(textStart_,clampPoint(p,desktop_->bounds()));b.right=std::max(b.left+1,b.right);b.bottom=std::max(b.top+1,b.bottom);text_.bounds(b);
            }
        }else if(textSelecting_)text_.placeCaret(p,true);
        else text_.bounds(InlineText::resized(textOriginal_,textHandle_,delta));
        return;
    }
    if(!dragging_)return;p=clampPoint(p,desktop_->bounds());
    if(selecting_) {
        const auto bounds=handle_==Handle::None?normalized(dragStart_,p):adjustSelection(original_,handle_,{p.x-dragStart_.x,p.y-dragStart_.y},desktop_->bounds());
        if(bounds==selection_)return;selection_=bounds;
    } else if(draft_) {
        if(p==draft_->points.back())return;
        if(draft_->tool==Tool::Pen || draft_->tool==Tool::Highlighter)draft_->points.push_back(p);
        else {if(draft_->points.size()==1)draft_->points.push_back(p);else draft_->points.back()=p;}
    }
    repaint();
}
void OverlaySession::mouseUp(Point p) {
    if(textDragging_){mouseMove(p);textDragging_=textCreating_=textSelecting_=false;text_.endGesture();ReleaseCapture();return;}
    if(!dragging_)return;mouseMove(p);dragging_=false;ReleaseCapture();
    if(selecting_ && selection_.empty())selection_=original_;
    if(selecting_ && !firstRegionCompleted_ && !selection_.empty()) {
        firstRegionCompleted_=true;tool_=Tool::Pen;
    }
    if(draft_){history_.add(std::move(*draft_));draft_.reset();}
    if(selecting_)placeToolbar();refreshButtons();if(selecting_)refreshStatus();repaint();
}
void OverlaySession::editText(HWND hwnd,Point p) {
    textWindow_=hwnd;
    try {
        text_.begin(p,desktop_->bounds(),settings_.color,settings_.textSize);
        textStore_.Attach(new TextStore(text_,hwnd,desktop_->bounds()));textStore_->start();
        textStart_=p;textOriginal_=*text_.annotation().textBounds;textCreating_=textDragging_=true;textSelecting_=false;
        text_.beginGesture();SetCapture(hwnd);SetFocus(hwnd);
    }catch(...){closeTextEditor();throw;}
}
void OverlaySession::textChanged(const TextUpdate& update) {
    if(has(update.flags,TextChange::Formatting)) {
        settings_.color=text_.annotation().color;settings_.textSize=text_.annotation().textSize;refreshButtons();
    }
    if(textStore_)textStore_->notify(update);
    if(has(update.flags,TextChange::Content|TextChange::Selection|TextChange::Composition) || update.previousVisual.empty()) {
        caretVisible_=true;const UINT blink=GetCaretBlinkTime();
        if(blink!=INFINITE)SetTimer(owner(),42,std::max(100u,blink),nullptr);
    }
    repaintText(update.previousVisual,text_.visualBounds());
}
void OverlaySession::closeTextEditor() {
    textDragging_=textCreating_=textSelecting_=false;KillTimer(owner(),42);
    if(textStore_){textStore_->stop();textStore_.Reset();}
    KillTimer(owner(),42);text_.finish(true);textWindow_=nullptr;
    if(GetCapture())ReleaseCapture();
}
void OverlaySession::commitText(bool discard) {
    if(!text_.active())return;
    if(textStore_){textStore_->completeComposition(discard);textStore_->stop();textStore_.Reset();}
    const auto previous=text_.visualBounds();
    if(auto annotation=text_.finish(discard))history_.add(std::move(*annotation));
    closeTextEditor();refreshButtons();repaintText(previous,{});
}
void OverlaySession::textClipboard(bool copy,bool cut) {
    if(!OpenClipboard(textWindow_))return;
    struct Close{~Close(){CloseClipboard();}}close;
    if(copy) {
        if(text_.start()==text_.end())return;
        const auto selected=text_.annotation().text.substr(text_.start(),text_.end()-text_.start());std::wstring value;
        for(wchar_t c:selected){if(c==L'\n')value+=L'\r';value+=c;}
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));if(!memory)return;
        auto target=static_cast<wchar_t*>(GlobalLock(memory));if(!target){GlobalFree(memory);return;}
        std::copy(value.c_str(),value.c_str()+value.size()+1,target);GlobalUnlock(memory);
        if(!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,memory)){GlobalFree(memory);return;}
        if(cut)text_.insert(L"");
    }else {
        HANDLE memory=GetClipboardData(CF_UNICODETEXT);if(!memory)return;
        auto source=static_cast<const wchar_t*>(GlobalLock(memory));if(!source)return;
        std::wstring value;const auto count=GlobalSize(memory)/sizeof(wchar_t);
        for(size_t i=0;i<count && source[i] && value.size()<InlineText::limit;++i) {
            if(source[i]==L'\r'){value+=L'\n';if(i+1<count && source[i+1]==L'\n')++i;}else value+=source[i];
        }
        GlobalUnlock(memory);text_.insert(value);
    }
}
bool OverlaySession::translate(MSG& msg) {
    if(msg.message!=WM_KEYDOWN && msg.message!=WM_SYSKEYDOWN && msg.message!=WM_KEYUP && msg.message!=WM_SYSKEYUP)return false;
    const HWND focus=GetFocus();bool ours=focus==toolbar_ || (toolbar_ && IsChild(toolbar_,focus));

    for(const auto& window:windows_)ours=ours || focus==window->hwnd || IsChild(window->hwnd,focus);
    if(!ours)return false; // Native dialogs retain all of their keyboard behavior.
    if(busy_)return true;
    if(text_.active() && (focus!=toolbar_ && !IsChild(toolbar_,focus))) {
        const bool down=msg.message==WM_KEYDOWN || msg.message==WM_SYSKEYDOWN;
        if(down && msg.wParam==VK_ESCAPE) {
            if(text_.composing()){if(textStore_)textStore_->completeComposition(true);}else commitText(true);return true;
        }
        if(down && msg.wParam==VK_RETURN && controlDown()){commitText();return true;}
        if(textStore_ && textStore_->key(msg))return true;
        if(!down)return false;
        if(controlDown())switch(msg.wParam) {
            case 'A':text_.select(0,static_cast<UINT32>(text_.annotation().text.size()));return true;
            case 'C':textClipboard(true,false);return true;
            case 'X':textClipboard(true,true);return true;
            case 'V':textClipboard(false,false);return true;
            case 'Z':if(shiftDown())text_.redo();else text_.undo();return true;
            case 'Y':text_.redo();return true;
            case 'S':command(shiftDown()?SaveAsId:SaveId);return true;
            case VK_INSERT:textClipboard(true,false);return true;
        }
        if(shiftDown() && msg.wParam==VK_INSERT){textClipboard(false,false);return true;}
        if(shiftDown() && msg.wParam==VK_DELETE){textClipboard(true,true);return true;}
        switch(msg.wParam) {
            case VK_LEFT:case VK_RIGHT:case VK_UP:case VK_DOWN:case VK_HOME:case VK_END:text_.navigate(static_cast<UINT>(msg.wParam),shiftDown(),controlDown());return true;
            case VK_BACK:text_.erase(true,controlDown());return true;
            case VK_DELETE:text_.erase(false,controlDown());return true;
            case VK_TAB:text_.insert(L"\t");return true;
        }
        return false;
    }
    if(msg.message==WM_KEYUP || msg.message==WM_SYSKEYUP)return false;
    if(busy_)return true;
    if(msg.wParam==VK_ESCAPE){action_(SessionAction::Cancel);return true;}
    if(controlDown())switch(msg.wParam) {
        case 'C':command(CopyId);return true;
        case 'S':command(shiftDown()?SaveAsId:SaveId);return true;
        case 'Z':command(UndoId);return true;
        case 'Y':command(RedoId);return true;
    }
    if(msg.wParam==VK_TAB && toolbar_ && IsWindowVisible(toolbar_)) {
        const HWND next=GetNextDlgTabItem(toolbar_,focus,shiftDown());SetFocus(next?next:buttons_.front().hwnd);return true;
    }
    const bool modified=controlDown() || shiftDown() || (GetKeyState(VK_MENU)&0x8000) || (GetKeyState(VK_LWIN)&0x8000) || (GetKeyState(VK_RWIN)&0x8000);
    if(msg.message==WM_KEYDOWN && !modified && firstRegionCompleted_ && !selection_.empty() && !dragging_) {
        for(const auto& shortcut:toolbarShortcuts)if(msg.wParam==static_cast<WPARAM>(shortcut.key)) {
            // Consume repeats without reopening menus or repeatedly toggling censor mode.
            if(!(msg.lParam&(static_cast<LPARAM>(1)<<30)))command(shortcut.id);
            return true;
        }
    }
    return false;
}
void OverlaySession::busy(bool value) {
    busy_=value;for(auto& view:windows_)EnableWindow(view->hwnd,!value);EnableWindow(toolbar_,!value);

    if(!value){SetForegroundWindow(owner());SetFocus(owner());}refreshButtons();refreshStatus();
}
void OverlaySession::validateDisplays() {
    if(errorReported_)return;
    for(const auto& view:windows_)if(!view->graphics->isCurrent())throw std::runtime_error("Display configuration changed. Press Print Screen to capture the new layout.");
    for(const auto& monitor:desktop_->monitors)if(monitor.hdr && std::abs(querySdrWhite(monitor.deviceName,true)-monitor.sdrWhiteNits)>0.5f)
        throw std::runtime_error("Windows SDR brightness changed. Press Print Screen to capture at the new brightness.");
}
void OverlaySession::createToolbar() {
    toolbar_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,ToolbarClass,L"ScreenshotTool tools",WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,0,0,580,140,owner(),nullptr,instance_,this);
    wincheck(toolbar_!=nullptr,"Create annotation toolbar");
    tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,toolbar_,nullptr,instance_,nullptr);
    auto add=[&](int id,const wchar_t* label,const wchar_t* tip) {
        HWND button=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,toolbar_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
        wincheck(button!=nullptr,"Create toolbar button");
        std::wstring hint=tip;
        for(const auto& shortcut:toolbarShortcuts)if(shortcut.id==id){hint+=L" (";hint+=shortcut.key;hint+=L")";break;}
        buttons_.push_back({id,button,label,std::move(hint)});
    };
    for(int i=0;i<9;++i)add(ToolBase+i,toolNames[i],toolTips[i]);
    add(ColorId,L"Color",L"Choose the annotation color.");add(WidthId,L"Width",L"Stroke width in captured desktop pixels.");add(TextSizeId,L"Text size",L"Text size in captured desktop pixels.");add(CensorModeId,L"Cover: black",L"Opaque black is the default censor. Pixelation is optional.");
    add(UndoId,L"Undo",L"Undo annotation (Ctrl+Z)");add(RedoId,L"Redo",L"Redo annotation (Ctrl+Y)");add(CopyId,L"Copy",L"Copy SDR image (Ctrl+C)");add(SaveId,L"Save",L"Quick save to Pictures / ScreenshotTool (Ctrl+S)");add(SaveAsId,L"Save as",L"Save an SDR PNG to a chosen location (Ctrl+Shift+S)");add(CancelId,L"Cancel",L"Cancel capture (Esc)");
    // Register tips after the vector stops growing so their backing strings stay valid.
    if(tooltip_)for(auto& button:buttons_) {
        TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=toolbar_;info.uId=reinterpret_cast<UINT_PTR>(button.hwnd);info.lpszText=button.tip.data();
        SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));
    }
    layoutToolbar(96);refreshButtons();refreshStatus();
}
void OverlaySession::layoutToolbar(unsigned dpi) {
    toolbarDpi_=dpi;const auto px=[&](int n){return MulDiv(n,static_cast<int>(dpi),96);};
    toolbarWidth_=px(584);toolbarHeight_=px(146);
    if(toolbarFont_)DeleteObject(toolbarFont_);
    toolbarFont_=CreateFontW(-px(12),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(size_t i=0;i<buttons_.size();++i) {
        int x{},y{},width{};
        if(i<9){x=10+static_cast<int>(i)*63;y=10;width=60;}
        else if(i<13){x=10+static_cast<int>(i-9)*142;y=46;width=139;}
        else{x=10+static_cast<int>(i-13)*94;y=82;width=91;}
        SetWindowPos(buttons_[i].hwnd,nullptr,px(x),px(y),px(width),px(30),SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(buttons_[i].hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(toolbarFont_),FALSE);
    }
}
void OverlaySession::placeToolbar() {
    if(!toolbar_ || selection_.empty() || (dragging_ && selecting_)){if(toolbar_)ShowWindow(toolbar_,SW_HIDE);return;}
    // Anchor on the monitor nearest the selection's lower-right edge, then clamp to its work area.
    POINT anchor{selection_.right-1,selection_.bottom-1};HMONITOR monitor=MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
    unsigned dpi=96;for(const auto& view:windows_)if(MonitorFromWindow(view->hwnd,MONITOR_DEFAULTTONEAREST)==monitor){dpi=GetDpiForWindow(view->hwnd);break;}
    // On narrow portrait work areas shrink toolbar UI only; geometry remains physical pixels.
    dpi=std::min(dpi,static_cast<unsigned>(std::max(48L,(info.rcWork.right-info.rcWork.left-12)*96/584)));
    if(dpi!=toolbarDpi_)layoutToolbar(dpi);
    int x=selection_.right-toolbarWidth_,y=selection_.bottom+10;
    if(y+toolbarHeight_>info.rcWork.bottom)y=selection_.top-toolbarHeight_-10;
    x=std::clamp(x,static_cast<int>(info.rcWork.left)+4,std::max(static_cast<int>(info.rcWork.left)+4,static_cast<int>(info.rcWork.right)-toolbarWidth_-4));
    y=std::clamp(y,static_cast<int>(info.rcWork.top)+4,std::max(static_cast<int>(info.rcWork.top)+4,static_cast<int>(info.rcWork.bottom)-toolbarHeight_-4));
    RECT current{};GetWindowRect(toolbar_,&current);
    if(current.left==x && current.top==y && current.right-current.left==toolbarWidth_ && current.bottom-current.top==toolbarHeight_ && IsWindowVisible(toolbar_))return;
    SetWindowPos(toolbar_,HWND_TOPMOST,x,y,toolbarWidth_,toolbarHeight_,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void OverlaySession::refreshButtons() {
    if(!toolbar_)return;
    for(auto& button:buttons_) {
        auto label=button.label;bool enabled=true;
        switch(button.id) {
        case WidthId:label=L"Width: "+std::to_wstring(static_cast<int>(settings_.strokeWidth))+L" px";break;
        case TextSizeId:label=L"Text: "+std::to_wstring(static_cast<int>(settings_.textSize))+L" px";break;
        case CensorModeId:label=pixelated_?L"Cover: pixelate":L"Cover: black";break;
        case UndoId:enabled=!busy_ && history_.canUndo();break;
        case RedoId:enabled=!busy_ && history_.canRedo();break;
        case CopyId:case SaveId:case SaveAsId:enabled=!busy_ && !selection_.empty();break;
        }
        const bool selected=button.id>=ToolBase && button.id<ToolBase+9 && tool_==toolAt(button.id);
        const std::optional<Pixel> swatch=button.id==ColorId?std::optional{settings_.color}:std::nullopt;
        const bool labelChanged=label!=button.label,enabledChanged=enabled!=button.enabled;
        const bool appearanceChanged=selected!=button.selected || swatch!=button.swatch;
        button.label=std::move(label);button.enabled=enabled;button.selected=selected;button.swatch=swatch;
        if(labelChanged)SetWindowTextW(button.hwnd,button.label.c_str());
        if(enabledChanged)EnableWindow(button.hwnd,enabled);
        if(appearanceChanged && !labelChanged && !enabledChanged)InvalidateRect(button.hwnd,nullptr,FALSE);
    }
}
RECT OverlaySession::statusRect() const {
    RECT bounds{};GetClientRect(toolbar_,&bounds);
    return {MulDiv(12,toolbarDpi_,96),MulDiv(118,toolbarDpi_,96),bounds.right-8,bounds.bottom};
}
void OverlaySession::refreshStatus() {
    if(!toolbar_)return;
    auto status=busy_?L"Exporting\u2026":std::to_wstring(selection_.width())+L" \u00d7 "+std::to_wstring(selection_.height())+L" px  \u00b7  "+(desktop_->intersectsHdr(selection_)?L"HDR + SDR":L"SDR")+L"  \u00b7  Esc to cancel";
    if(status==toolbarStatus_)return;toolbarStatus_=std::move(status);
    const auto line=statusRect();InvalidateRect(toolbar_,&line,FALSE);
}
void OverlaySession::command(int id) {
    if(busy_)return;
    const bool formatting=id==ColorId || id==TextSizeId;
    if(!formatting)commitText();
    if(id>=ToolBase && id<ToolBase+9){tool_=toolAt(id);refreshButtons();repaint();return;}
    switch(id) {
    case UndoId:history_.undo();refreshButtons();repaint();break;
    case RedoId:history_.redo();refreshButtons();repaint();break;
    case CopyId:if(!selection_.empty())action_(SessionAction::Copy);break;
    case SaveId:if(!selection_.empty())action_(SessionAction::QuickSave);break;
    case SaveAsId:if(!selection_.empty())action_(SessionAction::SaveAs);break;
    case CancelId:action_(SessionAction::Cancel);break;
    case CensorModeId:pixelated_=!pixelated_;refreshButtons();break;
    case ColorId: {
        static COLORREF custom[16]{};CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=toolbar_;choose.Flags=CC_FULLOPEN|CC_RGBINIT;choose.lpCustColors=custom;
        choose.rgbResult=RGB(static_cast<int>(settings_.color.r*255),static_cast<int>(settings_.color.g*255),static_cast<int>(settings_.color.b*255));
        if(ChooseColorW(&choose))settings_.color={GetRValue(choose.rgbResult)/255.f,GetGValue(choose.rgbResult)/255.f,GetBValue(choose.rgbResult)/255.f,1};break;
    }
    case WidthId:case TextSizeId: {
        HMENU menu=CreatePopupMenu();const std::vector<int> values=id==WidthId?std::vector<int>{1,2,3,5,8,12,18,24}:std::vector<int>{8,12,16,20,24,32,48,72,96,144};
        for(const int value:values) {auto label=std::to_wstring(value)+L" px";AppendMenuW(menu,MF_STRING,value,label.c_str());}
        RECT bounds{};GetWindowRect(GetDlgItem(toolbar_,id),&bounds);
        UINT chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,bounds.left,bounds.bottom,0,toolbar_,nullptr);DestroyMenu(menu);
        if(chosen){if(id==WidthId)settings_.strokeWidth=static_cast<float>(chosen);else settings_.textSize=static_cast<float>(chosen);}if(id==WidthId)refreshButtons();break;
    }
    }
    if(formatting) {
        if(text_.active()){text_.format(settings_.color,settings_.textSize);SetFocus(textWindow_);if(textStore_)textStore_->focus(textWindow_);}
        else refreshButtons();
    }
}
void OverlaySession::paintToolbar(HDC dc,const RECT& dirty) {
    FillRect(dc,&dirty,darkBrush_);
    auto old=SelectObject(dc,toolbarFont_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(164,177,195));
    RECT line=statusRect(),intersection{};
    if(IntersectRect(&intersection,&line,&dirty))DrawTextW(dc,toolbarStatus_.c_str(),-1,&line,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);
}
LRESULT OverlaySession::toolbarMessage(UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)command(LOWORD(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(toolbar_,&paint);paintToolbar(dc,paint.rcPaint);
        EndPaint(toolbar_,&paint);return 0;
    }
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->CtlType!=ODT_BUTTON)break;
        bool selected=item->CtlID>=ToolBase && item->CtlID<ToolBase+9 && tool_==toolAt(item->CtlID);
        bool pressed=(item->itemState&ODS_SELECTED)!=0,disabled=(item->itemState&ODS_DISABLED)!=0;
        COLORREF fill=selected?RGB(34,88,125):pressed?RGB(66,73,86):RGB(43,48,59);
        HBRUSH brush=CreateSolidBrush(fill);FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
        SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,disabled?RGB(105,115,130):RGB(239,244,250));
        auto old=SelectObject(item->hDC,toolbarFont_);wchar_t label[128]{};GetWindowTextW(item->hwndItem,label,128);RECT r=item->rcItem;
        if(item->CtlID==ColorId) {
            RECT swatch{r.left+MulDiv(9,toolbarDpi_,96),r.top+MulDiv(8,toolbarDpi_,96),r.left+MulDiv(23,toolbarDpi_,96),r.bottom-MulDiv(8,toolbarDpi_,96)};
            brush=CreateSolidBrush(RGB(static_cast<int>(settings_.color.r*255),static_cast<int>(settings_.color.g*255),static_cast<int>(settings_.color.b*255)));FillRect(item->hDC,&swatch,brush);DeleteObject(brush);r.left+=MulDiv(20,toolbarDpi_,96);
        }
        DrawTextW(item->hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        if(item->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(item->hDC,&r);}SelectObject(item->hDC,old);return TRUE;
    }
    case WM_DPICHANGED:placeToolbar();return 0;
    case WM_CLOSE:if(!busy_)action_(SessionAction::Cancel);return 0;
    }
    return DefWindowProcW(toolbar_,msg,wp,lp);
}
}
