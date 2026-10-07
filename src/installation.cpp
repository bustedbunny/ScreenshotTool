#include "installation.hpp"
#include "update.hpp"
#include "version.hpp"
#include <optional>
#include <string>

namespace shot {
namespace {
constexpr auto uninstallKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\bustedbunny.ScreenshotTool_is1";
struct RegistryKey {
    HKEY value{};
    ~RegistryKey(){if(value)RegCloseKey(value);}
};
std::optional<std::wstring> registryString(HKEY key,const wchar_t* name) {
    DWORD type{},bytes{};
    if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes)!=ERROR_SUCCESS || type!=REG_SZ ||
       bytes<sizeof(wchar_t) || bytes>65536 || bytes%sizeof(wchar_t)!=0)return {};
    std::wstring value(bytes/sizeof(wchar_t),L'\0');
    if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes)!=ERROR_SUCCESS ||
       type!=REG_SZ || bytes<sizeof(wchar_t) || bytes%sizeof(wchar_t)!=0)return {};
    value.resize(bytes/sizeof(wchar_t));
    if(value.back()!=L'\0' || value.find(L'\0')!=value.size()-1)return {};
    value.pop_back();return value;
}
bool refreshDisplayVersion(HKEY root,const wchar_t* subkey,const std::filesystem::path& executable,std::wstring_view version) noexcept {
    try {
        if(!root || !subkey || version.empty())return false;
        RegistryKey key;
        if(RegOpenKeyExW(root,subkey,0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,&key.value)!=ERROR_SUCCESS)return false;
        const auto location=registryString(key.value,L"InstallLocation");
        if(!location || location->empty())return false;
        const std::filesystem::path directory(*location);
        if(!directory.is_absolute() || !executable.is_absolute())return false;
        std::error_code error;
        if(!std::filesystem::equivalent(directory,executable.parent_path(),error) || error)return false;
        if(!std::filesystem::equivalent(directory/L"ScreenshotTool.exe",executable,error) || error)return false;
        if(registryString(key.value,L"DisplayVersion")==version)return true;
        const std::wstring text(version);
        if(text.size()>(65536/sizeof(wchar_t))-1)return false;
        return RegSetValueExW(key.value,L"DisplayVersion",0,REG_SZ,reinterpret_cast<const BYTE*>(text.c_str()),
                             static_cast<DWORD>((text.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
    }catch(...){return false;}
}
}
void Installation::refreshDisplayVersion() noexcept {
    try{shot::refreshDisplayVersion(HKEY_CURRENT_USER,uninstallKey,UpdateService::executablePath(),AppVersion);}catch(...){}
}
bool InstallationTestAccess::refreshDisplayVersion(HKEY root,const wchar_t* subkey,const std::filesystem::path& executable,std::wstring_view version) noexcept {
    return shot::refreshDisplayVersion(root,subkey,executable,version);
}
}
