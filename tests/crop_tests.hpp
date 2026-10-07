namespace shot {
struct GraphicsTestAccess {
    static Image preview(Graphics& graphics,ID2D1Bitmap1* background,Rect monitor,Rect crop,std::span<const Annotation> annotations,const Annotation* draft=nullptr,const InlineText* text=nullptr) {
        ComPtr<ID2D1Bitmap1> output;
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT,D2D1_ALPHA_MODE_IGNORE),96,96);
        check(graphics.context_->CreateBitmap(D2D1::SizeU(monitor.width(),monitor.height()),nullptr,0,props,&output),"Create FP16 preview pixel target");
        graphics.drawPreview(output.Get(),background,monitor,crop,annotations,draft,1,true,text,true);
        return graphics.readback(output.Get()); // Inspect before Present rotates the swap-chain buffers.
    }
};
struct CropTestAccess {
    static constexpr Rect crop{500,400,900,800};
    static LRESULT CALLBACK keepOffscreen(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(msg==WM_WINDOWPOSCHANGING){auto pos=reinterpret_cast<WINDOWPOS*>(lp);pos->x=pos->y=-30000;}
        return DefSubclassProc(hwnd,msg,wp,lp);
    }
    static void prepare(OverlaySession& s) {
        OverlayTestAccess::setup(s);s.closeTextEditor();s.selection_=crop;s.firstRegionCompleted_=true;s.tool_=Tool::Pen;
        require(SetWindowSubclass(s.toolbar_,keepOffscreen,2,0)!=FALSE,"Keep test toolbar offscreen during placement");
        s.refreshButtons();s.refreshStatus();OverlayTestAccess::clear(s);
    }
    static void cancel(OverlaySession& s) {
        s.monitorMessage(*s.windows_.front(),WM_CAPTURECHANGED,0,0);ReleaseCapture();
    }
    static void tests() {
        test("crop resize handles and cursors work with every tool without region movement",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),OverlayTestAccess::desktop(),settings,[](SessionAction){},[](std::wstring){require(false,"Unexpected overlay error");});prepare(s);
            struct Case {Point point;Handle handle;Rect resized;LPCWSTR cursor;};
            const Case cases[]{
                {{500,400},Handle::NW,{540,430,900,800},IDC_SIZENWSE},{{700,400},Handle::N,{500,430,900,800},IDC_SIZENS},
                {{900,400},Handle::NE,{500,430,940,800},IDC_SIZENESW},{{900,600},Handle::E,{500,400,940,800},IDC_SIZEWE},
                {{900,800},Handle::SE,{500,400,940,830},IDC_SIZENWSE},{{700,800},Handle::S,{500,400,900,830},IDC_SIZENS},
                {{500,800},Handle::SW,{540,400,900,830},IDC_SIZENESW},{{500,600},Handle::W,{540,400,900,800},IDC_SIZEWE}
            };
            for(int tool=0;tool<9;++tool)for(const auto& item:cases) {
                s.selection_=crop;s.tool_=static_cast<Tool>(tool);
                require(s.cropHandle(item.point)==item.handle && s.cursorAt(item.point)==item.cursor,"Resize cursor matches handle with every tool");
                s.mouseDown(s.owner(),item.point);require(s.dragging_ && s.selecting_ && s.handle_==item.handle && !s.draft_ && !s.text_.active(),"Crop handle starts resize before drawing");
                const Point end{item.point.x+40,item.point.y+30};s.mouseMove(end);require(s.selection_==item.resized,"Correct crop edges resize");
                OverlayTestAccess::clear(s);s.mouseMove(end);for(const auto& w:s.windows_)require(!GetUpdateRect(w->hwnd,nullptr,FALSE),"Repeated crop pointer does not repaint");
                s.mouseUp(end);require(s.tool_==static_cast<Tool>(tool) && s.history_.visible().empty(),"Resize preserves tool and creates no annotation");
            }
            s.selection_=crop;s.tool_=Tool::Select;
            require(s.cropHandle({600,600})==Handle::None && s.cursorAt({600,600})==IDC_ARROW,"Crop interior has no move target/cursor");
            s.mouseDown(s.owner(),{600,600});s.mouseMove({650,650});s.mouseUp({650,650});require(s.selection_==crop && !s.dragging_ && !s.textDragging_,"Select interior drag does nothing");
            require(s.cursorAt({200,200})==IDC_CROSS,"Select outside invites replacement drag");
            s.busy_=true;require(s.cursorAt({500,400})==IDC_WAIT,"Export busy cursor wins over crop handles");
        });
        test("every tool replaces crop from outside and restores empty or interrupted gestures",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),OverlayTestAccess::desktop(),settings,[](SessionAction){},[](std::wstring){});prepare(s);
            s.history_.add(shape(Tool::Line,{600,500},{1000,900}));const auto points=s.history_.visible()[0].points;
            const std::pair<Point,Point> drags[]{{{200,200},{1000,900}},{{1200,200},{300,900}},{{200,1000},{1000,200}},{{1200,1000},{300,200}}};
            for(int tool=0;tool<9;++tool) {
                s.tool_=static_cast<Tool>(tool);
                for(const auto& [start,end]:drags) {
                    s.selection_=crop;s.mouseDown(s.owner(),start);
                    require(s.dragging_ && s.selecting_ && s.handle_==Handle::None && !s.draft_ && !s.text_.active(),"Blank outside canvas starts replacement with every tool");
                    require(!IsWindowVisible(s.toolbar_),"Replacement hides toolbar");
                    s.mouseMove(end);require(s.selection_==normalized(start,end),"Replacement preview normalizes each direction");
                    s.mouseUp(end);require(s.selection_==normalized(start,end) && s.tool_==static_cast<Tool>(tool) && !s.dragging_,"Replacement retains tool and completes gesture");
                    require(IsWindowVisible(s.toolbar_) && !s.draft_ && !s.text_.active() && s.history_.visible().size()==1 && s.history_.visible()[0].points==points,"Replacement restores toolbar without adding or transforming annotations");
                }
                s.selection_=crop;s.mouseDown(s.owner(),{200,200});s.mouseUp({6000,1200});
                require(s.selection_==Rect{200,200,5760,1080} && s.tool_==static_cast<Tool>(tool),"Outside replacement clamps endpoint to captured desktop bounds");
                for(const Point end:{Point{200,200},Point{300,200},Point{200,300}}) {
                    s.selection_=crop;s.mouseDown(s.owner(),{200,200});s.mouseUp(end);
                    require(s.selection_==crop && s.tool_==static_cast<Tool>(tool) && IsWindowVisible(s.toolbar_),"Click and zero-area replacements restore crop toolbar and tool");
                    require(!s.draft_ && !s.text_.active() && s.history_.visible().size()==1,"Empty replacement creates no annotation or editor");
                }
                s.selection_=crop;s.mouseDown(s.owner(),{200,200});s.mouseMove({1000,900});cancel(s);
                require(s.selection_==crop && !s.dragging_ && s.tool_==static_cast<Tool>(tool) && IsWindowVisible(s.toolbar_),"Capture loss restores replacement crop toolbar and tool");
                s.mouseDown(s.owner(),{900,600});s.mouseMove({1100,600});cancel(s);
                require(s.selection_==crop && !s.dragging_ && s.tool_==static_cast<Tool>(tool) && IsWindowVisible(s.toolbar_),"Capture loss restores interrupted resize");
                require(!s.draft_ && !s.text_.active() && s.history_.visible().size()==1 && s.history_.visible()[0].points==points,"Interrupted gestures preserve annotation history");
            }
            s.mouseMove({200,200});require(s.selection_==crop && !s.dragging_,"Pointer movement without a drag leaves completed crop intact");
            s.history_.clear();s.tool_=Tool::Select;
            s.selection_={};s.firstRegionCompleted_=false;s.mouseDown(s.owner(),{200,200});s.mouseUp({200,200});require(!s.firstRegionCompleted_ && s.selection_.empty(),"Empty initial region keeps transition pending");
            s.mouseDown(s.owner(),{200,200});s.mouseUp({800,700});require(s.firstRegionCompleted_ && s.tool_==Tool::Pen,"First valid region still selects Pen");
            s.mouseDown(s.owner(),{1200,200});require(s.selecting_ && !s.draft_ && !IsWindowVisible(s.toolbar_),"Outside drag immediately after first crop replaces it with automatic Pen");
            s.mouseUp({1600,600});require(s.selection_==Rect{1200,200,1600,600} && s.tool_==Tool::Pen && s.history_.visible().empty() && IsWindowVisible(s.toolbar_),"First replacement preserves automatic Pen and creates no ink");
        });
        test("drawing tools started inside can cross crop and survive crop changes",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),OverlayTestAccess::desktop(),settings,[](SessionAction){},[](std::wstring){});prepare(s);
            for(const Tool tool:{Tool::Pen,Tool::Highlighter,Tool::Rectangle,Tool::Ellipse,Tool::Line,Tool::Arrow,Tool::Censor}) {
                s.selection_=crop;s.tool_=tool;s.history_.clear();s.mouseDown(s.owner(),{600,500});
                require(s.draft_ && s.draft_->points.front()==Point{600,500} && !s.selecting_ && IsWindowVisible(s.toolbar_),"Inside drawing starts while toolbar remains visible");
                s.mouseMove({200,200});require(s.selection_==crop && IsWindowVisible(s.toolbar_),"Drawing across crop keeps original crop and toolbar");
                OverlayTestAccess::clear(s);s.mouseMove({200,200});for(const auto& w:s.windows_)require(!GetUpdateRect(w->hwnd,nullptr,FALSE),"Repeated drawing pointer does not repaint");
                s.mouseUp({200,200});require(s.history_.visible().size()==1 && s.history_.visible()[0].tool==tool && s.selection_==crop,"Crossing gesture commits one annotation without replacing crop");
                const auto points=s.history_.visible()[0].points;require(points.front()==Point{600,500} && points.back()==Point{200,200},"Crossing crop preserves full desktop coordinates");
                s.mouseDown(s.owner(),{900,600});s.mouseUp({800,600});require(s.selection_==Rect{500,400,800,800} && s.tool_==tool && s.history_.visible()[0].points==points,"Shrink does not transform or discard drawings");
                s.mouseDown(s.owner(),{1000,200});s.mouseUp({1400,600});require(s.history_.visible().size()==1 && s.history_.visible()[0].points==points && s.tool_==tool,"Replacement with drawing tool preserves outside annotation");
                require(s.history_.undo() && s.history_.visible().empty() && s.history_.redo() && s.history_.visible()[0].points==points,"Annotation undo/redo survives crop replacement");
            }
        });
        test("outside text editing uses desktop bounds and crop handles take priority",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),OverlayTestAccess::desktop(),settings,[](SessionAction){},[](std::wstring){require(false,"Unexpected text error");});prepare(s);s.tool_=Tool::Text;
            s.mouseDown(s.owner(),{600,500});require(s.text_.active() && s.text_.annotation().textBounds->width()==300 && IsWindowVisible(s.toolbar_),"Text starts inside crop with desktop-based default width");s.mouseUp({600,500});
            const auto initial=*s.text_.annotation().textBounds;const Point initialBorder{initial.left+30,initial.top};
            s.mouseDown(s.owner(),initialBorder);s.mouseUp({initialBorder.x+900,initialBorder.y-300});
            require(s.text_.annotation().textBounds==translated(initial,{900,-300}) && s.selection_==crop && IsWindowVisible(s.toolbar_),"Text movement can carry an inside box outside crop");
            s.text_.insert(L"one two");const auto position=s.text_.caretRect(5);const Point wordPoint{position.left+1,(position.top+position.bottom)/2};
            require(s.cursorAt(wordPoint)==IDC_IBEAM,"Outside text has editing cursor");s.mouseDoubleClick(s.owner(),wordPoint);require(s.text_.start()==4 && s.text_.end()==7,"Outside double-click selects word");
            s.mouseDown(s.owner(),wordPoint);require(s.textSelecting_ && s.textDragging_ && !s.dragging_,"Outside text selection routes to inline editor");s.mouseUp(wordPoint);
            RECT screen{};check(s.textStore_->GetScreenExt(1,&screen),"Outside TSF screen extent");require(screen.left==1500 && screen.right>crop.right,"Production TSF store clips to desktop, not crop");
            ComPtr<TextStore> store;store.Attach(new TextStore(s.text_,s.owner(),s.desktop_->bounds()));ComPtr<TextSink> sink;sink.Attach(new TextSink);
            check(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),0),"Outside caret test sink");sink->grant=[&](DWORD){RECT extent{};BOOL clipped{};auto caret=s.text_.caretRect(s.text_.caret());check(store->GetTextExt(1,s.text_.caret(),s.text_.caret(),&extent,&clipped),"Outside caret extent");require(!clipped && extent.left==caret.left && extent.top==caret.top,"Outside IME caret extent is visible");LONG acp{};POINT point{wordPoint.x,wordPoint.y};check(store->GetACPFromPoint(1,&point,0,&acp),"Outside text point");};
            HRESULT session{};check(store->RequestLock(TS_LF_READ,&session),"Read outside caret");check(session,"Outside caret session");if(sink->failure)std::rethrow_exception(sink->failure);store->UnadviseSink(sink.Get());
            auto original=*s.text_.annotation().textBounds;ComPtr<IDWriteTextLayout> layout=s.text_.layout();const Point border{original.left+30,original.top};
            require(s.cursorAt(border)==IDC_SIZEALL,"Text-box border still moves");s.mouseDown(s.owner(),border);s.mouseUp({border.x+80,border.y+20});require(s.text_.annotation().textBounds==translated(original,{80,20}) && s.text_.layout()==layout.Get() && s.selection_==crop,"Text movement reuses layout without moving crop");
            original=*s.text_.annotation().textBounds;const Point resize{original.right,(original.top+original.bottom)/2};
            require(s.cursorAt(resize)==IDC_SIZEWE,"Outside active text has resize cursor");s.mouseDown(s.owner(),resize);
            require(s.textDragging_ && s.textHandle_==Handle::E && !s.dragging_,"Outside active text handle routes to resize instead of replacement");
            s.mouseUp({resize.x+80,resize.y});require(s.text_.annotation().textBounds==InlineText::resized(original,Handle::E,{80,0}) && s.selection_==crop,"Outside text resize leaves crop intact");
            s.commitText();s.mouseDown(s.owner(),{600,500});s.mouseUp({1300,950});require(s.text_.annotation().textBounds==Rect{600,500,1300,950} && s.selection_==crop,"Initial text drag started inside can cross crop bounds");s.text_.insert(L"outside text");
            s.text_.bounds(crop);s.text_.beginComposition();s.text_.insert(L" composition");
            require(s.cursorAt({500,400})==IDC_SIZENWSE,"Crop handle cursor takes priority over overlapping text handle");s.mouseDown(s.owner(),{500,400});require(!s.text_.active() && !s.textStore_ && s.dragging_ && s.selecting_ && s.handle_==Handle::NW && s.history_.visible().size()==2,"Crop resize commits composing text exactly once");s.mouseUp({540,430});require(s.selection_==Rect{540,430,900,800} && s.tool_==Tool::Text,"Crop resize preserves Text tool");
            s.mouseDown(s.owner(),{5500,100});s.mouseUp({5760,600});require(s.selection_==Rect{5500,100,5760,600} && !s.text_.active(),"Blank outside Text gesture replaces crop before another box is created");
            s.mouseDown(s.owner(),{5750,200});require(s.text_.active() && s.text_.annotation().textBounds->width()==10,"Inside text default width stops at desktop edge");s.mouseUp({5750,200});s.commitText(true);
        });
        test("blank outside replacement commits composing text exactly once",[]{
            Settings settings;OverlaySession s(GetModuleHandleW(nullptr),OverlayTestAccess::desktop(),settings,[](SessionAction){},[](std::wstring){require(false,"Unexpected text error");});prepare(s);s.tool_=Tool::Text;
            s.mouseDown(s.owner(),{600,500});s.mouseUp({600,500});s.text_.insert(L"composing text");s.text_.beginComposition();s.text_.insert(L" composition");
            s.mouseDown(s.owner(),{1200,200});require(!s.text_.active() && !s.textStore_ && s.dragging_ && s.selecting_ && !s.draft_ && !IsWindowVisible(s.toolbar_),"Blank outside replacement closes composing editor and hides toolbar");
            require(s.history_.visible().size()==1 && s.history_.visible()[0].text==L"composing text composition","Replacement commits composing text once");
            s.mouseUp({1600,600});require(s.selection_==Rect{1200,200,1600,600} && s.tool_==Tool::Text && s.history_.visible().size()==1 && !s.text_.active() && IsWindowVisible(s.toolbar_),"Replacement creates no next text box or duplicate annotation");
        });
        test("desktop preview shows outside drawings with shade while exports remain cropped",[]{
            Graphics graphics({},true);DesktopImage desktop;auto m=monitor({0,0,256,160});
            for(int y=0;y<160;++y)for(int x=0;x<256;++x)m.image.at(x,y)={0.1f+x/512.f,0.2f+y/512.f,0.3f,1};desktop.monitors.push_back(m);auto background=graphics.upload(m.image);const Rect selected{100,40,220,136};
            std::vector<Annotation> cases;for(Tool tool:{Tool::Pen,Tool::Highlighter,Tool::Rectangle,Tool::Ellipse,Tool::Line,Tool::Arrow})cases.push_back(shape(tool,{20,20},{180,110}));
            Annotation text;text.tool=Tool::Text;text.points={{20,20}};text.textBounds=Rect{20,20,180,100};text.text=L"outside and inside text";text.textSize=24;cases.push_back(text);
            cases.push_back(shape(Tool::Censor,{16,16},{220,136}));auto pixelated=cases.back();pixelated.pixelated=true;cases.push_back(pixelated);
            for(const auto& annotation:cases) {
                auto full=graphics.render(desktop,m.bounds,{&annotation,1},RenderDestination::Preview);auto output=graphics.render(desktop,selected,{&annotation,1},RenderDestination::Sdr);
                auto frame=GraphicsTestAccess::preview(graphics,background.Get(),m.bounds,selected,{&annotation,1});int outsideInk=0;
                for(int y=8;y<152;++y)for(int x=8;x<90;++x){if(full.at(x,y)!=m.image.at(x,y))++outsideInk;auto p=frame.at(x,y),expected=full.at(x,y);expectNear(p.r,expected.r*0.75f);expectNear(p.g,expected.g*0.75f);expectNear(p.b,expected.b*0.75f);}
                require(outsideInk>10,"Every tool draws real preview pixels outside crop");require(output.width==selected.width() && output.height==selected.height(),"Export keeps crop dimensions");
                for(int y=10;y<output.height-10;++y)for(int x=10;x<output.width-10;++x){auto p=frame.at(x+selected.left,y+selected.top),expected=output.at(x,y);expectNear(p.r,expected.r);expectNear(p.g,expected.g);expectNear(p.b,expected.b);}
                auto outside=annotation;outside.points={{16,16},{84,32}};if(outside.tool==Tool::Text)outside.textBounds=Rect{16,8,84,32};
                // Cover and pixelation entirely outside the crop must not affect exported pixels.
                if(outside.tool==Tool::Censor){auto clipped=graphics.render(desktop,selected,{&outside,1},RenderDestination::Sdr);require(clipped.pixels==composite(desktop,selected,false).pixels,"Outside censor does not enter export");}
            }
            InlineText edit;edit.begin({20,20},m.bounds,{1,0,0,1},24);edit.insert(L"edit");edit.select(0,2);edit.beginComposition();
            auto frame=GraphicsTestAccess::preview(graphics,background.Get(),m.bounds,selected,{},&edit.annotation(),&edit);expectNear(frame.at(21,21).r,1);expectNear(frame.at(21,21).g,1);require(frame.at(21,21).r>m.image.at(21,21).r,"Text editing handles appear outside crop");
            edit.bounds(selected);frame=GraphicsTestAccess::preview(graphics,background.Get(),m.bounds,selected,{},&edit.annotation(),&edit);expectNear(frame.at(100,40).r,0.13f);expectNear(frame.at(100,40).g,0.6f);expectNear(frame.at(100,40).b,1); // Crop handle is above text decorations.
        });
    }
};
}
