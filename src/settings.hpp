#pragma once
#include "win.hpp"
#include "localization.hpp"
#include <functional>
#include <iosfwd>
#include <optional>
namespace shot {
struct Settings {
    Pixel color{1,0.12f,0.22f,1};
    float strokeWidth{3};
    float textSize{24};
    bool automaticUpdates{true};
    Language language{Language::Automatic};
    Language effectiveLanguage() const { return resolveLanguage(language,GetUserDefaultUILanguage()); }
    static Settings load();
    static Settings read(std::istream& stream);
    void write(std::ostream& stream) const;
    void save() const;
    static bool launchAtSignIn();
    static void setLaunchAtSignIn(bool enabled);
    void show(HWND owner,std::function<void()> applied={});
};
// Creates the same native dialog without saving settings or changing sign-in.
// Callbacks simulate persistence and observe success; lastError replaces error UI.
// The caller owns the window and must release it with DestroyWindow.
struct SettingsDialogTestAccess {
    static HWND create(const Settings& settings,bool signIn=false,unsigned dpi=0,Settings* accepted=nullptr,
        std::function<void(const Settings&,bool)> commit={},std::function<void()> applied={});
    static std::optional<Message> lastError(HWND window);
};
}
