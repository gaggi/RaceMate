// Race Control page: opens the Race Control window and chooses the columns of
// its standings and event tables (replacing the "Manage Fields" dialog).
class RaceControlPage : public PageWindow {
public:
    RaceControlPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Open, OnTop, RestoreOrder, Field, EventField, Info, ShowSectors, SectorParts };
    static constexpr int ListId=100;
    static constexpr const wchar_t* FieldNames[]={L"Position",L"Car number",L"Driver",L"Class",L"Gap",L"Interval",L"Best lap",L"Last lap",
        L"Track and pit status",L"License and safety rating",L"iRating",L"Tires",L"Last pit lap",L"Pit lane time",L"Pit stop time",L"Flags",
        L"Completed laps",L"Positions gained",L"Incidents",L"Fuel to finish",L"Laps since stop",L"Stint time",L"Pit stops",L"iRating change",L"Car brand",L"Observed sector times"};
    static constexpr const wchar_t* EventNames[]={L"Replay from 5 s before",L"Replay from 10 s before",L"Time",L"Event",L"Car number",L"Driver",L"Details",L"Event lap"};
    static_assert(std::size(FieldNames)==RaceFieldCount);

    void OnCreate() override {
        // While the window is closed its fields live only in the settings file.
        if(!raceControl.window) LoadRaceFields();
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); ids_.clear();
        auto push=[&](RowList::Row row,Key key,int id=-1) { rows.push_back(std::move(row)); keys_.push_back(key); ids_.push_back(id); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        header(L"Window");
        RowList::Row open; open.title=L"Race Control"; open.button=L"Open";
        RowList::Row top; top.title=L"Keep on top"; top.detail=L"Stays above iRacing while you drive; iRacing must run in borderless windowed mode."; top.toggle=raceControl.onTop?1:0;
        open.detail=L"Choose Live, 5 s or 10 s under the tables, then double-click an event or a driver.";
        if(raceControl.window && IsWindowVisible(raceControl.window)) { open.pill=L"Open"; open.pillTone=RowList::Tone::Active; open.button=L"Show"; }
        push(std::move(open),Key::Open);
        push(std::move(top),Key::OnTop);
        RowList::Row split; split.title=L"Table widths";
        split.detail=L"Drag the divider: events stay left, standings right. Double-click resets it; arrow keys also resize. The ratio is saved.";
        push(std::move(split),Key::None);
        if(raceControl.sortColumn>=0) {
            RowList::Row order; order.title=L"Standings order"; order.detail=L"Sorted by "+(raceControl.sortColumn>=RaceSectorColumn?SectorColumnTitle(raceControl.sortColumn-RaceSectorColumn,raceControl.sectorParts):std::wstring(FieldNames[raceControl.sortColumn])); order.button=L"Restore race order";
            push(std::move(order),Key::RestoreOrder);
        }
        header(L"Race information above the tables");
        const wchar_t* infoNames[]={L"Leader projected race distance",L"Time left at leader's final lap"};
        const wchar_t* infoDetails[]={L"Overall leader's fractional projection, for example ~19.12 laps.",L"Race clock when the leader is projected to start the final lap, for example ~13 s."};
        for(int i=0;i<RaceInfoCount;++i) {
            RowList::Row row; row.title=infoNames[i]; row.detail=infoDetails[i]; row.toggle=raceControl.info[i]?1:0; row.muted=!raceControl.info[i];
            push(std::move(row),Key::Info,i);
        }
        header(L"Sectors");
        RowList::Row sectors; sectors.title=L"Show sectors"; sectors.detail=L"One standings column per sector, sortable by its header. Options below appear when enabled."; sectors.toggle=raceControl.showSectors?1:0; push(std::move(sectors),Key::ShowSectors);
        if(raceControl.showSectors) {
            RowList::Row parts; parts.title=L"Sector detail"; parts.detail=raceControl.sectorParts==3?L"Three equal-distance mini-sectors per SDK sector: S1.1, S1.2 …":L"SDK sectors: S1, S2, S3 …"; parts.button=L"Choose..."; push(std::move(parts),Key::SectorParts);
            RowList::Row times; times.title=L"Sector times"; times.detail=L"Each sector appears when completed. A new S1 clears the previous lap's later sectors; after the pits a lap starts at its first completed sector. Green personal best, purple class best, yellow slower. Estimated seconds."; push(std::move(times),Key::None);
        }
        header(L"Standings columns");
        for(int i=0;i<RaceSectorTimesField;++i) {
            RowList::Row row; row.title=FieldNames[i]; row.toggle=raceControl.fields[i]?1:0; row.muted=!raceControl.fields[i];
            if(i==RaceFuelField) row.detail=L"Same END / SAVE as the overlays; own class only, full tank assumed. Saving laps are shared in Settings.";
            if(i==RaceStintTimeField) row.detail=L"Time since an observed race start or pit exit; unavailable after lost telemetry.";
            if(i==RaceRatingChangeField) row.detail=L"Estimated as if the race ended in the current class order.";
            push(std::move(row),Key::Field,i);
        }
        header(L"Event columns");
        // Events 0 and 1 were per-row replay buttons; the replay bar replaces them.
        for(int i:RaceEventOrder) {
            RowList::Row row; row.title=EventNames[i]; row.toggle=raceControl.eventFields[i]?1:0; row.muted=!raceControl.eventFields[i];
            push(std::move(row),Key::EventField,i);
        }
        list_.SetRows(std::move(rows));
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id!=ListId) return false;
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        switch(keys_[row]) {
        case Key::ShowSectors: if(code==RowList::kToggled) {
            raceControl.showSectors=on; raceControl.fields[RaceSectorTimesField]=on;
            if(!on && std::none_of(raceControl.fields.begin(),raceControl.fields.begin()+RaceSectorTimesField,[](bool v){return v;})) raceControl.fields[2]=true;
            if(!on && raceControl.sortColumn>=RaceSectorTimesField) raceControl.sortColumn=-1;
            SaveRaceFields(); UpdateRaceControl(true); Refresh();
        } break;
        case Key::SectorParts: if(pressed) {
            const int choice=ChooseFromMenu({L"SDK sectors",L"3 mini-sectors per sector"},raceControl.sectorParts==3?1:0);
            if(choice>=0) { raceControl.sectorParts=choice?3:1; if(raceControl.sortColumn>=RaceSectorColumn) raceControl.sortColumn=-1; SaveRaceFields(); UpdateRaceControl(true); Refresh(); }
        } break;
        case Key::Open: if(pressed) { OpenRaceControl(); Refresh(); } break;
        case Key::OnTop: if(code==RowList::kToggled) { SetRaceOnTop(on); Refresh(); } break;
        case Key::RestoreOrder: if(pressed) { SetRaceSort(false,-1,0); Refresh(); } break;
        case Key::Field: case Key::EventField:
            if(code==RowList::kToggled) { SetRaceField(keys_[row]==Key::EventField,ids_[row],on); Refresh(); }
            break;
        case Key::Info: if(code==RowList::kToggled) { SetRaceInfo(ids_[row],on); Refresh(); } break;
        default: break;
        }
        return true;
    }

    RowList list_;
    std::vector<Key> keys_; std::vector<int> ids_;
};
HWND CreateRaceControlPage(HWND parent) { return PageWindow::Show(std::make_unique<RaceControlPage>(),app.instance,parent,560,360); }
