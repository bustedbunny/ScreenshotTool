#include "settings.hpp"
#include "export.hpp"
#include <shlobj.h>
#include <commctrl.h>
#include <fstream>
#include "../resources/resource.h"

namespace shot {
namespace {
std::filesystem::path settingsPath() {
    PWSTR raw{};check(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw),"Locate local application data");
    std::filesystem::path path(raw);CoTaskMemFree(raw);return path/L"ScreenshotTool"/L"settings.ini";
}
constexpr auto runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
struct SettingsDialog {bool automatic{},signIn{};};
INT_PTR CALLBACK settingsDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_INITDIALOG) {
        auto* state=reinterpret_cast<SettingsDialog*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);
        CheckDlgButton(window,IDC_AUTO_UPDATES,state->automatic?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(window,IDC_SIGN_IN,state->signIn?BST_CHECKED:BST_UNCHECKED);return TRUE;
    }
    if(message==WM_COMMAND && (LOWORD(wp)==IDOK || LOWORD(wp)==IDCANCEL)) {
        if(LOWORD(wp)==IDOK) {
            auto* state=reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window,DWLP_USER));
            state->automatic=IsDlgButtonChecked(window,IDC_AUTO_UPDATES)==BST_CHECKED;
            state->signIn=IsDlgButtonChecked(window,IDC_SIGN_IN)==BST_CHECKED;
        }
        EndDialog(window,LOWORD(wp));return TRUE;
    }
    if(message==WM_CLOSE){EndDialog(window,IDCANCEL);return TRUE;}
    return FALSE;
}
}
Settings Settings::load() {
    std::ifstream file(settingsPath());return read(file);
}
Settings Settings::read(std::istream& file) {
    Settings settings;std::string key;float value{};
    while(file>>key>>value) {
        if(!std::isfinite(value))continue;
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
    stream<<"stroke "<<strokeWidth<<"\ntext "<<textSize<<"\nred "<<color.r<<"\ngreen "<<color.g<<"\nblue "<<color.b<<"\nupdates "<<(automaticUpdates?1:0)<<'\n';
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
    HKEY key{};LONG status=RegCreateKeyExW(HKEY_CURRENT_USER,runKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);check(HRESULT_FROM_WIN32(status),"Open sign-in settings");
    struct Close{HKEY key;~Close(){RegCloseKey(key);}}close{key};
    if(enabled) {
        std::wstring executable(32768,L'\0');DWORD count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        wincheck(count>0 && count<executable.size(),"Locate portable executable");executable.resize(count);executable=L"\""+executable+L"\"";
        status=RegSetValueExW(key,L"ScreenshotTool",0,REG_SZ,reinterpret_cast<const BYTE*>(executable.c_str()),static_cast<DWORD>((executable.size()+1)*sizeof(wchar_t)));
    } else {status=RegDeleteValueW(key,L"ScreenshotTool");if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;}
    check(HRESULT_FROM_WIN32(status),"Update sign-in preference");
}
void Settings::show(HWND owner) {
    SettingsDialog state{automaticUpdates,launchAtSignIn()};
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),owner,settingsDialog,reinterpret_cast<LPARAM>(&state));
    if(result==-1)wincheck(FALSE,"Open settings");
    if(result==IDOK) {
        setLaunchAtSignIn(state.signIn);
        auto updated=*this;updated.automaticUpdates=state.automatic;updated.save();*this=updated;
    }
}
}
