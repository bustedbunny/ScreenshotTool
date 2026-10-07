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
void selectSettingsLanguage(HWND dialog,Language language) {
    SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_SETCURSEL,static_cast<WPARAM>(language),0);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_LANGUAGE)));
}
void checkSettingsOption(HWND dialog,int id,bool checked) {
    CheckDlgButton(dialog,id,checked?BST_CHECKED:BST_UNCHECKED);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,id)));
}
struct SettingsTestWindow {
    HWND value;
    ~SettingsTestWindow(){if(IsWindow(value))DestroyWindow(value);}
};
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
    test("settings dialog fits saved and applied translations at each DPI",[]{
        for(auto language:std::array{Language::Automatic,Language::English,Language::Russian,Language::Chinese,Language::Japanese,Language::German,Language::Spanish})for(unsigned dpi:{96u,144u,192u}) {
            Settings settings;settings.language=language;HWND dialog=SettingsDialogTestAccess::create(settings,false,dpi);
            require(dialog!=nullptr,"Create actual settings resource without side effects");
            SettingsTestWindow cleanup{dialog};
            const auto caption=[](HWND control) {
                const int length=GetWindowTextLengthW(control);std::wstring value(static_cast<size_t>(length)+1,L'\0');
                GetWindowTextW(control,value.data(),length+1);value.resize(length);return value;
            };
            const auto verify=[&](Language selected) {
                const auto locale=resolveLanguage(selected,GetUserDefaultUILanguage());
                require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETCOUNT,0,0)==7,"Relocalization retains exactly seven language options");
                require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETCURSEL,0,0)==static_cast<int>(selected),"Saved or applied selection displayed");
                require(caption(dialog)==text(locale,TextId::SettingsTitle),"Dialog title translated immediately");
                require(caption(GetDlgItem(dialog,IDC_SETTINGS_HEADER))==format(locale,TextId::SettingsHeader,{AppVersion}),"Version header translated immediately");
                for(const auto [id,textId]:std::array{
                    std::pair{IDC_SETTINGS_INSTRUCTIONS,TextId::SettingsInstructions},std::pair{IDC_SETTINGS_PRINT_SCREEN,TextId::SettingsPrintScreen},
                    std::pair{IDC_AUTO_UPDATES,TextId::AutomaticUpdates},std::pair{IDC_SIGN_IN,TextId::LaunchAtSignIn},
                    std::pair{IDC_SIGN_IN_HELP,TextId::SignInHelp},std::pair{IDC_LANGUAGE_LABEL,TextId::LanguageLabel},
                    std::pair{IDOK,TextId::Ok},std::pair{IDCANCEL,TextId::Cancel},std::pair{IDC_APPLY,TextId::Apply}})
                    require(caption(GetDlgItem(dialog,id))==text(locale,textId),"Settings control caption translated immediately");
                for(int index=0;index<7;++index) {
                    const auto itemLanguage=static_cast<Language>(index);
                    const auto length=SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETLBTEXTLEN,index,0);
                    require(length!=CB_ERR,"Language option remains readable");std::wstring value(static_cast<size_t>(length)+1,L'\0');
                    SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(value.data()));value.resize(static_cast<size_t>(length));
                    require(value==(index?std::wstring_view(languageName(itemLanguage)):text(locale,TextId::Automatic)),"Automatic caption translated and native language labels retained");
                    require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETITEMDATA,index,0)==index,"Language option data retained");
                }
                RECT client{};GetClientRect(dialog,&client);
                const auto uiFont=reinterpret_cast<HFONT>(SendDlgItemMessageW(dialog,IDC_SETTINGS_HEADER,WM_GETFONT,0,0));require(uiFont!=nullptr,"Settings controls have a locale UI font");
                LOGFONTW fontInfo{};require(GetObjectW(uiFont,sizeof(fontInfo),&fontInfo)!=0 && fontInfo.lfHeight==-MulDiv(9,dpi,72),"UI font follows dialog DPI");
                for(auto id:{IDC_SETTINGS_HEADER,IDC_SETTINGS_INSTRUCTIONS,IDC_SETTINGS_PRINT_SCREEN,IDC_AUTO_UPDATES,IDC_SIGN_IN,IDC_SIGN_IN_HELP,IDC_LANGUAGE_LABEL,IDC_LANGUAGE,IDOK,IDCANCEL,IDC_APPLY}) {
                    HWND control=GetDlgItem(dialog,id);require(control!=nullptr,"Settings control exists");RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bounds),2);
                    require(bounds.left>=0 && bounds.top>=0 && bounds.right<=client.right && bounds.bottom<=client.bottom,"Settings controls remain within client area");
                    const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));require(font==uiFont,"All controls use the refreshed locale font");
                    if(id!=IDC_LANGUAGE) {
                        HDC dc=GetDC(control);auto old=SelectObject(dc,font);const auto value=caption(control);
                        const bool checkbox=id==IDC_AUTO_UPDATES || id==IDC_SIGN_IN;
                        RECT needed{0,0,bounds.right-bounds.left-(checkbox?MulDiv(24,dpi,96):0),0};
                        DrawTextW(dc,value.data(),static_cast<int>(value.size()),&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                        SelectObject(dc,old);ReleaseDC(control,dc);
                        require(needed.bottom-needed.top<=bounds.bottom-bounds.top,"Translated paragraphs and control captions fit vertically");
                        if(id==IDOK || id==IDCANCEL || id==IDC_APPLY)require(needed.right<=bounds.right-bounds.left,"Translated button caption fits horizontally");
                    }
                }
                HDC dc=GetDC(dialog);auto old=SelectObject(dc,uiFont);const auto sample=std::wstring(languageName(locale));std::vector<WORD> glyphs(sample.size());
                const auto count=GetGlyphIndicesW(dc,sample.data(),static_cast<int>(sample.size()),glyphs.data(),GGI_MARK_NONEXISTING_GLYPHS);
                SelectObject(dc,old);ReleaseDC(dialog,dc);
                require(count!=GDI_ERROR && std::none_of(glyphs.begin(),glyphs.end(),[](WORD glyph){return glyph==0xffff;}),"Refreshed font contains selected language glyphs");
                RECT ok{},cancel{},apply{};GetWindowRect(GetDlgItem(dialog,IDOK),&ok);GetWindowRect(GetDlgItem(dialog,IDCANCEL),&cancel);GetWindowRect(GetDlgItem(dialog,IDC_APPLY),&apply);
                require(ok.right<=cancel.left && cancel.right<=apply.left,"OK Cancel and Apply buttons do not overlap");
            };
            verify(language);require(!IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Apply starts disabled");
            const auto next=language==Language::Spanish?Language::Automatic:static_cast<Language>(static_cast<int>(language)+1);
            for(auto selected:{next,language}) {
                RECT before{};GetWindowRect(dialog,&before);selectSettingsLanguage(dialog,selected);
                require(IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Language edits enable Apply");
                SendMessageW(dialog,WM_COMMAND,IDC_APPLY,0);require(IsWindow(dialog),"Apply keeps the same settings dialog open");
                require(!IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Successful Apply establishes the new baseline");
                verify(selected);RECT after{};GetWindowRect(dialog,&after);require(after.left==before.left && after.top==before.top,"Relocalization preserves dialog position");
            }
        }
    });
    test("confirming settings accepts a language and cancel leaves it unchanged",[]{
        Settings initial;initial.language=Language::German;initial.automaticUpdates=false;
        for(bool accept:{false,true}) {
            Settings result;const HWND dialog=SettingsDialogTestAccess::create(initial,false,96,&result);
            SettingsTestWindow cleanup{dialog};selectSettingsLanguage(dialog,Language::Japanese);checkSettingsOption(dialog,IDC_AUTO_UPDATES,true);
            SendMessageW(dialog,WM_COMMAND,accept?IDOK:IDCANCEL,0);
            require(!IsWindow(dialog),"Confirmation closes native dialog");
            require(result.language==(accept?Language::Japanese:Language::German) && result.automaticUpdates==accept,"Only OK accepts dialog selections");
            require(result.color==initial.color && result.strokeWidth==initial.strokeWidth && result.textSize==initial.textSize,"Dialog preserves drawing preferences");
        }
    });
    test("Apply commits all pending preferences and closing keeps only accepted edits",[]{
        Settings initial;initial.language=Language::German;initial.automaticUpdates=false;initial.color={0.2f,0.4f,0.6f,1};initial.strokeWidth=7;initial.textSize=36;
        for(UINT close:{static_cast<UINT>(IDCANCEL),static_cast<UINT>(WM_CLOSE),static_cast<UINT>(IDOK)}) {
            Settings result;std::vector<std::pair<Settings,bool>> commits;std::vector<Settings> acceptedBefore,refreshed;std::vector<int> events;
            const HWND dialog=SettingsDialogTestAccess::create(initial,false,96,&result,
                [&](const Settings& pending,bool signIn){acceptedBefore.push_back(result);commits.emplace_back(pending,signIn);events.push_back(1);},
                [&]{refreshed.push_back(result);events.push_back(2);});
            SettingsTestWindow cleanup{dialog};
            selectSettingsLanguage(dialog,Language::Japanese);checkSettingsOption(dialog,IDC_AUTO_UPDATES,true);checkSettingsOption(dialog,IDC_SIGN_IN,true);
            SendMessageW(dialog,WM_COMMAND,IDC_APPLY,0);
            require(IsWindow(dialog) && result.language==Language::Japanese && result.automaticUpdates,"Apply accepts pending preferences without closing");
            require(commits.size()==1 && commits.front().first.language==Language::Japanese && commits.front().first.automaticUpdates && commits.front().second,"Commit receives language update and sign-in preferences together");
            require(acceptedBefore.front().language==initial.language && !acceptedBefore.front().automaticUpdates && refreshed.size()==1 && refreshed.front().language==Language::Japanese,"Commit precedes acceptance and app refresh follows acceptance");
            require(events==std::vector<int>{1,2},"Apply commits and refreshes exactly once in order");
            selectSettingsLanguage(dialog,Language::Russian);checkSettingsOption(dialog,IDC_AUTO_UPDATES,false);checkSettingsOption(dialog,IDC_SIGN_IN,false);
            if(close==WM_CLOSE)SendMessageW(dialog,WM_CLOSE,0,0);else SendMessageW(dialog,WM_COMMAND,close,0);
            const bool accept=close==IDOK;require(!IsWindow(dialog),"OK Cancel and window close dismiss settings");
            require(result.language==(accept?Language::Russian:Language::Japanese) && result.automaticUpdates==!accept,"Closing retains last Apply and only OK accepts later edits");
            require(commits.size()==(accept?2u:1u) && refreshed.size()==commits.size(),"Cancelled edits do not commit or refresh");
            if(accept)require(!commits.back().second && commits.back().first.language==Language::Russian && acceptedBefore.back().language==Language::Japanese && refreshed.back().language==Language::Russian && events==std::vector<int>{1,2,1,2},"OK commits later sign-in changes before refreshing");
            require(result.color==initial.color && result.strokeWidth==initial.strokeWidth && result.textSize==initial.textSize,"Applying preferences preserves drawing settings");
        }
    });
    test("Apply tracks net edits and failed commits remain open for retry",[]{
        for(int command:{IDC_APPLY,IDOK}) {
            Settings initial;initial.language=Language::English;Settings result;unsigned attempts{},refreshes{};bool fail=true;
            const HWND dialog=SettingsDialogTestAccess::create(initial,false,96,&result,
                [&](const Settings&,bool){++attempts;if(fail)throw std::runtime_error("Injected settings persistence failure");},[&]{++refreshes;});
            SettingsTestWindow cleanup{dialog};
            for(int id:{IDC_AUTO_UPDATES,IDC_SIGN_IN}) {
                const bool original=id==IDC_AUTO_UPDATES;checkSettingsOption(dialog,id,!original);
                require(IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Each checkbox edit enables Apply");checkSettingsOption(dialog,id,original);
                require(!IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Reverting a checkbox clears pending changes");
            }
            selectSettingsLanguage(dialog,Language::Russian);require(IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Language edit enables Apply");
            selectSettingsLanguage(dialog,Language::English);require(!IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Reverting language clears pending changes");
            selectSettingsLanguage(dialog,Language::Japanese);checkSettingsOption(dialog,IDC_AUTO_UPDATES,false);checkSettingsOption(dialog,IDC_SIGN_IN,true);
            SendMessageW(dialog,WM_COMMAND,command,0);
            require(IsWindow(dialog) && IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)),"Failed Apply or OK keeps pending edits available");
            require(attempts==1 && refreshes==0 && result.language==initial.language && result.automaticUpdates,"Failed persistence does not accept settings or refresh the app");
            const auto error=SettingsDialogTestAccess::lastError(dialog);require(error.has_value(),"Failure is recorded for the test dialog");
            require(error->id==TextId::SettingsFailed && error->render(Language::English).find(L"Injected settings persistence failure")!=std::wstring::npos,"Failure includes localized settings guidance and diagnostic");
            wchar_t title[128]{};GetWindowTextW(dialog,title,128);require(title==text(Language::English,TextId::SettingsTitle),"Failed commit retains applied UI language");
            require(SendDlgItemMessageW(dialog,IDC_LANGUAGE,CB_GETCURSEL,0,0)==static_cast<int>(Language::Japanese) && IsDlgButtonChecked(dialog,IDC_AUTO_UPDATES)==BST_UNCHECKED && IsDlgButtonChecked(dialog,IDC_SIGN_IN)==BST_CHECKED,"Failure preserves all pending control values");
            fail=false;SendMessageW(dialog,WM_COMMAND,command,0);
            require(attempts==2 && refreshes==1 && result.language==Language::Japanese && !result.automaticUpdates,"Retry accepts preferences and refreshes exactly once");
            if(command==IDC_APPLY) {
                require(IsWindow(dialog) && !IsWindowEnabled(GetDlgItem(dialog,IDC_APPLY)) && !SettingsDialogTestAccess::lastError(dialog),"Successful retry clears error and establishes baseline");
                SendMessageW(dialog,WM_COMMAND,IDOK,0);require(attempts==3 && refreshes==2,"Clean OK still follows the shared commit path");
            }
            require(!IsWindow(dialog),"Successful OK closes after persistence");
        }
    });
}
