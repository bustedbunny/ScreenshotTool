#include "settings.hpp"
#include "export.hpp"
#include <shlobj.h>
#include <commctrl.h>
#include <fstream>

namespace shot {
namespace {
std::filesystem::path settingsPath() {
    PWSTR raw{};check(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw),"Locate local application data");
    std::filesystem::path path(raw);CoTaskMemFree(raw);return path/L"ScreenshotTool"/L"settings.ini";
}
constexpr auto runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}
Settings Settings::load() {
    Settings settings;std::ifstream file(settingsPath());std::string key;float value{};
    while(file>>key>>value) {
        if(!std::isfinite(value))continue;
        if(key=="stroke")settings.strokeWidth=std::clamp(value,1.f,24.f);
        else if(key=="text")settings.textSize=std::clamp(value,8.f,144.f);
        else if(key=="red")settings.color.r=std::clamp(value,0.f,1.f);
        else if(key=="green")settings.color.g=std::clamp(value,0.f,1.f);
        else if(key=="blue")settings.color.b=std::clamp(value,0.f,1.f);
    }
    return settings;
}
void Settings::save() const {
    const auto path=settingsPath();std::filesystem::create_directories(path.parent_path());
    const auto temporary=path.parent_path()/(L"settings-"+uniqueToken()+L".tmp");
    try {
        std::ofstream stream(temporary);stream.exceptions(std::ios::failbit|std::ios::badbit);
        stream<<"stroke "<<strokeWidth<<"\ntext "<<textSize<<"\nred "<<color.r<<"\ngreen "<<color.g<<"\nblue "<<color.b<<'\n';stream.close();
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
    TASKDIALOGCONFIG config{};config.cbSize=sizeof(config);config.hwndParent=owner;config.pszWindowTitle=L"ScreenshotTool settings";
    config.pszMainInstruction=L"Capture with Print Screen";
    config.pszContent=L"Drag to select, then annotate or export.\n\nQuick saves: Pictures \\ ScreenshotTool\nHDR selections save an SDR PNG and a lossless HDR JPEG XR.\n\nIf Windows also opens Snipping Tool, turn off “Use the Print Screen key to open screen capture” in Windows Settings > Accessibility > Keyboard.\n\nLaunch at sign-in uses this executable's current location. If you move it, disable and enable this option again.";
    config.pszVerificationText=L"Launch ScreenshotTool when I sign in";config.dwCommonButtons=TDCBF_OK_BUTTON|TDCBF_CANCEL_BUTTON;
    config.dwFlags=TDF_SIZE_TO_CONTENT|TDF_ALLOW_DIALOG_CANCELLATION;
    if(launchAtSignIn())config.dwFlags|=TDF_VERIFICATION_FLAG_CHECKED;
    int button{};BOOL enabled{};check(TaskDialogIndirect(&config,&button,nullptr,&enabled),"Open settings");
    if(button==IDOK)setLaunchAtSignIn(enabled!=FALSE);
}
}
