#include "update.hpp"
#include "settings.hpp"
#include "export.hpp"
#include "version.hpp"
#include <sddl.h>
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <winrt/base.h>

using namespace shot;
namespace {
int passed{},failed{};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void test(const char* name,F function) {
    try{function();++passed;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}
}
template<class F> void rejects(F function){bool threw=false;try{function();}catch(const std::exception&){threw=true;}require(threw,"Expected rejection");}
Version version(std::wstring_view value){auto parsed=Version::parse(value);require(parsed.has_value(),"Parse test version");return *parsed;}
std::wstring json(std::wstring_view tag=L"v1.1.0",std::wstring_view digest=L"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") {
    return L"{\"tag_name\":\""+std::wstring(tag)+L"\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/bustedbunny/ScreenshotTool/releases/tag/v1.1.0\",\"assets\":[{\"name\":\"ScreenshotTool.exe\",\"state\":\"uploaded\",\"size\":577024,\"browser_download_url\":\"https://github.com/bustedbunny/ScreenshotTool/releases/download/v1.1.0/ScreenshotTool.exe\",\"digest\":\""+std::wstring(digest)+L"\"}]}";
}
std::wstring replaced(std::wstring value,std::wstring_view from,std::wstring_view to) {
    const auto at=value.find(from);require(at!=std::wstring::npos,"Fixture substring exists");value.replace(at,from.size(),to);return value;
}
struct Folder {
    std::filesystem::path path=UpdateService::executablePath().parent_path()/(L"update tests 日本語 "+uniqueToken());
    Folder(){std::filesystem::create_directory(path);}
    ~Folder(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void write(const std::filesystem::path& path,std::string_view value){std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);file<<value;}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
ReleaseInfo fixtureRelease(const std::filesystem::path& path) {
    return {version(AppVersion),L"https://github.com/bustedbunny/ScreenshotTool/releases/latest",L"https://github.com/bustedbunny/ScreenshotTool/releases/download/v1.1.0/ScreenshotTool.exe",UpdateService::sha256(path),std::filesystem::file_size(path)};
}
void copyFixture(const std::filesystem::path& path){std::filesystem::copy_file(UpdateService::executablePath(),path);}
void versionTests() {
    test("numeric version compatibility and ordering",[]{
        require(version(L"1.0")==version(L"v1.0.0"),"Normalize existing release");
        require(version(L"1.10.0")>version(L"1.9.99"),"Numeric, not lexical ordering");
        for(auto text:{L"",L"v",L"1",L"1.2.",L"1.2.3.4",L"1.2-beta",L"1.2.3+build",L"-1.2",L"1.65536.0",L"1..3",L"1.2/3"})require(!Version::parse(text),"Reject malformed version");
    });
    test("release selection accepts only newer stable executable assets",[]{
        auto result=UpdateService::parseRelease(json(),version(L"1.0"));require(result.status==UpdateStatus::Available && result.release->version==version(L"1.1.0"),"Select compatible release");
        require(UpdateService::parseRelease(json(),version(L"1.1.0")).status==UpdateStatus::Current,"Ignore equal version");
        require(UpdateService::parseRelease(json(L"1.0"),version(L"1.1.0")).status==UpdateStatus::Current,"Never downgrade");
        for(auto flag:{L"\"draft\":false",L"\"prerelease\":false"})require(UpdateService::parseRelease(replaced(json(),flag,replaced(std::wstring(flag),L"false",L"true")),version(L"1.0")).status==UpdateStatus::Current,"Ignore unpublished and prerelease builds");
    });
    test("asset requirements retain a manual release URL",[]{
        for(auto data:{json(L"v1.1.0",L""),json(L"v1.1.0",L"sha256:bad"),replaced(json(),L"\"digest\":\"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"",L"\"digest\":null"),replaced(json(),L"577024",L"67108865"),replaced(json(),L"ScreenshotTool.exe\",\"state",L"other.exe\",\"state"),replaced(json(),L"\"uploaded\"",L"\"new\""),replaced(json(),L"https://github.com/bustedbunny/ScreenshotTool/releases/download/",L"http://github.com/bustedbunny/ScreenshotTool/releases/download/")}) {
            const auto result=UpdateService::parseRelease(data,version(L"1.0"));require(result.status==UpdateStatus::Incompatible && result.release && !result.release->releaseUrl.empty(),"Offer manual fallback");
        }
    });
    test("malformed and wrongly typed release responses fail safely",[]{
        for(auto data:{L"{",L"[]",L"{}",L"{\"draft\":\"false\"}",L"null"})require(UpdateService::parseRelease(data,version(L"1.0")).status==UpdateStatus::Failed,"Reject invalid JSON or schema");
        require(UpdateService::parseRelease(json(L"latest"),version(L"1.0")).status==UpdateStatus::Failed,"Reject non-version tag");
    });
    test("HTTP failures and rate limits have bounded backoff",[]{
        require(UpdateService::httpFailure(404).status==UpdateStatus::NoRelease,"No published releases is normal");
        require(UpdateService::httpFailure(500).status==UpdateStatus::Failed,"Server failure reported");
        require(UpdateService::httpFailure(429,L"120").retryAfterSeconds==120,"Respect retry-after");
        require(UpdateService::httpFailure(403,L"bad").retryAfterSeconds==3600,"Conservative fallback");
        require(UpdateService::httpFailure(429,L"999999").retryAfterSeconds==86400,"Bound retry-after");
    });
    test("canceled startup checks do not contact GitHub",[]{std::stop_source stop;stop.request_stop();require(UpdateService::check(stop.get_token()).status==UpdateStatus::Canceled,"Cancellation result");});
    test("settings round trip preserves existing preferences",[]{
        std::istringstream old("stroke 7\ntext 36\nred 0.5\ngreen 0.25\nblue 0.75\n");auto settings=Settings::read(old);
        require(settings.automaticUpdates && settings.strokeWidth==7 && settings.textSize==36,"Old settings enable checks and preserve drawing defaults");
        settings.automaticUpdates=false;std::ostringstream encoded;settings.write(encoded);std::istringstream input(encoded.str());auto restored=Settings::read(input);
        require(!restored.automaticUpdates && restored.color==settings.color && restored.strokeWidth==7 && restored.textSize==36,"Persist update toggle without losing existing values");
    });
    test("native process arguments round trip without shell evaluation",[]{
        for(auto value:{L"",L"simple",L"path with spaces 日本語",L"C:\\folder\\",L"a\\\"b",L"$(anything) & | %PATH%"}) {
            const auto command=L"program "+quoteArgument(value);int argc{};auto* argv=CommandLineToArgvW(command.c_str(),&argc);
            require(argv && argc==2 && std::wstring_view(argv[1])==value,"Correct Windows quoting");LocalFree(argv);
        }
    });
}
void verificationTests() {
    test("download verification checks size digest architecture identity and version",[]{
        Folder folder;const auto path=folder.path/L"download.exe";copyFixture(path);auto release=fixtureRelease(path);UpdateService::verify(release,path);
        auto wrong=release;wrong.size++;rejects([&]{UpdateService::verify(wrong,path);});wrong=release;wrong.sha256[0]=wrong.sha256[0]==L'0'?L'1':L'0';rejects([&]{UpdateService::verify(wrong,path);});
        wrong=release;wrong.version=version(L"1.2.0");rejects([&]{UpdateService::verify(wrong,path);});
        std::stop_source stop;stop.request_stop();rejects([&]{UpdateService::verify(release,path,stop.get_token());});
        {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);IMAGE_DOS_HEADER dos{};file.read(reinterpret_cast<char*>(&dos),sizeof(dos));file.seekp(dos.e_lfanew+sizeof(DWORD));WORD machine=IMAGE_FILE_MACHINE_I386;file.write(reinterpret_cast<char*>(&machine),sizeof(machine));}
        release=fixtureRelease(path);rejects([&]{UpdateService::verify(release,path);});
        write(path,"not an executable");release=fixtureRelease(path);rejects([&]{UpdateService::verify(release,path);});
    });
    test("staging cancellation and ownership cleanup preserve installed app",[]{
        Folder folder;const auto target=folder.path/L"installed.exe",download=folder.path/L"download.exe";copyFixture(target);copyFixture(download);const auto release=fixtureRelease(download);const auto original=UpdateService::sha256(target);
        std::filesystem::path replacement,helper;
        {auto prepared=UpdateService::prepare(release,download,target,{});replacement=prepared.replacement;helper=prepared.helperDirectory;require(std::filesystem::exists(replacement) && std::filesystem::exists(helper/L"helper.exe"),"Prepared native helper and same-directory replacement");}
        require(!std::filesystem::exists(replacement) && !std::filesystem::exists(helper) && UpdateService::sha256(target)==original,"Uninstalled staging cleans only owned files");
        std::stop_source stop;stop.request_stop();rejects([&]{UpdateService::prepare(release,download,target,stop.get_token());});
        rejects([&]{UpdateService::download(release,target,stop.get_token());});
    });
    test("unwritable portable directory fails before handoff",[]{
        Folder folder;const auto restricted=folder.path/L"restricted";std::filesystem::create_directory(restricted);
        const auto target=restricted/L"installed.exe",download=folder.path/L"download.exe";copyFixture(target);copyFixture(download);const auto release=fixtureRelease(download);
        PSECURITY_DESCRIPTOR deny{},allow{};
        wincheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;0x00000002;;;WD)(A;;FA;;;WD)",SDDL_REVISION_1,&deny,nullptr),"Build test directory ACL");
        wincheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;FA;;;WD)",SDDL_REVISION_1,&allow,nullptr),"Build cleanup ACL");
        struct Restore {std::filesystem::path path;PSECURITY_DESCRIPTOR deny,allow;~Restore(){SetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION,allow);LocalFree(deny);LocalFree(allow);}}restore{restricted,deny,allow};
        wincheck(SetFileSecurityW(restricted.c_str(),DACL_SECURITY_INFORMATION,deny),"Restrict test directory writes");
        rejects([&]{UpdateService::prepare(release,download,target,{});});require(std::filesystem::exists(target),"Installed executable survives permission failure");
    });
}
void transactionTests() {
    test("replacement succeeds and removes its backup",[]{
        Folder folder;PreparedUpdate update;update.target=folder.path/L"app.exe";update.replacement=folder.path/L"new.exe";update.backup=folder.path/L"backup.exe";
        write(update.target,"old");write(update.replacement,"new");bool started=false;UpdateService::replaceAndRestart(update,[&]{started=true;require(read(update.target)=="new","Restart sees replacement");});
        require(started && read(update.target)=="new" && !std::filesystem::exists(update.backup),"Successful commit");
    });
    test("restart failure restores the previous executable",[]{
        Folder folder;PreparedUpdate update;update.target=folder.path/L"app.exe";update.replacement=folder.path/L"new.exe";update.backup=folder.path/L"backup.exe";
        write(update.target,"old");write(update.replacement,"new");rejects([&]{UpdateService::replaceAndRestart(update,[]{throw std::runtime_error("Injected launch failure");});});
        require(read(update.target)=="old" && !std::filesystem::exists(update.backup),"Rollback recovered original");
    });
    test("locked target and existing backup are never overwritten",[]{
        Folder folder;PreparedUpdate update;update.target=folder.path/L"app.exe";update.replacement=folder.path/L"new.exe";update.backup=folder.path/L"backup.exe";
        write(update.target,"old");write(update.replacement,"new");
        UniqueHandle lock(CreateFileW(update.target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));require(lock.get()!=INVALID_HANDLE_VALUE,"Lock test target");
        rejects([&]{UpdateService::replaceAndRestart(update,[]{});});lock.reset();require(read(update.target)=="old","Retain locked original");
        write(update.backup,"unrelated");rejects([&]{UpdateService::replaceAndRestart(update,[]{});});require(read(update.backup)=="unrelated","Do not replace another backup");
    });
    test("native helper waits for its parent replaces and confirms restart",[]{
        Folder folder;const auto target=folder.path/L"portable app 日本語.exe",download=folder.path/L"download.exe";copyFixture(target);copyFixture(download);
        {std::ofstream tail(download,std::ios::binary|std::ios::app);tail<<"updated fixture";}
        std::wstring command=quoteArgument(target.wstring())+L" --fixture";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION info{};
        wincheck(CreateProcessW(target.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.path.c_str(),&startup,&info),"Start disposable update parent");
        UniqueHandle parent(info.hProcess),thread(info.hThread);
        struct StopFixture {HANDLE process;~StopFixture(){if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0){TerminateProcess(process,1);WaitForSingleObject(process,5000);}}}stopFixture{parent.get()};
        auto running=target;running+=L".running";
        const auto bootDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!std::filesystem::exists(running) && std::chrono::steady_clock::now()<bootDeadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(std::filesystem::exists(running) && WaitForSingleObject(parent.get(),0)==WAIT_TIMEOUT,"Prepare against a running portable executable");
        auto prepared=UpdateService::prepare(fixtureRelease(download),download,target,{});const auto helper=prepared.helperDirectory;
        UpdateService::launchHelper(prepared,parent.get());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        auto marker=target;marker+=L".started";
        while(std::chrono::steady_clock::now()<deadline && (!std::filesystem::exists(marker) || std::filesystem::exists(helper)))std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(std::filesystem::exists(marker),"New executable reached startup acknowledgement");
        require(UpdateService::sha256(target)==UpdateService::sha256(download),"Installed downloaded bytes");require(!std::filesystem::exists(helper),"Post-update startup removes temporary helper");
        require(WaitForSingleObject(parent.get(),0)==WAIT_OBJECT_0,"Replacement waited for parent exit");
    });
    test("helper rejects a target unrelated to its parent",[]{
        Folder folder;const auto target=folder.path/L"app.exe";copyFixture(target);auto prepared=UpdateService::prepare(fixtureRelease(target),target,target,{});
        rejects([&]{UpdateService::launchHelper(prepared);});require(std::filesystem::exists(target),"Mismatch does not exit or replace app");
    });
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(auto result=UpdateService::runHelper(argc,argv))return *result;
        if(argc>1 && std::wstring_view(argv[1])==L"--fixture"){auto marker=UpdateService::executablePath();marker+=L".running";write(marker,"running");Sleep(2000);return 0;}
        if(argc>1 && std::wstring_view(argv[1])==L"--update-started") {
            auto marker=UpdateService::executablePath();marker+=L".started";write(marker,"ready");UpdateService::finishStartup(argc,argv);return 0;
        }
        if(argc>1 && std::wstring_view(argv[1])==L"--github-probe") {
            const auto result=UpdateService::check({});std::wcout<<result.message<<L'\n';
            return result.status==UpdateStatus::Failed || result.status==UpdateStatus::RateLimited?1:0;
        }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        versionTests();verificationTests();transactionTests();winrt::uninit_apartment();
        std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
