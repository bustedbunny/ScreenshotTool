#pragma once
#include "installation.hpp"

namespace {
struct InstallationRegistry {
    std::wstring path=L"Software\\ScreenshotTool\\InstallationTests-"+uniqueToken();
    HKEY root{},key{};
    PSECURITY_DESCRIPTOR restoreSecurity{};
    InstallationRegistry() {
        check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&root,nullptr)),"Create disposable installation test root");
        const auto result=RegCreateKeyExW(root,L"Uninstall",0,nullptr,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&key,nullptr);
        if(result!=ERROR_SUCCESS){RegCloseKey(root);root=nullptr;RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());check(HRESULT_FROM_WIN32(result),"Create disposable uninstall key");}
    }
    ~InstallationRegistry() {
        if(restoreSecurity){RegSetKeySecurity(key,DACL_SECURITY_INFORMATION,restoreSecurity);LocalFree(restoreSecurity);}
        if(key)RegCloseKey(key);if(root){RegDeleteTreeW(root,nullptr);RegCloseKey(root);}
        RegDeleteKeyExW(HKEY_CURRENT_USER,path.c_str(),KEY_WOW64_64KEY,0);
    }
    void string(const wchar_t* name,const std::wstring& value) {
        check(HRESULT_FROM_WIN32(RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))),"Write disposable installation metadata");
    }
    std::wstring version() const {
        DWORD bytes{};check(HRESULT_FROM_WIN32(RegGetValueW(key,nullptr,L"DisplayVersion",RRF_RT_REG_SZ,nullptr,nullptr,&bytes)),"Read installation test version size");
        std::wstring value(bytes/sizeof(wchar_t),L'\0');
        check(HRESULT_FROM_WIN32(RegGetValueW(key,nullptr,L"DisplayVersion",RRF_RT_REG_SZ,nullptr,value.data(),&bytes)),"Read installation test version");
        value.resize(bytes/sizeof(wchar_t)-1);return value;
    }
    bool refresh(const std::filesystem::path& executable) const {
        return InstallationTestAccess::refreshDisplayVersion(root,L"Uninstall",executable,AppVersion);
    }
    void denyVersionWrites() {
        PSECURITY_DESCRIPTOR deny{};
        wincheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;KA;;;WD)",SDDL_REVISION_1,&restoreSecurity,nullptr),"Create registry cleanup ACL");
        wincheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;0x00000002;;;WD)(A;;KA;;;WD)",SDDL_REVISION_1,&deny,nullptr),"Create read-only installation ACL");
        const auto result=RegSetKeySecurity(key,DACL_SECURITY_INFORMATION,deny);LocalFree(deny);
        check(HRESULT_FROM_WIN32(result),"Restrict disposable uninstall key writes");
    }
};
}

void installationTests() {
    test("installed startup refreshes only its matching uninstall version",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");
        InstallationRegistry metadata;metadata.string(L"InstallLocation",folder.path.wstring()+L"\\");metadata.string(L"DisplayVersion",L"1.0.0");
        require(metadata.refresh(executable),"Refresh matching Unicode path with trailing separator");
        require(metadata.version()==AppVersion,"Publish executable version into Installed Apps metadata");
    });
    test("current installed version performs no registry write",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");
        InstallationRegistry metadata;metadata.string(L"InstallLocation",folder.path.wstring());metadata.string(L"DisplayVersion",std::wstring(AppVersion));
        UniqueHandle changed(CreateEventW(nullptr,TRUE,FALSE,nullptr));wincheck(changed!=nullptr,"Create registry change event");
        check(HRESULT_FROM_WIN32(RegNotifyChangeKeyValue(metadata.key,FALSE,REG_NOTIFY_CHANGE_LAST_SET,changed.get(),TRUE)),"Observe disposable uninstall metadata writes");
        require(metadata.refresh(executable),"Already-current metadata succeeds");
        require(WaitForSingleObject(changed.get(),0)==WAIT_TIMEOUT,"No write when installed metadata is current");
    });
    test("portable startup does not create an uninstall registration",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"portable fixture");InstallationRegistry metadata;
        require(!InstallationTestAccess::refreshDisplayVersion(metadata.root,L"Missing",executable,AppVersion),"Missing registration is skipped");
        HKEY missing{};const auto result=RegOpenKeyExW(metadata.root,L"Missing",0,KEY_READ|KEY_WOW64_64KEY,&missing);
        if(missing)RegCloseKey(missing);require(result==ERROR_FILE_NOT_FOUND,"Refresh did not create registration");
    });
    test("missing malformed or relative installation locations are skipped",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");InstallationRegistry metadata;metadata.string(L"DisplayVersion",L"1.0.0");
        require(!metadata.refresh(executable),"Missing InstallLocation is skipped");
        for(const auto* location:{L"",L"relative\\ScreenshotTool"}){metadata.string(L"InstallLocation",location);require(!metadata.refresh(executable),"Empty or relative location is skipped");}
        const DWORD invalid=17;
        check(HRESULT_FROM_WIN32(RegSetValueExW(metadata.key,L"InstallLocation",0,REG_DWORD,reinterpret_cast<const BYTE*>(&invalid),sizeof(invalid))),"Write wrong registry type");
        require(!metadata.refresh(executable),"Non-string InstallLocation is skipped");
        auto embedded=folder.path.wstring();embedded.push_back(L'\0');embedded+=L"ignored";metadata.string(L"InstallLocation",embedded);
        require(!metadata.refresh(executable),"Embedded-null InstallLocation is skipped");
        require(metadata.version()==L"1.0.0","Invalid metadata was left intact");
    });
    test("registry-normalized string termination remains a valid install location",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");InstallationRegistry metadata;metadata.string(L"DisplayVersion",L"1.0.0");
        const auto location=folder.path.wstring();
        check(HRESULT_FROM_WIN32(RegSetValueExW(metadata.key,L"InstallLocation",0,REG_SZ,reinterpret_cast<const BYTE*>(location.data()),static_cast<DWORD>(location.size()*sizeof(wchar_t)))),"Write registry string without counting its terminator");
        DWORD type{},bytes{};check(HRESULT_FROM_WIN32(RegQueryValueExW(metadata.key,L"InstallLocation",nullptr,&type,nullptr,&bytes)),"Inspect stored installation string size");
        std::wstring stored(bytes/sizeof(wchar_t),L'\0');
        check(HRESULT_FROM_WIN32(RegQueryValueExW(metadata.key,L"InstallLocation",nullptr,&type,reinterpret_cast<BYTE*>(stored.data()),&bytes)),"Inspect stored installation string bytes");
        // RegSetValueExW normalized this fixture; it cannot inject a genuinely
        // unterminated value when the supplied string has a trailing null.
        require(type==REG_SZ && bytes==(location.size()+1)*sizeof(wchar_t) && stored==location+L'\0',"Windows stored a complete terminated REG_SZ");
        require(metadata.refresh(executable) && metadata.version()==AppVersion,"Accept the actual normalized registry value");
    });
    test("other portable copies and renamed executables cannot refresh installed metadata",[]{
        Folder installed,portable;const auto executable=installed.path/L"ScreenshotTool.exe",other=portable.path/L"ScreenshotTool.exe",renamed=installed.path/L"older-copy.exe";
        write(executable,"installed fixture");write(other,"portable fixture");write(renamed,"renamed old fixture");
        InstallationRegistry metadata;metadata.string(L"InstallLocation",installed.path.wstring());metadata.string(L"DisplayVersion",L"1.0.0");
        require(!metadata.refresh(other),"Different portable directory is skipped");require(!metadata.refresh(renamed),"Renamed executable in installed directory is skipped");
        require(!metadata.refresh(installed.path/L"missing.exe"),"Missing executable is skipped");require(metadata.version()==L"1.0.0","Installed metadata remains owned by installed executable");
    });
    test("existing registration can repair a missing display version",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");InstallationRegistry metadata;metadata.string(L"InstallLocation",folder.path.wstring());
        require(metadata.refresh(executable) && metadata.version()==AppVersion,"Restore only missing version in matching registration");
    });
    test("denied registry writes leave installed startup usable",[]{
        Folder folder;const auto executable=folder.path/L"ScreenshotTool.exe";write(executable,"installed fixture");InstallationRegistry metadata;metadata.string(L"InstallLocation",folder.path.wstring());metadata.string(L"DisplayVersion",L"1.0.0");metadata.denyVersionWrites();
        require(!metadata.refresh(executable),"Write-denied refresh returns without throwing");require(metadata.version()==L"1.0.0","Denied refresh preserves previous version");
    });
}
