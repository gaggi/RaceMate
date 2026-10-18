// Overlays page: one row per overlay instance with its switch, size, opacity, corner and
// refresh rate in the expanded row, and buttons to duplicate or remove it; below them
// "Add overlay" and the setup actions (preview, move, hide, reset).
class OverlaysPage : public PageWindow {
public:
    OverlaysPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Instance, Add, Preview, Move, Hide, Reset };
    static constexpr int ListId=100, ScaleId=200, OpacityId=201, CornerId=202, RateId=203, ShowInId=210;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E', RemoveGlyph=L'\uE74D';
    static constexpr const wchar_t* Corners[]={L"Top left",L"Top right",L"Bottom left",L"Bottom right"};

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    static int Corner(const Placement& place) { return (place.growLeft?1:0)+(place.growUp?2:0); }
    static std::wstring Summary(const Overlay& o) {
        std::wstring text=std::to_wstring(o.place.scale)+L" %  \u00B7  opacity "+std::to_wstring(o.place.opacity)+L" %  \u00B7  "+Corners[Corner(o.place)]+L"  \u00B7  "+std::to_wstring(o.place.rate)+L" Hz";
        if(o.place.rounded) text+=L"  \u00B7  rounded corners";
        // Only where it differs from showing everywhere.
        if(o.kind!=PitLimiterKind && std::find(o.place.showIn.begin(),o.place.showIn.end(),false)!=o.place.showIn.end()) {
            constexpr const wchar_t* situations[]={L"in car",L"spotting",L"replay",L"garage"};
            std::wstring where; for(int i=0;i<SituationCount;++i) if(o.place.showIn[i]) where+=(where.empty()?L"":L", ")+std::wstring(situations[i]);
            text+=L"  \u00B7  "+(where.empty()?std::wstring(L"never shown"):L"shows "+where);
        }
        return text;
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); overlays_.clear();
        auto push=[&](RowList::Row row,Key key,Overlay* o=nullptr) { rows.push_back(std::move(row)); keys_.push_back(key); overlays_.push_back(o); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        // Setup first, so it stays in view however many overlays follow.
        header(L"Setup");
        auto toggle=[&](Key key,const wchar_t* title,std::wstring detail,bool on) {
            RowList::Row row; row.title=title; row.detail=std::move(detail); row.toggle=on?1:0; push(std::move(row),key);
        };
        toggle(Key::Preview,L"Preview with sample data",L"Shows every overlay with made-up data, without iRacing.",app.preview);
        toggle(Key::Move,L"Move overlays",L"Drag overlays with the mouse.  \u00B7  "+HotkeyText(app.hotkeys[0]),app.edit);
        toggle(Key::Hide,L"Hide all overlays",L"Keeps their settings.  \u00B7  "+HotkeyText(app.hotkeys[1]),app.hidden);
        header(L"Overlays");
        for(auto& o:app.overlays) {
            RowList::Row row; row.title=Name(o); row.detail=Summary(o);
            row.toggle=o.place.enabled?1:0; row.button=L"Settings";
            // Right to left: expand, remove (added instances only); an empty slot keeps them in line.
            row.iconButtons={&o==expanded_?CollapseGlyph:ExpandGlyph,o.number>1?RemoveGlyph:wchar_t{0}};
            row.expandHeight=&o==expanded_?(o.kind==PitLimiterKind?84:118):0;
            push(std::move(row),Key::Instance,&o);
        }
        RowList::Row add; add.title=L"Add overlay"; add.detail=L"Another Relative, Standings, Fuel Calculator, Weather or Track Map with its own settings and place."; add.button=L"Add...";
        push(std::move(add),Key::Add);
        RowList::Row reset; reset.title=L"Reset positions"; reset.detail=L"Puts every overlay back in its default place on the main monitor."; reset.button=L"Reset";
        push(std::move(reset),Key::Reset);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==Key::Instance && overlays_[i]==expanded_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        const auto& place=expanded_->place;
        std::vector<std::wstring> scales,opacities,corners(std::begin(Corners),std::end(Corners)),rates;
        for(int n=50;n<=150;n+=10) scales.push_back(std::to_wstring(n)+L" %");
        for(int n=40;n<=100;n+=10) opacities.push_back(std::to_wstring(n)+L" %");
        for(int n:OverlayRates) rates.push_back(std::to_wstring(n)+L" Hz");
        const int rate=static_cast<int>(std::find(std::begin(OverlayRates),std::end(OverlayRates),place.rate)-std::begin(OverlayRates));
        syncing_=true; editors_.Begin(row);
        // Two per line, so they fit the narrowest window.
        editors_.Label(L"Size",0,10,84); editors_.Combo(ScaleId,scales,(place.scale-50)/10,88,10,130);
        editors_.Label(L"Opacity",280,10,84); editors_.Combo(OpacityId,opacities,(place.opacity-40)/10,368,10,130);
        editors_.Label(L"Fixed corner",0,46,84); editors_.Combo(CornerId,corners,Corner(place),88,46,130);
        editors_.Label(L"Updates",280,46,84); editors_.Combo(RateId,rates,rate,368,46,130);
        // When it shows; the Pit Limiter shows only while you drive near or in the pits.
        if(expanded_->kind!=PitLimiterKind) {
            constexpr const wchar_t* situations[]={L"In car",L"Spotting",L"Replay",L"Garage"};
            editors_.Label(L"Shows",0,84,84);
            for(int i=0;i<SituationCount;++i) editors_.Check(ShowInId+i,situations[i],place.showIn[i],88+i*104,84,100);
        }
        syncing_=false; editors_.Position();
    }
    void Expand(Overlay* o) {
        expanded_=expanded_==o?nullptr:o;
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
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==ScaleId || id==OpacityId || id==CornerId || id==RateId) { if(!syncing_ && code==CBN_SELCHANGE) ApplyEditor(id); return true; }
        if(id>=ShowInId && id<ShowInId+SituationCount) {
            if(!syncing_ && code==BN_CLICKED && expanded_) { expanded_->place.showIn[id-ShowInId]=editors_.Checked(id); SettingChanged(); Refresh(); }
            return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const Key key=keys_[row]; Overlay* o=overlays_[row];
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        switch(key) {
        case Key::Instance:
            if(code==RowList::kToggled) { o->place.enabled=on; SettingChanged(); Refresh(); }
            else if(code==RowList::kButton) OpenOverlay(*o);
            else if(glyph==RemoveGlyph) Remove(*o);
            else if(glyph==ExpandGlyph || glyph==CollapseGlyph || code==RowList::kActivated) Expand(o);
            break;
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
    std::vector<Key> keys_; std::vector<Overlay*> overlays_;
    Overlay* expanded_=nullptr; bool syncing_=false;
};
HWND CreateOverlaysPage(HWND parent) { return PageWindow::Show(std::make_unique<OverlaysPage>(),app.instance,parent,560,360); }
