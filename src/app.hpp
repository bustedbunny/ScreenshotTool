#pragma once
#include "capture.hpp"
#include "export.hpp"
#include "overlay.hpp"
#include <thread>

namespace shot {
class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    int run(bool captureImmediately);
    static constexpr UINT CaptureMessage=WM_APP+1;
private:
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK keyboardProc(int,WPARAM,LPARAM);
    LRESULT message(UINT,WPARAM,LPARAM);
    void capture();
    void action(SessionAction action);
    void endSession();
    void trayMenu();
    void addTray();
    void notify(const std::wstring& text,bool error=false);
    void error(const std::wstring& text);
    void captureFinished(LPARAM data);
    void exportFinished(LPARAM data);
    HINSTANCE instance_{};HWND window_{};HICON icon_{};
    std::jthread keyboardThread_;DWORD keyboardThreadId_{};
    UINT taskbarCreated_{};bool trayAdded_{},exiting_{};
    SessionState state_;PrintScreenGate gate_;Settings settings_;
    std::jthread worker_;unsigned generation_{};
    std::shared_ptr<const DesktopImage> desktop_;
    std::unique_ptr<OverlaySession> session_;
    static App* active_;
};
}
