#pragma once
#include "win.hpp"
#include "localization.hpp"
#include <array>
#include <chrono>
#include <compare>
#include <stop_token>

namespace shot {
struct Version {
    std::array<unsigned,3> parts{};
    auto operator<=>(const Version&) const = default;
    static std::optional<Version> parse(std::wstring_view text);
    std::wstring text() const;
};
struct ReleaseInfo {
    Version version;
    std::wstring releaseUrl, assetUrl, sha256;
    uint64_t size{};
};
enum class UpdateStatus { Current, Available, NoRelease, Incompatible, Failed, RateLimited, Canceled };
struct UpdateCheckResult {
    UpdateStatus status{UpdateStatus::Failed};
    std::optional<ReleaseInfo> release;
    Message message{TextId::UpdateFailed};
    unsigned retryAfterSeconds{};
};
// Owns only the uniquely named files created for this update. Move-only so a
// posted completion can transfer cleanup responsibility to the controller.
struct PreparedUpdate {
    ReleaseInfo release;
    std::filesystem::path target, replacement, backup, helperDirectory;
    PreparedUpdate() = default;
    PreparedUpdate(const PreparedUpdate&) = delete;
    PreparedUpdate& operator=(const PreparedUpdate&) = delete;
    PreparedUpdate(PreparedUpdate&& other) noexcept;
    PreparedUpdate& operator=(PreparedUpdate&& other) noexcept;
    ~PreparedUpdate();
    void cleanup() noexcept;
    void releaseOwnership() noexcept;
};
class UpdateService {
public:
    static UpdateCheckResult check(std::stop_token stop);
    static UpdateCheckResult parseRelease(std::wstring_view json, Version installed);
    static UpdateCheckResult httpFailure(unsigned status, std::wstring_view retryAfter = {});
    static PreparedUpdate download(const ReleaseInfo&, const std::filesystem::path& target, std::stop_token);
    static PreparedUpdate prepare(const ReleaseInfo&, const std::filesystem::path& downloaded,
                                  const std::filesystem::path& target, std::stop_token);
    static void verify(const ReleaseInfo&, const std::filesystem::path&, std::stop_token = {});
    static std::wstring sha256(const std::filesystem::path&, std::stop_token = {});
    // Returns only after the helper has validated its inputs and opened its
    // parent handle; caller must then exit, without deleting PreparedUpdate.
    static void launchHelper(PreparedUpdate&, HANDLE parent = GetCurrentProcess());
    static std::optional<int> runHelper(int argc, wchar_t** argv, Language language = Language::English);
    static void finishStartup(int argc, wchar_t** argv);
    static std::filesystem::path executablePath();
    static void replaceAndRestart(const PreparedUpdate&, const std::function<void()>& restart);
};
struct UpdateServiceTestAccess {
    static UniqueHandle beginOperation(const wchar_t* operationMutex,const wchar_t* setupMutex);
};
// Used for both helper and post-update startup arguments; shell-free quoting.
std::wstring quoteArgument(std::wstring_view value);
}
