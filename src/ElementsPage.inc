// Pages that list an overlay's elements top to bottom with switch and order:
// Fuel Calculator (the consumption table expands to its rows; clean-lap option)
// and Weather (the wind card expands to its direction switch). Pit Limiter has
// nothing to choose, so its page only explains when and how it shows.
class ElementsPage : public PageWindow {
public:
    explicit ElementsPage(int kind) : PageWindow(ui.heading,ui.text), kind_(kind) {}
private:
    enum class Key { None, Element, ExcludeYellow, Info };
    static constexpr int ListId=100, FuelRowEditId=200, PitColumnEditId=210, WindDirectionEditId=220;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E', UpGlyph=L'\uE74A', DownGlyph=L'\uE74B';
    static constexpr const wchar_t* FuelDetails[]={
        L"Leader's laps when the race clock runs out",L"Race clock when the leader starts the last lap",
        L"Until the tank is empty, at the race average",L"Fuel left or missing at the finish",
        L"Minimum number of further stops",L"Current fuel and tank capacity",L"Lap blocks of one full tank",
        L"Open or closed, its laps and the fuel for the next stop",L"",
        L"Laps the leader still has to drive",L"All fuel still needed to finish",
        L"Tank, range in laps and fuel at the finish",L"Laps of one tank, the pit window and fuel to add"};
    static constexpr const wchar_t* FuelRowLabels[]={L"Last lap",L"Average of the last 10 laps",L"Average of the last 5 laps",L"Maximum",L"Minimum",L"Race average"};
    static_assert(std::size(FuelDetails)==FuelElementCount);
    static constexpr const wchar_t* WeatherDetails[]={L"Track surface temperature",L"Air temperature",
        L"Speed in km/h; the direction relative to your car while you drive",L"From dry to extremely wet",L"How much rubber is on the track"};
    static constexpr const wchar_t* WeatherNames[]={L"Track temperature",L"Air temperature",L"Wind",L"Track wetness",L"Track usage"};
    static_assert(std::size(WeatherDetails)==WeatherCardCount && std::size(WeatherNames)==WeatherCardCount);

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    std::vector<int>& Order() { return kind_==WeatherKind?app.weatherOrder:app.fuelOrder; }
    bool Expandable(int id) const { return kind_==WeatherKind?id==WindElement:id==ConsumptionTableElement; }
    std::wstring TableDetail() const {
        std::wstring text;
        for(int i:{RaceAverage,0,1,2,3,4}) if(app.fuelRows[i]) text+=(text.empty()?L"":L", ")+std::wstring(FuelMetricNames[i]);
        if(text.empty()) return L"No rows, so the table is hidden";
        if(app.fuelDisplay[ConsumptionTableElement]) text+=L"  \u00B7  pit window column";
        return text;
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); ids_.clear();
        auto push=[&](RowList::Row row,Key key,int id=-1) { rows.push_back(std::move(row)); keys_.push_back(key); ids_.push_back(id); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        if(kind_==PitLimiterKind) {
            auto info=[&](const wchar_t* title,const wchar_t* detail,const wchar_t* pill=L"",RowList::Tone tone=RowList::Tone::Neutral) {
                RowList::Row row; row.title=title; row.detail=detail; row.pill=pill; row.pillTone=tone; push(std::move(row),Key::Info);
            };
            header(L"When it shows");
            info(L"On pit approach and in the pit lane",L"Only while you drive your own car, not while spotting or in replays.");
            info(L"After pit exit",L"Stays until you switch the limiter off; with the limiter off it hides at pit exit.");
            header(L"Colors");
            info(L"Limiter on",L"PIT LIMITER ACTIVE",L"Green",RowList::Tone::Active);
            info(L"Limiter off",L"PIT LIMITER NOT ACTIVE",L"Red",RowList::Tone::Warning);
            info(L"Status unknown",L"iRacing doesn't report the limiter",L"Gray");
            list_.SetRows(std::move(rows)); return;
        }
        header(kind_==WeatherKind?L"Cards  \u00B7  top to bottom":L"Elements  \u00B7  top to bottom");
        for(int id:Order()) {
            RowList::Row row;
            // Icon buttons are laid out right to left: the expand arrow stays rightmost.
            row.iconButtons={DownGlyph,UpGlyph};
            if(kind_==FuelKind && id==ConsumptionTableElement) { row.title=L"Consumption table"; row.detail=TableDetail(); row.muted=!FuelElementVisible(id); }
            else {
                auto& shown=kind_==WeatherKind?app.weatherDisplay[id]:app.fuelDisplay[id];
                row.title=kind_==WeatherKind?WeatherNames[id]:FuelElements[id].option;
                row.detail=kind_==WeatherKind?WeatherDetails[id]:FuelDetails[id];
                if(kind_==WeatherKind && id==WindElement) row.detail+=app.weatherDisplay[WindDirectionOption]?L"  \u00B7  direction shown":L"  \u00B7  no direction";
                row.toggle=shown?1:0; row.muted=!shown;
            }
            if(Expandable(id)) {
                const bool open=expanded_==id;
                row.iconButtons.insert(row.iconButtons.begin(),open?CollapseGlyph:ExpandGlyph); row.expandHeight=open?(kind_==FuelKind?112:48):0;
            }
            push(std::move(row),Key::Element,id);
        }
        if(kind_==FuelKind) {
            header(L"Learning");
            RowList::Row yellow; yellow.title=L"Exclude yellow and red flag laps";
            yellow.detail=L"Laps under caution don't count toward consumption. Pit, refuel and first laps never count.";
            yellow.toggle=app.excludeYellowFuelLaps?1:0;
            push(std::move(yellow),Key::ExcludeYellow);
        }
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==Key::Element && ids_[i]==expanded_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        syncing_=true; editors_.Begin(row);
        if(kind_==WeatherKind) {
            editors_.Check(WindDirectionEditId,L"Show the wind direction (needs your car's heading, so only while you drive)",app.weatherDisplay[WindDirectionOption],0,10,0);
            syncing_=false; editors_.Position(); return;
        }
        for(int i=0;i<FuelMetricCount;++i) editors_.Check(FuelRowEditId+i,FuelRowLabels[i],app.fuelRows[i],(i%2)*260,4+(i/2)*28,250);
        editors_.Check(PitColumnEditId,L"Pit window column (PIT \u2248) for every row",app.fuelDisplay[ConsumptionTableElement],0,4+3*28,360);
        syncing_=false; editors_.Position();
    }
    void Expand(int id) {
        expanded_=expanded_==id?-1:id;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void Changed() { SettingChanged(); Refresh(); }
    void Move(int id,int step) { if(MoveInOrder(Order(),id,step)) { Refresh(); BuildEditors(); } }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id>=FuelRowEditId && id<=WindDirectionEditId) {
            if(syncing_ || code!=BN_CLICKED) return true;
            if(id==WindDirectionEditId) app.weatherDisplay[WindDirectionOption]=editors_.Checked(id);
            else if(id==PitColumnEditId) app.fuelDisplay[ConsumptionTableElement]=editors_.Checked(id);
            else if(id<FuelRowEditId+FuelMetricCount) app.fuelRows[id-FuelRowEditId]=editors_.Checked(id);
            Changed(); return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const bool on=list_.Rows()[row].toggle==1;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        const int element=ids_[row];
        switch(keys_[row]) {
        case Key::Element:
            if(code==RowList::kToggled) { (kind_==WeatherKind?app.weatherDisplay[element]:app.fuelDisplay[element])=on; Changed(); }
            else if(glyph==UpGlyph) Move(element,-1);
            else if(glyph==DownGlyph) Move(element,1);
            else if(Expandable(element) && (glyph==ExpandGlyph || glyph==CollapseGlyph || code==RowList::kActivated)) Expand(element);
            break;
        case Key::ExcludeYellow: if(code==RowList::kToggled) { app.excludeYellowFuelLaps=on; Changed(); } break;
        default: break;
        }
        return true;
    }

    int kind_;
    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> ids_;
    int expanded_=-1; bool syncing_=false;
};
HWND CreateElementsPage(HWND parent,int kind) { return PageWindow::Show(std::make_unique<ElementsPage>(kind),app.instance,parent,560,360); }
