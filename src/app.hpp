#pragma once
#include "capture.hpp"
#include "export.hpp"
#include "overlay.hpp"
#include "update.hpp"
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
    void checkUpdates(bool manual);
    void updateChecked(LPARAM data);
    void updateDownloaded(LPARAM data);
    void promptUpdate();
    void processPendingUpdate();
    void installPreparedUpdate();
    void openReleasePage();
    HINSTANCE instance_{};HWND window_{};HICON icon_{};
    std::jthread keyboardThread_;DWORD keyboardThreadId_{};
    UINT taskbarCreated_{};bool trayAdded_{},exiting_{};
    SessionState state_;PrintScreenGate gate_;Settings settings_;
    std::jthread worker_;unsigned generation_{};
    std::jthread updateWorker_;
    bool updateBusy_{},manualUpdateCheck_{},updateBalloon_{},pendingUpdateNotification_{},pendingUpdatePrompt_{},dialogOpen_{},updateHandoff_{};
    std::optional<ReleaseInfo> updateRelease_;
    bool updateCompatible_{true};
    std::optional<PreparedUpdate> preparedUpdate_;
    std::chrono::steady_clock::time_point updateRetryAfter_{};
    std::shared_ptr<const DesktopImage> desktop_;
    std::unique_ptr<OverlaySession> session_;
    static App* active_;
};
}
