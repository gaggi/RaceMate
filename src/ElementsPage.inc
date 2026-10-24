// Pages that list what an overlay shows: Fuel Calculator and Weather choose a layout,
// switch and order modules (the fuel consumption module expands to its values) and set
// header and footer. Pit Assist shares the bands and explains when and how it shows.
class ElementsPage : public PageWindow {
public:
    explicit ElementsPage(Overlay& o) : PageWindow(ui.heading,ui.text), o_(o) {}
private:
    enum class Key { None, Layout, Rounded, Module, Band, Info, SetupChanges, PitStrip, Countdown };
    static constexpr int ListId=100, FuelRowEditId=200, WindPartEditId=210, CautionEditId=220, SlotEditId=230, HeaderRow=1000, FooterRow=1001;

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    bool Fuel() const { return o_.kind==FuelKind; }
    const ModuleInfo* Modules() const { return Fuel()?FuelModules:WeatherModules; }
    std::vector<int>& Order() { return Fuel()?o_.fuel.order:o_.weather.order; }
    // Display options stay in place; wind rose and wind text are switched in the wind module's expanded row.
    // Caution marks are switched in the expanded "Last 10 laps" row.
    std::vector<int> Options() const { return Fuel()?std::vector<int>{}:std::vector<int>{TrendArrows,WindRelative}; }
    bool Expandable(int id) const { return Fuel()?id==FiguresModule || id==HistoryModule:id==WindModule; }
    bool& Shown(int id) { return Fuel()?o_.fuel.display[id]:o_.weather.display[id]; }
    bool& Strip() { return Fuel()?o_.fuel.strip:o_.weather.strip; }
    std::wstring FiguresDetail() const {
        std::wstring text;
        for(int i=0;i<FuelMetricCount;++i) if(o_.fuel.figures[i]) text+=(text.empty()?L"":L", ")+std::wstring(FuelFigures[i].title);
        return text.empty()?L"No values, so the row is hidden":text;
    }
    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); ids_.clear();
        auto push=[&](RowList::Row row,Key key,int id=-1) { rows.push_back(std::move(row)); keys_.push_back(key); ids_.push_back(id); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        auto info=[&](const wchar_t* title,const wchar_t* detail,const wchar_t* pill=L"",RowList::Tone tone=RowList::Tone::Neutral) {
            RowList::Row row; row.title=title; row.detail=detail; row.pill=pill; row.pillTone=tone; push(std::move(row),Key::Info);
        };
        auto bands=[&] {
            header(L"Header and footer");
            for(int id:{HeaderRow,FooterRow}) {
                RowList::Row row=BandRow(o_,id==FooterRow); const bool open=expanded_==id;
                row.iconButtons={open?Glyph::Collapse:Glyph::Expand}; row.expandHeight=open?BandEditorHeight:0;
                push(std::move(row),Key::Band,id);
            }
        };
        if(o_.kind==PitLimiterKind) {
            header(L"Layout");
            push(RoundedRow(o_),Key::Rounded);
            bands();
            header(L"When it shows");
            info(L"On pit approach and in the pit lane",L"Only while you drive your own car, not while spotting or in replays.");
            info(L"After pit exit",L"With the limiter still on it flashes TURN OFF PIT LIMITER until you switch it off; with the limiter off it hides at pit exit.");
            header(L"In the pit lane");
            RowList::Row strip; strip.title=L"Pit lane progress"; strip.toggle=o_.pitStrip?1:0;
            strip.detail=L"Entry, your box and exit with your car on the way: where to brake for the speed limit and for your box, and when the limiter can go off.";
            push(std::move(strip),Key::PitStrip);
            RowList::Row countdown; countdown.title=o_.pitSeconds?L"Countdown in seconds":L"Countdown in metres"; countdown.button=L"Choose...";
            countdown.detail=o_.pitSeconds?L"To the brake points, your box and the exit at your current speed, in tenths of a second":L"To the brake points, your box and the exit along the lap";
            countdown.muted=!o_.pitStrip; push(std::move(countdown),Key::Countdown);
            RowList::Row setup; setup.title=L"Setup changes"; setup.toggle=o_.setupChanges?1:0;
            setup.detail=L"Lists brake bias, ABS, TC and other in-car adjustments that no longer match the loaded setup, as long as you're in the pit lane.";
            push(std::move(setup),Key::SetupChanges);
            header(L"Colors");
            info(L"Limiter on",L"PIT LIMITER ACTIVE",L"Green",RowList::Tone::Active);
            info(L"Limiter off",L"PIT LIMITER NOT ACTIVE",L"Red",RowList::Tone::Warning);
            info(L"Limiter on after pit exit",L"TURN OFF PIT LIMITER",L"Flashing red",RowList::Tone::Warning);
            info(L"Status unknown",L"iRacing doesn't report the limiter",L"Gray");
            list_.SetRows(std::move(rows)); return;
        }
        header(L"Layout");
        RowList::Row layout; layout.title=Strip()?L"Strip":L"Panel"; layout.button=L"Choose...";
        layout.detail=!Strip()?L"The modules below, one under the other"
            :(Fuel()?L"Tank, range, average, refuel, latest stop and targets in one line":L"Track condition, rain, temperatures and wind in one line");
        push(std::move(layout),Key::Layout);
        push(RoundedRow(o_),Key::Rounded);
        auto module=[&](int id,bool movable) {
            RowList::Row row; row.title=Modules()[id].title; row.detail=Fuel() && id==FiguresModule?FiguresDetail():Modules()[id].detail;
            if(Fuel() && id==HistoryModule && o_.fuel.display[CautionMarks]) row.detail+=L"; caution laps marked";
            if(!Fuel() && id==WindModule) row.detail=o_.weather.display[WindRose] && o_.weather.display[WindText]?L"Wind rose and text":(o_.weather.display[WindRose]?L"Wind rose":(o_.weather.display[WindText]?L"Wind text":L"Nothing chosen"));
            row.toggle=Shown(id)?1:0; row.muted=!Shown(id) || (Strip() && !InStrip(o_.kind,id));
            if(Strip() && !InStrip(o_.kind,id)) row.pill=L"Panel only";
            else if(Strip() && Fuel() && id==FiguresModule) row.pill=L"Strip: AVG 10";
            // Same column for every row's button: the expand arrow or an empty slot.
            const bool open=expanded_==id;
            row.iconButtons={Expandable(id)?(open?Glyph::Collapse:Glyph::Expand):wchar_t{0}}; row.expandHeight=Expandable(id) && open?(id==FiguresModule?120:48):0;
            if(movable) row.iconButtons.insert(row.iconButtons.end(),{id!=Order().back()?Glyph::Down:wchar_t{0},id!=Order().front()?Glyph::Up:wchar_t{0}});
            push(std::move(row),Key::Module,id);
        };
        // Refuel and pit window share a row while they stand next to each other here.
        header(L"Modules  \u00B7  top to bottom");
        for(int id:Order()) module(id,true);
        if(!Options().empty()) header(L"Display");
        for(int id:Options()) module(id,false);
        bands();
        if(Fuel()) {
            header(L"Learning");
            info(L"Which laps count",L"Whole laps at racing speed: caution laps, pit and out laps, refuels and the first partial lap never count. The 10-lap average drives range, pit window, refuel and targets.");
        }
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if((keys_[i]==Key::Module || keys_[i]==Key::Band) && ids_[i]==expanded_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        syncing_=true; editors_.Begin(row);
        if(expanded_==HeaderRow || expanded_==FooterRow) BandEditors(editors_,o_,expanded_==FooterRow,SlotEditId);
        else if(Fuel() && expanded_==HistoryModule) editors_.Check(CautionEditId,FuelModules[CautionMarks].title,o_.fuel.display[CautionMarks],0,10,360);
        else if(Fuel()) for(int i=0;i<FuelMetricCount;++i) editors_.Check(FuelRowEditId+i,FuelFigures[i].title,o_.fuel.figures[i],(i%2)*260,4+(i/2)*28,250);
        else for(int i=0;i<2;++i) editors_.Check(WindPartEditId+i,WeatherModules[WindRose+i].title,o_.weather.display[WindRose+i],i*200,10,190);
        syncing_=false; editors_.Position();
    }
    void Expand(int id) {
        expanded_=expanded_==id?-1:id;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void Changed() { SettingChanged(); Refresh(); }
    bool OnCommand(int id,int code,HWND) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id>=SlotEditId && id<SlotEditId+SlotCount) {
            if(!syncing_ && code==CBN_SELCHANGE) { ApplyBandEditor(editors_,o_,expanded_==FooterRow,id,SlotEditId); Changed(); }
            return true;
        }
        if(id==CautionEditId) {
            if(!syncing_ && code==BN_CLICKED) { o_.fuel.display[CautionMarks]=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id==WindPartEditId || id==WindPartEditId+1) {
            if(!syncing_ && code==BN_CLICKED) { o_.weather.display[WindRose+id-WindPartEditId]=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id>=FuelRowEditId && id<FuelRowEditId+FuelMetricCount) {
            if(syncing_ || code!=BN_CLICKED) return true;
            o_.fuel.figures[id-FuelRowEditId]=editors_.Checked(id); Changed(); return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        const int icon=code==RowList::kIconButton?list_.NotifiedIconButton():-1;
        const wchar_t glyph=icon>=0 && icon<static_cast<int>(list_.Rows()[row].iconButtons.size())?list_.Rows()[row].iconButtons[icon]:0;
        const int element=ids_[row];
        switch(keys_[row]) {
        case Key::Module:
            if(code==RowList::kToggled) { Shown(element)=on; Changed(); }
            else if(glyph==Glyph::Up || glyph==Glyph::Down) { if(MoveInOrder(Order(),element,glyph==Glyph::Up?-1:1)) { Refresh(); BuildEditors(); } }
            else if(Expandable(element) && (glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated)) Expand(element);
            break;
        case Key::Band:
            if(code==RowList::kToggled) { SetBand(o_,element==FooterRow,on); Changed(); }
            else if(glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated) Expand(element);
            break;
        case Key::Rounded: if(code==RowList::kToggled) { SetRounded(o_,on); Changed(); } break;
        case Key::SetupChanges: if(code==RowList::kToggled) { o_.setupChanges=on; Fit(o_); Changed(); } break;
        case Key::PitStrip: if(code==RowList::kToggled) { o_.pitStrip=on; Fit(o_); Changed(); } break;
        case Key::Countdown:
            if(pressed) { const int choice=ChooseFromMenu({L"Metres",L"Seconds"},o_.pitSeconds?1:0); if(choice>=0) { o_.pitSeconds=choice==1; Changed(); } }
            break;
        case Key::Layout:
            if(pressed) { const int choice=ChooseFromMenu({L"Panel",L"Strip"},Strip()?1:0); if(choice>=0) { Strip()=choice==1; Changed(); } }
            break;
        default: break;
        }
        return true;
    }

    Overlay& o_;
    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> ids_;
    int expanded_=-1; bool syncing_=false;
};
HWND CreateElementsPage(HWND parent,Overlay& overlay) { return PageWindow::Show(std::make_unique<ElementsPage>(overlay),app.instance,parent,560,360); }
