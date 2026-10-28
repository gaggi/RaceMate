// Pages that list what an overlay shows: Fuel Calculator and Weather choose a layout,
// switch and order modules (the fuel consumption module expands to its values) and set
// header and footer. Pit Assist shares the bands and explains when and how it shows; the Side Warning and the
// Radar have no bands, only their layout, when they show and what is on them.
class ElementsPage : public PageWindow {
public:
    explicit ElementsPage(Overlay& o) : PageWindow(ui.heading,ui.text), o_(o) {}
private:
    enum class Key { None, Layout, Rounded, Module, Band, Info, SetupChanges, PitStrip, Countdown, SideCar, SideApart, RadarBackground, RadarRange, RadarShows, RadarDistances, TrafficBackground, TrafficCars, TrafficGapKey, TrafficFaster,
        AdjustPlace, AdjustTime, AdjustWas, AdjustTimer, AdjustGroup, AdjustSample };
    static constexpr int ListId=100, FuelRowEditId=200, WindPartEditId=210, HumidityEditId=212, HistoryPartEditId=213, CautionEditId=220, TargetRefuelEditId=221, SlotEditId=230, HeaderRow=1000, FooterRow=1001;

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
    bool Expandable(int id) const { return Fuel()?id==FiguresModule || id==HistoryModule || id==TargetRow:id==WindModule || id==TemperatureModule || id==TemperatureHistoryModule; }
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
        if(o_.kind==AdjustmentsKind) {
            const auto& a=o_.adjust;
            header(L"Where it shows");
            RowList::Row place; place.title=a.replace<0?L"Its own window":L"In place of the "+std::wstring(Titles[a.replace]); place.button=L"Choose...";
            place.detail=a.replace<0?L"Placed and sized like the other overlays; hidden until you change a dial."
                :L"Every shown "+std::wstring(Titles[a.replace])+L" shows it instead for a moment, at its size, on the dial's color.";
            push(std::move(place),Key::AdjustPlace);
            RowList::Row time; time.title=L"For "+Format(a.tenths/10.0,a.tenths%10?1:0)+L" s"; time.button=L"Choose...";
            time.detail=L"After your last change; another change starts it again.";
            push(std::move(time),Key::AdjustTime);
            RowList::Row sample; sample.title=L"Sample"; sample.button=L"Show";
            sample.detail=L"Shows ABS 4 for that long, as a change would.";
            push(std::move(sample),Key::AdjustSample);
            header(L"On it");
            RowList::Row was; was.title=L"Value before"; was.toggle=a.was?1:0;
            was.detail=L"“was 3” under the value: before your first change, so two steps from 3 to 1 read was 3.";
            push(std::move(was),Key::AdjustWas);
            RowList::Row timer; timer.title=L"Time left"; timer.toggle=a.timer?1:0;
            timer.detail=L"A bar along the bottom edge that runs down until it goes.";
            push(std::move(timer),Key::AdjustTimer);
            info(L"Steps",L"Under the value, once the lowest and highest steps of the dial have been seen in this car");
            header(L"Dials");
            const wchar_t* members[]={L"Brake bias, ABS, peak brake bias",L"TC 1 to 4",L"Anti-roll bars, weight jackers, differential",
                L"Throttle map, fuel mixture, engine braking, power, boost",L"MGU-K deploy mode and fixed, regen gain"};
            for(int g=0;g<static_cast<int>(a.groups.size());++g) {
                RowList::Row group; group.title=DialGroupNames[g]; group.toggle=a.groups[g]?1:0;
                group.detail=std::wstring(members[g])+L"; in "+(g==0?L"red":g==1?L"yellow":g==2?L"violet":g==3?L"teal":L"blue")+L". Each car has only some.";
                push(std::move(group),Key::AdjustGroup,g);
            }
            list_.SetRows(std::move(rows)); return;
        }
        if(o_.kind==TrafficKind) {
            const auto& t=o_.traffic;
            header(L"Layout");
            RowList::Row background; background.title=L"Background and frame"; background.toggle=t.background?1:0;
            background.detail=L"Off: transparent, only the boxes over the game.";
            push(std::move(background),Key::TrafficBackground);
            RowList::Row count; count.title=t.cars==1?L"1 car":std::to_wstring(t.cars)+L" cars"; count.button=L"Choose...";
            count.detail=L"The closest ones, one box each.";
            push(std::move(count),Key::TrafficCars);
            auto rounded=RoundedRow(o_); rounded.muted=!t.background; rounded.toggleEnabled=t.background; push(std::move(rounded),Key::Rounded);
            header(L"When it shows");
            RowList::Row gap; gap.title=L"Within "+std::to_wstring(t.gap)+L" s behind"; gap.button=L"Choose...";
            gap.detail=L"While you drive and a car is this close behind you, by its gap as in the Relative.";
            push(std::move(gap),Key::TrafficGapKey);
            RowList::Row faster; faster.title=L"Faster classes only"; faster.toggle=t.fasterOnly?1:0;
            faster.detail=L"Their estimated lap at least 2 % quicker than your class's. Off: every car behind you.";
            push(std::move(faster),Key::TrafficFaster);
            header(L"Colors");
            info(L"Box",L"The car's class colour; it pulses within 1.5 s",L"Class");
            info(L"Bar",L"Fills as the car closes in, from green to red",L"Green to red");
            list_.SetRows(std::move(rows)); return;
        }
        if(o_.kind==RadarKind) {
            const auto& r=o_.radar;
            header(L"Layout");
            RowList::Row background; background.title=L"Background and frame"; background.toggle=r.background?1:0;
            background.detail=L"Off: transparent, only the cars, guides and distances over the game.";
            push(std::move(background),Key::RadarBackground);
            RowList::Row range; range.title=L"Range"; range.button=L"Choose...";
            range.detail=std::to_wstring(r.range)+L" m ahead and behind; guides every "+Format(r.range/4.0,r.range%4?1:0)+L" m";
            push(std::move(range),Key::RadarRange);
            auto rounded=RoundedRow(o_); rounded.muted=!r.background; rounded.toggleEnabled=r.background; push(std::move(rounded),Key::Rounded);
            header(L"When it shows");
            RowList::Row shows; shows.title=r.always?L"Always while you drive":L"With cars near"; shows.button=L"Choose...";
            shows.detail=r.always?L"While you drive your own car, also without cars in range":L"While you drive and a car is within range, until 2 s after the last one left it";
            push(std::move(shows),Key::RadarShows);
            header(L"On the radar");
            RowList::Row distances; distances.title=L"Distances"; distances.toggle=r.distances?1:0;
            distances.detail=L"Bumper to bumper, to the nearest car ahead and behind, also on the lanes beside yours.";
            push(std::move(distances),Key::RadarDistances);
            info(L"Lanes",L"Estimated: iRacing reports cars along the lap only. Beside you the spotter tells the side; a car keeps it 3 s.");
            header(L"Colors");
            info(L"Cars",L"In their class color; yours outlined",L"Class");
            info(L"Spotter",L"Where it reports cars beside you",L"Red",RowList::Tone::Warning);
            list_.SetRows(std::move(rows)); return;
        }
        if(o_.kind==SideWarningKind) {
            header(L"Layout");
            RowList::Row apart; apart.title=L"Distance apart"; apart.button=L"Choose...";
            apart.detail=std::to_wstring(o_.sideApart)+(o_.sideApart==100?L" % of the screen width: at its edges":L" % of the screen width, centred on it");
            push(std::move(apart),Key::SideApart);
            push(RoundedRow(o_),Key::Rounded);
            info(L"Placement",L"Drag either bar up or down, or onto another monitor; they stay centred on the one under the mouse. Size and opacity under Overlays.");
            header(L"When it shows");
            info(L"A car beside you",L"While you drive and iRacing's spotter reports a car on that side. Both bars show while overlays are moved.");
            header(L"On the bars");
            RowList::Row car; car.title=L"Where the car beside you is"; car.toggle=o_.sideCar?1:0;
            car.detail=L"A block at its place along your car, higher when it is ahead; the bar spans 7 m either way.";
            push(std::move(car),Key::SideCar);
            header(L"Colors");
            info(L"One car beside you",L"The whole bar",L"Red",RowList::Tone::Warning);
            info(L"Two cars on one side",L"Two stripes",L"Red",RowList::Tone::Warning);
            info(L"Where the car is",L"The block on the bar",L"Amber");
            list_.SetRows(std::move(rows)); return;
        }
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
            if(Fuel() && id==TargetRow && o_.fuel.display[TargetRefuel]) row.detail=L"Most fuel per lap to reach the latest stop, and the fuel to add there; to the finish without a stop";
            if(!Fuel() && id==TemperatureModule) row.detail=o_.weather.display[TemperatureHumidity]?L"Track, air and humidity":L"Track and air";
            if(!Fuel() && id==TemperatureHistoryModule) {
                const auto& d=o_.weather.display;
                row.detail=std::wstring(L"Track")+(d[HistoryAir] && d[HistoryHumidity]?L", air and humidity":(d[HistoryAir]?L" and air":(d[HistoryHumidity]?L" and humidity":L"")))+L" over the last 20 minutes";
            }
            if(!Fuel() && id==WindModule) row.detail=o_.weather.display[WindRose] && o_.weather.display[WindText]?L"Wind rose and text":(o_.weather.display[WindRose]?L"Wind rose":(o_.weather.display[WindText]?L"Wind text":L"Nothing chosen"));
            row.toggle=Shown(id)?1:0; row.muted=!Shown(id) || (Strip() && !InStrip(o_.kind,id));
            if(Strip() && !InStrip(o_.kind,id)) row.pill=L"Panel only";
            else if(Strip() && Fuel() && id==FiguresModule) row.pill=L"Strip: AVG 10";
            // Same column for every row's button: the expand arrow or an empty slot.
            const bool open=expanded_==id;
            row.iconButtons={Expandable(id)?(open?Glyph::Collapse:Glyph::Expand):wchar_t{0}}; row.expandHeight=Expandable(id) && open?(id==FiguresModule?120:48):0;
            if(movable) row.dragGroup=1; // Reordered by dragging its grip.
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
        else if(Fuel() && expanded_==TargetRow) editors_.Check(TargetRefuelEditId,FuelModules[TargetRefuel].title,o_.fuel.display[TargetRefuel],0,10,360);
        else if(Fuel()) for(int i=0;i<FuelMetricCount;++i) editors_.Check(FuelRowEditId+i,FuelFigures[i].title,o_.fuel.figures[i],(i%2)*260,4+(i/2)*28,250);
        else if(expanded_==TemperatureModule) editors_.Check(HumidityEditId,L"Show humidity",o_.weather.display[TemperatureHumidity],0,10,360);
        else if(expanded_==TemperatureHistoryModule) for(int i=0;i<2;++i) editors_.Check(HistoryPartEditId+i,i?L"Humidity line":L"Air temperature line",o_.weather.display[HistoryAir+i],i*200,10,190);
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
        if(id==CautionEditId || id==TargetRefuelEditId) {
            if(!syncing_ && code==BN_CLICKED) { o_.fuel.display[id==CautionEditId?CautionMarks:TargetRefuel]=editors_.Checked(id); Changed(); }
            return true;
        }
        if(id==HumidityEditId || id==HistoryPartEditId || id==HistoryPartEditId+1) {
            if(!syncing_ && code==BN_CLICKED) { o_.weather.display[id==HumidityEditId?TemperatureHumidity:HistoryAir+id-HistoryPartEditId]=editors_.Checked(id); Changed(); }
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
            if(code==RowList::kMoved) { if(MoveToPlace(Order(),element,ids_[list_.DropRow()])) { Refresh(); BuildEditors(); } }
            else if(code==RowList::kToggled) { Shown(element)=on; Changed(); }
            else if(Expandable(element) && (glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated)) Expand(element);
            break;
        case Key::Band:
            if(code==RowList::kToggled) { SetBand(o_,element==FooterRow,on); Changed(); }
            else if(glyph==Glyph::Expand || glyph==Glyph::Collapse || code==RowList::kActivated) Expand(element);
            break;
        case Key::Rounded: if(code==RowList::kToggled) { SetRounded(o_,on); Changed(); } break;
        case Key::SetupChanges: if(code==RowList::kToggled) { o_.setupChanges=on; Fit(o_); Changed(); } break;
        case Key::PitStrip: if(code==RowList::kToggled) { o_.pitStrip=on; Fit(o_); Changed(); } break;
        case Key::SideCar: if(code==RowList::kToggled) { o_.sideCar=on; Changed(); } break;
        case Key::AdjustWas: if(code==RowList::kToggled) { o_.adjust.was=on; Changed(); } break;
        case Key::AdjustTimer: if(code==RowList::kToggled) { o_.adjust.timer=on; Changed(); } break;
        case Key::AdjustGroup: if(code==RowList::kToggled) { o_.adjust.groups[element]=on; Changed(); } break;
        case Key::AdjustSample: if(pressed) { app.now=GetTickCount64(); app.adjustSample=app.now; Changed(); } break;
        case Key::AdjustTime:
            if(pressed) { const int choice=ChooseFromMenu({L"1 s",L"2 s",L"3 s",L"4 s",L"5 s"},o_.adjust.tenths/10-1); if(choice>=0) { o_.adjust.tenths=10*(choice+1); Changed(); } }
            break;
        case Key::AdjustPlace:
            if(pressed) {
                std::vector<std::wstring> items{L"Its own window"}; for(int kind:AdjustmentTargets) items.push_back(L"In place of the "+std::wstring(Titles[kind]));
                const int current=o_.adjust.replace<0?0:static_cast<int>(std::find(std::begin(AdjustmentTargets),std::end(AdjustmentTargets),o_.adjust.replace)-std::begin(AdjustmentTargets))+1;
                const int choice=ChooseFromMenu(items,current);
                if(choice>=0) { o_.adjust.replace=choice?AdjustmentTargets[choice-1]:-1; Changed(); }
            }
            break;
        case Key::TrafficBackground: if(code==RowList::kToggled) { o_.traffic.background=on; Fit(o_); Changed(); } break;
        case Key::TrafficFaster: if(code==RowList::kToggled) { o_.traffic.fasterOnly=on; Changed(); } break;
        case Key::TrafficCars:
            if(pressed) { const int choice=ChooseFromMenu({L"1 car",L"2 cars",L"3 cars"},o_.traffic.cars-1); if(choice>=0) { o_.traffic.cars=choice+1; Fit(o_); Changed(); } }
            break;
        case Key::TrafficGapKey:
            if(pressed) {
                const int current=static_cast<int>(std::find(std::begin(TrafficGaps),std::end(TrafficGaps),o_.traffic.gap)-std::begin(TrafficGaps));
                const int choice=ChooseFromMenu({L"1 s",L"2 s",L"3 s",L"5 s"},current); if(choice>=0) { o_.traffic.gap=TrafficGaps[choice]; Changed(); }
            }
            break;
        case Key::RadarBackground: if(code==RowList::kToggled) { o_.radar.background=on; Fit(o_); Changed(); } break;
        case Key::RadarDistances: if(code==RowList::kToggled) { o_.radar.distances=on; Changed(); } break;
        case Key::RadarRange:
            if(pressed) { const int choice=ChooseFromMenu({L"10 m",L"20 m",L"30 m",L"40 m"},o_.radar.range/10-1); if(choice>=0) { o_.radar.range=10*(choice+1); Changed(); } }
            break;
        case Key::RadarShows:
            if(pressed) { const int choice=ChooseFromMenu({L"With cars near",L"Always while you drive"},o_.radar.always?1:0); if(choice>=0) { o_.radar.always=choice==1; Changed(); } }
            break;
        case Key::SideApart:
            if(pressed) {
                std::vector<std::wstring> items; for(int percent=100;percent>=20;percent-=10) items.push_back(std::to_wstring(percent)+(percent==100?L" %  ·  at the screen edges":L" %"));
                const int choice=ChooseFromMenu(items,(100-o_.sideApart)/10);
                if(choice>=0) { o_.sideApart=100-10*choice; Fit(o_); Changed(); }
            }
            break;
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
