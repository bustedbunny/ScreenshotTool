// Locale and real dialog checks use no desktop capture or persisted preferences.
std::vector<std::wstring> placeholders(std::wstring_view value) {
    std::vector<std::wstring> result;
    for(size_t at=0;at<value.size();++at)if(value[at]==L'{') {
        const auto end=value.find(L'}',at);require(end!=std::wstring_view::npos,"Closed catalog placeholder");
        const auto token=value.substr(at+1,end-at-1);
        require(!token.empty() && std::all_of(token.begin(),token.end(),[](wchar_t c){return c>=L'0' && c<=L'9';}),"Positional catalog placeholder");
        result.emplace_back(token);at=end;
    }
    std::sort(result.begin(),result.end());return result;
}
void localizationTests() {
    test("automatic locale uses Windows UI language and regional variants",[]{
        const std::pair<int,Language> fixtures[]{
            {MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),Language::English},
            {MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_UK),Language::English},
            {MAKELANGID(LANG_RUSSIAN,SUBLANG_DEFAULT),Language::Russian},
            {MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED),Language::Chinese},
            {MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_TRADITIONAL),Language::Chinese},
            {MAKELANGID(LANG_JAPANESE,SUBLANG_DEFAULT),Language::Japanese},
            {MAKELANGID(LANG_GERMAN,SUBLANG_GERMAN_SWISS),Language::German},
            {MAKELANGID(LANG_SPANISH,SUBLANG_SPANISH_MEXICAN),Language::Spanish},
            {MAKELANGID(LANG_FRENCH,SUBLANG_DEFAULT),Language::English},
            {LANGID{0},Language::English}
        };
        for(auto [system,expected]:fixtures) {
            require(resolveLanguage(Language::Automatic,static_cast<LANGID>(system))==expected,"Map primary display language");
            for(auto fixed:Languages)require(resolveLanguage(fixed,static_cast<LANGID>(system))==fixed,"Fixed selection overrides system language");
        }
        for(auto language:Languages)require(parseLanguage(languageTag(language))==language,"Stable locale tag round trip");
        require(parseLanguage("auto")==Language::Automatic && parseLanguage("unsupported")==Language::Automatic,"Unknown tag follows system");
    });
    test("all embedded translations are complete and preserve placeholders",[]{
        for(size_t index=0;index<static_cast<size_t>(TextId::Count);++index) {
            const auto id=static_cast<TextId>(index);const auto expected=placeholders(text(Language::English,id));
            for(auto language:Languages) {
                require(!text(language,id).empty(),"Every message has a translation");
                require(placeholders(text(language,id))==expected,"Translated arguments match English catalog");
            }
        }
        const auto value=format(Language::German,TextId::Saved,{L"2",L"C:\\folder {0} 日本語"});
        require(value.find(L"C:\\folder {0} 日本語")!=std::wstring::npos,"Arguments are substituted once and remain literal");
        rejects([]{format(Language::English,TextId::WidthValue);});
    });
    test("language persistence tolerates old and malformed settings",[]{
        std::istringstream old("stroke 7\ntext 36\nupdates 0\n");const auto settings=Settings::read(old);
        require(settings.language==Language::Automatic,"Old settings default to automatic");
        for(auto language:std::array{Language::Automatic,Language::English,Language::Russian,Language::Chinese,Language::Japanese,Language::German,Language::Spanish}) {
            auto selected=settings;selected.language=language;std::ostringstream saved;selected.write(saved);std::istringstream input(saved.str());const auto restored=Settings::read(input);
            require(restored.language==language && restored.strokeWidth==7 && restored.textSize==36 && !restored.automaticUpdates,"Language round trip preserves preferences");
        }
        std::istringstream malformed("language unsupported\nstroke bad\nunknown ignored\ntext 40\nred 0.3\nupdates 0\n");const auto restored=Settings::read(malformed);
        require(restored.language==Language::Automatic && restored.textSize==40 && restored.color.r==0.3f && !restored.automaticUpdates,"Malformed entries do not stop subsequent preferences");
    });
    test("structured errors and update results render in the selected language",[]{
        const AppError expected(TextId::ProtectedContent);const auto message=errorMessage(expected,TextId::CaptureFailed);
        require(message.id==TextId::ProtectedContent && std::string_view(expected.what()).find("Windows hid protected content")!=std::string_view::npos,"Expected error retains English diagnostics");
        const std::runtime_error api("Create bitmap (0x80070005)");const auto detail=errorMessage(api,TextId::CaptureFailed);
        for(auto language:Languages) {
            const auto value=detail.render(language);require(value.find(text(language,TextId::CaptureFailed))!=std::wstring::npos && value.find(L"0x80070005")!=std::wstring::npos,"Localized guidance preserves API error detail");
            require(message.render(language)==text(language,TextId::ProtectedContent),"Typed error renders chosen locale");
            const Message longDiagnostic{TextId::UpdateFailed,{},std::wstring(1000,L'x')};
            const auto balloon=abbreviate(longDiagnostic.render(language,TextId::ManualInstallHelp),255);
            require(balloon.find(text(language,TextId::ManualInstallHelp))!=std::wstring::npos,"Recovery guidance appears before shortened technical details");
        }
        const auto pending=UpdateService::httpFailure(500);
        require(pending.message.render(Language::Russian)!=pending.message.render(Language::English),"Completion renders current language instead of worker language");
        require(pending.message.render(Language::Japanese).find(L"500")!=std::wstring::npos,"HTTP codes preserved");
        require(format(Language::German,TextId::UpdateTitle)!=format(Language::English,TextId::UpdateTitle),"Helper title localized");
    });
    test("notification shortening preserves UTF16 boundaries",[]{
        require(abbreviate(L"short",5)==L"short","Unshortened notification intact");
        const std::wstring value=L"ab\U0001F600cdef";const auto result=abbreviate(value,4);
        require(result.size()<=4 && result.back()==L'\u2026',"Shortened text has ellipsis");
        for(size_t i=0;i<result.size();++i)if(result[i]>=0xd800 && result[i]<=0xdbff)require(i+1<result.size() && result[i+1]>=0xdc00 && result[i+1]<=0xdfff,"No split surrogate pair");
        require(abbreviate(value,0).empty(),"Empty buffer handled");
    });
    test("locale UI fonts contain native language labels",[]{
        HDC dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"Create font test DC");
        struct Dc{HDC value;~Dc(){DeleteDC(value);}}dcCleanup{dc};
        for(auto language:Languages) {
            HFONT font=createUiFont(language,-18);require(font!=nullptr,"Create locale font");auto old=SelectObject(dc,font);
            const auto sample=std::wstring(languageName(language));std::vector<WORD> glyphs(sample.size());
            const auto count=GetGlyphIndicesW(dc,sample.data(),static_cast<int>(sample.size()),glyphs.data(),GGI_MARK_NONEXISTING_GLYPHS);
            SelectObject(dc,old);DeleteObject(font);
            require(count!=GDI_ERROR && std::none_of(glyphs.begin(),glyphs.end(),[](WORD glyph){return glyph==0xffff;}),"Native script glyphs available");
        }
    });
    test("settings dialog shows saved language and fits translated content",[]{
        for(auto language:std::array{Language::Automatic,Language::English,Language::Russian,Language::Chinese,Language::Japanese,Language::German,Language::Spanish})for(unsigned dpi:{96u,144u,192u}) {
            Settings settings;settings.language=language;HWND dialog=SettingsDialogTestAccess::create(settings,false,dpi);
            require(dialog!=nullptr,"Create actual settings resource without side effects");
            struct Window{HWND value;~Window(){DestroyWindow(value);}}cleanup{dialog};
            require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETCOUNT,0,0)==7,"Automatic and six language options");
            require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETCURSEL,0,0)==static_cast<int>(language),"Saved selection displayed");
            RECT client{};GetClientRect(dialog,&client);
            const auto locale=settings.effectiveLanguage();
            wchar_t title[128]{};GetWindowTextW(dialog,title,128);require(title==text(locale,TextId::SettingsTitle),"Dialog title translated");
            for(auto id:{IDC_SETTINGS_HEADER,IDC_SETTINGS_INSTRUCTIONS,IDC_SETTINGS_PRINT_SCREEN,IDC_AUTO_UPDATES,IDC_SIGN_IN,IDC_SIGN_IN_HELP,IDC_LANGUAGE_LABEL,IDC_LANGUAGE,IDOK,IDCANCEL}) {
                HWND control=GetDlgItem(dialog,id);require(control!=nullptr,"Settings control exists");RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bounds),2);
                require(bounds.left>=0 && bounds.top>=0 && bounds.right<=client.right && bounds.bottom<=client.bottom,"Settings controls remain within client area");
                if(id!=IDC_LANGUAGE) {
                    const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));HDC dc=GetDC(control);auto old=SelectObject(dc,font);
                    const int length=GetWindowTextLengthW(control);std::wstring caption(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(control,caption.data(),length+1);caption.resize(length);
                    const bool checkbox=id==IDC_AUTO_UPDATES || id==IDC_SIGN_IN;
                    RECT needed{0,0,bounds.right-bounds.left-(checkbox?MulDiv(24,dpi,96):0),0};
                    DrawTextW(dc,caption.data(),length,&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                    SelectObject(dc,old);ReleaseDC(control,dc);
                    require(needed.bottom-needed.top<=bounds.bottom-bounds.top,"Translated paragraphs and control captions fit vertically");
                }
            }
        }
    });
    test("confirming settings accepts a language and cancel leaves it unchanged",[]{
        Settings initial;initial.language=Language::German;initial.automaticUpdates=false;
        for(bool accept:{false,true}) {
            Settings result;const HWND dialog=SettingsDialogTestAccess::create(initial,false,96,&result);
            SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_SETCURSEL,static_cast<WPARAM>(Language::Japanese),0);
            CheckDlgButton(dialog,IDC_AUTO_UPDATES,BST_CHECKED);
            SendMessageW(dialog,WM_COMMAND,accept?IDOK:IDCANCEL,0);
            require(!IsWindow(dialog),"Confirmation closes native dialog");
            require(result.language==(accept?Language::Japanese:Language::German) && result.automaticUpdates==accept,"Only OK accepts dialog selections");
            require(result.color==initial.color && result.strokeWidth==initial.strokeWidth && result.textSize==initial.textSize,"Dialog preserves drawing preferences");
        }
    });
}
