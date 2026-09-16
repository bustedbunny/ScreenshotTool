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
const wchar_t* toolTips[]{L"Select: move or resize the crop. Drawing stays anchored to the desktop.",L"Pen: draw a freehand stroke.",L"Highlighter: draw a translucent wide stroke.",L"Rectangle: draw an outline.",L"Ellipse: draw an oval outline.",L"Line: drag between two points.",L"Arrow: point at a detail.",L"Text: click, type, then Ctrl+Enter or click outside to finish.",L"Censor: drag an opaque black cover (or choose pixelation)."};
}
OverlaySession::OverlaySession(HINSTANCE instance,std::shared_ptr<const DesktopImage> desktop,Settings& settings,std::function<void(SessionAction)> action,std::function<void(std::wstring)> failure)
    :instance_(instance),desktop_(std::move(desktop)),settings_(settings),action_(std::move(action)),failure_(std::move(failure)) {
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance_;wc.lpfnWndProc=windowProc;wc.lpszClassName=OverlayClass;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)wincheck(FALSE,"Register overlay window");
    wc.lpfnWndProc=toolbarProc;wc.lpszClassName=ToolbarClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)wincheck(FALSE,"Register toolbar window");
    darkBrush_=CreateSolidBrush(RGB(27,30,37));
}
OverlaySession::~OverlaySession() {
    closing_=true;ReleaseCapture();
    if(textEdit_)DestroyWindow(textEdit_);
    if(textFont_)DeleteObject(textFont_);
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
    case WM_MOUSEMOVE:if(!busy_)mouseMove(cursorPoint());return 0;
    case WM_LBUTTONUP:if(!busy_)mouseUp(cursorPoint());return 0;
    case WM_CAPTURECHANGED:
        if(dragging_ && reinterpret_cast<HWND>(lp)!=view.hwnd) {
            // An interrupted initial drag has not established the first region yet.
            if(selecting_ && !firstRegionCompleted_)selection_=original_;
            dragging_=false;draft_.reset();placeToolbar();repaint();
        }return 0;
    case WM_SETCURSOR: {
        if(LOWORD(lp)!=HTCLIENT)break;
        LPCWSTR cursor=busy_?IDC_WAIT:IDC_CROSS;
        if(!busy_ && tool_==Tool::Select && !selection_.empty())switch(hitSelection(selection_,cursorPoint())) {
            case Handle::Move:cursor=IDC_SIZEALL;break;
            case Handle::N:case Handle::S:cursor=IDC_SIZENS;break;
            case Handle::E:case Handle::W:cursor=IDC_SIZEWE;break;
            case Handle::NW:case Handle::SE:cursor=IDC_SIZENWSE;break;
            case Handle::NE:case Handle::SW:cursor=IDC_SIZENESW;break;
            default:break;
        }
        if(textEdit_ && reinterpret_cast<HWND>(wp)==textEdit_)cursor=IDC_IBEAM;
        SetCursor(LoadCursorW(nullptr,cursor));return TRUE;
    }
    case WM_CTLCOLOREDIT: {
        auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,RGB(245,246,250));SetBkColor(dc,RGB(27,30,37));return reinterpret_cast<LRESULT>(darkBrush_);
    }
    case WM_DPICHANGED: // Physical desktop bounds are immutable; a live layout/DPI change aborts the session.
        if(IsWindowVisible(view.hwnd) && !closing_)failure_(L"Display scaling changed. Press Print Screen to capture the new layout.");return 0;
    case WM_CLOSE:if(!busy_)action_(SessionAction::Cancel);return 0;
    }
    return DefWindowProcW(view.hwnd,msg,wp,lp);
}
void OverlaySession::render(MonitorWindow& view) {
    const auto& monitor=desktop_->monitors[view.index];
    view.graphics->present(view.background.Get(),monitor.bounds,selection_,history_.visible(),draft_?&*draft_:nullptr,monitor.hdr?monitor.sdrWhiteNits/80.f:1.f,tool_==Tool::Select && !dragging_);
}
void OverlaySession::repaint() {for(auto& window:windows_)InvalidateRect(window->hwnd,nullptr,FALSE);refreshButtons();}
void OverlaySession::mouseDown(HWND hwnd,Point p) {
    commitText();SetFocus(hwnd);p=clampPoint(p,desktop_->bounds());
    dragStart_=p;original_=selection_;draft_.reset();
    selecting_=selection_.empty() || tool_==Tool::Select;
    if(selecting_) {
        handle_=hitSelection(selection_,p);
        if(handle_==Handle::None)selection_={};
    } else {
        if(!selection_.contains(p))return;
        if(tool_==Tool::Text){editText(hwnd,p);return;}
        draft_=Annotation{tool_,{p},settings_.color,settings_.strokeWidth,settings_.textSize,L"",pixelated_};
    }
    dragging_=true;SetCapture(hwnd);ShowWindow(toolbar_,SW_HIDE);repaint();
}
void OverlaySession::mouseMove(Point p) {
    if(!dragging_)return;p=clampPoint(p,desktop_->bounds());
    if(selecting_) {
        if(handle_==Handle::None)selection_=normalized(dragStart_,p);
        else selection_=adjustSelection(original_,handle_,{p.x-dragStart_.x,p.y-dragStart_.y},desktop_->bounds());
    } else if(draft_) {
        if(draft_->tool==Tool::Pen || draft_->tool==Tool::Highlighter) {if(p!=draft_->points.back())draft_->points.push_back(p);}
        else {if(draft_->points.size()==1)draft_->points.push_back(p);else draft_->points.back()=p;}
    }
    repaint();
}
void OverlaySession::mouseUp(Point p) {
    if(!dragging_)return;mouseMove(p);dragging_=false;ReleaseCapture();
    if(selecting_ && !firstRegionCompleted_ && !selection_.empty()) {
        firstRegionCompleted_=true;tool_=Tool::Pen;
    }
    if(draft_){history_.add(std::move(*draft_));draft_.reset();}
    placeToolbar();repaint();
}
void OverlaySession::editText(HWND hwnd,Point p) {
    textAnnotation_=Annotation{Tool::Text,{p},settings_.color,settings_.strokeWidth,settings_.textSize,L"",false};
    POINT local{p.x,p.y};ScreenToClient(hwnd,&local);RECT client{};GetClientRect(hwnd,&client);
    const int width=std::max(60,std::min(450,static_cast<int>(client.right-local.x)-4));
    const int height=std::max(40,std::min(180,static_cast<int>(client.bottom-local.y)-4));
    textEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,local.x,local.y,width,height,hwnd,nullptr,instance_,nullptr);
    wincheck(textEdit_!=nullptr,"Open annotation text editor");
    textFont_=CreateFontW(-static_cast<int>(std::lround(settings_.textSize)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    SendMessageW(textEdit_,WM_SETFONT,reinterpret_cast<WPARAM>(textFont_),TRUE);SendMessageW(textEdit_,EM_SETLIMITTEXT,16384,0);SetFocus(textEdit_);
}
void OverlaySession::commitText(bool discard) {
    if(!textEdit_)return;
    if(!discard && textAnnotation_) {
        int length=GetWindowTextLengthW(textEdit_);std::wstring text(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(textEdit_,text.data(),length+1);text.resize(length);
        if(!text.empty()){textAnnotation_->text=std::move(text);history_.add(std::move(*textAnnotation_));}
    }
    HWND edit=textEdit_;textEdit_=nullptr;DestroyWindow(edit);textAnnotation_.reset();
    if(textFont_){DeleteObject(textFont_);textFont_=nullptr;}
    repaint();
}
bool OverlaySession::translate(MSG& msg) {
    if(msg.message!=WM_KEYDOWN && msg.message!=WM_SYSKEYDOWN)return false;
    const HWND focus=GetFocus();bool ours=focus==toolbar_ || (toolbar_ && IsChild(toolbar_,focus));
    for(const auto& window:windows_)ours=ours || focus==window->hwnd || IsChild(window->hwnd,focus);
    if(!ours)return false; // Native dialogs retain all of their keyboard behavior.
    if(textEdit_ && focus==textEdit_) {
        if(msg.wParam==VK_ESCAPE){commitText(true);SetFocus(owner());return true;}
        if(msg.wParam==VK_RETURN && controlDown()){commitText();SetFocus(owner());return true;}
        return false; // Ctrl+C/S/Z/Y, selection, IME, and clipboard belong to the edit control.
    }
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
    if(!value){SetForegroundWindow(owner());SetFocus(owner());}refreshButtons();
}
void OverlaySession::validateDisplays() {
    if(errorReported_)return;
    for(const auto& view:windows_)if(!view->graphics->isCurrent())throw std::runtime_error("Display configuration changed. Press Print Screen to capture the new layout.");
    for(const auto& monitor:desktop_->monitors)if(monitor.hdr && std::abs(querySdrWhite(monitor.deviceName,true)-monitor.sdrWhiteNits)>0.5f)
        throw std::runtime_error("Windows SDR brightness changed. Press Print Screen to capture at the new brightness.");
}
void OverlaySession::createToolbar() {
    toolbar_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,ToolbarClass,L"ScreenshotTool tools",WS_POPUP|WS_BORDER,0,0,580,140,owner(),nullptr,instance_,this);
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
    layoutToolbar(96);refreshButtons();
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
    if(!toolbar_ || selection_.empty() || dragging_){if(toolbar_)ShowWindow(toolbar_,SW_HIDE);return;}
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
    SetWindowPos(toolbar_,HWND_TOPMOST,x,y,toolbarWidth_,toolbarHeight_,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(toolbar_,nullptr,FALSE);
}
void OverlaySession::refreshButtons() {
    if(!toolbar_)return;
    SetDlgItemTextW(toolbar_,WidthId,(L"Width: "+std::to_wstring(static_cast<int>(settings_.strokeWidth))+L" px").c_str());
    SetDlgItemTextW(toolbar_,TextSizeId,(L"Text: "+std::to_wstring(static_cast<int>(settings_.textSize))+L" px").c_str());
    SetDlgItemTextW(toolbar_,CensorModeId,pixelated_?L"Cover: pixelate":L"Cover: black");
    EnableWindow(GetDlgItem(toolbar_,UndoId),!busy_ && history_.canUndo());EnableWindow(GetDlgItem(toolbar_,RedoId),!busy_ && history_.canRedo());
    for(int id:{CopyId,SaveId,SaveAsId})EnableWindow(GetDlgItem(toolbar_,id),!busy_ && !selection_.empty());
    InvalidateRect(toolbar_,nullptr,FALSE);
    for(const auto& button:buttons_)InvalidateRect(button.hwnd,nullptr,FALSE);
}
void OverlaySession::command(int id) {
    if(busy_)return;commitText();
    if(id>=ToolBase && id<ToolBase+9){tool_=toolAt(id);refreshButtons();repaint();return;}
    switch(id) {
    case UndoId:history_.undo();repaint();break;
    case RedoId:history_.redo();repaint();break;
    case CopyId:if(!selection_.empty())action_(SessionAction::Copy);break;
    case SaveId:if(!selection_.empty())action_(SessionAction::QuickSave);break;
    case SaveAsId:if(!selection_.empty())action_(SessionAction::SaveAs);break;
    case CancelId:action_(SessionAction::Cancel);break;
    case CensorModeId:pixelated_=!pixelated_;refreshButtons();break;
    case ColorId: {
        static COLORREF custom[16]{};CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=toolbar_;choose.Flags=CC_FULLOPEN|CC_RGBINIT;choose.lpCustColors=custom;
        choose.rgbResult=RGB(static_cast<int>(settings_.color.r*255),static_cast<int>(settings_.color.g*255),static_cast<int>(settings_.color.b*255));
        if(ChooseColorW(&choose))settings_.color={GetRValue(choose.rgbResult)/255.f,GetGValue(choose.rgbResult)/255.f,GetBValue(choose.rgbResult)/255.f,1};refreshButtons();break;
    }
    case WidthId:case TextSizeId: {
        HMENU menu=CreatePopupMenu();const std::vector<int> values=id==WidthId?std::vector<int>{1,2,3,5,8,12,18,24}:std::vector<int>{8,12,16,20,24,32,48,72,96,144};
        for(const int value:values) {auto label=std::to_wstring(value)+L" px";AppendMenuW(menu,MF_STRING,value,label.c_str());}
        RECT bounds{};GetWindowRect(GetDlgItem(toolbar_,id),&bounds);
        UINT chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,bounds.left,bounds.bottom,0,toolbar_,nullptr);DestroyMenu(menu);
        if(chosen){if(id==WidthId)settings_.strokeWidth=static_cast<float>(chosen);else settings_.textSize=static_cast<float>(chosen);}refreshButtons();break;
    }
    }
}
LRESULT OverlaySession::toolbarMessage(UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)command(LOWORD(wp));return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(toolbar_,&paint);RECT bounds{};GetClientRect(toolbar_,&bounds);FillRect(dc,&bounds,darkBrush_);
        auto old=SelectObject(dc,toolbarFont_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(164,177,195));
        auto status=busy_?L"Exporting…":std::to_wstring(selection_.width())+L" × "+std::to_wstring(selection_.height())+L" px  ·  "+(desktop_->intersectsHdr(selection_)?L"HDR + SDR":L"SDR")+L"  ·  Esc to cancel";
        RECT line{MulDiv(12,toolbarDpi_,96),MulDiv(118,toolbarDpi_,96),bounds.right-8,bounds.bottom};DrawTextW(dc,status.c_str(),-1,&line,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,old);EndPaint(toolbar_,&paint);return 0;
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
