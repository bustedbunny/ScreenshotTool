// Included inside the test executable's anonymous namespace.
void textTests() {
    test("layout reuse and precise no-op text notifications",[]{
        InlineText text;text.begin({-50,-40},{-100,-100,1000,1000},{1,0,0,1},24);text.insert(L"first line\nsecond line with wrapping words");
        ComPtr<IDWriteTextLayout> layout=text.layout();int updates=0;TextChange flags{};Rect previous{};
        text.changed=[&](const TextUpdate& update){++updates;flags=update.flags;previous=update.previousVisual;require(update.previousText.empty() || has(flags,TextChange::Content),"Only content changes carry old text");};
        const auto visual=text.visualBounds();auto b=*text.annotation().textBounds;text.beginGesture();text.bounds(translated(b,{17,23}));text.endGesture();
        require(text.layout()==layout.Get() && updates==1 && flags==TextChange::Geometry && previous==visual,"Translation reuses layout and only reports geometry");
        require(text.visualBounds()==translated(visual,{17,23}),"Visual overhang and handles translate together");
        require(text.undo() && text.layout()==layout.Get(),"Geometry undo reuses layout");require(text.redo() && text.layout()==layout.Get(),"Geometry redo reuses layout");
        b=*text.annotation().textBounds;b.bottom+=100;text.bounds(b);require(text.layout()==layout.Get(),"Height-only resize reuses layout");
        text.format({0,1,0,1},24);require(text.layout()==layout.Get() && flags==TextChange::Formatting,"Color reuses layout without geometry/selection notifications");
        text.select(1,3);require(text.layout()==layout.Get() && flags==TextChange::Selection,"Selection reuses layout");
        const int before=updates;for(int i=0;i<100;++i){text.bounds(b);text.select(1,3);text.format({0,1,0,1},24);}require(updates==before && text.layout()==layout.Get(),"Identical updates emit nothing");
        b.bottom=b.top+1;text.bounds(b);const int fitted=updates;text.bounds(b);require(updates==fitted && text.layout()==layout.Get(),"Repeated height clamped to content is a no-op");
        b=*text.annotation().textBounds;b.right=b.left+45;text.bounds(b);require(text.layout()!=layout.Get(),"Width rebuilds layout");
        DWRITE_TEXT_METRICS narrow{},wide{};text.layout()->GetMetrics(&narrow);layout->GetMetrics(&wide);require(narrow.lineCount>wide.lineCount,"Width reflows lines");
        layout=text.layout();text.format({0,1,0,1},48);require(text.layout()!=layout.Get() && has(flags,TextChange::Formatting|TextChange::Geometry),"Font size reflows and reports geometry");
        layout=text.layout();text.insert(L"new");require(text.layout()!=layout.Get() && has(flags,TextChange::Content),"Text changes reflow");
        layout=text.layout();text.beginComposition();text.compositionRange(0,1);text.endComposition();require(text.layout()==layout.Get() && flags==TextChange::Composition,"Composition decorations reuse layout");
    });
    test("inline text selection replacement and local undo branching",[]{
        InlineText text;text.begin({-20,-30},{-100,-100,500,500},{1,0,0,1},24);
        require(text.active() && text.annotation().textBounds->width()==300,"Click default is 300 physical pixels");
        text.insert(L"hello world");text.select(11,6);text.insert(L"Paint");
        require(text.annotation().text==L"hello Paint" && text.caret()==11,"Replace reverse selection");
        require(text.undo() && text.annotation().text==L"hello world" && text.anchor()==11 && text.caret()==6,"Undo restores selection direction");
        require(text.redo() && text.annotation().text==L"hello Paint","Redo replacement");
        text.undo();text.insert(L"box");require(!text.canRedo(),"Edit branches local history");
        text.select(0,static_cast<UINT32>(text.annotation().text.size()));text.erase(false,false);require(text.annotation().text.empty(),"Delete all selection");
        require(!text.finish(),"Empty text creates no annotation");
        text.begin({490,0},{0,0,500,500},{1,1,1,1},24);require(text.annotation().textBounds->width()==10,"Default clipped to available crop width");
    });
    test("Unicode caret and deletion respect surrogate and combining clusters",[]{
        InlineText text;text.begin({0,0},{0,0,1000,1000},{1,1,1,1},24);
        text.insert(L"A\U0001F600e\u0301Z");
        require(text.previous(3)==1 && text.next(1)==3,"Surrogate pair is one caret step");
        require(text.previous(5)==3 && text.next(3)==5,"Combining sequence is one caret step");
        text.select(5,5);text.erase(true,false);require(text.annotation().text==L"A\U0001F600Z","Backspace removes whole combining cluster");
        text.erase(true,false);require(text.annotation().text==L"AZ","Backspace removes whole supplementary character");
        text.select(1,1);text.character(0xd83d);require(text.annotation().text==L"AZ","No half surrogate rendered");text.character(0xde00);
        require(text.annotation().text==L"A\U0001F600Z","WM_CHAR surrogate pair inserts atomically");
        text.select(0,static_cast<UINT32>(text.annotation().text.size()));text.insert(std::wstring(InlineText::limit-1,L'x'));
        text.insert(L"\U0001F600");require(text.annotation().text.size()==InlineText::limit-1,"Limit cannot split surrogate pair");text.insert(L"ab");require(text.annotation().text.size()==InlineText::limit,"Character limit enforced");
    });
    test("word selection, keyboard navigation, shift and line affinity",[]{
        InlineText text;text.begin({-70,-80},{-100,-100,500,500},{1,1,1,1},24);text.insert(L"one two three\nlast");
        text.selectWord(5);require(text.start()==4 && text.end()==7,"Double click selects word");
        text.navigate(VK_RIGHT,false,true);text.navigate(VK_LEFT,true,true);require(text.start()==4 && text.end()==7,"Ctrl Shift selects preceding word");
        text.navigate(VK_HOME,false,true);require(text.caret()==0,"Ctrl Home");text.navigate(VK_END,true,true);require(text.start()==0 && text.end()==18,"Ctrl Shift End");
        text.select(0,0);text.navigate(VK_DOWN,false,false);require(text.caret()==14,"Down preserves column");text.navigate(VK_HOME,false,false);require(text.caret()==14,"Home current line");
        text.bounds({-70,-80,5,-79});text.select(0,0);text.navigate(VK_END,false,false);auto r=text.caretRect(text.caret());
        require(r.top==-80,"End caret stays on wrapped line's trailing edge");text.navigate(VK_DOWN,false,false);require(text.caretRect(text.caret()).top>r.top,"Down goes to next visual line");
    });
    test("formatting affects whole box and preserves caret and selection",[]{
        InlineText text;text.begin({0,0},{0,0,500,500},{1,0,0,1},24);text.insert(L"first second third");text.select(11,6);
        text.format({0,1,0,1},48);require(text.annotation().color==Pixel{0,1,0,1} && text.annotation().textSize==48,"Whole box format");
        require(text.anchor()==11 && text.caret()==6,"Selection and caret unchanged");
        require(text.undo() && text.annotation().textSize==24 && text.annotation().color==Pixel{1,0,0,1},"Format undo");
        text.redo();text.format({0,1,0,1},48);text.undo();require(text.annotation().textSize==24,"Canceled/no-op format adds no undo item");
    });
    test("text handles move resize reflow and preserve all content",[]{
        InlineText text;text.begin({-250,-80},{-500,-500,500,500},{1,1,1,1},24);text.insert(L"long text wraps without losing any characters\nsecond line");
        auto original=*text.annotation().textBounds;auto content=text.annotation().text;
        const int mx=(original.left+original.right)/2,my=(original.top+original.bottom)/2;
        std::vector<std::pair<Point,Handle>> handles{{{original.left,original.top},Handle::NW},{{mx,original.top},Handle::N},{{original.right,original.top},Handle::NE},{{original.right,my},Handle::E},{{original.right,original.bottom},Handle::SE},{{mx,original.bottom},Handle::S},{{original.left,original.bottom},Handle::SW},{{original.left,my},Handle::W}};
        for(auto [p,h]:handles){require(text.hitBorder(p)==h,"All eight handles hit correctly");auto r=InlineText::resized(original,h,{17,13});require(!r.empty(),"Each handle keeps valid bounds");}
        require(text.hitBorder({original.left+20,original.top})==Handle::Move,"Border moves box");
        require(text.hitBorder({original.left+20,original.top+20})==Handle::None,"Interior belongs to text selection");
        text.beginGesture();text.bounds(InlineText::resized(original,Handle::E,{-230,0}));text.bounds(InlineText::resized(original,Handle::E,{-250,0}));text.endGesture();
        require(text.annotation().textBounds->height()>original.height(),"Narrowing grows height to fit");require(text.annotation().text==content && text.annotation().textSize==24,"No text loss or font scaling");
        require(text.undo() && text.annotation().textBounds==original,"Resize gesture is one undo entry");
        auto moved=InlineText::resized(original,Handle::Move,{-100,-200});text.bounds(moved);require(text.annotation().points.front()==Point{moved.left,moved.top},"Negative desktop origin is preserved");
    });
    test("composition cancel, composition undo, commit discard and repeated boxes",[]{
        InlineText text;History history;text.begin({0,0},{0,0,500,500},{1,1,1,1},24);text.insert(L"base");text.select(0,4);
        text.beginComposition();text.insert(L"ni");text.select(0,2);text.insert(L"\u65e5");require(!text.undo(),"Cannot undo during composition");
        text.endComposition(true);require(text.annotation().text==L"base" && text.start()==0 && text.end()==4 && text.active(),"First Escape cancels composition and restores selection");
        text.beginComposition();text.insert(L"n");text.select(0,1);text.insert(L"\u65e5");text.endComposition();
        require(text.undo() && text.annotation().text==L"base","Composition is one local undo");text.redo();
        auto annotation=text.finish();require(annotation.has_value() && !text.active(),"Commit returns one annotation");history.add(*annotation);
        require(!text.finish(),"Commit is idempotent");history.undo();require(history.visible().empty(),"Annotation undo removes complete box");history.redo();require(history.visible()[0].text==L"\u65e5","Annotation redo restores complete box");
        text.begin({10,10},{0,0,500,500},{1,0,0,1},32);text.insert(L"discard");require(!text.finish(true),"Escape discards nonempty box");
        text.begin({20,20},{0,0,500,500},{1,0,0,1},32);require(!text.canUndo() && !text.canRedo(),"Repeated annotations start fresh local history");
    });
    test("text visual extent contains overflowing glyph pixels",[]{
        Graphics graphics({},true);DesktopImage desktop;auto m=monitor({0,0,400,400});std::fill(m.image.pixels.begin(),m.image.pixels.end(),Pixel{0.1f,0.2f,0.3f,1});desktop.monitors.push_back(m);
        InlineText text;text.begin({100,100},m.bounds,{1,0,0,1},144);text.bounds({100,100,101,101});text.insert(L"Ẃ");
        const auto visual=text.visualBounds();require(visual.right>text.annotation().textBounds->right+5,"Visual extent includes glyphs overflowing narrow layout");
        auto output=graphics.render(desktop,m.bounds,{&text.annotation(),1},RenderDestination::Sdr);int ink=0;
        for(int y=0;y<400;++y)for(int x=0;x<400;++x)if(output.at(x,y)!=m.image.at(x,y)){++ink;require(visual.contains({x,y}),"All rendered overhang pixels fit the invalidation extent");}
        require(ink>100,"Overflow test draws real glyph pixels");
    });
    test("bounded text draft commit preview export pixels and crop clipping",[]{
        Graphics graphics({},true);DesktopImage desktop;auto m=monitor({-100,-80,220,160});
        std::fill(m.image.pixels.begin(),m.image.pixels.end(),Pixel{0.1f,0.2f,0.3f,1});desktop.monitors.push_back(m);
        InlineText text;text.begin({-70,-65},m.bounds,{0.8f,0.3f,0.1f,1},24);text.bounds({-70,-65,20,-64});text.insert(L"Wrapped text\n日本語\nEnd");
        auto preview=[&](Rect bounds,Rect clip,float white,IDWriteTextLayout* layout,const Annotation& annotation) {
            auto bg=graphics.upload(composite(desktop,bounds,false)),target=graphics.target(bounds.width(),bounds.height());
            auto dc=graphics.context();dc->SetTarget(target.Get());dc->SetTransform(D2D1::Matrix3x2F::Identity());dc->BeginDraw();dc->DrawBitmap(bg.Get());check(dc->EndDraw(),"Initialize text pixel comparison");
            graphics.document(target.Get(),bounds,clip,{&annotation,1},white,layout);return graphics.readback(target.Get());
        };
        auto draft=preview(m.bounds,m.bounds,1,text.layout(),text.annotation());
        for(const Rect part:{Rect{-100,-80,0,160},Rect{0,-80,220,160}}) {
            auto split=preview(part,m.bounds,1,text.layout(),text.annotation());
            for(int y=0;y<part.height();++y)for(int x=0;x<part.width();++x)require(split.at(x,y)==draft.at(x+part.left-m.bounds.left,y+part.top-m.bounds.top),"Shared text layout crosses monitor boundary without reflow");
        }
        auto committed=*text.finish();
        auto after=preview(m.bounds,m.bounds,1,nullptr,committed);require(draft.pixels==after.pixels,"Commit cannot move or recolor text");
        auto output=graphics.render(desktop,m.bounds,{&committed,1},RenderDestination::Sdr);require(output.pixels==draft.pixels,"Preview and export use identical layout pixels");
        require(draft.pixels!=m.image.pixels,"Test actually renders visible text");
        Rect crop{-40,-40,50,90};auto clipped=preview(m.bounds,crop,1,nullptr,committed);auto exported=graphics.render(desktop,crop,{&committed,1},RenderDestination::Sdr);
        for(int y=0;y<m.bounds.height();++y)for(int x=0;x<m.bounds.width();++x){Point p{x+m.bounds.left,y+m.bounds.top};if(!crop.contains(p))require(clipped.at(x,y)==m.image.at(x,y),"Crop clips text pixels");else require(clipped.at(x,y)==exported.at(p.x-crop.left,p.y-crop.top),"Cropped export matches preview");}
        const float white=203.f/80;auto bright=preview(m.bounds,m.bounds,white,nullptr,committed);require(bright.pixels!=draft.pixels,"HDR annotation white scaling applied");
        Annotation legacy=committed;legacy.textBounds.reset();auto layout=textLayout(legacy);DWRITE_TEXT_METRICS a{},b{};layout->GetMetrics(&a);textLayout(committed)->GetMetrics(&b);require(a.lineCount<b.lineCount,"Legacy unbounded text stays unwrapped");
    });
}
class TextSink final:public ITextStoreACPSink {
public:
    std::function<void(DWORD)> grant;
    int textChanges{},selectionChanges{},layoutChanges{};std::exception_ptr failure;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_INVALIDARG;*p=nullptr;if(id!=IID_IUnknown && id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*p=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*) override{++textChanges;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange() override{++selectionChanges;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie) override{++layoutChanges;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags) override{try{grant(flags);return S_OK;}catch(...){failure=std::current_exception();return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction() override{return S_OK;}
private:ULONG references{1};
};
void textStoreTests() {
    test("TSF geometry updates caret extent without selection changes",[]{
        InlineText text;Rect crop{-1000,-1000,2000,2000};text.begin({-50,-40},crop,{1,1,1,1},24);text.insert(L"caret position");
        ComPtr<TextStore> store;store.Attach(new TextStore(text,nullptr,crop));ComPtr<TextSink> sink;sink.Attach(new TextSink);
        check(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Advise geometry sink");
        text.changed=[&](const TextUpdate& update){store->notify(update);};
        auto before=text.caretRect(text.caret());text.beginGesture();auto bounds=translated(*text.annotation().textBounds,{200,100});text.bounds(bounds);
        require(sink->layoutChanges==1 && !sink->textChanges && !sink->selectionChanges,"Move reports layout only");
        sink->grant=[&](DWORD){RECT r{};BOOL clipped{};check(store->GetTextExt(1,text.caret(),text.caret(),&r,&clipped),"Moved caret extent");require(r.left==before.left+200 && r.top==before.top+100 && !clipped,"IME sees translated caret");};
        HRESULT session{};check(store->RequestLock(TS_LF_READ,&session),"Read moved caret");check(session,"Caret extent session");if(sink->failure)std::rethrow_exception(sink->failure);
        text.bounds(bounds);require(sink->layoutChanges==1,"Repeated pointer emits no layout notification");
        bounds.bottom+=100;text.bounds(bounds);text.format({1,0,0,1},24);require(sink->layoutChanges==2 && !sink->selectionChanges && !sink->textChanges,"Height reports layout; color reports no TSF changes");
        sink->grant=[&](DWORD){bounds=translated(bounds,{1,0});text.bounds(bounds);require(sink->layoutChanges==2,"Geometry layout notification deferred under lock");};
        check(store->RequestLock(TS_LF_READ,&session),"Geometry under lock");check(session,"Deferred geometry session");if(sink->failure)std::rethrow_exception(sink->failure);
        require(sink->layoutChanges==3 && !sink->selectionChanges,"Deferred geometry reports no selection");
        text.endGesture();text.select(0,2);require(sink->selectionChanges==1,"Real selection still notifies");
        text.beginComposition();text.compositionRange(0,1);text.endComposition();require(sink->selectionChanges==1 && sink->textChanges==0,"Composition decoration changes do not report false selection/text");
        store->UnadviseSink(sink.Get());text.changed={};
    });
    test("TSF lock protocol selection replacement notifications and caret extent",[]{
        InlineText text;Rect crop{-100,-100,500,500};text.begin({-50,-40},crop,{1,1,1,1},24);text.insert(L"hello");
        ComPtr<TextStore> store;store.Attach(new TextStore(text,nullptr,crop));ComPtr<TextSink> sink;sink.Attach(new TextSink);
        check(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Advise test sink");
        text.changed=[&](const TextUpdate& update){store->notify(update);};LONG end{};require(store->GetEndACP(&end)==TS_E_NOLOCK,"Reads need a lock");
        int grants=0;sink->grant=[&](DWORD flags) {
            ++grants;check(store->GetEndACP(&end),"Read text length");require(end==5,"ACP length");
            if(grants==1){HRESULT sync{},async{};store->RequestLock(TS_LF_SYNC|TS_LF_READ,&sync);require(sync==TS_E_SYNCHRONOUS,"Reject reentrant synchronous lock");store->RequestLock(TS_LF_READWRITE,&async);require(async==TS_S_ASYNC,"Queue asynchronous upgrade");return;}
            require((flags&TS_LF_READWRITE)==TS_LF_READWRITE,"Upgrade grants write lock");
            TS_SELECTION_ACP selection{1,4,{TS_AE_START,FALSE}};check(store->SetSelection(1,&selection),"Set reverse selection");
            TS_TEXTCHANGE change{};LONG a{},b{};check(store->InsertTextAtSelection(0,L"X",1,&a,&b,&change),"Service insertion");require(a==1 && b==2 && change.acpOldEnd==4 && text.annotation().text==L"hXo","TSF replacement range");
            wchar_t buffer[10]{};ULONG copied{},runs{};LONG next{};TS_RUNINFO run{};check(store->GetText(0,-1,buffer,10,&copied,&run,1,&runs,&next),"Read plain text");require(std::wstring(buffer,copied)==L"hXo" && next==3 && run.uCount==3,"TSF plain text");
            RECT extent{};BOOL clipped{};check(store->GetTextExt(1,2,2,&extent,&clipped),"Caret screen extent");auto caret=text.caretRect(2);require(extent.left==caret.left && extent.top==caret.top && !clipped,"IME candidate anchor uses actual negative desktop caret");
        };
        HRESULT session{};check(store->RequestLock(TS_LF_READ,&session),"Request read lock");check(session,"Read session");if(sink->failure)std::rethrow_exception(sink->failure);require(grants==2 && text.annotation().text==L"hXo","Deferred write session completed");
        require(sink->textChanges==0,"Service owns its text-change notification");
        sink->grant=[](DWORD){};text.insert(L"!");require(sink->textChanges==1 && sink->selectionChanges>0 && sink->layoutChanges>0,"Application changes notify service");
        check(store->UnadviseSink(sink.Get()),"Unadvise sink");text.changed={};
    });
    test("TSF composition starts after initial service insertion and cancels atomically",[]{
        InlineText text;text.begin({0,0},{0,0,500,500},{1,1,1,1},24);text.insert(L"base");text.select(0,4);
        ComPtr<TextStore> store;store.Attach(new TextStore(text,nullptr,{0,0,500,500}));ComPtr<TextSink> sink;sink.Attach(new TextSink);
        check(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),0),"Advise composition test sink");
        sink->grant=[&](DWORD){TS_TEXTCHANGE change{};check(store->SetText(0,0,4,L"n",1,&change),"Initial service text");BOOL accepted{};check(store->OnStartComposition(nullptr,&accepted),"Composition starts after insertion");require(accepted,"Accept composition");};
        HRESULT session{};check(store->RequestLock(TS_LF_READWRITE,&session),"Service write lock");check(session,"Composition session");
        store->completeComposition(true);require(text.annotation().text==L"base" && text.start()==0 && text.end()==4,"Cancellation restores pre-insertion text and selection");
        require(text.undo() && text.annotation().text.empty(),"Canceled composition leaves no duplicate undo step");
        store->UnadviseSink(sink.Get());
    });
    test("TSF document activation composition lifecycle and repeated cleanup",[]{
        HWND window=CreateWindowExW(0,L"STATIC",L"Text service test",WS_POPUP,0,0,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(window!=nullptr,"Create TSF window");
        struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}}cleanup{window};
        for(int i=0;i<3;++i){InlineText text;text.begin({0,0},{0,0,300,200},{1,1,1,1},24);ComPtr<TextStore> store;store.Attach(new TextStore(text,window,{0,0,300,200}));store->start();BOOL accepted{};check(store->OnStartComposition(nullptr,&accepted),"Start composition");require(accepted && text.composing(),"Composition state registered");text.insert(L"test");store->completeComposition(true);require(text.annotation().text.empty() && !text.composing(),"Canceled composition restored");store->stop();store->stop();}
    });
}
