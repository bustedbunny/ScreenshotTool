#include "app.hpp"
#include <shellapi.h>
#include <future>
#include <commctrl.h>

namespace shot {
namespace {
constexpr UINT TrayMessage=WM_APP+2,CapturedMessage=WM_APP+3,ExportedMessage=WM_APP+4,FailureMessage=WM_APP+5,ActionMessage=WM_APP+6;
constexpr UINT CheckUpdatesMessage=WM_APP+7,UpdateCheckedMessage=WM_APP+8,UpdateDownloadedMessage=WM_APP+9,PendingUpdateMessage=WM_APP+10;
constexpr UINT CaptureId=100,FolderId=101,SettingsId=102,ExitId=103,CheckUpdatesId=104,InstallUpdateId=105,ReleasePageId=106;
struct DownloadResult {std::optional<PreparedUpdate> update;std::optional<Message> error;};
struct CaptureResult {unsigned generation{};std::shared_ptr<const DesktopImage> desktop;std::optional<Message> error;};
struct ExportResult {unsigned generation{};SessionAction action{};EncodedImage image;std::vector<std::filesystem::path> paths;std::optional<Message> error;};
HICON makeIcon() {
    // A small native vector-drawn tray glyph, no external assets or runtime files.
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPV5HEADER header{};header.bV5Size=sizeof(header);header.bV5Width=32;header.bV5Height=-32;header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5RedMask=0xff0000;header.bV5GreenMask=0xff00;header.bV5BlueMask=0xff;header.bV5AlphaMask=0xff000000;
    void* pixels{};HBITMAP color=CreateDIBSection(screen,reinterpret_cast<BITMAPINFO*>(&header),DIB_RGB_COLORS,&pixels,nullptr,0);HBITMAP mask=CreateBitmap(32,32,1,1,nullptr);
    HICON icon{};
    if(color && mask && pixels) {
        auto* p=static_cast<DWORD*>(pixels);std::fill(p,p+1024,0);
        for(int y=5;y<27;++y)for(int x=5;x<27;++x)if(x<8 || x>23 || y<8 || y>23)p[y*32+x]=0xff58bdff;
        for(int y=12;y<20;++y)for(int x=12;x<20;++x)p[y*32+x]=0xffeef6ff;
        ICONINFO info{};info.fIcon=TRUE;info.hbmMask=mask;info.hbmColor=color;icon=CreateIconIndirect(&info);
    }
    if(color)DeleteObject(color);if(mask)DeleteObject(mask);DeleteDC(dc);ReleaseDC(nullptr,screen);return icon;
}
}
App* App::active_{};
App::App(HINSTANCE instance,Settings settings):instance_(instance),settings_(std::move(settings)),language_(settings_.effectiveLanguage()) {
    active_=this;icon_=makeIcon();
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance_;wc.lpfnWndProc=windowProc;wc.lpszClassName=L"ScreenshotTool.Controller";wc.hIcon=icon_;
    wincheck(RegisterClassExW(&wc)!=0,"Register app window");
    window_=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"ScreenshotTool",WS_POPUP,0,0,0,0,nullptr,nullptr,instance_,this);
    wincheck(window_!=nullptr,"Create tray app");taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
    addTray();
    // Windows can silently remove a low-level hook if its owning thread is
    // blocked by GPU work or a modal loop. Keep that message pump independent.
    std::promise<std::pair<DWORD,DWORD>> ready;auto initialized=ready.get_future();
    keyboardThread_=std::jthread([instance,ready=std::move(ready)]() mutable {
        MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        HHOOK hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboardProc,instance,0);
        const DWORD error=hook?ERROR_SUCCESS:GetLastError();ready.set_value({GetCurrentThreadId(),error});
        if(!hook)return;
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        UnhookWindowsHookEx(hook);
    });
    const auto [threadId,hookError]=initialized.get();keyboardThreadId_=threadId;
    check(HRESULT_FROM_WIN32(hookError),"Register Print Screen handler");
    SetTimer(window_,1,1000,nullptr);
}
App::~App() {
    exiting_=true;worker_.request_stop();if(worker_.joinable())worker_.join();session_.reset();
    updateWorker_.request_stop();if(updateWorker_.joinable())updateWorker_.join();
    if(keyboardThreadId_)PostThreadMessageW(keyboardThreadId_,WM_QUIT,0,0);
    if(keyboardThread_.joinable())keyboardThread_.join();
    if(trayAdded_) {NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);}
    if(window_) {
        MSG message{};
        while(PeekMessageW(&message,window_,CapturedMessage,PendingUpdateMessage,PM_REMOVE)) {
            if(message.message==CapturedMessage)delete reinterpret_cast<CaptureResult*>(message.lParam);
            if(message.message==ExportedMessage)delete reinterpret_cast<ExportResult*>(message.lParam);
            if(message.message==FailureMessage)delete reinterpret_cast<Message*>(message.lParam);
            if(message.message==UpdateCheckedMessage)delete reinterpret_cast<UpdateCheckResult*>(message.lParam);
            if(message.message==UpdateDownloadedMessage)delete reinterpret_cast<DownloadResult*>(message.lParam);
        }
        DestroyWindow(window_);
    }
    if(icon_)DestroyIcon(icon_);active_=nullptr;
}
int App::run(bool immediate) {
    if(immediate)PostMessageW(window_,CaptureMessage,0,0);
    else notify(format(language_,TextId::Ready));
    if(settings_.automaticUpdates)PostMessageW(window_,CheckUpdatesMessage,0,0);
    MSG msg{};int result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) {
        if(session_ && session_->translate(msg))continue;
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    return result<0?1:static_cast<int>(msg.wParam);
}
LRESULT CALLBACK App::windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));app->window_=hwnd;}
    if(app)try{return app->message(message,wp,lp);}catch(const std::exception& e){app->error(errorMessage(e,TextId::OperationFailed));return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
LRESULT CALLBACK App::keyboardProc(int code,WPARAM wp,LPARAM lp) {
    if(code==HC_ACTION && active_) {
        const auto* event=reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if(event->vkCode==VK_SNAPSHOT) {
            const bool down=wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN;
            const bool modified=(GetAsyncKeyState(VK_CONTROL)&0x8000) || (GetAsyncKeyState(VK_SHIFT)&0x8000) || (GetAsyncKeyState(VK_MENU)&0x8000) || (GetAsyncKeyState(VK_LWIN)&0x8000) || (GetAsyncKeyState(VK_RWIN)&0x8000) || (event->flags&LLKHF_ALTDOWN);
            const auto action=active_->gate_.key(down,modified);
            if(action.trigger)PostMessageW(active_->window_,CaptureMessage,0,0);
            if(action.suppress)return 1;
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
LRESULT App::message(UINT message,WPARAM wp,LPARAM lp) {
    if(taskbarCreated_ && message==taskbarCreated_) {addTray();return 0;}
    switch(message) {
    case CaptureMessage:capture();return 0;
    case CapturedMessage:captureFinished(lp);return 0;
    case ExportedMessage:exportFinished(lp);return 0;
    case FailureMessage: {
        std::unique_ptr<Message> detail(reinterpret_cast<Message*>(lp));endSession();error(*detail);return 0;
    }
    case ActionMessage:action(static_cast<SessionAction>(wp));return 0;
    case CheckUpdatesMessage:checkUpdates(false);return 0;
    case UpdateCheckedMessage:updateChecked(lp);return 0;
    case UpdateDownloadedMessage:updateDownloaded(lp);return 0;
    case PendingUpdateMessage:processPendingUpdate();return 0;
    case TrayMessage:
        if(LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==WM_RBUTTONUP)trayMenu();
        else if(LOWORD(lp)==NIN_BALLOONUSERCLICK && updateBalloon_){pendingUpdatePrompt_=true;processPendingUpdate();}
        else if(LOWORD(lp)==NIN_SELECT || LOWORD(lp)==WM_LBUTTONDBLCLK)capture();return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case CaptureId:capture();break;
        case FolderId: {auto folder=picturesDirectory();std::filesystem::create_directories(folder);auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));if(result<=32)throw AppError(TextId::OpenFolderFailed);break;}
        case SettingsId:if(state_.get()==State::Idle && !dialogOpen_ && !updateHandoff_) {
            dialogOpen_=true;
            try{settings_.show(window_);language_=settings_.effectiveLanguage();refreshTrayLanguage();}
            catch(const std::exception& e){dialogOpen_=false;error(errorMessage(e,TextId::SettingsFailed));PostMessageW(window_,PendingUpdateMessage,0,0);break;}
            dialogOpen_=false;PostMessageW(window_,PendingUpdateMessage,0,0);
        }break;
        case CheckUpdatesId:checkUpdates(true);break;
        case InstallUpdateId:pendingUpdatePrompt_=true;processPendingUpdate();break;
        case ReleasePageId:openReleasePage();break;
        case ExitId:exiting_=true;endSession();PostQuitMessage(0);break;
        }return 0;
    case WM_TIMER:
        if(session_ && state_.get()==State::Editing)try{session_->validateDisplays();}catch(const std::exception& e){endSession();error(errorMessage(e,TextId::RenderFailed));}return 0;
    case WM_DISPLAYCHANGE:
        if(state_.get()!=State::Idle){endSession();error({TextId::DisplayLayoutChanged});}return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(wp){exiting_=true;endSession();PostQuitMessage(0);}return 0;
    case WM_CLOSE:exiting_=true;endSession();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window_,message,wp,lp);
}
void App::capture() {
    if(dialogOpen_ || updateHandoff_ || exiting_)return;
    if(!state_.beginCapture())return;
    const unsigned generation=++generation_;const HWND target=window_;
    worker_=std::jthread([generation,target](std::stop_token stop) {
        auto result=std::make_unique<CaptureResult>();result->generation=generation;
        try {ComApartment com(COINIT_MULTITHREADED);result->desktop=CaptureService{}.capture(stop);}catch(const std::exception& e){result->error=errorMessage(e,TextId::CaptureFailed);}
        if(PostMessageW(target,CapturedMessage,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    });
}
void App::captureFinished(LPARAM raw) {
    std::unique_ptr<CaptureResult> result(reinterpret_cast<CaptureResult*>(raw));if(result->generation!=generation_)return;
    if(result->error){state_.reset();error(*result->error);PostMessageW(window_,PendingUpdateMessage,0,0);return;}
    desktop_=std::move(result->desktop);
    try {
        session_=std::make_unique<OverlaySession>(instance_,desktop_,settings_,[this](SessionAction a){PostMessageW(window_,ActionMessage,static_cast<WPARAM>(a),0);},[this](Message message){auto value=std::make_unique<Message>(std::move(message));if(PostMessageW(window_,FailureMessage,0,reinterpret_cast<LPARAM>(value.get())))value.release();});
        state_.captured();session_->show();
    } catch(const std::exception& e) {endSession();error(errorMessage(e,TextId::RenderFailed));}
}
void App::action(SessionAction action) {
    if(action==SessionAction::Cancel) {if(state_.get()!=State::Exporting)endSession();return;}
    if(!session_ || session_->selection().empty() || !state_.beginExport())return;
    try {
        session_->commitText();std::optional<std::filesystem::path> path;
        if(action==SessionAction::SaveAs) {
            path=ExportService::chooseSavePath(session_->owner(),picturesDirectory(),language_);
            if(!path){state_.exportFailed();return;}
        }
        session_->busy(true);
        const auto generation=generation_;const auto desktop=desktop_;const auto crop=session_->selection();
        const auto visible=session_->annotations();std::vector<Annotation> annotations(visible.begin(),visible.end());const HWND target=window_;
        worker_=std::jthread([generation,desktop,crop,annotations=std::move(annotations),action,path,target](std::stop_token) {
            auto result=std::make_unique<ExportResult>();result->generation=generation;result->action=action;
            try {
                ComApartment com(COINIT_MULTITHREADED);
                result->image=ExportService{}.prepare(*desktop,crop,annotations,action==SessionAction::QuickSave && desktop->intersectsHdr(crop));
                if(action==SessionAction::QuickSave)result->paths=ExportService::quickSave(result->image,picturesDirectory());
                else if(action==SessionAction::SaveAs) {ExportService::saveAs(result->image,*path);result->paths.push_back(*path);}
            }catch(const std::exception& e){result->error=errorMessage(e,TextId::ExportFailed);}
            if(PostMessageW(target,ExportedMessage,0,reinterpret_cast<LPARAM>(result.get())))result.release();
        });
    }catch(const std::exception& e) {state_.exportFailed();if(session_)session_->busy(false);error(errorMessage(e,TextId::ExportFailed),TextId::ExportRetry);}
}
void App::exportFinished(LPARAM raw) {
    std::unique_ptr<ExportResult> result(reinterpret_cast<ExportResult*>(raw));if(result->generation!=generation_ || !session_)return;
    if(!result->error && result->action==SessionAction::Copy)try{ExportService::copy(window_,result->image);}catch(const std::exception& e){result->error=errorMessage(e,TextId::ExportFailed);}
    if(result->error) {state_.exportFailed();session_->busy(false);error(*result->error,TextId::ExportRetry);return;}
    const auto action=result->action;const auto paths=result->paths;endSession();
    if(action==SessionAction::Copy)notify(format(language_,TextId::Copied));
    else if(!paths.empty())notify(format(language_,TextId::Saved,{std::to_wstring(paths.size()),paths.front().parent_path().wstring()}));
}
void App::endSession() {
    ++generation_;worker_.request_stop();session_.reset();desktop_.reset();state_.reset();
    if(!exiting_)try{settings_.save();}catch(const std::exception& e){notify(errorMessage(e,TextId::SaveSettingsFailed).render(language_),true);}
    if(!exiting_)PostMessageW(window_,PendingUpdateMessage,0,0);
}
void App::addTray() {
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayMessage;
    data.hIcon=icon_?icon_:LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(data.szTip,abbreviate(text(language_,TextId::TrayTip),std::size(data.szTip)-1).c_str());
    wincheck(Shell_NotifyIconW(NIM_ADD,&data),"Add system tray icon");trayAdded_=true;data.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data);
}
void App::refreshTrayLanguage() {
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;data.uFlags=NIF_TIP;
    wcscpy_s(data.szTip,abbreviate(text(language_,TextId::TrayTip),std::size(data.szTip)-1).c_str());
    Shell_NotifyIconW(NIM_MODIFY,&data);
}
void App::trayMenu() {
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,CaptureId,text(language_,TextId::CaptureMenu).data());AppendMenuW(menu,MF_STRING,FolderId,text(language_,TextId::OpenFolder).data());AppendMenuW(menu,MF_STRING|(state_.get()!=State::Idle?MF_GRAYED:0),SettingsId,text(language_,TextId::Settings).data());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,ExitId,text(language_,TextId::Exit).data());
    InsertMenuW(menu,3,MF_BYPOSITION|MF_STRING|(updateBusy_?MF_GRAYED:0),CheckUpdatesId,text(language_,updateBusy_?TextId::UpdateBusy:TextId::CheckUpdates).data());
    const bool installAvailable=updateRelease_ && updateCompatible_;
    if(installAvailable)InsertMenuW(menu,4,MF_BYPOSITION|MF_STRING|(updateBusy_?MF_GRAYED:0),InstallUpdateId,text(language_,TextId::InstallUpdate).data());
    InsertMenuW(menu,installAvailable?5:4,MF_BYPOSITION|MF_STRING,ReleasePageId,text(language_,TextId::ViewReleases).data());
    POINT point{};GetCursorPos(&point);SetForegroundWindow(window_);TrackPopupMenu(menu,TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);DestroyMenu(menu);PostMessageW(window_,WM_NULL,0,0);
}
void App::notify(const std::wstring& text,bool error) {
    updateBalloon_=false;
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window_;data.uID=1;data.uFlags=NIF_INFO;data.dwInfoFlags=error?NIIF_ERROR:NIIF_INFO;
    wcscpy_s(data.szInfoTitle,L"ScreenshotTool");wcscpy_s(data.szInfo,abbreviate(text,std::size(data.szInfo)-1).c_str());Shell_NotifyIconW(NIM_MODIFY,&data);
}
void App::checkUpdates(bool manual) {
    if(exiting_ || updateBusy_)return;
    if(std::chrono::steady_clock::now()<updateRetryAfter_){if(manual)notify(format(language_,TextId::RateLimited),true);return;}
    updateBusy_=true;manualUpdateCheck_=manual;const auto target=window_;
    updateWorker_=std::jthread([target](std::stop_token stop) {
        auto result=std::make_unique<UpdateCheckResult>(UpdateService::check(stop));
        if(PostMessageW(target,UpdateCheckedMessage,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    });
}
void App::updateChecked(LPARAM raw) {
    std::unique_ptr<UpdateCheckResult> result(reinterpret_cast<UpdateCheckResult*>(raw));updateBusy_=false;if(exiting_)return;
    if(result->status==UpdateStatus::RateLimited)updateRetryAfter_=std::chrono::steady_clock::now()+std::chrono::seconds(result->retryAfterSeconds);
    if(result->status==UpdateStatus::Available) {
        updateCompatible_=true;updateRelease_=std::move(result->release);pendingUpdateNotification_=true;processPendingUpdate();
    }else if(result->status==UpdateStatus::Incompatible) {
        updateCompatible_=false;updateRelease_=std::move(result->release);pendingUpdateNotification_=true;processPendingUpdate();
    }else {
        if(result->status==UpdateStatus::Current || result->status==UpdateStatus::NoRelease){updateRelease_.reset();pendingUpdateNotification_=pendingUpdatePrompt_=false;}
        if(manualUpdateCheck_ && result->status!=UpdateStatus::Canceled)notify(result->message.render(language_),result->status==UpdateStatus::Failed || result->status==UpdateStatus::RateLimited);
    }
}
void App::processPendingUpdate() {
    if(exiting_ || dialogOpen_ || updateHandoff_ || state_.get()!=State::Idle)return;
    if(preparedUpdate_){installPreparedUpdate();return;}
    if(pendingUpdatePrompt_ && updateRelease_ && !updateBusy_){pendingUpdatePrompt_=false;promptUpdate();return;}
    if(pendingUpdateNotification_ && updateRelease_) {
        pendingUpdateNotification_=false;
        notify(format(language_,updateCompatible_?TextId::UpdateAvailable:TextId::UpdateManualAvailable,{updateRelease_->version.text()}));updateBalloon_=true;
    }
}
void App::openReleasePage() {
    const auto url=updateRelease_?updateRelease_->releaseUrl:L"https://github.com/bustedbunny/ScreenshotTool/releases/latest";
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window_,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32)throw AppError(TextId::OpenReleaseFailed);
}
void App::promptUpdate() {
    if(!updateRelease_)return;
    if(!updateCompatible_){openReleasePage();return;}
    dialogOpen_=true;
    struct Reset {bool& flag;~Reset(){flag=false;}}reset{dialogOpen_};
    const auto instruction=format(language_,TextId::InstallQuestion,{updateRelease_->version.text()});
    TASKDIALOG_BUTTON buttons[]{{IDOK,text(language_,TextId::DownloadRestart).data()},{ReleasePageId,text(language_,TextId::ViewNotes).data()},{IDCANCEL,text(language_,TextId::Cancel).data()}};
    TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=window_;config.pszWindowTitle=text(language_,TextId::UpdateTitle).data();
    config.pszMainInstruction=instruction.c_str();config.pszContent=text(language_,TextId::UpdateExplanation).data();
    config.cButtons=3;config.pButtons=buttons;config.nDefaultButton=IDOK;
    config.dwFlags=TDF_SIZE_TO_CONTENT|TDF_ALLOW_DIALOG_CANCELLATION;int selected{};
    check(TaskDialogIndirect(&config,&selected,nullptr,nullptr),"Confirm update");
    if(selected==ReleasePageId){openReleasePage();return;}
    if(selected!=IDOK)return;
    settings_.save();const auto release=*updateRelease_;const auto path=UpdateService::executablePath();const auto target=window_;updateBusy_=true;
    updateWorker_=std::jthread([release,path,target](std::stop_token stop) {
        auto result=std::make_unique<DownloadResult>();
        try{result->update=UpdateService::download(release,path,stop);}catch(const std::exception& error){result->error=errorMessage(error,TextId::UpdateFailed);}
        if(PostMessageW(target,UpdateDownloadedMessage,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    });
    notify(format(language_,TextId::Downloading));
}
void App::updateDownloaded(LPARAM raw) {
    std::unique_ptr<DownloadResult> result(reinterpret_cast<DownloadResult*>(raw));updateBusy_=false;if(exiting_)return;
    if(result->error){notify(result->error->render(language_,TextId::ManualInstallHelp),true);return;}
    preparedUpdate_=std::move(result->update);processPendingUpdate();
}
void App::installPreparedUpdate() {
    if(!preparedUpdate_)return;updateHandoff_=true;
    try {
        settings_.save();UpdateService::launchHelper(*preparedUpdate_);preparedUpdate_.reset();
        exiting_=true;endSession();PostQuitMessage(0);
    }catch(const std::exception& error) {
        updateHandoff_=false;preparedUpdate_.reset();notify(errorMessage(error,TextId::UpdateFailed).render(language_,TextId::ManualInstallHelp),true);
    }
}
void App::error(const Message& message,TextId guidance) {
    const auto content=message.render(language_,guidance);
    MessageBoxW(session_?session_->owner():window_,content.c_str(),L"ScreenshotTool",MB_OK|MB_ICONERROR|MB_TOPMOST);
}
}
