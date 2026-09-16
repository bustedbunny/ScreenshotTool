#include "app.hpp"
#include <commctrl.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    using namespace shot;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ComApartment com;
        UniqueHandle singleton(CreateMutexW(nullptr,FALSE,L"Local\\ScreenshotTool-1C6860C8-243D-4F14-B85C-26E74508D8AA"));
        if(!singleton)wincheck(FALSE,"Create single-instance lock");
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            if(auto existing=FindWindowW(L"ScreenshotTool.Controller",nullptr))PostMessageW(existing,App::CaptureMessage,0,0);
            return 0;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        int argc{};LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool immediate=false;
        if(argv) {for(int i=1;i<argc;++i)if(std::wstring_view(argv[i])==L"--capture")immediate=true;LocalFree(argv);}
        App app(instance);return app.run(immediate);
    } catch(const std::exception& e) {MessageBoxW(nullptr,widen(e.what()).c_str(),L"ScreenshotTool",MB_OK|MB_ICONERROR);return 1;}
}
