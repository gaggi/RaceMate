// Overlays page: one row per overlay with its switch, and size, opacity and corner
// in the expanded row; below it the setup actions (preview, move, hide, reset).
class OverlaysPage : public PageWindow {
public:
    OverlaysPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Overlay, Preview, Move, Hide, Reset };
    static constexpr int ListId=100, ScaleId=200, OpacityId=201, CornerId=202;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E';
    static constexpr const wchar_t* Corners[]={L"Top left",L"Top right",L"Bottom left",L"Bottom right"};

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    static int Corner(const Placement& place) { return (place.growLeft?1:0)+(place.growUp?2:0); }
    static std::wstring Summary(const Overlay& o) {
        std::wstring text=std::to_wstring(o.place.scale)+L" %  \u00B7  opacity "+std::to_wstring(o.place.opacity)+L" %  \u00B7  "+Corners[Corner(o.place)];
        return text;
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); kinds_.clear();
        auto push=[&](RowList::Row row,Key key,int kind=-1) { rows.push_back(std::move(row)); keys_.push_back(key); kinds_.push_back(kind); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        header(L"Overlays");
        for(const auto& o:app.overlays) {
            RowList::Row row; row.title=Titles[o.kind]; row.detail=Summary(o);
            row.toggle=o.place.enabled?1:0; row.button=L"Settings";
            row.iconButtons={o.kind==expanded_?CollapseGlyph:ExpandGlyph};
            row.expandHeight=o.kind==expanded_?48:0;
            push(std::move(row),Key::Overlay,o.kind);
        }
        header(L"Setup");
        auto toggle=[&](Key key,const wchar_t* title,std::wstring detail,bool on) {
            RowList::Row row; row.title=title; row.detail=std::move(detail); row.toggle=on?1:0; push(std::move(row),key);
        };
        toggle(Key::Preview,L"Preview with sample data",L"Shows every overlay with made-up data, without iRacing.",app.preview);
        toggle(Key::Move,L"Move overlays",L"Drag overlays with the mouse.  \u00B7  "+HotkeyText(app.hotkeys[0]),app.edit);
        toggle(Key::Hide,L"Hide all overlays",L"Keeps their settings.  \u00B7  "+HotkeyText(app.hotkeys[1]),app.hidden);
        RowList::Row reset; reset.title=L"Reset positions"; reset.detail=L"Puts every overlay back in its default place on the main monitor."; reset.button=L"Reset";
        push(std::move(reset),Key::Reset);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==Key::Overlay && kinds_[i]==expanded_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        const auto& place=app.overlays[expanded_].place;
        std::vector<std::wstring> scales,opacities,corners(std::begin(Corners),std::end(Corners));
        for(int n=50;n<=150;n+=10) scales.push_back(std::to_wstring(n)+L" %");
        for(int n=40;n<=100;n+=10) opacities.push_back(std::to_wstring(n)+L" %");
        syncing_=true; editors_.Begin(row);
        editors_.Label(L"Size",0,10,40); editors_.Combo(ScaleId,scales,(place.scale-50)/10,44,10,90);
        editors_.Label(L"Opacity",152,10,56); editors_.Combo(OpacityId,opacities,(place.opacity-40)/10,210,10,90);
        editors_.Label(L"Fixed corner",318,10,84); editors_.Combo(CornerId,corners,Corner(place),404,10,130);
        syncing_=false; editors_.Position();
    }
    void Expand(int kind) {
        expanded_=expanded_==kind?-1:kind;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ApplyEditor(int id) {
        if(expanded_<0) return;
        auto& o=app.overlays[expanded_]; const int choice=editors_.Selection(id);
        if(choice<0) return;
        if(id==CornerId) SetOverlayCorner(expanded_,choice);
        else { (id==ScaleId?o.place.scale:o.place.opacity)=id==ScaleId?50+10*choice:40+10*choice; Fit(o); SettingChanged(); }
        Refresh();
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==ScaleId || id==OpacityId || id==CornerId) { if(!syncing_ && code==CBN_SELCHANGE) ApplyEditor(id); return true; }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const Key key=keys_[row]; const int kind=kinds_[row];
        const bool on=list_.Rows()[row].toggle==1;
        switch(key) {
        case Key::Overlay:
            if(code==RowList::kToggled) { app.overlays[kind].place.enabled=on; SettingChanged(); Refresh(); }
            else if(code==RowList::kButton) OpenOverlay(kind);
            else if(code==RowList::kIconButton || code==RowList::kActivated) Expand(kind);
            break;
        case Key::Preview: if(code==RowList::kToggled && on!=app.preview) TogglePreview(); break;
        case Key::Move: if(code==RowList::kToggled && on!=app.edit) ToggleEdit(); break;
        case Key::Hide: if(code==RowList::kToggled && on!=app.hidden) ToggleHidden(); break;
        case Key::Reset: if(code==RowList::kButton || code==RowList::kActivated) ResetPositions(); break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> kinds_;
    int expanded_=-1; bool syncing_=false;
};
HWND CreateOverlaysPage(HWND parent) { return PageWindow::Show(std::make_unique<OverlaysPage>(),app.instance,parent,560,360); }
