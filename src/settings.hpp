#pragma once
#include "win.hpp"
#include <iosfwd>
namespace shot {
struct Settings {
    Pixel color{1,0.12f,0.22f,1};
    float strokeWidth{3};
    float textSize{24};
    bool automaticUpdates{true};
    static Settings load();
    static Settings read(std::istream& stream);
    void write(std::ostream& stream) const;
    void save() const;
    static bool launchAtSignIn();
    static void setLaunchAtSignIn(bool enabled);
    void show(HWND owner);
};
}
