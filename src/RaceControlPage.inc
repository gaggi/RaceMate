// Race Control page: opens the Race Control window and chooses the columns of
// its standings and event tables (replacing the "Manage Fields" dialog).
class RaceControlPage : public PageWindow {
public:
    RaceControlPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, Open, OnTop, RestoreOrder, Field, EventField, Info, ShowSectors };
    static constexpr int ListId=100, SectorPartsEditId=200;
    static constexpr const wchar_t* FieldNames[]={L"Position",L"Car number",L"Driver",L"Class",L"Gap",L"Interval",L"Best lap",L"Last lap",
        L"Track and pit status",L"License and safety rating",L"iRating",L"Tires",L"Last pit lap",L"Pit lane time",L"Pit stop time",L"Flags",
        L"Completed laps",L"Positions gained",L"Incidents",L"Fuel to finish",L"Laps since stop",L"Stint time",L"Pit stops",L"iRating change",L"Car brand",L"Observed sector times"};
    static constexpr const wchar_t* EventNames[]={L"Replay from 5 s before",L"Replay from 10 s before",L"Time",L"Event",L"Car number",L"Driver",L"Details",L"Event lap"};
    static_assert(std::size(FieldNames)==RaceFieldCount);

    void OnCreate() override {
        // While the window is closed its fields live only in the settings file.
        if(!raceControl.window) LoadRaceFields();
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
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
        header(L"Standings columns");
        // Left to right as in the table; drag a row by its grip to move the column. Sector times follow Show sectors.
        for(int i:raceControl.fieldOrder) {
            RowList::Row row; row.title=FieldNames[i]; row.dragGroup=1;
            // Sectors: a column per sector or mini-sector; expand for which. The other rows keep the expand slot free.
            if(i==RaceSectorTimesField) {
                row.title=L"Sector times"; row.toggle=raceControl.showSectors?1:0; row.muted=!raceControl.showSectors;
                row.detail=std::wstring(raceControl.sectorParts==3?L"S1.1, S1.2 \u2026 per mini-sector":L"S1, S2, S3 \u2026 per SDK sector")
                    +L"  \u00B7  Each appears when completed; a new S1 clears the previous lap's later sectors. Green personal best, purple class best, yellow slower or with pit lane or off-track.";
                row.iconButtons.push_back(expanded_?Glyph::Collapse:Glyph::Expand); row.expandHeight=expanded_?48:0;
                push(std::move(row),Key::ShowSectors,i); continue;
            }
            row.iconButtons.push_back(0);
            row.toggle=raceControl.fields[i]?1:0; row.muted=!raceControl.fields[i];
            if(i==RaceFuelField) row.detail=L"Same END / SAVE as the overlays; own class only, full tank assumed. Saving laps are shared in Settings.";
            if(i==RaceStintTimeField) row.detail=L"Time since an observed race start or pit exit; unavailable after lost telemetry.";
            if(i==RaceRatingChangeField) row.detail=L"Estimated as if the race ended in the current class order.";
            push(std::move(row),Key::Field,i);
        }
        header(L"Event columns");
        // Events 0 and 1 were per-row replay buttons; the replay bar replaces them.
        for(int i:raceControl.eventOrder) {
            RowList::Row row; row.title=EventNames[i]; row.toggle=raceControl.eventFields[i]?1:0; row.muted=!raceControl.eventFields[i]; row.dragGroup=2;
            push(std::move(row),Key::EventField,i);
        }
        list_.SetRows(std::move(rows));
        BuildEditors();
    }
    // Sector detail inside the Sectors row, like the column options of Relative and Standings.
    void BuildEditors() {
        editors_.Clear();
        if(!expanded_) return;
        const auto at=std::find(keys_.begin(),keys_.end(),Key::ShowSectors); if(at==keys_.end()) return;
        syncing_=true; editors_.Begin(static_cast<int>(at-keys_.begin()));
        editors_.Label(L"Sector detail",0,10,90);
        editors_.Combo(SectorPartsEditId,{L"SDK sectors",L"3 mini-sectors per sector"},raceControl.sectorParts==3?1:0,94,10,200);
        syncing_=false; editors_.Position();
    }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==SectorPartsEditId) {
            if(!syncing_ && code==CBN_SELCHANGE && editors_.Selection(id)>=0) {
                raceControl.sectorParts=editors_.Selection(id)==1?3:1; if(raceControl.sortColumn>=RaceSectorColumn) raceControl.sortColumn=-1;
                SaveRaceFields(); UpdateRaceControl(true); Refresh();
            }
            return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        switch(keys_[row]) {
        case Key::ShowSectors: {
            const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
            if(code==RowList::kToggled) {
                raceControl.showSectors=on; raceControl.fields[RaceSectorTimesField]=on;
                if(!on && std::none_of(raceControl.fields.begin(),raceControl.fields.begin()+RaceSectorTimesField,[](bool v){return v;})) raceControl.fields[2]=true;
                if(!on && raceControl.sortColumn>=RaceSectorTimesField) raceControl.sortColumn=-1;
                SaveRaceFields(); UpdateRaceControl(true); Refresh();
            } else if(code==RowList::kMoved) {
                if(MoveToPlace(raceControl.fieldOrder,ids_[row],ids_[list_.DropRow()])) { SaveRaceFields(); UpdateRaceControl(true); Refresh(); }
            } else if(icon==0 || code==RowList::kActivated) {
                expanded_=!expanded_; Refresh(); list_.ScrollIntoView(row);
            }
        } break;
        case Key::Open: if(pressed) { OpenRaceControl(); Refresh(); } break;
        case Key::OnTop: if(code==RowList::kToggled) { SetRaceOnTop(on); Refresh(); } break;
        case Key::RestoreOrder: if(pressed) { SetRaceSort(false,-1,0); Refresh(); } break;
        case Key::Field: case Key::EventField:
            if(code==RowList::kToggled) { SetRaceField(keys_[row]==Key::EventField,ids_[row],on); Refresh(); }
            if(code==RowList::kMoved) {
                auto& order=keys_[row]==Key::EventField?raceControl.eventOrder:raceControl.fieldOrder;
                if(MoveToPlace(order,ids_[row],ids_[list_.DropRow()])) { SaveRaceFields(); UpdateRaceControl(true); Refresh(); }
            }
            break;
        case Key::Info: if(code==RowList::kToggled) { SetRaceInfo(ids_[row],on); Refresh(); } break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> ids_;
    bool expanded_=false, syncing_=false; // The Sectors row is open; editors are being filled.
};
HWND CreateRaceControlPage(HWND parent) { return PageWindow::Show(std::make_unique<RaceControlPage>(),app.instance,parent,560,360); }
