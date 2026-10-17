// Settings page: startup, how overlays update, keyboard shortcuts and updates.
class GeneralPage : public PageWindow {
public:
    GeneralPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, StartWithWindows, StartMinimized, RefreshRate, TitleBar, OpponentFuel, PitLane, PitService, Hotkey, CheckUpdates, Version };
    static constexpr int ListId=100, HotkeyId=200, HotkeyResetId=201, LaneEditId=202, TiresEditId=203, RateEditId=204;
    // Expanded rows beyond the three shortcuts.
    static constexpr int LaneRow=10, ServiceRow=11;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E';
    // Below 5 Hz short offtracks and pit timing get lost.
    static constexpr int Rates[]={5,10,15,20};

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        Refresh();
    }
    void OnSize(int width,int height) override { Place(list_.Handle(),0,0,std::min(width,760),height); }

    void Refresh() {
        std::vector<RowList::Row> rows; keys_.clear(); hotkeys_.clear();
        auto push=[&](RowList::Row row,Key key,int hotkey=-1) { rows.push_back(std::move(row)); keys_.push_back(key); hotkeys_.push_back(hotkey); };
        auto header=[&](const wchar_t* title) { RowList::Row row; row.header=true; row.title=title; push(std::move(row),Key::None); };
        auto toggle=[&](Key key,const wchar_t* title,std::wstring detail,bool on) {
            RowList::Row row; row.title=title; row.detail=std::move(detail); row.toggle=on?1:0; push(std::move(row),key);
        };
        header(L"Startup");
        toggle(Key::StartWithWindows,L"Start with Windows",startupError_.empty()?L"Starts RaceMate when you sign in. No administrator rights needed.":startupError_,app.startWithWindows);
        if(!startupError_.empty()) { rows.back().pill=L"Not changed"; rows.back().pillTone=RowList::Tone::Warning; }
        toggle(Key::StartMinimized,L"Start minimized",L"Starts in the tray; enabled overlays work as usual.",app.startMinimized);
        header(L"Overlays");
        RowList::Row rate; rate.title=L"Background rate"; rate.detail=std::to_wstring(app.rate)+L" Hz  \u00B7  for Race Control events and pit stops; overlays have their own rate, a faster one raises it"; rate.button=L"Choose...";
        push(std::move(rate),Key::RefreshRate);
        toggle(Key::TitleBar,L"Title bar",L"Switches the configurable header of every overlay. Each overlay also has its own header and footer settings.",app.showTitleBar);
        header(L"Opponent fuel estimate");
        RowList::Row fuel; fuel.title=L"Additional laps with fuel saving";
        fuel.detail=L"+"+std::to_wstring(app.opponentFuelExtraLaps)+(app.opponentFuelExtraLaps==1?L" lap":L" laps")+L" for yellow SAVE. Green END uses your normal full-tank range. Shared by Relative and Standings."; fuel.button=L"Choose...";
        push(std::move(fuel),Key::OpponentFuel);
        RowList::Row assumption; assumption.title=L"Full tank assumed at the last observed stop";
        assumption.detail=L"Uses your class, tank capacity, learned consumption and pace. Blank without enough data; opponents' actual fuel is unknown.";
        push(std::move(assumption),Key::None);
        header(L"Pit stop estimate  \u00B7  where you rejoin if you box now");
        {
            const auto& t=app.pitTimes; const double learned=app.pitLane.Loss(); const int samples=app.pitLane.Samples();
            RowList::Row lane; lane.title=L"Pit lane loss";
            if(t.track<0) lane.detail=L"Learned per track from every car's observed pit visit, drive-throughs too. Connect to iRacing to see or set it.";
            else if(t.fixedLane>0) lane.detail=Format(t.fixedLane,1)+L" s, set by you for this track"+(learned>0?L"  \u00B7  learned "+Format(learned,1)+L" s":std::wstring());
            else if(samples) lane.detail=Format(learned,1)+L" s at this track, from "+std::to_wstring(samples)+(samples==1?L" observed visit":L" observed visits");
            else if(learned>0) lane.detail=Format(learned,1)+L" s, learned at this track in an earlier session";
            else lane.detail=L"Not known for this track yet: learned from the first pit visit anyone makes, or set it here.";
            lane.iconButtons={expanded_==LaneRow?CollapseGlyph:ExpandGlyph}; lane.expandHeight=expanded_==LaneRow?48:0;
            push(std::move(lane),Key::PitLane);
            RowList::Row service; service.title=L"Service times"+(t.car.empty()?std::wstring():L"  \u00B7  "+Wide(PlayerDriver()?PlayerDriver()->carName:std::string()));
            const double tires=TireChangeSeconds(); const bool together=SimultaneousService(app.session);
            const std::wstring rules=app.session.ruleSet.empty() || app.session.ruleSet=="None"?L"default rules":Wide(app.session.ruleSet)+L" rules";
            if(t.car.empty()) service.detail=L"Per car and rule set: four tires and the refuel rate. The server's rules decide whether tires follow the fuel.";
            else service.detail=L"Four tires "+(tires>0?Format(tires,1)+L" s"+(t.tires>0?L"":L" (iRacing's value for the class)"):std::wstring(L"not set"))
                +L"  \u00B7  refuel "+(t.rate>0?Format(t.rate,2)+L" l/s":std::wstring(L"measured at your next stop"))
                +L"  \u00B7  "+rules+(together?L": fuel and tires together":L": tires after fuel");
            service.iconButtons={expanded_==ServiceRow?CollapseGlyph:ExpandGlyph}; service.expandHeight=expanded_==ServiceRow?48:0;
            push(std::move(service),Key::PitService);
        }
        header(L"Shortcuts  \u00B7  work in every program");
        const wchar_t* names[]={L"Move overlays",L"Hide all overlays",L"Open RaceMate"};
        for(int i=0;i<HotkeyCount;++i) {
            RowList::Row row; row.title=names[i]; row.detail=HotkeyText(app.hotkeys[i]);
            if(app.hotkeyError.find(HotkeyKeys[i])!=std::wstring::npos) { row.pill=L"In use"; row.pillTone=RowList::Tone::Warning; }
            row.iconButtons={i==expanded_?CollapseGlyph:ExpandGlyph}; row.expandHeight=i==expanded_?48:0;
            push(std::move(row),Key::Hotkey,i);
        }
        header(L"Updates");
        toggle(Key::CheckUpdates,L"Check for updates on startup",L"Shows a newer release here and in the sidebar; never opens a window.",app.checkUpdates);
        push(UpdateVersionRow(),Key::Version);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) {
            if(keys_[i]==Key::Hotkey && hotkeys_[i]==expanded_) return static_cast<int>(i);
            if((keys_[i]==Key::PitLane && expanded_==LaneRow) || (keys_[i]==Key::PitService && expanded_==ServiceRow)) return static_cast<int>(i);
        }
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        editors_.Begin(row);
        if(expanded_==LaneRow || expanded_==ServiceRow) {
            // Empty means learned (lane) or not set; times are per track and per car.
            auto seconds=[](double value){ return value>0?Format(value,1):std::wstring(); };
            const bool known=expanded_==LaneRow?app.pitTimes.track>=0:!app.pitTimes.car.empty();
            if(expanded_==LaneRow) { editors_.Label(L"Fixed value",0,10,84); editors_.Edit(LaneEditId,seconds(app.pitTimes.fixedLane),88,10,64); editors_.Label(L"s  \u00B7  empty: learned",158,10,0); }
            else {
                editors_.Label(L"Four tires",0,10,72); editors_.Edit(TiresEditId,seconds(app.pitTimes.tires),76,10,64); editors_.Label(L"s",146,10,16);
                editors_.Label(L"Refuel",186,10,52); editors_.Edit(RateEditId,app.pitTimes.rate>0?Format(app.pitTimes.rate,2):L"",242,10,64); editors_.Label(L"l/s",312,10,30);
            }
            if(!known) for(int id:{LaneEditId,TiresEditId,RateEditId}) if(HWND edit=editors_.Get(id)) EnableWindow(edit,FALSE);
            editors_.Position(); return;
        }
        const HWND field=editors_.Add(HOTKEY_CLASSW,L"",WS_TABSTOP|WS_BORDER,HotkeyId,0,10,200,26);
        // A shortcut always needs Ctrl or Alt so plain typing is never captured.
        SendMessageW(field,HKM_SETRULES,HKCOMB_NONE|HKCOMB_S,MAKELPARAM(HOTKEYF_CONTROL|HOTKEYF_ALT,0));
        SendMessageW(field,HKM_SETHOTKEY,app.hotkeys[expanded_],0); SetWindowSubclass(field,HotkeyFieldProc,1,0);
        editors_.Button(HotkeyResetId,L"Reset",212,9,90);
        editors_.Label(L"Click the field and press a combination with Ctrl or Alt.",316,10,0);
        editors_.Position();
    }
    // Keeps a typed value; empty or invalid removes it.
    void ApplyPitTime(int id) {
        double value=-1; const auto text=editors_.Text(id);
        if(!text.empty()) { std::wstring dot=text; std::replace(dot.begin(),dot.end(),L',',L'.'); wchar_t* end=nullptr; const double v=wcstod(dot.c_str(),&end); if(end!=dot.c_str() && std::isfinite(v) && v>0 && v<600) value=v; }
        auto& t=app.pitTimes;
        if(id==LaneEditId && t.track>=0) { t.fixedLane=value; WriteNumber(L"PitStop",L"LaneFixed "+std::to_wstring(t.track),value); }
        if(id==TiresEditId && !t.car.empty()) { t.tires=value; WriteNumber(L"PitStop",L"Tires "+t.car,value); }
        if(id==RateEditId && !t.car.empty()) { t.rate=value; WriteNumber(L"PitStop",L"FuelRate "+t.car,value); }
        app.pitExitVersion=~0u; UpdateOverlays();
    }
    void Expand(int hotkey) {
        expanded_=expanded_==hotkey?-1:hotkey;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ChooseRate() {
        std::vector<std::wstring> items; int checked=-1;
        for(int i=0;i<static_cast<int>(std::size(Rates));++i) { items.push_back(std::to_wstring(Rates[i])+L" Hz"); if(Rates[i]==app.rate) checked=i; }
        const int choice=ChooseFromMenu(items,checked); if(choice<0) return;
        app.rate=Rates[choice]; Timer(); Save(); Refresh(); UpdateStatus();
    }
    bool OnCommand(int id,int code,HWND control) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==HotkeyId) {
            // Modifier-only intermediate states have no key yet; registration follows when the field loses focus.
            const WORD hotkey=static_cast<WORD>(SendMessageW(control,HKM_GETHOTKEY,0,0));
            if(code==EN_CHANGE && expanded_>=0 && LOBYTE(hotkey)) { app.hotkeys[expanded_]=hotkey; Save(); Refresh(); }
            return true;
        }
        if(id==LaneEditId || id==TiresEditId || id==RateEditId) {
            if(code==EN_KILLFOCUS) { ApplyPitTime(id); Refresh(); }
            return true;
        }
        if(id==HotkeyResetId) {
            // Back to the default shortcut.
            if(code==BN_CLICKED && expanded_>=0) { app.hotkeys[expanded_]=DefaultHotkeys[expanded_]; SendMessageW(editors_.Get(HotkeyId),HKM_SETHOTKEY,app.hotkeys[expanded_],0); RegisterHotkeys(); Save(); Refresh(); UpdateStatus(); }
            return true;
        }
        if(id!=ListId) return false;
        if(code==RowList::kLayoutChanged) { editors_.Position(); return true; }
        const int row=list_.NotifiedRow();
        if(row<0 || static_cast<size_t>(row)>=keys_.size()) return true;
        const bool on=list_.Rows()[row].toggle==1, pressed=code==RowList::kButton || code==RowList::kActivated;
        switch(keys_[row]) {
        case Key::StartWithWindows:
            if(code==RowList::kToggled) { startupError_=SetStartWithWindows(on)?L"":L"Windows startup could not be changed; the previous setting is kept."; Refresh(); }
            break;
        case Key::StartMinimized: if(code==RowList::kToggled) { SetStartMinimized(on); Refresh(); } break;
        case Key::RefreshRate: if(pressed) ChooseRate(); break;
        case Key::TitleBar: if(code==RowList::kToggled) { SetShowTitleBar(on); Refresh(); } break;
        case Key::OpponentFuel:
            if(pressed) {
                std::vector<std::wstring> items; for(int i=0;i<=10;++i) items.push_back(L"+"+std::to_wstring(i)+(i==1?L" lap":L" laps"));
                const int choice=ChooseFromMenu(items,app.opponentFuelExtraLaps);
                if(choice>=0) { app.opponentFuelExtraLaps=choice; SettingChanged(); Refresh(); }
            }
            break;
        case Key::Hotkey: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(hotkeys_[row]); break;
        case Key::PitLane: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(LaneRow); break;
        case Key::PitService: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(ServiceRow); break;
        case Key::CheckUpdates: if(code==RowList::kToggled) { SetCheckUpdates(on); Refresh(); } break;
        case Key::Version:
            if(code==RowList::kIconButton) UpdateChecker::OpenReleasePage(updates.release.releasePageUrl);
            else if(code==RowList::kButton) UpdateVersionButton();
            break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> hotkeys_;
    int expanded_=-1; std::wstring startupError_;
};
HWND CreateGeneralPage(HWND parent) { return PageWindow::Show(std::make_unique<GeneralPage>(),app.instance,parent,560,360); }
