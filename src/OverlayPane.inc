// The overlay list beside the page, the compact hub: every layout and the open layout's overlays, each with its
// switch and where it shows. Clicking a layout opens it and its page on the right, clicking an overlay its settings;
// the open overlay and its layout are marked. Renaming and removing are on those pages; moving, hiding
// and pausing OBS in the status bar above.
class OverlayPane : public PageWindow {
public:
    OverlayPane() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Layout, AddLayout, Overlay, Add };
    static constexpr int ListId=100;

    // No scroll bar beside the page's: the mouse wheel scrolls a long list.
    void OnCreate() override { list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont()); list_.HideScrollBar(); Refresh(); }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,width,height); }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); overlays_.clear(); layouts_.clear();
        auto push=[&](RowList::Row row,Key key,Overlay* o=nullptr,int layout=0) { rows.push_back(std::move(row)); keys_.push_back(key); overlays_.push_back(o); layouts_.push_back(layout); };
        auto header=[&](std::wstring title) { RowList::Row row; row.header=true; row.title=std::move(title); push(std::move(row),Key::None); };
        header(L"Layouts");
        for(const auto& l:app.layouts) {
            RowList::Row row; row.title=l.name; row.dragGroup=1;
            row.detail=std::wstring(ConditionNames[l.condition])+(l.session?L" · "+std::wstring(SessionNames[l.session]):L"");
            if(LayoutPreviewing(l.id)) { row.pill=L"Preview"; row.pillTone=RowList::Tone::Warning; }
            else if(LayoutLive(l.id)) { row.pill=L"Active"; row.pillTone=RowList::Tone::Active; }
            row.selected=l.id==app.layout; // Also while one of its overlays is open.
            push(std::move(row),Key::Layout,nullptr,l.id);
        }
        RowList::Row addLayout; addLayout.title=L"Add layout"; addLayout.button=L"Add..."; push(std::move(addLayout),Key::AddLayout);
        if(const Layout* layout=ActiveLayout()) {
            int count=0,on=0; for(const auto& o:app.overlays) if(o.layout==app.layout) { ++count; on+=o.place.enabled || o.place.spout; }
            header(layout->name+L"  ·  "+std::to_wstring(on)+L" of "+std::to_wstring(count)+L" on");
            for(auto& o:app.overlays) if(o.layout==app.layout) {
                // On wherever it shows, the screen or OBS; the text under it says where.
                RowList::Row row; row.title=Name(o); row.toggle=o.place.enabled || o.place.spout?1:0;
                row.detail=o.place.enabled && o.place.spout?L"Screen + OBS":(o.place.spout?L"OBS":(o.place.enabled?L"Screen":L"Off"));
                if(!o.customName.empty()) row.detail=std::wstring(Titles[o.kind])+L"  ·  "+row.detail;
                row.selected=ui.page==Page::Overlay && ui.overlay==&o;
                push(std::move(row),Key::Overlay,&o);
            }
            RowList::Row add; add.title=L"Add overlay"; add.button=L"Add..."; push(std::move(add),Key::Add);
        }
        list_.SetRows(std::move(rows));
    }
    void ChooseLayout(int id) {
        if(id!=app.layout) SwitchLayout(id);
        ShowPage(Page::Overlays);
    }
    void AddLayoutChoice() {
        const Layout* active=ActiveLayout();
        std::vector<std::wstring> items; if(active) items.push_back(L"Copy of "+active->name);
        items.push_back(L"Default overlays"); items.push_back(L""); items.push_back(L"From a file...");
        int choice=ChooseFromMenu(items); if(choice<0) return;
        if(!active) ++choice; // The same numbers with or without the copy.
        if(choice==0 || choice==1) { AddLayout(choice==0); ShowPage(Page::Overlays); }
        if(choice==3) if(const int id=ImportLayoutChoice()) ChooseLayout(id);
    }
    // A layout dragged to where another stands in the order.
    void DropLayout(int id,int place) {
        const auto from=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;});
        const auto to=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==place;});
        if(from==app.layouts.end() || to==app.layouts.end() || from==to) return;
        if(from<to) std::rotate(from,from+1,to+1); else std::rotate(to,from,from+1);
        ReconsiderLayout(); Save(); Refresh();
    }
    // Off remembers where it showed, on brings that back: the screen, OBS or both.
    static void SwitchOverlay(Overlay& o,bool on) {
        const int shows=(o.place.enabled?1:0)|(o.place.spout?2:0);
        if(!on && shows) o.place.lastShows=shows;
        const int next=on?(o.place.lastShows?o.place.lastShows:1):0;
        o.place.enabled=next&1; o.place.spout=(next&2)!=0;
    }
    void AddOverlay() {
        const int kind=ChooseFromMenu(std::vector<std::wstring>(std::begin(Titles),std::end(Titles)));
        if(kind>=0) OpenOverlay(NewInstance(kind));
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id!=ListId) return false;
        const int row=list_.NotifiedRow(); if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        Overlay* o=overlays_[row]; const int layout=layouts_[row];
        // Each choice below may rebuild the pages, this list included: nothing of it is used afterwards.
        switch(keys_[row]) {
        case Key::Layout:
            if(code==RowList::kMoved) DropLayout(layout,layouts_[list_.DropRow()]);
            else if(code==RowList::kActivated) ChooseLayout(layout);
            break;
        case Key::AddLayout: if(code==RowList::kButton || code==RowList::kActivated) AddLayoutChoice(); break;
        case Key::Overlay:
            if(code==RowList::kToggled) { SwitchOverlay(*o,list_.Rows()[row].toggle==1); SettingChanged(); RefreshPage(); }
            else if(code==RowList::kActivated) OpenOverlay(*o);
            break;
        case Key::Add: if(code==RowList::kButton || code==RowList::kActivated) AddOverlay(); break;
        default: break;
        }
        return true;
    }
    RowList list_;
    std::vector<Key> keys_; std::vector<Overlay*> overlays_; std::vector<int> layouts_;
};
HWND CreateOverlayPane(HWND parent) { return PageWindow::Show(std::make_unique<OverlayPane>(),app.instance,parent,280,360); }
