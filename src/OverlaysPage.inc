// Expandable layout groups own their overlay instances and editing controls.
class OverlaysPage : public PageWindow {
public:
    OverlaysPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Instance, Add, Move, Hide, Reset, Layout, AddLayout };
    static constexpr int ListId=100, ScaleId=200, OpacityId=201, CornerId=202, RateId=203, SpoutId=204, OverlayNameId=205, BackgroundOpacityId=206, SpoutAlphaId=207, LayoutNameId=220, LayoutConditionId=221;
    static constexpr const wchar_t* Corners[]={L"Top left",L"Top right",L"Bottom left",L"Bottom right"};

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        if(!app.layoutGroupsInitialized) { app.openLayoutGroups.push_back(app.layout); app.layoutGroupsInitialized=true; }
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    static int Corner(const Placement& place) { return (place.growLeft?1:0)+(place.growUp?2:0); }
    static std::wstring Summary(const Overlay& o) {
        std::wstring text=std::to_wstring(o.place.scale)+L" %  ·  opacity "+std::to_wstring(o.place.opacity)+L" %  ·  "+Corners[Corner(o.place)]+L"  ·  "+std::to_wstring(o.place.rate)+L" Hz";
        if(o.place.rounded) text+=L"  ·  rounded corners";
        if(o.place.spout) text+=L"  ·  OBS (Spout2)";
        if(o.place.backgroundOnly) text+=L"  ·  background opacity only";
        return text;
    }
    Layout* FindLayout(int id) { for(auto& l:app.layouts) if(l.id==id) return &l; return nullptr; }
    void Refresh() {
        // After a layout switch an expanded overlay may be gone.
        if(expanded_ && std::none_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return &o==expanded_;})) { expanded_=nullptr; ui.expandedOverlay=nullptr; editors_.Clear(); }
        if(expandedLayout_ && !FindLayout(expandedLayout_)) { expandedLayout_=0; editors_.Clear(); }
        std::vector<RowList::Row> rows; keys_.clear(); overlays_.clear(); layouts_.clear();
        auto push=[&](RowList::Row row,Key key,Overlay* o=nullptr,int layout=0) { rows.push_back(std::move(row)); keys_.push_back(key); overlays_.push_back(o); layouts_.push_back(layout); };
        auto header=[&](std::wstring title) { RowList::Row row; row.header=true; row.title=std::move(title); push(std::move(row),Key::None); };
        // Setup first, so it stays in view however many overlays follow.
        header(L"Setup");
        auto toggle=[&](Key key,const wchar_t* title,std::wstring detail,bool on) {
            RowList::Row row; row.title=title; row.detail=std::move(detail); row.toggle=on?1:0; push(std::move(row),key);
        };
        toggle(Key::Move,L"Move overlays",L"Drag overlays with the mouse.  ·  "+HotkeyText(app.hotkeys[0]),app.edit);
        toggle(Key::Hide,L"Hide all overlays",L"Keeps their settings.  ·  "+HotkeyText(app.hotkeys[1]),app.hidden);
        header(L"Layouts");
        for(const auto& l:app.layouts) {
            const bool open=std::find(app.openLayoutGroups.begin(),app.openLayoutGroups.end(),l.id)!=app.openLayoutGroups.end();
            RowList::Row row; row.title=l.name;
            row.detail=std::wstring(ConditionNames[l.condition])+L": "+ConditionDetails[l.condition];
            if(LayoutPreviewing(l.id)) { row.pill=L"Preview"; row.pillTone=RowList::Tone::Warning; }
            else if(LayoutLive(l.id)) { row.pill=L"Active"; row.pillTone=RowList::Tone::Active; }
            row.button=LayoutPreviewRequested(l.id)?L"Stop preview":L"Preview";
            row.buttonEnabled=!LayoutLive(l.id) || LayoutPreviewRequested(l.id);
            row.iconButtons={open?Glyph::Collapse:Glyph::Expand,Glyph::Remove};
            row.expandHeight=l.id==expandedLayout_?84:0; row.dragGroup=1;
            push(std::move(row),Key::Layout,nullptr,l.id);
            if(!open) continue;
            for(auto& o:app.overlays) if(o.layout==l.id) {
                RowList::Row child; child.title=Name(o); child.detail=Summary(o); child.indent=40;
                if(!o.customName.empty()) child.detail=std::wstring(Titles[o.kind])+L"  ·  "+child.detail;
                child.toggle=o.place.enabled?1:0; child.button=L"Settings";
                child.iconButtons={&o==expanded_?Glyph::Collapse:Glyph::Expand,o.number>1?Glyph::Remove:wchar_t{0}};
                child.expandHeight=&o==expanded_?150:0;
                push(std::move(child),Key::Instance,&o,l.id);
            }
            RowList::Row add; add.title=L"Add overlay"; add.indent=40;
            add.detail=L"Add an overlay with its own name and settings to "+l.name+L"."; add.button=L"Add...";
            push(std::move(add),Key::Add,nullptr,l.id);
            RowList::Row reset; reset.title=L"Reset positions"; reset.indent=40;
            reset.detail=L"Restore overlay positions in "+l.name+L"."; reset.button=L"Reset";
            push(std::move(reset),Key::Reset,nullptr,l.id);
        }
        RowList::Row addLayout; addLayout.title=L"Add layout";
        addLayout.detail=L"An independent layout, copied or starting with defaults."; addLayout.button=L"Add...";
        push(std::move(addLayout),Key::AddLayout);
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
        syncing_=true;
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) { syncing_=false; return; }
        editors_.Begin(row);
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
        editors_.Label(L"Name",0,10,84); SendMessageW(editors_.Edit(OverlayNameId,Name(*expanded_),88,10,410),EM_SETLIMITTEXT,127,0);
        // Two per line, so they fit the narrowest window.
        editors_.Label(L"Size",0,46,84); editors_.Combo(ScaleId,scales,(place.scale-50)/10,88,46,130);
        editors_.Label(L"Opacity",280,46,60); editors_.Combo(OpacityId,opacities,(place.opacity-40)/10,348,46,100);
        editors_.Check(BackgroundOpacityId,L"Background only",place.backgroundOnly,458,46,140);
        editors_.Label(L"Fixed corner",0,82,84); editors_.Combo(CornerId,corners,Corner(place),88,82,130);
        editors_.Label(L"Updates",280,82,84); editors_.Combo(RateId,rates,rate,368,82,130);
        editors_.Check(SpoutId,L"OBS output",place.spout,0,118,260);
        editors_.Label(L"OBS alpha mode",280,118,130);
        editors_.Combo(SpoutAlphaId,{L"Premultiplied",L"Straight"},place.spoutPremultiplied?0:1,420,118,178);
        syncing_=false; editors_.Position();
    }
    void Expand(Overlay* o) {
        expanded_=expanded_==o?nullptr:o; expandedLayout_=0;
        ui.expandedOverlay=expanded_; UpdateOverlays(); Timer();
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ExpandLayout(int id) {
        const bool open=std::find(app.openLayoutGroups.begin(),app.openLayoutGroups.end(),id)!=app.openLayoutGroups.end();
        if(open) { std::erase(app.openLayoutGroups,id); expandedLayout_=0; if(expanded_ && expanded_->layout==id) expanded_=nullptr; }
        else { app.openLayoutGroups.push_back(id); expandedLayout_=id; expanded_=nullptr; Save(); app.layout=id; UpdateOverlays(); }
        ui.expandedOverlay=expanded_; UpdateOverlays(); Timer();
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
        else {
            o.place.opacity=40+10*choice;
            if(o.place.backgroundOnly) { Fit(o); SettingChanged(); }
            else { ApplyOpacity(o); Save(); }
        }
        Refresh();
    }
    void Add(int layout) {
        static constexpr int kinds[]={RelativeKind,StandingsKind,FuelKind,WeatherKind,TrackMapKind};
        std::vector<std::wstring> items; for(int kind:kinds) items.push_back(Titles[kind]);
        const int choice=ChooseFromMenu(items,-1); if(choice<0) return;
        Save(); app.layout=layout; NewInstance(kinds[choice]); Refresh();
    }
    void Remove(Overlay& o) {
        const auto question=L"Remove "+Name(o)+L" and its settings?";
        if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
        if(expanded_==&o) { expanded_=nullptr; ui.expandedOverlay=nullptr; editors_.Clear(); }
        RemoveInstance(o); UpdateOverlays(); Refresh();
    }
    void AddLayoutChoice() {
        const Layout* active=ActiveLayout();
        const int choice=active?ChooseFromMenu({L"Copy of "+active->name,L"Default overlays"},-1):1;
        if(choice<0) return;
        AddLayout(choice==0);
        if(std::find(app.openLayoutGroups.begin(),app.openLayoutGroups.end(),app.layout)==app.openLayoutGroups.end()) app.openLayoutGroups.push_back(app.layout);
        Refresh();
    }
    void RemoveLayoutChoice(int id) {
        const Layout* layout=FindLayout(id); if(!layout) return;
        const auto question=L"Remove the layout "+layout->name+L" with its overlays and their settings?";
        if(MessageBoxW(app.main,question.c_str(),L"RaceMate",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return;
        if(expandedLayout_==id) { expandedLayout_=0; editors_.Clear(); }
        RemoveLayout(id); Refresh();
    }
    // A layout dragged to where another stands in the display order.
    void DropLayout(int id,int place) {
        const auto from=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;});
        const auto to=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==place;});
        if(from==app.layouts.end() || to==app.layouts.end() || from==to) return;
        if(from<to) std::rotate(from,from+1,to+1); else std::rotate(to,from,from+1);
        ReconsiderLayout(); Save(); Refresh(); BuildEditors();
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==BackgroundOpacityId && code==BN_CLICKED && expanded_ && !syncing_) {
            expanded_->place.backgroundOnly=SendMessageW(editors_.Get(BackgroundOpacityId),BM_GETCHECK,0,0)==BST_CHECKED;
            Fit(*expanded_); SettingChanged(); Refresh(); BuildEditors(); return true;
        }
        if(id==OverlayNameId && expanded_ && !syncing_) {
            if(code==EN_CHANGE) { expanded_->customName=editors_.Text(id).substr(0,127); Save(); }
            if(code==EN_KILLFOCUS) {
                auto& text=expanded_->customName;
                const auto first=text.find_first_not_of(L" \t\r\n");
                text=first==std::wstring::npos?std::wstring():text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);
                const auto title=Name(*expanded_);
                if(expanded_->window) SetWindowTextW(expanded_->window,title.c_str());
                if(expanded_->mirror) SetWindowTextW(expanded_->mirror,title.c_str());
                Save(); InvalidateModel(); Refresh();
            }
            return true;
        }
        if(id==SpoutId && code==BN_CLICKED && expanded_ && !syncing_) {
            expanded_->place.spout=SendMessageW(editors_.Get(SpoutId),BM_GETCHECK,0,0)==BST_CHECKED;
            SettingChanged(); Refresh(); BuildEditors(); return true;
        }
        if(id==SpoutAlphaId && code==CBN_SELCHANGE && expanded_ && !syncing_) {
            expanded_->place.spoutPremultiplied=SendMessageW(editors_.Get(SpoutAlphaId),CB_GETCURSEL,0,0)==0;
            SettingChanged(); Refresh(); BuildEditors(); return true;
        }
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
            else if(code==RowList::kButton) { ToggleLayoutPreview(layout); Refresh(); }
            else if(glyph==Glyph::Remove) RemoveLayoutChoice(layout);
            else if(glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated) ExpandLayout(layout);
            break;
        case Key::AddLayout: if(pressed) AddLayoutChoice(); break;
        case Key::Add: if(pressed) Add(layout); break;
        case Key::Move: if(code==RowList::kToggled && on!=app.edit) ToggleEdit(); break;
        case Key::Hide: if(code==RowList::kToggled && on!=app.hidden) ToggleHidden(); break;
        case Key::Reset: if(pressed) { Save(); app.layout=layout; ResetPositions(); } break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<Overlay*> overlays_; std::vector<int> layouts_;
    Overlay* expanded_=nullptr; int expandedLayout_=0; bool syncing_=false;
};
HWND CreateOverlaysPage(HWND parent) { return PageWindow::Show(std::make_unique<OverlaysPage>(),app.instance,parent,560,360); }
