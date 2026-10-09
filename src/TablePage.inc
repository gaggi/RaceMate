// Relative and Standings pages: what the rows show, the columns (switch, order and
// options in the expanded row) and the header and footer metrics.
class TablePage : public PageWindow {
public:
    explicit TablePage(int kind) : PageWindow(ui.heading,ui.text), kind_(kind) {}
private:
    enum class Key { None, Rows, GapDecimals, HidePit, StandingsRows, ClassView, OwnClass, LiveGaps, UntimedOrder, Column, Header, Footer, ColumnHeadings };
    static constexpr int ListId=100, RowsEditId=200, LeadersEditId=210, AheadEditId=211, BehindEditId=212, NameFormatEditId=201, FlagEditId=202, ShortViewEditId=205, SlotEditId=206;
    static constexpr UINT_PTR TypingTimer=1;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E', UpGlyph=L'\uE74A', DownGlyph=L'\uE74B';
    struct ColumnText { const wchar_t* name; const wchar_t* detail; };
    static constexpr ColumnText ColumnTexts[]={
        {L"Position",L"Overall or class position"},{L"Car number",L"On the class color"},{L"Driver",L""},
        {L"Gap",L""},{L"Interval",L"Gap to the car directly ahead, including hidden rows"},{L"Best lap",L""},{L"Last lap",L""},
        {L"License",L"License class and safety rating"},{L"iRating",L""},{L"Tires",L"Dry / wet, or the compound where a car has several"},
        {L"Last pit lap",L"Lap of the last observed pit entry"},{L"Pit lane time",L"From pit entry to pit exit"},
        {L"Pit stop time",L"Time standing in the pit stall, estimated"},{L"Completed laps",L""},
        {L"Positions gained",L"Compared with the starting grid"},{L"Status",L""}};
    static constexpr const wchar_t* NameFormats[]={L"First name Last name",L"F. Last name",L"F Last name"};
    static constexpr const wchar_t* FlagNames[]={L"Black flag",L"Slowdown",L"Required repairs"};
    static_assert(std::size(ColumnTexts)==ColumnCount);

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }
    void OnDestroy() override { if(typing_) ApplyRows(); }

    static bool Optional(int id) { return id==DriverColumn || id==StatusColumn || id==IntervalColumn || id==LapsColumn || id==GainedColumn || id>=LicenseColumn && id<=PitStopColumn; }
    static bool HasOptions(int id) { return id==DriverColumn || id==StatusColumn || id==LicenseColumn || id==IRatingColumn; }
    std::wstring ColumnDetail(int id) const {
        switch(id) {
        case DriverColumn: return NameFormats[app.nameFormat[kind_]];
        case GapColumn: return kind_==RelativeKind?L"Time to each car, estimated":L"Gap to the class leader";
        case StatusColumn: {
            std::wstring text=L"PIT, TOW, OUT";
            for(int i=0;i<3;++i) if(app.driverFlags[kind_][i]) text+=std::wstring(L", ")+FlagNames[i];
            return text;
        }
        case LicenseColumn: case IRatingColumn: {
            const bool shortView=app.driverShortView[kind_][id-LicenseColumn];
            const std::wstring base=ColumnTexts[id].detail[0]?ColumnTexts[id].detail:L"Driver rating";
            return base+(shortView?(id==LicenseColumn?L"  \u00B7  short: 3,45":L"  \u00B7  short: 2,4K"):L"");
        }
        default: return ColumnTexts[id].detail;
        }
    }
    static std::wstring SlotsText(const Slots& metrics) {
        std::wstring text;
        for(int slot=0;slot<3;++slot) text+=(slot?L"  \u00B7  ":L"")+std::wstring(metrics[slot]==NoMetric?L"\u2014":MetricTitles[metrics[slot]]);
        return text;
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); ids_.clear();
        auto push=[&](RowList::Row row,Key key,int id=-1) { rows.push_back(std::move(row)); keys_.push_back(key); ids_.push_back(id); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        // Icon buttons are laid out right to left: the expand arrow stays rightmost.
        auto expandable=[&](RowList::Row& row,Key key,int id,int height) {
            const bool open=expandedKey_==key && expandedId_==id;
            row.iconButtons.insert(row.iconButtons.begin(),open?CollapseGlyph:ExpandGlyph); row.expandHeight=open?height:0;
        };
        if(kind_==RelativeKind) {
            header(L"Rows");
            RowList::Row count; count.title=L"Rows"; count.detail=std::to_wstring(app.rows)+L" rows including your car, which stays in the middle";
            expandable(count,Key::Rows,-1,48); push(std::move(count),Key::Rows);
            RowList::Row decimals; decimals.title=L"Gap decimals"; decimals.detail=std::to_wstring(app.gapDecimals)+L"  \u00B7  "+Format(0,app.gapDecimals)+L" s"; decimals.button=L"Choose...";
            push(std::move(decimals),Key::GapDecimals);
            RowList::Row pit; pit.title=L"Hide drivers in the pit lane"; pit.detail=L"Your own car always stays."; pit.toggle=app.relativeHidePit?1:0;
            push(std::move(pit),Key::HidePit);
        }
        if(kind_==StandingsKind) {
            const auto& standings=app.standings;
            header(L"Rows");
            RowList::Row count; count.title=standings.separateClasses?L"Rows per class":L"Rows";
            count.detail=std::to_wstring(standings.leaders)+L" leaders, "+std::to_wstring(standings.ahead)+L" ahead and "+std::to_wstring(standings.behind)+L" behind your car";
            expandable(count,Key::StandingsRows,-1,48); push(std::move(count),Key::StandingsRows);
            RowList::Row view; view.title=L"Classes"; view.button=L"Choose...";
            view.detail=standings.separateClasses?L"Separate classes, each with its leader and entrants":L"Combined overall standings";
            push(std::move(view),Key::ClassView);
            RowList::Row own; own.title=L"Only my class"; own.detail=L"Your class, or the class of the car you watch."; own.toggle=standings.ownClass?1:0;
            push(std::move(own),Key::OwnClass);
            RowList::Row live; live.title=L"Live positions and gaps"; live.detail=L"Estimated from track position between start/finish crossings."; live.toggle=standings.liveGaps?1:0;
            push(std::move(live),Key::LiveGaps);
            RowList::Row untimed; untimed.title=L"Drivers without a lap time"; untimed.button=L"Choose...";
            untimed.detail=standings.fallbackByRating?L"Ordered by iRating, highest first":L"Ordered by car number";
            push(std::move(untimed),Key::UntimedOrder);
        }
        header(L"Columns  \u00B7  left to right");
        const auto& order=app.columnOrder[kind_];
        // Same button, same column: {expand, down, up} or {0, down, up} (see "Controls line up in columns").
        const bool anyOptions=std::any_of(order.begin(),order.end(),[&](int id){ return HasOptions(id); });
        for(size_t i=0;i<order.size();++i) {
            const int id=order[i];
            RowList::Row row; row.title=ColumnTexts[id].name; row.detail=ColumnDetail(id);
            if(Optional(id)) row.toggle=ColumnVisible(kind_,id)?1:0;
            else row.pill=L"Always";
            row.muted=!ColumnVisible(kind_,id);
            row.iconButtons={DownGlyph,UpGlyph};
            if(HasOptions(id)) expandable(row,Key::Column,id,48);
            else if(anyOptions) row.iconButtons.insert(row.iconButtons.begin(),0);
            push(std::move(row),Key::Column,id);
        }
        header(L"Header and footer");
        RowList::Row head; head.title=L"Header"; head.detail=SlotsText(app.headerMetrics[kind_]); head.toggle=app.tableHeaders[kind_]?1:0;
        expandable(head,Key::Header,-1,48); push(std::move(head),Key::Header);
        RowList::Row foot; foot.title=L"Footer"; foot.detail=SlotsText(app.footers[kind_].metrics); foot.toggle=app.footers[kind_].enabled?1:0;
        expandable(foot,Key::Footer,-1,48); push(std::move(foot),Key::Footer);
        RowList::Row headings; headings.title=L"Column headings"; headings.detail=L"Without them, columns shrink to their values."; headings.toggle=app.columnHeaders[kind_]?1:0;
        push(std::move(headings),Key::ColumnHeadings);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==expandedKey_ && ids_[i]==expandedId_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        syncing_=true; editors_.Begin(row);
        if(expandedKey_==Key::Rows) {
            editors_.Label(L"Rows",0,10,44);
            const HWND edit=editors_.Edit(RowsEditId,std::to_wstring(app.rows),48,10,70);
            SetWindowLongPtrW(edit,GWL_STYLE,GetWindowLongPtrW(edit,GWL_STYLE)|ES_NUMBER); SendMessageW(edit,EM_SETLIMITTEXT,3,0);
            editors_.Label(L"Odd numbers from 1 to 127; even numbers round up.",132,10,0);
        } else if(expandedKey_==Key::StandingsRows) {
            const wchar_t* labels[]={L"Leaders",L"Ahead",L"Behind"}; const int values[]={app.standings.leaders,app.standings.ahead,app.standings.behind};
            for(int i=0;i<3;++i) {
                editors_.Label(labels[i],i*150,10,56);
                const HWND edit=editors_.Edit(LeadersEditId+i,std::to_wstring(values[i]),i*150+60,10,64);
                SetWindowLongPtrW(edit,GWL_STYLE,GetWindowLongPtrW(edit,GWL_STYLE)|ES_NUMBER); SendMessageW(edit,EM_SETLIMITTEXT,3,0);
            }
            editors_.Label(L"0 to 128 each",450,10,0);
        } else if(expandedKey_==Key::Header || expandedKey_==Key::Footer) {
            const Slots& metrics=expandedKey_==Key::Header?app.headerMetrics[kind_]:app.footers[kind_].metrics;
            const std::vector<std::wstring> titles(std::begin(MetricTitles),std::end(MetricTitles));
            const wchar_t* labels[]={L"Left",L"Center",L"Right"};
            for(int slot=0;slot<3;++slot) { editors_.Label(labels[slot],slot*228,10,44); editors_.Combo(SlotEditId+slot,titles,metrics[slot],slot*228+46,10,170); }
        } else if(expandedId_==DriverColumn) {
            editors_.Label(L"Name format",0,10,90);
            editors_.Combo(NameFormatEditId,std::vector<std::wstring>(std::begin(NameFormats),std::end(NameFormats)),app.nameFormat[kind_],94,10,200);
        } else if(expandedId_==StatusColumn) {
            for(int i=0;i<3;++i) editors_.Check(FlagEditId+i,FlagNames[i],app.driverFlags[kind_][i],i*170,10,160);
        } else if(expandedId_==LicenseColumn || expandedId_==IRatingColumn) {
            const int index=expandedId_-LicenseColumn;
            editors_.Check(ShortViewEditId,expandedId_==LicenseColumn?L"Short view: safety rating only (3,45)":L"Short view: thousands (2,4K)",app.driverShortView[kind_][index],0,10,360);
        }
        syncing_=false; editors_.Position();
    }
    void Expand(Key key,int id) {
        if(typing_) ApplyRows();
        const bool same=expandedKey_==key && expandedId_==id;
        expandedKey_=same?Key::None:key; expandedId_=same?-1:id;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void Changed() { SettingChanged(); Refresh(); }
    void Move(int id,int step) { if(MoveInOrder(app.columnOrder[kind_],id,step)) { Refresh(); BuildEditors(); } }
    // Applies typed numbers; empty or invalid fields keep their saved value.
    void ApplyRows() {
        typing_=false; KillTimer(Handle(),TypingTimer);
        auto read=[&](int id,int& target,int minimum) {
            BOOL valid=FALSE; const UINT value=GetDlgItemInt(list_.Handle(),id,&valid,FALSE);
            if(!valid || static_cast<int>(std::min<UINT>(value,MaxCars))<minimum) return false;
            const int clamped=static_cast<int>(std::min<UINT>(value,MaxCars));
            if(clamped==target) return false;
            target=clamped; return true;
        };
        bool changed=false;
        if(expandedKey_==Key::Rows) { int rows=app.rows; if(read(RowsEditId,rows,1)) { rows=RelativeRowCount(rows); changed=rows!=app.rows; app.rows=rows; } }
        if(expandedKey_==Key::StandingsRows) {
            changed=read(LeadersEditId,app.standings.leaders,0) | changed;
            changed=read(AheadEditId,app.standings.ahead,0) | changed;
            changed=read(BehindEditId,app.standings.behind,0) | changed;
        }
        if(changed) Changed();
    }
    void ChooseDecimals() {
        const int choice=ChooseFromMenu({L"0  \u00B7  1 s",L"1  \u00B7  0.1 s",L"2  \u00B7  0.01 s",L"3  \u00B7  0.001 s"},app.gapDecimals);
        if(choice<0) return;
        app.gapDecimals=choice; Changed();
    }
    void OnTimer(UINT_PTR id) override { if(id==TypingTimer) ApplyRows(); }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==RowsEditId || (id>=LeadersEditId && id<=BehindEditId)) { if(code==EN_CHANGE && !syncing_) { typing_=true; SetTimer(Handle(),TypingTimer,500,nullptr); } return true; }
        if(id>=NameFormatEditId && id<SlotEditId+3) {
            if(syncing_) return true;
            if(id==NameFormatEditId && code==CBN_SELCHANGE) app.nameFormat[kind_]=std::max(0,editors_.Selection(id));
            else if(id>=FlagEditId && id<FlagEditId+3 && code==BN_CLICKED) app.driverFlags[kind_][id-FlagEditId]=editors_.Checked(id);
            else if(id==ShortViewEditId && code==BN_CLICKED) app.driverShortView[kind_][expandedId_-LicenseColumn]=editors_.Checked(id);
            else if(id>=SlotEditId && code==CBN_SELCHANGE) {
                auto& metrics=expandedKey_==Key::Header?app.headerMetrics[kind_]:app.footers[kind_].metrics;
                metrics[id-SlotEditId]=std::clamp(editors_.Selection(id),0,MetricCount-1);
            }
            else return true;
            Changed(); return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const Key key=keys_[row]; const int column=ids_[row];
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        const bool toggleExpand=glyph==ExpandGlyph || glyph==CollapseGlyph || code==RowList::kActivated;
        switch(key) {
        case Key::Rows: if(toggleExpand) Expand(key,-1); break;
        case Key::GapDecimals: if(pressed) ChooseDecimals(); break;
        case Key::HidePit: if(code==RowList::kToggled) { app.relativeHidePit=on; Changed(); } break;
        case Key::StandingsRows: if(toggleExpand) Expand(key,-1); break;
        case Key::ClassView:
            if(pressed) { const int choice=ChooseFromMenu({L"Separate classes",L"Combined overall standings"},app.standings.separateClasses?0:1); if(choice>=0) { app.standings.separateClasses=choice==0; Changed(); } }
            break;
        case Key::OwnClass: if(code==RowList::kToggled) { app.standings.ownClass=on; Changed(); } break;
        case Key::LiveGaps: if(code==RowList::kToggled) { app.standings.liveGaps=on; Changed(); } break;
        case Key::UntimedOrder:
            if(pressed) { const int choice=ChooseFromMenu({L"By car number",L"By iRating, highest first"},app.standings.fallbackByRating?1:0); if(choice>=0) { app.standings.fallbackByRating=choice==1; Changed(); } }
            break;
        case Key::Column:
            if(code==RowList::kToggled) { SetColumnVisible(kind_,column,on); Refresh(); }
            else if(glyph==UpGlyph) Move(column,-1);
            else if(glyph==DownGlyph) Move(column,1);
            else if(toggleExpand && HasOptions(column)) Expand(key,column);
            break;
        case Key::Header: case Key::Footer:
            if(code==RowList::kToggled) { (key==Key::Header?app.tableHeaders[kind_]:app.footers[kind_].enabled)=on; Changed(); }
            else if(toggleExpand) Expand(key,-1);
            break;
        case Key::ColumnHeadings: if(code==RowList::kToggled) { app.columnHeaders[kind_]=on; Changed(); } break;
        default: break;
        }
        return true;
    }

    int kind_;
    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> ids_;
    Key expandedKey_=Key::None; int expandedId_=-1;
    bool syncing_=false, typing_=false;
};
HWND CreateTablePage(HWND parent,int kind) { return PageWindow::Show(std::make_unique<TablePage>(kind),app.instance,parent,560,360); }
