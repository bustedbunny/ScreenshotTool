#include "update.hpp"
#include "installation.hpp"
#include "export.hpp"
#include "version.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <limits>
#include <thread>
#include <utility>

namespace shot {
namespace {
constexpr uint64_t MaxAssetSize=64*1024*1024;
constexpr size_t MaxMetadataSize=1024*1024;
constexpr std::wstring_view ReleasePage=L"https://github.com/bustedbunny/ScreenshotTool/releases/latest";
constexpr std::wstring_view ReleasePrefix=L"https://github.com/bustedbunny/ScreenshotTool/releases/";
constexpr std::wstring_view AssetPrefix=L"https://github.com/bustedbunny/ScreenshotTool/releases/download/";
constexpr std::wstring_view DirectoryPrefix=L"ScreenshotTool-update-";
struct Canceled : std::runtime_error { Canceled():std::runtime_error("Update canceled."){} };
void cancellation(std::stop_token stop) {if(stop.stop_requested())throw Canceled{};}
void requireUpdate(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
UniqueHandle beginOperation(const wchar_t* operationMutex,const wchar_t* setupMutex) {
    UniqueHandle operation(CreateMutexW(nullptr,FALSE,operationMutex));const auto error=GetLastError();
    if(!operation)check(HRESULT_FROM_WIN32(error),"Create update operation lock");
    requireUpdate(error!=ERROR_ALREADY_EXISTS,"Another ScreenshotTool update is already running. Try again after it finishes.");
    UniqueHandle setup(OpenMutexW(SYNCHRONIZE,FALSE,setupMutex));const auto setupError=GetLastError();
    requireUpdate(!setup && setupError==ERROR_FILE_NOT_FOUND,"ScreenshotTool setup is running. Close setup before installing an update.");
    return operation;
}
bool validDigest(std::wstring_view value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](wchar_t c){return (c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f');});
}
uint64_t decimal(std::wstring_view value) {
    requireUpdate(!value.empty(),"Missing numeric update argument.");uint64_t result{};
    for(auto c:value) {
        requireUpdate(c>=L'0' && c<=L'9',"Invalid numeric update argument.");
        requireUpdate(result<=(std::numeric_limits<uint64_t>::max()-(c-L'0'))/10,"Update argument overflow.");
        result=result*10+(c-L'0');
    }
    return result;
}
HANDLE argumentHandle(std::wstring_view value) {
    const auto number=decimal(value);requireUpdate(number>0 && number<=std::numeric_limits<uintptr_t>::max(),"Invalid update handle.");
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(number));
}
std::wstring handleText(HANDLE handle) {return std::to_wstring(reinterpret_cast<uintptr_t>(handle));}
std::filesystem::path processPath(HANDLE process) {
    std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());
    wincheck(QueryFullProcessImageNameW(process,0,path.data(),&size),"Read update parent executable");path.resize(size);return path;
}
void removeFile(const std::filesystem::path& path) noexcept {
    if(!path.empty()){std::error_code ignored;std::filesystem::remove(path,ignored);}
}
bool ownedDirectory(const std::filesystem::path& path) {
    if(path.empty())return false;
    std::error_code error;const auto parent=std::filesystem::weakly_canonical(path.parent_path(),error);
    if(error)return false;
    const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
    const auto name=path.filename().wstring();
    return !error && parent==temp && name.starts_with(DirectoryPrefix) && name.size()>DirectoryPrefix.size();
}
void cleanupDirectory(const std::filesystem::path& path) noexcept {
    try {
        if(!ownedDirectory(path))return;
        removeFile(path/L"download.exe");removeFile(path/L"helper.exe");removeFile(path);
    }catch(...){}
}
struct InternetCloser {void operator()(void* handle) const {if(handle)WinHttpCloseHandle(handle);}};
using Internet=std::unique_ptr<void,InternetCloser>;
// One outstanding async operation at a time. The closing notification keeps
// callback storage alive even when cancellation closes an in-flight request.
struct AsyncRequest {
    UniqueHandle completed{CreateEventW(nullptr,TRUE,FALSE,nullptr)},closed{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Internet request;
    DWORD error{},count{};
    bool contextSet{};
    AsyncRequest(){wincheck(completed && closed,"Create network completion events");}
    ~AsyncRequest() {
        if(request){request.reset();if(contextSet)WaitForSingleObject(closed.get(),INFINITE);}
    }
    static void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD status,void* data,DWORD length) {
        if(!context)return;auto& state=*reinterpret_cast<AsyncRequest*>(context);
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){SetEvent(state.closed.get());return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)state.error=static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError;
        else if(status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)state.count=*static_cast<DWORD*>(data);
        else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)state.count=length;
        else if(status!=WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status!=WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)return;
        SetEvent(state.completed.get());
    }
    void begin() {ResetEvent(completed.get());error=0;count=0;}
    void wait(BOOL started,std::stop_token stop,std::chrono::steady_clock::time_point deadline) {
        if(!started && GetLastError()!=ERROR_IO_PENDING)wincheck(FALSE,"Start update request");
        for(;;) {
            cancellation(stop);
            requireUpdate(std::chrono::steady_clock::now()<deadline,"Update request timed out.");
            const auto result=WaitForSingleObject(completed.get(),100);
            if(result==WAIT_OBJECT_0)break;
            wincheck(result!=WAIT_FAILED,"Wait for update request");
        }
        if(error)check(HRESULT_FROM_WIN32(error),"GitHub update request");
    }
};
struct HttpResponse {unsigned status{};std::string body;std::wstring retryAfter;};
std::wstring header(HINTERNET request,DWORD query) {
    DWORD bytes{};
    if(!WinHttpQueryHeaders(request,query,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX)) {
        if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER)return {};
    }
    std::wstring result(bytes/sizeof(wchar_t),L'\0');
    if(!WinHttpQueryHeaders(request,query,WINHTTP_HEADER_NAME_BY_INDEX,result.data(),&bytes,WINHTTP_NO_HEADER_INDEX))return {};
    result.resize(bytes/sizeof(wchar_t));while(!result.empty() && result.back()==L'\0')result.pop_back();return result;
}
HttpResponse request(std::wstring_view url,size_t limit,std::stop_token stop,const std::filesystem::path& output={}) {
    cancellation(stop);
    std::wstring address(url);URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    wincheck(WinHttpCrackUrl(address.c_str(),static_cast<DWORD>(address.size()),0,&parts),"Parse GitHub URL");
    requireUpdate(parts.nScheme==INTERNET_SCHEME_HTTPS,"Update URL must use HTTPS.");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    const auto agent=L"ScreenshotTool/"+std::wstring(AppVersion);
    Internet session(WinHttpOpen(agent.c_str(),WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
    wincheck(session!=nullptr,"Open update connection");
    wincheck(WinHttpSetTimeouts(session.get(),10000,10000,10000,10000),"Set update timeouts");
    Internet connection(WinHttpConnect(session.get(),host.c_str(),parts.nPort,0));wincheck(connection!=nullptr,"Connect to GitHub");
    AsyncRequest operation;
    operation.request.reset(WinHttpOpenRequest(connection.get(),L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    wincheck(operation.request!=nullptr,"Create update request");
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    wincheck(WinHttpSetOption(operation.request.get(),WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)),"Require secure update redirects");
    DWORD redirects=5;wincheck(WinHttpSetOption(operation.request.get(),WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,&redirects,sizeof(redirects)),"Limit update redirects");
    auto callback=WinHttpSetStatusCallback(operation.request.get(),AsyncRequest::callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0);
    requireUpdate(callback!=WINHTTP_INVALID_STATUS_CALLBACK,"Register update network callback.");
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(&operation);
    wincheck(WinHttpSetOption(operation.request.get(),WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)),"Set update request context");operation.contextSet=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    operation.begin();operation.wait(WinHttpSendRequest(operation.request.get(),L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,context),stop,deadline);
    operation.begin();operation.wait(WinHttpReceiveResponse(operation.request.get(),nullptr),stop,deadline);
    HttpResponse response;DWORD status{},bytes=sizeof(status);
    wincheck(WinHttpQueryHeaders(operation.request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX),"Read GitHub status");
    response.status=status;response.retryAfter=header(operation.request.get(),WINHTTP_QUERY_RETRY_AFTER);
    if(status!=200)return response;
    const auto declared=header(operation.request.get(),WINHTTP_QUERY_CONTENT_LENGTH);
    if(!declared.empty())requireUpdate(decimal(declared)<=limit,"Update response exceeds the size limit.");
    std::ofstream file;
    if(!output.empty()){file.open(output,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);}
    std::array<char,16384> buffer{};size_t total{};
    for(;;) {
        operation.begin();operation.wait(WinHttpQueryDataAvailable(operation.request.get(),nullptr),stop,deadline);
        if(!operation.count)break;
        const auto readSize=std::min<DWORD>(operation.count,static_cast<DWORD>(buffer.size()));
        operation.begin();operation.wait(WinHttpReadData(operation.request.get(),buffer.data(),readSize,nullptr),stop,deadline);
        requireUpdate(operation.count>0 && operation.count<=limit-total,"Update response exceeds the size limit.");total+=operation.count;
        if(file.is_open())file.write(buffer.data(),operation.count);else response.body.append(buffer.data(),operation.count);
    }
    cancellation(stop);if(file.is_open())file.close();return response;
}
struct Process {UniqueHandle process,thread;};
Process startProcess(const std::filesystem::path& executable,const std::wstring& arguments,std::span<HANDLE> inherited={}) {
    std::wstring command=quoteArgument(executable.wstring())+L" "+arguments;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=inherited.empty()?sizeof(STARTUPINFOW):sizeof(startup);SIZE_T bytes{};
    std::vector<BYTE> storage;
    if(!inherited.empty()) {
        InitializeProcThreadAttributeList(nullptr,1,0,&bytes);storage.resize(bytes);
        startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        wincheck(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes),"Initialize helper handle list");
    }
    struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST list;~Attributes(){if(list)DeleteProcThreadAttributeList(list);}}attributes{startup.lpAttributeList};
    if(!inherited.empty())wincheck(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited.data(),inherited.size_bytes(),nullptr,nullptr),"Limit inherited helper handles");
    PROCESS_INFORMATION info{};
    wincheck(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,!inherited.empty(),CREATE_NO_WINDOW|(inherited.empty()?0:EXTENDED_STARTUPINFO_PRESENT),nullptr,executable.parent_path().c_str(),&startup.StartupInfo,&info),"Start update process");
    return {UniqueHandle(info.hProcess),UniqueHandle(info.hThread)};
}
void restartUpdated(const PreparedUpdate& update) {
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    UniqueHandle ready(CreateEventW(&security,TRUE,FALSE,nullptr)),helper;
    wincheck(ready!=nullptr,"Create updated app readiness event");HANDLE copy{};
    wincheck(DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&copy,SYNCHRONIZE,TRUE,0),"Prepare cleanup process handle");helper.reset(copy);
    std::array<HANDLE,2> handles{ready.get(),helper.get()};
    auto child=startProcess(update.target,L"--update-started "+handleText(ready.get())+L" --cleanup-update "+quoteArgument(update.helperDirectory.wstring())+L" "+handleText(helper.get()),handles);
    std::array<HANDLE,2> waits{ready.get(),child.process.get()};
    const auto result=WaitForMultipleObjects(static_cast<DWORD>(waits.size()),waits.data(),FALSE,15000);
    if(result!=WAIT_OBJECT_0) {
        TerminateProcess(child.process.get(),1);WaitForSingleObject(child.process.get(),5000);
        throw AppError(TextId::UpdateStartupFailed);
    }
}
}

std::optional<Version> Version::parse(std::wstring_view text) {
    if(text.starts_with(L"v"))text.remove_prefix(1);
    Version result;size_t index{};
    while(!text.empty() && index<3) {
        const auto dot=text.find(L'.');const auto part=text.substr(0,dot);
        if(part.empty() || part.size()>5)return {};
        unsigned value{};
        for(auto c:part){if(c<L'0' || c>L'9')return {};value=value*10+(c-L'0');}
        if(value>65535)return {};result.parts[index++]=value;
        if(dot==std::wstring_view::npos)return index>=2?std::optional{result}:std::nullopt;
        text.remove_prefix(dot+1);
    }
    return {};
}
std::wstring Version::text() const {return std::to_wstring(parts[0])+L"."+std::to_wstring(parts[1])+L"."+std::to_wstring(parts[2]);}
std::wstring quoteArgument(std::wstring_view value) {
    std::wstring result=L"\"";size_t slashes{};
    for(auto c:value) {
        if(c==L'\\'){++slashes;continue;}
        result.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;
        if(c==L'"')result.push_back(L'\\');result.push_back(c);
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');return result;
}
PreparedUpdate::PreparedUpdate(PreparedUpdate&& other) noexcept { *this=std::move(other); }
PreparedUpdate& PreparedUpdate::operator=(PreparedUpdate&& other) noexcept {
    if(this!=&other){cleanup();release=std::move(other.release);target=std::move(other.target);replacement=std::move(other.replacement);backup=std::move(other.backup);helperDirectory=std::move(other.helperDirectory);other.releaseOwnership();}return *this;
}
PreparedUpdate::~PreparedUpdate(){cleanup();}
void PreparedUpdate::cleanup() noexcept {removeFile(replacement);cleanupDirectory(helperDirectory);releaseOwnership();}
void PreparedUpdate::releaseOwnership() noexcept {replacement.clear();backup.clear();helperDirectory.clear();}

UpdateCheckResult UpdateService::parseRelease(std::wstring_view json,Version installed) {
    UpdateCheckResult result;
    try {
        winrt::Windows::Data::Json::JsonObject object;
        if(!winrt::Windows::Data::Json::JsonObject::TryParse(winrt::hstring(json),object))throw AppError(TextId::MalformedMetadata);
        if(object.GetNamedBoolean(L"draft") || object.GetNamedBoolean(L"prerelease"))return {UpdateStatus::Current,{},{TextId::NoNewRelease}};
        const auto version=Version::parse(object.GetNamedString(L"tag_name").c_str());
        requireUpdate(version.has_value(),"GitHub release tag is not a supported numeric version.");
        if(*version<=installed)return {UpdateStatus::Current,{},{TextId::UpToDate}};
        ReleaseInfo release;release.version=*version;release.releaseUrl=object.GetNamedString(L"html_url").c_str();
        requireUpdate(release.releaseUrl.starts_with(ReleasePrefix),"Unexpected GitHub release URL.");
        result.status=UpdateStatus::Incompatible;result.release=release;
        result.message={TextId::IncompatibleRelease};
        const auto assets=object.GetNamedArray(L"assets");bool found=false;
        for(const auto& value:assets) {
            const auto asset=value.GetObject();
            if(asset.GetNamedString(L"name")!=L"ScreenshotTool.exe")continue;
            if(found)throw std::runtime_error("GitHub release has duplicate update assets.");found=true;
            if(asset.GetNamedString(L"state")!=L"uploaded")continue;
            const auto size=asset.GetNamedNumber(L"size");
            if(!std::isfinite(size) || size<1 || size>MaxAssetSize || std::floor(size)!=size)continue;
            release.size=static_cast<uint64_t>(size);
            release.assetUrl=asset.GetNamedString(L"browser_download_url").c_str();
            const auto digest=asset.GetNamedValue(L"digest",winrt::Windows::Data::Json::JsonValue::CreateNullValue());
            if(digest.ValueType()!=winrt::Windows::Data::Json::JsonValueType::String)continue;
            const std::wstring hash=digest.GetString().c_str();
            if(!hash.starts_with(L"sha256:"))continue;release.sha256=hash.substr(7);
            if(!validDigest(release.sha256) || !release.assetUrl.starts_with(AssetPrefix))continue;
            result.status=UpdateStatus::Available;result.release=release;result.message={TextId::ReleaseAvailable,{version->text()}};
        }
        return result;
    }catch(const winrt::hresult_error&) {result.status=UpdateStatus::Failed;result.message={TextId::InvalidMetadata};return result;}
    catch(const std::exception& error){result.status=UpdateStatus::Failed;result.message=errorMessage(error,TextId::UpdateFailed);return result;}
}
UpdateCheckResult UpdateService::httpFailure(unsigned status,std::wstring_view retryAfter) {
    if(status==404)return {UpdateStatus::NoRelease,{},{TextId::NoRelease}};
    if(status==403 || status==429) {
        unsigned seconds=3600;
        try{if(!retryAfter.empty())seconds=static_cast<unsigned>(std::clamp<uint64_t>(decimal(retryAfter),1,86400));}catch(...){}
        return {UpdateStatus::RateLimited,{},{TextId::RateLimited},seconds};
    }
    return {UpdateStatus::Failed,{},{TextId::HttpFailure,{std::to_wstring(status)}}};
}
UpdateCheckResult UpdateService::check(std::stop_token stop) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment {~Apartment(){winrt::uninit_apartment();}}apartment;
        auto response=request(L"https://api.github.com/repos/bustedbunny/ScreenshotTool/releases/latest",MaxMetadataSize,stop);
        if(response.status!=200)return httpFailure(response.status,response.retryAfter);
        return parseRelease(widen(response.body),*Version::parse(AppVersion));
    }catch(const Canceled&){return {UpdateStatus::Canceled,{},{TextId::UpdateCanceled}};}
    catch(const winrt::hresult_error&){return {UpdateStatus::Failed,{},{TextId::ParserFailed}};}
    catch(const std::exception& error){return {UpdateStatus::Failed,{},errorMessage(error,TextId::UpdateFailed)};}
}
std::filesystem::path UpdateService::executablePath() {return processPath(GetCurrentProcess());}
std::wstring UpdateService::sha256(const std::filesystem::path& path,std::stop_token stop) {
    BCRYPT_ALG_HANDLE algorithm{};shot::check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0),"Open SHA-256 provider");
    struct Algorithm {BCRYPT_ALG_HANDLE value;~Algorithm(){BCryptCloseAlgorithmProvider(value,0);}}close{algorithm};
    BCRYPT_HASH_HANDLE hash{};shot::check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0),"Create SHA-256 hash");
    struct Hash {BCRYPT_HASH_HANDLE value;~Hash(){BCryptDestroyHash(value);}}destroy{hash};
    std::ifstream file(path,std::ios::binary);requireUpdate(file.is_open(),"Could not open the downloaded executable.");
    std::array<char,16384> buffer{};
    while(file){cancellation(stop);file.read(buffer.data(),buffer.size());const auto count=file.gcount();if(count)shot::check(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(count),0),"Hash executable");}
    requireUpdate(file.eof(),"Could not read the downloaded executable.");
    std::array<UCHAR,32> digest{};shot::check(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0),"Finish executable hash");
    std::wstring result;constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto byte:digest){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}return result;
}
void UpdateService::verify(const ReleaseInfo& release,const std::filesystem::path& path,std::stop_token stop) {
    cancellation(stop);requireUpdate(release.size>0 && release.size<=MaxAssetSize && validDigest(release.sha256),"Invalid update size or SHA-256 digest.");
    requireUpdate(std::filesystem::file_size(path)==release.size,"Downloaded executable size does not match the release.");
    requireUpdate(sha256(path,stop)==release.sha256,"Downloaded executable SHA-256 does not match the release.");
    std::ifstream file(path,std::ios::binary);IMAGE_DOS_HEADER dos{};file.read(reinterpret_cast<char*>(&dos),sizeof(dos));
    requireUpdate(file && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=static_cast<LONG>(sizeof(dos)) && static_cast<uint64_t>(dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<=release.size,"Downloaded file is not a valid executable.");
    IMAGE_NT_HEADERS64 nt{};file.seekg(dos.e_lfanew);file.read(reinterpret_cast<char*>(&nt),sizeof(nt));
    requireUpdate(file && nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && !(nt.FileHeader.Characteristics&IMAGE_FILE_DLL),"Update executable is not a Windows x64 application.");
    DWORD ignored{};const auto bytes=GetFileVersionInfoSizeW(path.c_str(),&ignored);requireUpdate(bytes>0,"Update executable has no version metadata.");
    std::vector<BYTE> info(bytes);wincheck(GetFileVersionInfoW(path.c_str(),0,bytes,info.data()),"Read executable version");
    VS_FIXEDFILEINFO* version{};UINT length{};
    wincheck(VerQueryValueW(info.data(),L"\\",reinterpret_cast<void**>(&version),&length),"Read executable product version");
    requireUpdate(length>=sizeof(*version) && version->dwSignature==0xfeef04bd && version->dwFileType==VFT_APP,"Invalid executable version metadata.");
    const Version actual{{HIWORD(version->dwFileVersionMS),LOWORD(version->dwFileVersionMS),HIWORD(version->dwFileVersionLS)}};
    requireUpdate(actual==release.version && LOWORD(version->dwFileVersionLS)==0,"Executable version does not match the release tag.");
    wchar_t* name{};wincheck(VerQueryValueW(info.data(),L"\\StringFileInfo\\040904b0\\OriginalFilename",reinterpret_cast<void**>(&name),&length),"Read executable identity");
    requireUpdate(length>0 && std::wstring_view(name)==L"ScreenshotTool.exe","Update executable identity does not match ScreenshotTool.");
    cancellation(stop);
}
PreparedUpdate UpdateService::prepare(const ReleaseInfo& release,const std::filesystem::path& downloaded,const std::filesystem::path& target,std::stop_token stop) {
    verify(release,downloaded,stop);PreparedUpdate update;update.release=release;update.target=std::filesystem::canonical(target);
    const auto token=uniqueToken();const auto helperDirectory=std::filesystem::temp_directory_path()/(std::wstring(DirectoryPrefix)+token);
    requireUpdate(std::filesystem::create_directory(helperDirectory),"Could not reserve the update staging directory.");update.helperDirectory=helperDirectory;
    const auto replacement=update.target.parent_path()/(L".ScreenshotTool-update-"+token+L".exe");
    update.backup=update.target.parent_path()/(L".ScreenshotTool-backup-"+token+L".exe");
    // Reserve before copying so cleanup never removes a pre-existing file,
    // even if copying fails midway through a full-disk or permission error.
    UniqueHandle reservation(CreateFileW(replacement.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    wincheck(reservation.get()!=INVALID_HANDLE_VALUE,"Reserve replacement beside installed executable (move the app to a writable folder if needed)");
    update.replacement=replacement;reservation.reset();
    wincheck(CopyFileW(downloaded.c_str(),update.replacement.c_str(),FALSE),"Stage update beside installed executable (move the app to a writable folder if needed)");
    UniqueHandle staged(CreateFileW(update.replacement.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    wincheck(staged.get()!=INVALID_HANDLE_VALUE,"Open staged update for flush");wincheck(FlushFileBuffers(staged.get()),"Flush staged update");staged.reset();
    verify(release,update.replacement,stop);
    UniqueHandle targetAccess(CreateFileW(update.target.c_str(),DELETE|GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    wincheck(targetAccess.get()!=INVALID_HANDLE_VALUE,"Check permission to replace installed executable");
    wincheck(CopyFileW(executablePath().c_str(),(update.helperDirectory/L"helper.exe").c_str(),TRUE),"Prepare update helper");
    cancellation(stop);return update;
}
PreparedUpdate UpdateService::download(const ReleaseInfo& release,const std::filesystem::path& target,std::stop_token stop) {
    requireUpdate(release.assetUrl.starts_with(AssetPrefix),"Unexpected update download URL.");
    requireUpdate(release.size>0 && release.size<=MaxAssetSize,"Update executable exceeds the size limit.");
    const auto directory=std::filesystem::temp_directory_path()/(std::wstring(DirectoryPrefix)+uniqueToken());
    requireUpdate(std::filesystem::create_directory(directory),"Could not reserve download directory.");
    struct Cleanup {std::filesystem::path path;~Cleanup(){cleanupDirectory(path);}}cleanup{directory};
    const auto path=directory/L"download.exe";
    const auto response=request(release.assetUrl,static_cast<size_t>(release.size),stop,path);
    if(response.status!=200)throw AppError(TextId::DownloadHttpFailure,{std::to_wstring(response.status)});
    return prepare(release,path,target,stop);
}
void UpdateService::launchHelper(PreparedUpdate& update,HANDLE parent) {
    requireUpdate(std::filesystem::equivalent(processPath(parent),update.target),"Update target does not match its parent executable.");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};UniqueHandle ready(CreateEventW(&security,TRUE,FALSE,nullptr)),inheritedParent;
    wincheck(ready!=nullptr,"Create helper readiness event");HANDLE copy{};
    wincheck(DuplicateHandle(GetCurrentProcess(),parent,GetCurrentProcess(),&copy,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0),"Pass update parent handle");inheritedParent.reset(copy);
    const std::wstring args=L"--apply-update "+handleText(copy)+L" "+handleText(ready.get())+L" "+quoteArgument(update.target.wstring())+L" "+quoteArgument(update.replacement.wstring())+L" "+quoteArgument(update.backup.wstring())+L" "+quoteArgument(update.helperDirectory.wstring())+L" "+update.release.sha256+L" "+update.release.version.text()+L" "+std::to_wstring(update.release.size);
    std::array<HANDLE,2> handles{copy,ready.get()};auto child=startProcess(update.helperDirectory/L"helper.exe",args,handles);
    std::array<HANDLE,2> waits{ready.get(),child.process.get()};
    if(WaitForMultipleObjects(static_cast<DWORD>(waits.size()),waits.data(),FALSE,15000)!=WAIT_OBJECT_0) {
        TerminateProcess(child.process.get(),1);WaitForSingleObject(child.process.get(),5000);
        throw AppError(TextId::HelperInitializeFailed);
    }
    update.releaseOwnership();
}
void UpdateService::replaceAndRestart(const PreparedUpdate& update,const std::function<void()>& restart) {
    requireUpdate(!std::filesystem::exists(update.backup),"Update backup already exists.");
    // Antivirus scanners can briefly retain a handle after the parent exits.
    bool replaced=false;
    for(int attempt=0;attempt<20;++attempt) {
        if(ReplaceFileW(update.target.c_str(),update.replacement.c_str(),update.backup.c_str(),0,nullptr,nullptr)){replaced=true;break;}
        const auto error=GetLastError();
        if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION && error!=ERROR_ACCESS_DENIED)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if(!replaced) {
        // ReplaceFile can partially rename files on failure. Restore the
        // original whenever it reached the backup path.
        if(std::filesystem::exists(update.backup))wincheck(MoveFileExW(update.backup.c_str(),update.target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Recover failed update replacement");
        throw AppError(TextId::ReplaceFailed);
    }
    try{restart();}
    catch(...) {
        wincheck(MoveFileExW(update.backup.c_str(),update.target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Restore previous executable (backup retained on failure)");throw;
    }
    removeFile(update.backup);
}
std::optional<int> UpdateService::runHelper(int argc,wchar_t** argv,Language language) {
    if(argc<2 || std::wstring_view(argv[1])!=L"--apply-update")return {};
    UniqueHandle operation;PreparedUpdate update;bool parentExited=false,validated=false;
    try {
        requireUpdate(argc==11,"Invalid update helper arguments.");
        UniqueHandle parent(argumentHandle(argv[2])),ready(argumentHandle(argv[3]));
        update.target=std::filesystem::canonical(argv[4]);update.replacement=argv[5];update.backup=argv[6];update.helperDirectory=argv[7];
        requireUpdate(ownedDirectory(update.helperDirectory) && std::filesystem::equivalent(executablePath(),update.helperDirectory/L"helper.exe"),"Invalid helper staging directory.");
        const auto token=update.helperDirectory.filename().wstring().substr(DirectoryPrefix.size());
        requireUpdate(update.replacement==update.target.parent_path()/(L".ScreenshotTool-update-"+token+L".exe") && update.backup==update.target.parent_path()/(L".ScreenshotTool-backup-"+token+L".exe"),"Invalid update replacement paths.");
        requireUpdate(std::filesystem::equivalent(processPath(parent.get()),update.target),"Update parent does not match target.");
        const auto version=Version::parse(argv[9]);requireUpdate(version.has_value(),"Invalid update version.");
        update.release.version=*version;update.release.sha256=argv[8];update.release.size=decimal(argv[10]);
        verify(update.release,update.replacement);
        // Hold a separate lock across parent exit and replacement so Setup can
        // detect the handoff without blocking the restarted app's singleton.
        operation=beginOperation(UpdateOperationMutex,SetupOperationMutex);
        validated=true;wincheck(SetEvent(ready.get()),"Confirm update helper readiness");
        requireUpdate(WaitForSingleObject(parent.get(),30000)==WAIT_OBJECT_0,"ScreenshotTool did not exit. The update was canceled.");parentExited=true;
        verify(update.release,update.replacement);
        replaceAndRestart(update,[&]{restartUpdated(update);});return 0;
    }catch(const std::exception& error) {
        if(!validated)update.releaseOwnership(); // Do not clean paths supplied by an invalid command line.
        auto message=errorMessage(error,TextId::UpdateFailed).render(language,TextId::ManualInstallHelp);
        if(validated && std::filesystem::exists(update.backup))message+=L"\n\n"+format(language,TextId::BackupHelp,{update.backup.wstring()});
        MessageBoxW(nullptr,message.c_str(),text(language,TextId::UpdateTitle).data(),MB_OK|MB_ICONERROR);
        // Acknowledged restart also lets the recovered app clean the helper
        // after it exits. Report the failure first so cleanup never waits for
        // an error dialog that can remain open indefinitely.
        if(parentExited && !update.target.empty() && !std::filesystem::exists(update.backup))try{restartUpdated(update);}catch(...){}
        return 1;
    }
}
UniqueHandle UpdateServiceTestAccess::beginOperation(const wchar_t* operationMutex,const wchar_t* setupMutex) {
    return shot::beginOperation(operationMutex,setupMutex);
}
void UpdateService::finishStartup(int argc,wchar_t** argv) {
    for(int i=1;i<argc;++i) {
        if(std::wstring_view(argv[i])==L"--update-started" && i+1<argc){UniqueHandle ready(argumentHandle(argv[++i]));wincheck(SetEvent(ready.get()),"Confirm updated app startup");}
        else if(std::wstring_view(argv[i])==L"--cleanup-update" && i+2<argc) {
            const std::filesystem::path directory=argv[++i];UniqueHandle helper(argumentHandle(argv[++i]));
            if(ownedDirectory(directory) && WaitForSingleObject(helper.get(),10000)==WAIT_OBJECT_0)cleanupDirectory(directory);
        }
    }
}
}
