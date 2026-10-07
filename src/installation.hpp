#pragma once
#include <windows.h>
#include <filesystem>
#include <string_view>

namespace shot {
// Keep these names aligned with the installer's AppMutex and SetupMutex.
inline constexpr auto UpdateOperationMutex=L"Local\\ScreenshotTool.UpdateOperation";
inline constexpr auto SetupOperationMutex=L"Local\\ScreenshotTool.Setup";
class Installation {
public:
    // Cosmetic installed-app metadata must never prevent normal startup.
    static void refreshDisplayVersion() noexcept;
};

// Redirects registry access to disposable test keys, never the live install.
struct InstallationTestAccess {
    static bool refreshDisplayVersion(HKEY root,const wchar_t* subkey,
                                      const std::filesystem::path& executable,
                                      std::wstring_view version) noexcept;
};
}
