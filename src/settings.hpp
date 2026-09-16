#pragma once
#include "win.hpp"
namespace shot {
struct Settings {
    Pixel color{1,0.12f,0.22f,1};
    float strokeWidth{3};
    float textSize{24};
    static Settings load();
    void save() const;
    static bool launchAtSignIn();
    static void setLaunchAtSignIn(bool enabled);
    static void show(HWND owner);
};
}
