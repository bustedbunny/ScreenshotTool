#include "settings.hpp"
#include "export.hpp"
#include "version.hpp"
#include <shlobj.h>
#include <commctrl.h>
#include <exception>
#include <fstream>
#include <sstream>
#include "../resources/resource.h"

namespace shot {
namespace {
std::filesystem::path settingsPath() {
    PWSTR raw{};check(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw),"Locate local application data");
    std::filesystem::path path(raw);CoTaskMemFree(raw);return path/L"ScreenshotTool"/L"settings.ini";
}
constexpr auto runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
struct SignInValue {DWORD type{};std::vector<BYTE> data;};
std::optional<SignInValue> readSignInValue() {
    SignInValue value;DWORD bytes{};
    LONG status=RegGetValueW(HKEY_CURRENT_USER,runKey,L"ScreenshotTool",RRF_RT_ANY|RRF_NOEXPAND,&value.type,nullptr,&bytes);
    if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND)return std::nullopt;
    check(HRESULT_FROM_WIN32(status),"Read sign-in preference");value.data.resize(bytes);
    status=RegGetValueW(HKEY_CURRENT_USER,runKey,L"ScreenshotTool",RRF_RT_ANY|RRF_NOEXPAND,&value.type,value.data.data(),&bytes);
    check(HRESULT_FROM_WIN32(status),"Read sign-in preference");value.data.resize(bytes);return value;
}
void writeSignInValue(const std::optional<SignInValue>& value) {
    HKEY key{};LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,runKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);check(HRESULT_FROM_WIN32(status),"Open sign-in settings");
    struct Close{HKEY key;~Close(){RegCloseKey(key);}}close{key};
    if(value)status=RegSetValueExW(key,L"ScreenshotTool",0,value->type,value->data.data(),static_cast<DWORD>(value->data.size()));
    else {status=RegDeleteValueW(key,L"ScreenshotTool");if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;}
    check(HRESULT_FROM_WIN32(status),"Update sign-in preference");
}
struct SettingsDialog {
    Settings committed;
    bool signIn{},modeless{},heapOwned{};
    Language uiLanguage{Language::English};
    unsigned dpiOverride{};
    Settings* accepted{};
    std::function<void(const Settings&,bool)> commit;
    std::function<void()> applied;
    std::optional<Message> lastError;
    HFONT font{};
    std::exception_ptr initializationError;
    ~SettingsDialog(){if(font)DeleteObject(font);}
};
void setCaption(HWND window,int id,Language language,TextId textId) {
    const std::wstring caption(text(language,textId));SetDlgItemTextW(window,id,caption.c_str());
}
void layoutSettings(HWND window,SettingsDialog& state,bool center) {
    const UINT dpi=state.dpiOverride?state.dpiOverride:GetDpiForWindow(window);
    const auto scale=[dpi](int value){return MulDiv(value,static_cast<int>(dpi?dpi:96),96);};
    const HFONT font=createUiFont(state.uiLanguage,-MulDiv(9,static_cast<int>(dpi?dpi:96),72));
    wincheck(font!=nullptr,"Create settings font");
    const HFONT previous=state.font;state.font=font;
    SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for(int id:{IDC_SETTINGS_HEADER,IDC_SETTINGS_INSTRUCTIONS,IDC_SETTINGS_PRINT_SCREEN,IDC_AUTO_UPDATES,IDC_SIGN_IN,IDC_SIGN_IN_HELP,IDC_LANGUAGE_LABEL,IDC_LANGUAGE,IDOK,IDCANCEL,IDC_APPLY})
        SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(previous)DeleteObject(previous);
    HDC dc=GetDC(window);wincheck(dc!=nullptr,"Measure settings dialog");
    struct Release {HWND window;HDC dc;HGDIOBJ old;~Release(){SelectObject(dc,old);ReleaseDC(window,dc);}}release{window,dc,SelectObject(dc,font)};
    TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    const int margin=scale(18),width=scale(540),content=width-margin*2;
    auto measure=[&](int id,int available,bool wrap=true) {
        const auto control=GetDlgItem(window,id);const int length=GetWindowTextLengthW(control);
        std::wstring caption(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(control,caption.data(),length+1);caption.resize(length);
        RECT bounds{0,0,available,0};DrawTextW(dc,caption.c_str(),length,&bounds,DT_CALCRECT|DT_NOPREFIX|(wrap?DT_WORDBREAK:DT_SINGLELINE));
        return SIZE{bounds.right-bounds.left,std::max<int>(metrics.tmHeight,bounds.bottom-bounds.top)};
    };
    auto place=[&](int id,int x,int y,int w,int h) {SetWindowPos(GetDlgItem(window,id),nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);};
    int y=margin;
    for(int id:{IDC_SETTINGS_HEADER,IDC_SETTINGS_INSTRUCTIONS,IDC_SETTINGS_PRINT_SCREEN}) {
        const int height=measure(id,content).cy;place(id,margin,y,content,height);y+=height+scale(12);
    }
    for(int id:{IDC_AUTO_UPDATES,IDC_SIGN_IN}) {
        const int height=std::max<int>(scale(24),measure(id,content-scale(24)).cy+scale(6));
        place(id,margin,y,content,height);y+=height+scale(6);
    }
    const int helpHeight=measure(IDC_SIGN_IN_HELP,content).cy;place(IDC_SIGN_IN_HELP,margin,y,content,helpHeight);y+=helpHeight+scale(14);
    const int labelHeight=measure(IDC_LANGUAGE_LABEL,content).cy;place(IDC_LANGUAGE_LABEL,margin,y,content,labelHeight);y+=labelHeight+scale(5);
    place(IDC_LANGUAGE,margin,y,content,scale(190));
    SendDlgItemMessageW(window,IDC_LANGUAGE,CB_SETMINVISIBLE,7,0);
    SendDlgItemMessageW(window,IDC_LANGUAGE,CB_SETDROPPEDWIDTH,content,0);
    RECT combo{};GetWindowRect(GetDlgItem(window,IDC_LANGUAGE),&combo);y+=combo.bottom-combo.top+scale(18);
    const int buttonHeight=std::max(scale(28),static_cast<int>(metrics.tmHeight)+scale(10));
    int right=width-margin;
    for(int id:{IDC_APPLY,IDCANCEL,IDOK}) {
        const int buttonWidth=std::max<int>(scale(78),measure(id,content,false).cx+scale(28));
        right-=buttonWidth;place(id,right,y,buttonWidth,buttonHeight);right-=scale(10);
    }
    RECT outer{0,0,width,y+buttonHeight+margin};
    AdjustWindowRectExForDpi(&outer,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi?dpi:96);
    RECT current{};GetWindowRect(window,&current);int x=current.left,top=current.top;
    const int outerWidth=outer.right-outer.left,outerHeight=outer.bottom-outer.top;
    if(center) {
        x=(current.left+current.right-outerWidth)/2;top=(current.top+current.bottom-outerHeight)/2;
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            x=std::max(static_cast<int>(monitor.rcWork.left),std::min(x,static_cast<int>(monitor.rcWork.right)-outerWidth));
            top=std::max(static_cast<int>(monitor.rcWork.top),std::min(top,static_cast<int>(monitor.rcWork.bottom)-outerHeight));
        }
    }
    SetWindowPos(window,nullptr,x,top,outerWidth,outerHeight,SWP_NOZORDER|SWP_NOACTIVATE);
}
Settings pendingSettings(HWND window,const SettingsDialog& state) {
    auto pending=state.committed;pending.automaticUpdates=IsDlgButtonChecked(window,IDC_AUTO_UPDATES)==BST_CHECKED;
    const auto selection=SendDlgItemMessageW(window,IDC_LANGUAGE,CB_GETCURSEL,0,0);
    if(selection!=CB_ERR) {
        const auto language=SendDlgItemMessageW(window,IDC_LANGUAGE,CB_GETITEMDATA,selection,0);
        if(language!=CB_ERR)pending.language=static_cast<Language>(language);
    }
    return pending;
}
bool pendingSignIn(HWND window) {return IsDlgButtonChecked(window,IDC_SIGN_IN)==BST_CHECKED;}
void updateApplyState(HWND window,const SettingsDialog& state) {
    const auto pending=pendingSettings(window,state);
    const bool changed=pending.language!=state.committed.language || pending.automaticUpdates!=state.committed.automaticUpdates || pendingSignIn(window)!=state.signIn;
    if(!changed && GetFocus()==GetDlgItem(window,IDC_APPLY))SetFocus(GetDlgItem(window,IDOK));
    EnableWindow(GetDlgItem(window,IDC_APPLY),changed);
}
void initializeSettings(HWND window,SettingsDialog& state,bool center=true) {
    state.uiLanguage=state.committed.effectiveLanguage();
    const std::wstring title(text(state.uiLanguage,TextId::SettingsTitle));SetWindowTextW(window,title.c_str());
    const auto header=format(state.uiLanguage,TextId::SettingsHeader,{AppVersion});SetDlgItemTextW(window,IDC_SETTINGS_HEADER,header.c_str());
    setCaption(window,IDC_SETTINGS_INSTRUCTIONS,state.uiLanguage,TextId::SettingsInstructions);
    setCaption(window,IDC_SETTINGS_PRINT_SCREEN,state.uiLanguage,TextId::SettingsPrintScreen);
    setCaption(window,IDC_AUTO_UPDATES,state.uiLanguage,TextId::AutomaticUpdates);
    setCaption(window,IDC_SIGN_IN,state.uiLanguage,TextId::LaunchAtSignIn);
    setCaption(window,IDC_SIGN_IN_HELP,state.uiLanguage,TextId::SignInHelp);
    setCaption(window,IDC_LANGUAGE_LABEL,state.uiLanguage,TextId::LanguageLabel);
    setCaption(window,IDOK,state.uiLanguage,TextId::Ok);setCaption(window,IDCANCEL,state.uiLanguage,TextId::Cancel);setCaption(window,IDC_APPLY,state.uiLanguage,TextId::Apply);
    CheckDlgButton(window,IDC_AUTO_UPDATES,state.committed.automaticUpdates?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(window,IDC_SIGN_IN,state.signIn?BST_CHECKED:BST_UNCHECKED);
    const HWND combo=GetDlgItem(window,IDC_LANGUAGE);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    const std::wstring automatic(text(state.uiLanguage,TextId::Automatic));
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(automatic.c_str()));SendMessageW(combo,CB_SETITEMDATA,0,static_cast<LPARAM>(Language::Automatic));
    int selection=0,index=1;
    for(const auto language:Languages) {
        SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(languageName(language)));
        SendMessageW(combo,CB_SETITEMDATA,index,static_cast<LPARAM>(language));
        if(state.committed.language==language)selection=index;++index;
    }
    SendMessageW(combo,CB_SETCURSEL,selection,0);layoutSettings(window,state,center);updateApplyState(window,state);
}
void applySettings(HWND window,SettingsDialog& state) {
    const auto pending=pendingSettings(window,state);const bool signIn=pendingSignIn(window);
    if(state.commit)state.commit(pending,signIn);
    state.committed=pending;state.signIn=signIn;if(state.accepted)*state.accepted=pending;
    state.lastError.reset();if(state.applied)state.applied();
    updateApplyState(window,state);
}
void closeSettings(HWND window,const SettingsDialog& state,INT_PTR result) {
    if(state.modeless)DestroyWindow(window);else EndDialog(window,result);
}
INT_PTR CALLBACK settingsDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_INITDIALOG) {
        auto* state=reinterpret_cast<SettingsDialog*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);
        try{initializeSettings(window,*state);}catch(...){state->initializationError=std::current_exception();closeSettings(window,*state,IDCANCEL);}
        return TRUE;
    }
    auto* state=reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(!state)return FALSE;
    if(message==WM_NCDESTROY) {SetWindowLongPtrW(window,DWLP_USER,0);if(state->heapOwned)delete state;return FALSE;}
    if(message==WM_DPICHANGED) {
        const auto* suggested=reinterpret_cast<const RECT*>(lp);SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        try{layoutSettings(window,*state,false);}catch(...){state->initializationError=std::current_exception();closeSettings(window,*state,IDCANCEL);}
        return TRUE;
    }
    if(message==WM_COMMAND) {
        const auto id=LOWORD(wp),notification=HIWORD(wp);
        if((id==IDC_LANGUAGE && notification==CBN_SELCHANGE) || ((id==IDC_AUTO_UPDATES || id==IDC_SIGN_IN) && notification==BN_CLICKED)) {
            updateApplyState(window,*state);return TRUE;
        }
        if(id==IDCANCEL){closeSettings(window,*state,IDCANCEL);return TRUE;}
        if(id==IDOK || id==IDC_APPLY) {
            if(id==IDC_APPLY && !IsWindowEnabled(GetDlgItem(window,IDC_APPLY)))return TRUE;
            try {
                applySettings(window,*state);
                if(id==IDOK)closeSettings(window,*state,IDOK);
                else initializeSettings(window,*state,false);
            }catch(const std::exception& error) {
                state->lastError=errorMessage(error,TextId::SettingsFailed);
                if(!state->modeless) {
                    const auto content=state->lastError->render(state->uiLanguage);
                    MessageBoxW(window,content.c_str(),L"ScreenshotTool",MB_OK|MB_ICONERROR);
                }
            }
            return TRUE;
        }
    }
    if(message==WM_CLOSE){closeSettings(window,*state,IDCANCEL);return TRUE;}
    return FALSE;
}
}
Settings Settings::load() {
    std::ifstream file(settingsPath());return read(file);
}
Settings Settings::read(std::istream& file) {
    Settings settings;std::string line;
    while(std::getline(file,line)) {
        std::istringstream fields(line);std::string key;if(!(fields>>key))continue;
        if(key=="language") {std::string tag;fields>>tag;settings.language=parseLanguage(tag);continue;}
        float value{};if(!(fields>>value) || !std::isfinite(value))continue;
        if(key=="stroke")settings.strokeWidth=std::clamp(value,1.f,24.f);
        else if(key=="text")settings.textSize=std::clamp(value,8.f,144.f);
        else if(key=="red")settings.color.r=std::clamp(value,0.f,1.f);
        else if(key=="green")settings.color.g=std::clamp(value,0.f,1.f);
        else if(key=="blue")settings.color.b=std::clamp(value,0.f,1.f);
        else if(key=="updates")settings.automaticUpdates=value!=0;
    }
    return settings;
}
void Settings::write(std::ostream& stream) const {
    stream<<"stroke "<<strokeWidth<<"\ntext "<<textSize<<"\nred "<<color.r<<"\ngreen "<<color.g<<"\nblue "<<color.b<<"\nupdates "<<(automaticUpdates?1:0)<<"\nlanguage "<<languageTag(language)<<'\n';
}
void Settings::save() const {
    const auto path=settingsPath();std::filesystem::create_directories(path.parent_path());
    const auto temporary=path.parent_path()/(L"settings-"+uniqueToken()+L".tmp");
    try {
        std::ofstream stream(temporary);stream.exceptions(std::ios::failbit|std::ios::badbit);
        write(stream);stream.close();
        wincheck(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Save settings");
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
bool Settings::launchAtSignIn() {
    DWORD bytes{};return RegGetValueW(HKEY_CURRENT_USER,runKey,L"ScreenshotTool",RRF_RT_REG_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS;
}
void Settings::setLaunchAtSignIn(bool enabled) {
    if(enabled) {
        std::wstring executable(32768,L'\0');DWORD count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        wincheck(count>0 && count<executable.size(),"Locate portable executable");executable.resize(count);executable=L"\""+executable+L"\"";
        const auto* bytes=reinterpret_cast<const BYTE*>(executable.c_str());
        writeSignInValue(SignInValue{REG_SZ,{bytes,bytes+(executable.size()+1)*sizeof(wchar_t)}});
    } else writeSignInValue(std::nullopt);
}
void Settings::show(HWND owner,std::function<void()> applied) {
    SettingsDialog state;state.committed=*this;state.signIn=launchAtSignIn();state.applied=std::move(applied);
    state.commit=[this](const Settings& updated,bool signIn) {
        // Preserve the original command and registry type if the file save fails.
        const auto previous=readSignInValue();setLaunchAtSignIn(signIn);
        try{updated.save();}
        catch(const std::exception& error) {
            try{writeSignInValue(previous);}
            catch(const std::exception& rollback){throw std::runtime_error(std::string(error.what())+"\nRestore sign-in preference: "+rollback.what());}
            throw;
        }
        *this=updated;
    };
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),owner,settingsDialog,reinterpret_cast<LPARAM>(&state));
    if(state.initializationError)std::rethrow_exception(state.initializationError);
    if(result==-1)wincheck(FALSE,"Open settings");
}
HWND SettingsDialogTestAccess::create(const Settings& settings,bool signIn,unsigned dpi,Settings* accepted,
    std::function<void(const Settings&,bool)> commit,std::function<void()> applied) {
    auto state=std::make_unique<SettingsDialog>();state->committed=settings;state->signIn=signIn;state->modeless=true;
    state->commit=std::move(commit);state->applied=std::move(applied);
    state->dpiOverride=dpi;state->accepted=accepted;if(accepted)*accepted=settings;
    const HWND window=CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),nullptr,settingsDialog,reinterpret_cast<LPARAM>(state.get()));
    if(state->initializationError)std::rethrow_exception(state->initializationError);
    wincheck(window!=nullptr,"Create test settings dialog");state->heapOwned=true;state.release();return window;
}
std::optional<Message> SettingsDialogTestAccess::lastError(HWND window) {
    const auto* state=reinterpret_cast<const SettingsDialog*>(GetWindowLongPtrW(window,DWLP_USER));
    return state?state->lastError:std::nullopt;
}
}
