#include "app.hpp"
#include "installation.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <exception>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    using namespace shot;
    auto language=systemLanguage();
    try {
        Settings settings;std::exception_ptr settingsError;
        try{settings=Settings::load();language=settings.effectiveLanguage();}catch(...){settingsError=std::current_exception();}
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ComApartment com;
        int argc{};LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        struct Arguments {LPWSTR* value;~Arguments(){LocalFree(value);}}arguments{argv};
        wincheck(argv!=nullptr,"Read command line");
        if(auto result=UpdateService::runHelper(argc,argv,language))return *result;
        if(settingsError)std::rethrow_exception(settingsError);
        bool updateStartup=false;
        for(int i=1;i<argc;++i)if(std::wstring_view(argv[i])==L"--update-started")updateStartup=true;
        UniqueHandle singleton(CreateMutexW(nullptr,FALSE,L"Local\\ScreenshotTool-1C6860C8-243D-4F14-B85C-26E74508D8AA"));
        if(!singleton)wincheck(FALSE,"Create single-instance lock");
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            // A competing instance during update handoff must not trigger an
            // unsolicited capture or acknowledge startup of the wrong app.
            if(!updateStartup)if(auto existing=FindWindowW(L"ScreenshotTool.Controller",nullptr))PostMessageW(existing,App::CaptureMessage,0,0);
            return updateStartup?1:0;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        bool immediate=false;
        for(int i=1;i<argc;++i)if(std::wstring_view(argv[i])==L"--capture")immediate=true;
        App app(instance,std::move(settings));UpdateService::finishStartup(argc,argv);
        Installation::refreshDisplayVersion();return app.run(immediate);
    } catch(const std::exception& e) {MessageBoxW(nullptr,errorMessage(e,TextId::StartupFailed).render(language).c_str(),L"ScreenshotTool",MB_OK|MB_ICONERROR);return 1;}
}
