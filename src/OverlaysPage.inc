// Overlays page: the setup actions (preview, move, hide), the layouts with when each becomes
// active, then one row per overlay instance of the active layout with its switch, size,
// opacity, corner and refresh rate in the expanded row; below them "Add overlay" and "Reset".
class OverlaysPage : public PageWindow {
public:
    OverlaysPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Instance, Add, Preview, Move, Hide, Reset, Layout, AddLayout };
    static constexpr int ListId=100, ScaleId=200, OpacityId=201, CornerId=202, RateId=203, LayoutNameId=220, LayoutConditionId=221;
    static constexpr const wchar_t* Corners[]={L"Top left",L"Top right",L"Bottom left",L"Bottom right"};

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    static int Corner(const Placement& place) { return (place.growLeft?1:0)+(place.growUp?2:0); }
    static std::wstring Summary(const Overlay& o) {
        std::wstring text=std::to_wstring(o.place.scale)+L" %  ·  opacity "+std::to_wstring(o.place.opacity)+L" %  ·  "+Corners[Corner(o.place)]+L"  ·  "+std::to_wstring(o.place.rate)+L" Hz";
        if(o.place.rounded) text+=L"  ·  rounded corners";
        return text;
    }
    Layout* FindLayout(int id) { for(auto& l:app.layouts) if(l.id==id) return &l; return nullptr; }
    void Refresh() {
        // After a layout switch an expanded overlay may be gone.
        if(expanded_ && std::none_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return &o==expanded_;})) { expanded_=nullptr; editors_.Clear(); }
        if(expandedLayout_ && !FindLayout(expandedLayout_)) { expandedLayout_=0; editors_.Clear(); }
        std::vector<RowList::Row> rows; keys_.clear(); overlays_.clear(); layouts_.clear();
        auto push=[&](RowList::Row row,Key key,Overlay* o=nullptr,int layout=0) { rows.push_back(std::move(row)); keys_.push_back(key); overlays_.push_back(o); layouts_.push_back(layout); };
        auto header=[&](std::wstring title) { RowList::Row row; row.header=true; row.title=std::move(title); push(std::move(row),Key::None); };
        // Setup first, so it stays in view however many overlays follow.
        header(L"Setup");
        auto toggle=[&](Key key,const wchar_t* title,std::wstring detail,bool on) {
            RowList::Row row; row.title=title; row.detail=std::move(detail); row.toggle=on?1:0; push(std::move(row),key);
        };
        toggle(Key::Preview,L"Preview with sample data",L"Shows every overlay with made-up data, without iRacing.",app.preview);
        toggle(Key::Move,L"Move overlays",L"Drag overlays with the mouse.  ·  "+HotkeyText(app.hotkeys[0]),app.edit);
        toggle(Key::Hide,L"Hide all overlays",L"Keeps their settings.  ·  "+HotkeyText(app.hotkeys[1]),app.hidden);
        // Layouts in priority order: when the situation changes, the first whose condition applies becomes active.
        header(L"Layouts");
        for(size_t i=0;i<app.layouts.size();++i) {
            const auto& l=app.layouts[i]; const bool active=l.id==app.layout && !app.layoutIdle;
            RowList::Row row; row.title=l.name; row.detail=std::wstring(ConditionNames[l.condition])+L": "+ConditionDetails[l.condition];
            if(l.id==app.layout && app.layoutIdle) row.detail+=L"  ·  None applies now: overlays hidden";
            if(active) { row.pill=L"Active"; row.pillTone=RowList::Tone::Active; } else row.button=L"Use";
            // Right to left: expand, remove (not the first layout); the order changes by dragging the grip.
            row.iconButtons={l.id==expandedLayout_?Glyph::Collapse:Glyph::Expand,l.id!=1?Glyph::Remove:wchar_t{0}};
            row.expandHeight=l.id==expandedLayout_?84:0; row.dragGroup=1;
            push(std::move(row),Key::Layout,nullptr,l.id);
        }
        RowList::Row addLayout; addLayout.title=L"Add layout";
        addLayout.detail=L"Its own overlays, places and settings: a copy of the active layout, or default overlays."; addLayout.button=L"Add...";
        push(std::move(addLayout),Key::AddLayout);
        const Layout* active=ActiveLayout();
        header(app.layouts.size()>1 && active?L"Overlays in "+active->name:std::wstring(L"Overlays"));
        for(auto& o:app.overlays) {
            RowList::Row row; row.title=Name(o); row.detail=Summary(o);
            row.toggle=o.place.enabled?1:0; row.button=L"Settings";
            // Right to left: expand, remove (added instances only); an empty slot keeps them in line.
            row.iconButtons={&o==expanded_?Glyph::Collapse:Glyph::Expand,o.number>1?Glyph::Remove:wchar_t{0}};
            row.expandHeight=&o==expanded_?84:0;
            push(std::move(row),Key::Instance,&o);
        }
        RowList::Row add; add.title=L"Add overlay"; add.detail=L"Another Relative, Standings, Fuel Calculator, Weather or Track Map with its own settings and place."; add.button=L"Add...";
        push(std::move(add),Key::Add);
        RowList::Row reset; reset.title=L"Reset positions"; reset.detail=L"Puts every overlay back in its default place on the main monitor."; reset.button=L"Reset";
        push(std::move(reset),Key::Reset);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) {
            if(keys_[i]==Key::Instance && expanded_ && overlays_[i]==expanded_) return static_cast<int>(i);
            if(keys_[i]==Key::Layout && expandedLayout_ && layouts_[i]==expandedLayout_) return static_cast<int>(i);
        }
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        syncing_=true; editors_.Begin(row);
        if(const Layout* layout=expandedLayout_?FindLayout(expandedLayout_):nullptr) {
            editors_.Label(L"Name",0,10,84); editors_.Edit(LayoutNameId,layout->name,88,10,230);
            editors_.Label(L"Active when",0,46,84);
            editors_.Combo(LayoutConditionId,std::vector<std::wstring>(std::begin(ConditionNames),std::end(ConditionNames)),layout->condition,88,46,230);
            syncing_=false; editors_.Position(); return;
        }
        const auto& place=expanded_->place;
        std::vector<std::wstring> scales,opacities,corners(std::begin(Corners),std::end(Corners)),rates;
        for(int n=50;n<=150;n+=10) scales.push_back(std::to_wstring(n)+L" %");
        for(int n=40;n<=100;n+=10) opacities.push_back(std::to_wstring(n)+L" %");
        for(int n:OverlayRates) rates.push_back(std::to_wstring(n)+L" Hz");
        const int rate=static_cast<int>(std::find(std::begin(OverlayRates),std::end(OverlayRates),place.rate)-std::begin(OverlayRates));
        // Two per line, so they fit the narrowest window.
        editors_.Label(L"Size",0,10,84); editors_.Combo(ScaleId,scales,(place.scale-50)/10,88,10,130);
        editors_.Label(L"Opacity",280,10,84); editors_.Combo(OpacityId,opacities,(place.opacity-40)/10,368,10,130);
        editors_.Label(L"Fixed corner",0,46,84); editors_.Combo(CornerId,corners,Corner(place),88,46,130);
        editors_.Label(L"Updates",280,46,84); editors_.Combo(RateId,rates,rate,368,46,130);
        syncing_=false; editors_.Position();
    }
    void Expand(Overlay* o) {
        expanded_=expanded_==o?nullptr:o; expandedLayout_=0;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ExpandLayout(int id) {
        expandedLayout_=expandedLayout_==id?0:id; expanded_=nullptr;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ApplyEditor(int id) {
        if(!expanded_) return;
        auto& o=*expanded_; const int choice=editors_.Selection(id);
        if(choice<0) return;
        if(id==CornerId) SetOverlayCorner(o,choice);
        else if(id==RateId) { o.place.rate=OverlayRates[choice]; o.due=0; Timer(); SettingChanged(); UpdateStatus(); }
        else if(id==ScaleId) { o.place.scale=50+10*choice; Fit(o); SettingChanged(); }
        else { o.place.opacity=40+10*choice; ApplyOpacity(o); Save(); } // Only the window's alpha changes.
        Refresh();
    }
    void Add() {
        static constexpr int kinds[]={RelativeKind,StandingsKind,FuelKind,WeatherKind,TrackMapKind};
        std::vector<std::wstring> items; for(int kind:kinds) items.push_back(Titles[kind]);
        const int choice=ChooseFromMenu(items,-1); if(choice<0) return;
        NewInstance(kinds[choice]); Refresh();
    }
    void Remove(Overlay& o) {
        const auto question=L"Remove "+Name(o)+L" and its settings?";
        if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
        if(expanded_==&o) { expanded_=nullptr; editors_.Clear(); }
        RemoveInstance(o); UpdateOverlays(); Refresh();
    }
    void AddLayoutChoice() {
        const Layout* active=ActiveLayout();
        const int choice=ChooseFromMenu({L"Copy of "+(active?active->name:std::wstring(L"the active layout")),L"Default overlays"},-1);
        if(choice<0) return;
        AddLayout(choice==0); Refresh();
    }
    void RemoveLayoutChoice(int id) {
        const Layout* layout=FindLayout(id); if(!layout) return;
        const auto question=L"Remove the layout "+layout->name+L" with its overlays and their settings?";
        if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
        if(expandedLayout_==id) { expandedLayout_=0; editors_.Clear(); }
        RemoveLayout(id); Refresh();
    }
    // A layout dragged to where another stands in the priority order.
    void DropLayout(int id,int place) {
        const auto from=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;});
        const auto to=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==place;});
        if(from==app.layouts.end() || to==app.layouts.end() || from==to) return;
        if(from<to) std::rotate(from,from+1,to+1); else std::rotate(to,from,from+1);
        ReconsiderLayout(); Save(); Refresh(); BuildEditors();
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==ScaleId || id==OpacityId || id==CornerId || id==RateId) { if(!syncing_ && code==CBN_SELCHANGE) ApplyEditor(id); return true; }
        if(id==LayoutNameId) {
            // The name is saved as typed; the list shows it once the field is left.
            if(Layout* layout=FindLayout(expandedLayout_); layout && !syncing_) {
                if(code==EN_CHANGE) {
                    wchar_t name[128]{}; GetWindowTextW(editors_.Get(id),name,128);
                    if(name[0]) { layout->name=name; Save(); }
                } else if(code==EN_KILLFOCUS) Refresh();
            }
            return true;
        }
        if(id==LayoutConditionId) {
            if(Layout* layout=FindLayout(expandedLayout_); layout && !syncing_ && code==CBN_SELCHANGE && editors_.Selection(id)>=0) { layout->condition=editors_.Selection(id); ReconsiderLayout(); Save(); Refresh(); }
            return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const Key key=keys_[row]; Overlay* o=overlays_[row]; const int layout=layouts_[row];
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        switch(key) {
        case Key::Instance:
            if(code==RowList::kToggled) { o->place.enabled=on; SettingChanged(); Refresh(); }
            else if(code==RowList::kButton) OpenOverlay(*o);
            else if(glyph==Glyph::Remove) Remove(*o);
            else if(glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated) Expand(o);
            break;
        case Key::Layout:
            if(code==RowList::kMoved) DropLayout(layout,layouts_[list_.DropRow()]);
            else if(code==RowList::kButton) { SwitchLayout(layout); Refresh(); }
            else if(glyph==Glyph::Remove) RemoveLayoutChoice(layout);
            else if(glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated) ExpandLayout(layout);
            break;
        case Key::AddLayout: if(pressed) AddLayoutChoice(); break;
        case Key::Add: if(pressed) Add(); break;
        case Key::Preview: if(code==RowList::kToggled && on!=app.preview) TogglePreview(); break;
        case Key::Move: if(code==RowList::kToggled && on!=app.edit) ToggleEdit(); break;
        case Key::Hide: if(code==RowList::kToggled && on!=app.hidden) ToggleHidden(); break;
        case Key::Reset: if(pressed) ResetPositions(); break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<Overlay*> overlays_; std::vector<int> layouts_;
    Overlay* expanded_=nullptr; int expandedLayout_=0; bool syncing_=false;
};
HWND CreateOverlaysPage(HWND parent) { return PageWindow::Show(std::make_unique<OverlaysPage>(),app.instance,parent,560,360); }
