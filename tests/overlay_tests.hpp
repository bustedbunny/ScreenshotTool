// Native offscreen-window integration tests. No desktop capture or synthetic user input.
namespace shot {
struct OverlayTestAccess {
    struct Counts {int labels{},enabled{},positions{},fonts{};};
    static LRESULT CALLBACK countButton(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        auto& counts=*reinterpret_cast<Counts*>(data);
        if(msg==WM_SETTEXT)++counts.labels;if(msg==WM_ENABLE)++counts.enabled;
        if(msg==WM_WINDOWPOSCHANGED)++counts.positions;if(msg==WM_SETFONT)++counts.fonts;
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    static std::shared_ptr<DesktopImage> desktop() {
        auto result=std::make_shared<DesktopImage>();
        for(int i=0;i<3;++i){MonitorImage m;m.bounds={i*1920,0,(i+1)*1920,1080};result->monitors.push_back(std::move(m));}
        return result;
    }
    static void setup(OverlaySession& s) {
        for(size_t i=0;i<s.desktop_->monitors.size();++i) {
            auto view=std::make_unique<OverlaySession::MonitorWindow>();view->session=&s;view->index=i;
            view->hwnd=CreateWindowExW(0,L"STATIC",L"Hidden invalidation test",WS_POPUP,-30000,-30000,1920,1080,nullptr,nullptr,s.instance_,nullptr);
            require(view->hwnd!=nullptr,"Create hidden monitor test window");ShowWindow(view->hwnd,SW_SHOWNOACTIVATE);s.windows_.push_back(std::move(view));
        }
        s.selection_=s.desktop_->bounds();s.createToolbar();
        SetWindowPos(s.toolbar_,nullptr,-30000,-30000,s.toolbarWidth_,s.toolbarHeight_,SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW);
        s.text_.begin({100,100},s.selection_,{1,0,0,1},24);
        clear(s);
    }
    static void clear(OverlaySession& s) {
        ValidateRect(s.toolbar_,nullptr);for(const auto& b:s.buttons_)ValidateRect(b.hwnd,nullptr);
        for(const auto& w:s.windows_)ValidateRect(w->hwnd,nullptr);
    }
    static int dirtyButtons(const OverlaySession& s) {
        int result=0;for(const auto& b:s.buttons_)if(GetUpdateRect(b.hwnd,nullptr,FALSE))++result;return result;
    }
    static std::wstring caption(HWND control) {
        const int length=GetWindowTextLengthW(control);std::wstring result(static_cast<size_t>(length)+1,L'\0');
        GetWindowTextW(control,result.data(),length+1);result.resize(length);return result;
    }
    static void drag(OverlaySession& s,Handle handle=Handle::Move) {
        s.textDragging_=true;s.textCreating_=s.textSelecting_=false;s.textHandle_=handle;
        s.textStart_={0,0};s.textOriginal_=*s.text_.annotation().textBounds;s.text_.beginGesture();
    }
    static void tests() {
        test("localized toolbar fits all languages DPI scales and narrow work areas",[]{
            struct ExpectedCaption {int id;TextId label;const wchar_t* suffix;};
            constexpr ExpectedCaption expected[]{
                {1000,TextId::Select,L" (V)"},{1001,TextId::Pen,L" (F)"},{1002,TextId::Highlight,L" (H)"},
                {1003,TextId::Rectangle,L" (R)"},{1004,TextId::Ellipse,L" (E)"},{1005,TextId::Line,L" (L)"},
                {1006,TextId::Arrow,L" (A)"},{1007,TextId::Text,L" (T)"},{1008,TextId::Censor,L" (B)"},
                {1100,TextId::Color,L" (C)"},{1101,TextId::Width,L" (W)"},{1102,TextId::TextSize,L" (S)"},
                {1103,TextId::CoverBlack,L" (P)"},{1200,TextId::Undo,L" (Ctrl + Z)"},{1201,TextId::Redo,L" (Ctrl + Y)"},
                {1202,TextId::Copy,L" (Ctrl + C)"},{1203,TextId::Save,L" (Ctrl + S)"},
                {1204,TextId::SaveAs,L" (Ctrl + Shift + S)"},{1205,TextId::Cancel,L" (Esc)"}
            };
            constexpr std::pair<int,int> values[]{{1,8},{2,12},{3,16},{5,20},{8,24},{12,32},{18,48},{24,72},{1,96},{24,144}};
            for(Language language:Languages) {
                Settings settings;settings.language=language;
                OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[](Message){require(false,"Unexpected localized toolbar error");});setup(s);
                require(s.language_==language && s.buttons_.front().label==std::wstring(text(language,TextId::Select))+L" (V)","Toolbar uses selected language");
                const auto verifyCaptions=[&] {
                    require(s.buttons_.size()==std::size(expected),"Every toolbar command has an expected keybind caption");
                    for(size_t index=0;index<s.buttons_.size();++index) {
                        const auto& button=s.buttons_[index];const auto& fixture=expected[index];
                        require(button.id==fixture.id,"Caption fixture matches toolbar command");
                        std::wstring label(text(language,fixture.label));
                        if(fixture.id==1101)label=format(language,TextId::WidthValue,{std::to_wstring(static_cast<int>(settings.strokeWidth))});
                        if(fixture.id==1102)label=format(language,TextId::TextSizeValue,{std::to_wstring(static_cast<int>(settings.textSize))});
                        if(fixture.id==1103)label=text(language,s.pixelated_?TextId::CoverPixelate:TextId::CoverBlack);
                        label+=fixture.suffix;
                        require(button.label==label,"Localized caption retains exactly one correct keybind suffix");
                        require(caption(button.hwnd)==label,"Native caption matches stored caption and keybind");
                    }
                };
                verifyCaptions();
                require(IsWindow(s.tooltip_) && SendMessageW(s.tooltip_,TTM_GETTOOLCOUNT,0,0)==static_cast<LRESULT>(s.buttons_.size()),"Every toolbar control has a registered native tooltip");
                std::vector<std::wstring> originalTips;std::vector<const wchar_t*> originalTipBuffers;
                for(const auto& button:s.buttons_){originalTips.push_back(button.tip);originalTipBuffers.push_back(button.tip.data());}
                for(unsigned dpi:{96u,144u,192u,288u})for(int availableWidth:{540,1200}) {
                    s.layoutToolbar(dpi,availableWidth);
                    require(s.toolbarDpi_==dpi && s.toolbarWidth_<=availableWidth,"Toolbar wraps without reducing actual monitor DPI");
                    require(reinterpret_cast<HFONT>(SendMessageW(s.tooltip_,WM_GETFONT,0,0))==s.toolbarFont_,"Tooltips use the locale toolbar font");
                    require(SendMessageW(s.tooltip_,TTM_GETMAXTIPWIDTH,0,0)<=MulDiv(420,dpi,96),"Long translated tips wrap within a DPI-scaled width");
                    std::vector<RECT> originalBounds;
                    for(const auto& button:s.buttons_) {
                        RECT bounds{};GetWindowRect(button.hwnd,&bounds);MapWindowPoints(nullptr,s.toolbar_,reinterpret_cast<POINT*>(&bounds),2);originalBounds.push_back(bounds);
                    }
                    for(const auto [strokeWidth,textSize]:values)for(bool pixelated:{false,true}) {
                        settings.strokeWidth=static_cast<float>(strokeWidth);settings.textSize=static_cast<float>(textSize);
                        s.pixelated_=pixelated;s.refreshButtons();
                        verifyCaptions();
                        RECT client{};GetClientRect(s.toolbar_,&client);HDC dc=GetDC(s.toolbar_);auto old=SelectObject(dc,s.toolbarFont_);
                        std::vector<RECT> rectangles;
                        for(size_t index=0;index<s.buttons_.size();++index) {
                            const auto& button=s.buttons_[index];
                            RECT bounds{};GetWindowRect(button.hwnd,&bounds);MapWindowPoints(nullptr,s.toolbar_,reinterpret_cast<POINT*>(&bounds),2);
                            require(EqualRect(&bounds,&originalBounds[index]),"Changing values or censor mode keeps controls in place");
                            require(bounds.left>=0 && bounds.right<=client.right && bounds.top>=0 && bounds.bottom<=s.toolbarStatusTop_,"Every control remains inside toolbar and above status");
                            for(const auto& previous:rectangles){RECT overlap{};require(!IntersectRect(&overlap,&bounds,&previous),"Wrapped controls do not overlap");}rectangles.push_back(bounds);
                            const int padding=MulDiv(button.id==1100?40:20,dpi,96);
                            RECT caption{0,0,bounds.right-bounds.left-padding,0};const LONG width=caption.right;
                            DrawTextW(dc,button.label.c_str(),-1,&caption,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);
                            require(caption.right<=width && caption.bottom<=bounds.bottom-bounds.top-MulDiv(8,dpi,96),"Complete translated caption fits its control");
                            require(button.tip==originalTips[index] && button.tip.data()==originalTipBuffers[index],"Owned tooltip text stays stable after layout and value changes");
                            // Common controls may copy the supplied text. Query the registered
                            // contents into a caller-owned buffer rather than comparing pointers.
                            std::vector<wchar_t> tip(originalTips[index].size()+2);
                            TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=s.toolbar_;info.uId=reinterpret_cast<UINT_PTR>(button.hwnd);info.lpszText=tip.data();
                            SendMessageW(s.tooltip_,TTM_GETTEXTW,tip.size(),reinterpret_cast<LPARAM>(&info));
                            const std::wstring_view actualTip(tip.data());
                            if(actualTip!=originalTips[index]) {
                                const int bytes=WideCharToMultiByte(CP_UTF8,0,actualTip.data(),static_cast<int>(actualTip.size()),nullptr,0,nullptr,nullptr);
                                std::string nativeTip(bytes,'\0');if(bytes)WideCharToMultiByte(CP_UTF8,0,actualTip.data(),static_cast<int>(actualTip.size()),nativeTip.data(),bytes,nullptr,nullptr);
                                throw std::runtime_error("Registered translated tooltip survives layout and value changes: language="+std::string(languageTag(language))+" id="+std::to_string(button.id)+" tool_count="+std::to_string(SendMessageW(s.tooltip_,TTM_GETTOOLCOUNT,0,0))+" expected_chars="+std::to_string(originalTips[index].size())+" actual_chars="+std::to_string(actualTip.size())+" text="+nativeTip);
                            }
                        }
                        RECT status=s.statusRect();const LONG statusWidth=status.right-status.left;status.right=statusWidth;status.left=status.top=0;status.bottom=0;
                        s.refreshStatus();DrawTextW(dc,s.toolbarStatus_.c_str(),-1,&status,DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);
                        require(status.right<=statusWidth && status.bottom<=client.bottom-s.toolbarStatusTop_,"Translated status fits below all toolbar rows");
                        SelectObject(dc,old);ReleaseDC(s.toolbar_,dc);
                    }
                    clear(s);Counts counts;for(const auto& b:s.buttons_)SetWindowSubclass(b.hwnd,countButton,1,reinterpret_cast<DWORD_PTR>(&counts));
                    for(int i=0;i<10;++i){s.layoutToolbar(dpi,availableWidth);s.refreshButtons();s.refreshStatus();}
                    require(!counts.labels && !counts.enabled && !counts.positions && !counts.fonts,"Identical refresh does not relayout or mutate toolbar controls");
                    verifyCaptions();
                    for(const auto& b:s.buttons_)RemoveWindowSubclass(b.hwnd,countButton,1);
                }
            }
        });
        test("overlay keeps its language snapshot and reports owned contextual messages",[]{
            Settings settings;settings.language=Language::Japanese;std::vector<Message> failures;
            OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[&](Message message){failures.push_back(std::move(message));});settings.language=Language::German;setup(s);
            require(s.language_==Language::Japanese && s.buttons_.front().label==std::wstring(text(Language::Japanese,TextId::Select))+L" (V)","Active session keeps initial effective language");
            s.monitorMessage(*s.windows_.front(),WM_DPICHANGED,0,0);
            require(failures.size()==1 && failures.front().id==TextId::DisplayScalingChanged,"DPI failure carries translatable message ID");
            s.report(std::runtime_error("Direct2D operation failed: 0x887A0005"));s.report(std::runtime_error("second failure"));
            require(failures.size()==2 && failures.back().id==TextId::RenderFailed && failures.back().diagnostic.find(L"0x887A0005")!=std::wstring::npos,"Contextual render error owns API diagnostics and only reports once");
            require(failures.back().render(Language::German).find(text(Language::German,TextId::RenderFailed))!=std::wstring::npos,"Failure can render in current application language");
        });
        test("toolbar mutations only follow changed labels enabled tools and swatches",[]{
            Settings settings;Counts counts;OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[](Message){require(false,"Unexpected overlay error");});setup(s);
            for(const auto& b:s.buttons_)require(SetWindowSubclass(b.hwnd,countButton,1,reinterpret_cast<DWORD_PTR>(&counts))!=FALSE,"Observe button messages");
            for(int i=0;i<100;++i){s.refreshButtons();s.refreshStatus();s.repaint();}
            require(!counts.labels && !counts.enabled && !dirtyButtons(s) && !GetUpdateRect(s.toolbar_,nullptr,FALSE),"Canvas repaint never mutates toolbar");clear(s);
            settings.textSize=48;s.refreshButtons();require(counts.labels==1 && !counts.enabled,"Only text-size label changes");clear(s);
            s.refreshButtons();require(counts.labels==1 && !dirtyButtons(s),"Identical settings do not mutate controls");
            settings.color={0,1,0,1};s.refreshButtons();require(dirtyButtons(s)==1,"Only the color swatch invalidates");clear(s);
            s.tool_=Tool::Text;s.refreshButtons();require(dirtyButtons(s)==2,"Only old and new selected tools invalidate");clear(s);
            s.history_.add(shape(Tool::Line,{0,0},{10,10}));s.refreshButtons();require(counts.enabled==1,"Only Undo enablement changes");clear(s);
            s.history_.undo();s.refreshButtons();require(counts.enabled==3,"Undo and Redo update after undo");clear(s);
            s.history_.redo();s.refreshButtons();require(counts.enabled==5,"Undo and Redo update after redo");clear(s);
            s.selection_.right-=10;s.refreshStatus();RECT dirty{},line=s.statusRect();require(GetUpdateRect(s.toolbar_,&dirty,FALSE) && EqualRect(&dirty,&line),"Status line invalidates separately");require(!dirtyButtons(s),"Status does not dirty buttons");clear(s);
            std::vector<std::wstring> beforeBusyLabels;for(const auto& button:s.buttons_)beforeBusyLabels.push_back(button.label);
            s.busy_=true;s.refreshButtons();s.refreshStatus();require(counts.enabled==9,"Busy disables Undo and three export actions");clear(s);
            s.refreshButtons();s.refreshStatus();require(counts.enabled==9 && !dirtyButtons(s) && !GetUpdateRect(s.toolbar_,nullptr,FALSE),"Repeated busy state is a no-op");
            for(size_t index=0;index<s.buttons_.size();++index) {
                const auto& button=s.buttons_[index];require(button.label==beforeBusyLabels[index] && caption(button.hwnd)==button.label,"Disabled controls retain keybind captions without mutation");
            }
        });
        test("toolbar parent clips children and paints only its invalid area",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[](Message){});setup(s);
            require((GetWindowLongPtrW(s.toolbar_,GWL_STYLE)&WS_CLIPCHILDREN)!=0,"Parent excludes child buttons from its paint DC");
            HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=600;info.bmiHeader.biHeight=-160;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits{};
            HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);require(bitmap!=nullptr,"Create offscreen toolbar paint target");auto old=SelectObject(dc,bitmap);
            struct Cleanup{HDC dc;HBITMAP bitmap;HGDIOBJ old;~Cleanup(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}}cleanup{dc,bitmap,old};
            const COLORREF sentinel=RGB(200,50,90);HBRUSH brush=CreateSolidBrush(sentinel);RECT full{0,0,600,160};FillRect(dc,&full,brush);DeleteObject(brush);
            // Emulate the child exclusion supplied by WS_CLIPCHILDREN on BeginPaint's DC.
            RECT child{};GetWindowRect(s.buttons_.front().hwnd,&child);MapWindowPoints(nullptr,s.toolbar_,reinterpret_cast<POINT*>(&child),2);
            ExcludeClipRect(dc,child.left,child.top,child.right,child.bottom);RECT dirty{0,0,100,50};s.paintToolbar(dc,dirty);
            require(GetPixel(dc,2,2)==RGB(27,30,37),"Invalid background painted");require(GetPixel(dc,110,5)==sentinel,"Outside invalid rect untouched");
            SelectClipRgn(dc,nullptr);require(GetPixel(dc,child.left+3,child.top+3)==sentinel,"Child label area remains untouched");
        });
        test("text drag updates only old and new monitors and ignores repeated pointers",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[](Message){});setup(s);s.text_.insert(L"drag text");clear(s);
            ComPtr<IDWriteTextLayout> layout=s.text_.layout();const auto* buffer=s.text_.annotation().text.data();int updates=0;auto callback=s.text_.changed;
            s.text_.changed=[&](const TextUpdate& update){++updates;require(update.flags==TextChange::Geometry && update.previousText.empty(),"Drag reports geometry without copying content");callback(update);};
            s.caretVisible_=false;settings.color={0,1,0,1};settings.textSize=72;drag(s);SetTimer(s.owner(),42,USER_TIMER_MINIMUM,nullptr);s.mouseMove({3900,20});
            require(updates==1 && s.text_.layout()==layout.Get() && s.text_.annotation().text.data()==buffer,"One geometry update with existing text and layout");
            Sleep(50);MSG timer{};require(PeekMessageW(&timer,s.owner(),WM_TIMER,WM_TIMER,PM_REMOVE) && timer.wParam==42,"Geometry leaves the existing caret timer interval intact");KillTimer(s.owner(),42);
            require(!s.caretVisible_ && settings.textSize==72 && settings.color==Pixel{0,1,0,1},"Geometry does not reset caret visibility or overwrite settings");
            require(GetUpdateRect(s.windows_[0]->hwnd,nullptr,FALSE) && !GetUpdateRect(s.windows_[1]->hwnd,nullptr,FALSE) && GetUpdateRect(s.windows_[2]->hwnd,nullptr,FALSE),"Only old/new extent monitors invalidate");
            require(IsWindowVisible(s.toolbar_) && !dirtyButtons(s) && !GetUpdateRect(s.toolbar_,nullptr,FALSE),"Drag keeps toolbar visible and unchanged");clear(s);
            for(int i=0;i<100;++i)s.mouseMove({3900,20});
            require(updates==1 && s.text_.layout()==layout.Get() && !dirtyButtons(s),"Repeated pointers cause no updates rebuilds or button mutations");
            for(const auto& w:s.windows_)require(!GetUpdateRect(w->hwnd,nullptr,FALSE),"Repeated pointer causes no canvas invalidation");
            s.mouseUp({3900,20});require(updates==1 && !GetUpdateRect(s.toolbar_,nullptr,FALSE),"Mouse-up does not duplicate text repaint or toolbar placement");
            s.text_.changed=callback;
            // A handle spills onto the adjacent monitor even if the text box itself does not.
            s.text_.bounds({1622,100,1919,200});clear(s);auto before=s.text_.visualBounds();s.text_.bounds({1621,100,1918,200});
            require(before.right>1920 && GetUpdateRect(s.windows_[1]->hwnd,nullptr,FALSE),"Handle visual extent invalidates adjacent monitor");
            s.text_.begin({1910,100},s.selection_,{1,0,0,1},144);s.text_.bounds({1910,100,1911,101});s.text_.insert(L"W");clear(s);
            require(s.text_.visualBounds().right>1930,"Glyph overhang extends beyond handle margin");
            s.text_.bounds(translated(*s.text_.annotation().textBounds,{-1,0}));require(GetUpdateRect(s.windows_[1]->hwnd,nullptr,FALSE),"Glyph overhang invalidates adjacent monitor");
            clear(s);s.selection_={0,0,1920,1080};s.repaintText({1915,0,1930,20},{1916,0,1931,20});require(GetUpdateRect(s.windows_[1]->hwnd,nullptr,FALSE),"Text decorations outside the crop remain visible and invalidate");
        });
    }
    static void benchmark() {
        for(const auto& value:std::vector<std::wstring>{L"",L"first line\nsecond line with a few wrapped words\nthird line",std::wstring(InlineText::limit,L'x')}) {
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),desktop(),settings,[](SessionAction){},[](Message){});setup(s);s.text_.insert(value);clear(s);
            ComPtr<TextSink> sink;sink.Attach(new TextSink);sink->grant=[](DWORD){};
            s.textStore_.Attach(new TextStore(s.text_,s.owner(),s.selection_));check(s.textStore_->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Benchmark sink");
            Counts counts;for(const auto& b:s.buttons_)SetWindowSubclass(b.hwnd,countButton,1,reinterpret_cast<DWORD_PTR>(&counts));
            ComPtr<IDWriteTextLayout> layout=s.text_.layout();drag(s);constexpr int iterations=20000;
            auto measure=[&](bool repeat){std::vector<double> times;times.reserve(iterations);auto start=std::chrono::steady_clock::now();for(int i=0;i<iterations;++i){auto t=std::chrono::steady_clock::now();s.mouseMove(repeat?Point{499,199}:Point{i%500,i%200});times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count());}auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::sort(times.begin(),times.end());std::cout<<"drag chars="<<value.size()<<" repeated="<<repeat<<" samples="<<iterations<<" total_ms="<<elapsed<<" median_us="<<times[iterations/2]<<" p95_us="<<times[iterations*95/100]<<" p99_us="<<times[iterations*99/100]<<'\n';};
            measure(false);const int layoutNotifications=sink->layoutChanges;measure(true);
            require(s.text_.layout()==layout.Get() && !counts.labels && !counts.enabled && !dirtyButtons(s),"Drag benchmark does not rebuild layout or mutate toolbar");
            require(!sink->selectionChanges && !sink->textChanges && sink->layoutChanges==layoutNotifications,"Repeated drag emits no TSF changes");
            std::cout<<"layout_rebuilds=0 toolbar_mutations=0 text_notifications="<<sink->textChanges<<" selection_notifications="<<sink->selectionChanges<<" geometry_notifications="<<layoutNotifications<<'\n';
            s.textStore_->UnadviseSink(sink.Get());s.textStore_.Reset();s.textDragging_=false;s.text_.endGesture();
        }
    }
};
}
