// Settings page: startup, how overlays update, keyboard shortcuts and updates.
class GeneralPage : public PageWindow {
public:
    GeneralPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, StartWithWindows, StartMinimized, TitleBar, FuelReserve, OpponentFuel, PitLane, PitService, Hotkey, CheckUpdates, Version, SnapToGrid, SnapToOverlays };
    static constexpr int ListId=100, HotkeyId=200, LaneEditId=202, TiresEditId=203, RateEditId=204, GridEditId=205;
    static constexpr int LaneRow=10, ServiceRow=11, GridRow=12;
    // Below 5 Hz short offtracks and pit timing get lost.

    void OnCreate() override {
        list_.Create(Instance(),Handle(),ListId,HeadingFont(),TextFont());
        editors_.Attach(list_,Instance(),TextFont());
        // Records a new shortcut while a shortcut's button waits for one: a hotkey field out of sight that takes the
        // keys. A shortcut needs Ctrl or Alt so plain typing is never captured; an F key may stand alone (CaptureProc).
        capture_=CreateWindowExW(0,HOTKEY_CLASSW,L"",WS_CHILD|WS_VISIBLE,-50,-50,1,1,Handle(),reinterpret_cast<HMENU>(static_cast<INT_PTR>(HotkeyId)),Instance(),nullptr);
        SendMessageW(capture_,HKM_SETRULES,HKCOMB_NONE|HKCOMB_S,MAKELPARAM(HOTKEYF_CONTROL|HOTKEYF_ALT,0));
        SetWindowSubclass(capture_,CaptureProc,1,reinterpret_cast<DWORD_PTR>(this));
        Refresh();
    }
    // Esc cancels, Backspace or Delete turns the shortcut off; leaving the field ends the wait.
    static LRESULT CALLBACK CaptureProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto* page=reinterpret_cast<GeneralPage*>(data);
        const bool modified=GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_MENU)<0;
        if((m==WM_KEYDOWN || m==WM_SYSKEYDOWN) && !modified && (wp==VK_ESCAPE || wp==VK_BACK || wp==VK_DELETE)) {
            if(wp!=VK_ESCAPE && page->capturing_>=0) { app.hotkeys[page->capturing_]=0; Save(); }
            page->EndCapture(); return 0;
        }
        // F1 to F24 alone or with Shift, which the field's rules would turn into Ctrl+Alt+F: no text is typed with them.
        // F12 stays the debugger's; Windows refuses it as a shortcut.
        if((m==WM_KEYDOWN || m==WM_SYSKEYDOWN) && !modified && wp>=VK_F1 && wp<=VK_F24 && wp!=VK_F12 && page->capturing_>=0) {
            app.hotkeys[page->capturing_]=static_cast<WORD>(wp|(GetKeyState(VK_SHIFT)<0?HOTKEYF_SHIFT<<8:0)); Save();
            page->EndCapture(); return 0;
        }
        if(m==WM_GETDLGCODE) return DLGC_WANTALLKEYS;
        if(m==WM_SETFOCUS && app.hotkeysEnabled) UnregisterHotkeys();
        if(m==WM_KILLFOCUS) { RegisterHotkeys(); if(page->capturing_>=0) { page->capturing_=-1; PostMessageW(app.main,RefreshUiMessage,0,0); } }
        if(m==WM_NCDESTROY) RemoveWindowSubclass(w,CaptureProc,id);
        return DefSubclassProc(w,m,wp,lp);
    }
    void StartCapture(int hotkey) {
        capturing_=hotkey; SendMessageW(capture_,HKM_SETHOTKEY,0,0);
        Refresh(); SetFocus(capture_);
    }
    void EndCapture() {
        capturing_=-1; SetFocus(list_.Handle()); // Registers the shortcuts again.
        Refresh(); UpdateStatus();
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
        toggle(Key::TitleBar,L"Headers on all overlays",L"Switches the header of every overlay at once; each overlay's page also sets its own header and footer.",app.showTitleBar);
        toggle(Key::SnapToGrid,L"Snap to grid while moving",L"Every "+std::to_wstring(app.gridSize)+L" px from the corner of the screen. Hold Alt while dragging to place freely.",app.snapToGrid);
        rows.back().iconButtons={expanded_==GridRow?Glyph::Collapse:Glyph::Expand}; rows.back().expandHeight=expanded_==GridRow?48:0;
        toggle(Key::SnapToOverlays,L"Snap to other overlays",L"Edges and centre lines line up with other overlays and the screen; a magenta line shows where.",app.snapToOverlays);
        header(L"Fuel strategy");
        RowList::Row reserve; reserve.title=L"Reserve at the finish"; reserve.button=L"Choose...";
        reserve.detail=ReserveText()+L"  \u00B7  Kept in refuel, needed fuel, the pit window and the targets to the finish; spare fuel counts above it.";
        push(std::move(reserve),Key::FuelReserve);
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
            lane.iconButtons={expanded_==LaneRow?Glyph::Collapse:Glyph::Expand}; lane.expandHeight=expanded_==LaneRow?48:0;
            push(std::move(lane),Key::PitLane);
            RowList::Row service; service.title=L"Service times"+(t.car.empty()?std::wstring():L"  \u00B7  "+Wide(PlayerDriver()?PlayerDriver()->carName:std::string()));
            const double tires=TireChangeSeconds(); const bool together=SimultaneousService(app.session);
            const std::wstring rules=app.session.ruleSet.empty() || app.session.ruleSet=="None"?L"default rules":Wide(app.session.ruleSet)+L" rules";
            if(t.car.empty()) service.detail=L"Per car and rule set: four tires and the refuel rate. The server's rules decide whether tires follow the fuel.";
            else service.detail=L"Four tires "+(tires>0?Format(tires,1)+L" s"+(t.tires>0?L"":L" (iRacing's value for the class)"):std::wstring(L"not set"))
                +L"  \u00B7  refuel "+Format(RefuelRateLitres(),2)+L" l/s"+(t.rate>0?L"":L" (assumed until measured at your stop)")
                +L"  \u00B7  "+rules+(together?L": fuel and tires together":L": tires after fuel");
            service.iconButtons={expanded_==ServiceRow?Glyph::Collapse:Glyph::Expand}; service.expandHeight=expanded_==ServiceRow?48:0;
            push(std::move(service),Key::PitService);
        }
        header(L"Shortcuts  \u00B7  work in every program, iRacing too");
        // Click the shortcut, then press the new one; the reset arrow appears where it differs from the default.
        const wchar_t* names[]={L"Move overlays",L"Hide all overlays",L"Open settings",L"Previous event in Race Control",L"Next event in Race Control",L"Pause OBS output"};
        const wchar_t* details[]={L"Drag the overlays with the mouse; press it again when done.",L"Hides every overlay on your screen, and shows them again; OBS keeps its output.",
            L"Brings this window to the front.",L"Replays the event before the selected one; opens Race Control if it is closed.",L"Replays the event after the selected one.",L"Stops sending to OBS, and starts again; your screen is unchanged."};
        static_assert(std::size(names)==HotkeyCount && std::size(details)==HotkeyCount);
        for(int i=0;i<HotkeyCount;++i) {
            RowList::Row row; row.title=names[i]; row.detail=details[i];
            row.button=i==capturing_?L"Press a shortcut \u2026":HotkeyText(app.hotkeys[i]);
            // A shortcut takes its keys from every program: an F key alone no longer reaches iRacing.
            if(LOBYTE(app.hotkeys[i]) && !(HIBYTE(app.hotkeys[i])&(HOTKEYF_CONTROL|HOTKEYF_ALT))) row.detail+=L"  \u00B7  iRacing no longer gets "+HotkeyText(app.hotkeys[i]);
            if(i==capturing_) row.detail=L"With Ctrl or Alt, or an F key alone  \u00B7  Esc cancels  \u00B7  Backspace turns it off";
            if(app.hotkeyError.find(HotkeyKeys[i])!=std::wstring::npos) { row.pill=L"In use"; row.pillTone=RowList::Tone::Warning; }
            row.iconButtons={app.hotkeys[i]!=DefaultHotkeys[i]?Glyph::Reset:wchar_t{0}};
            push(std::move(row),Key::Hotkey,i);
        }
        header(L"Updates");
        toggle(Key::CheckUpdates,L"Check for updates on startup",L"Shows a newer release here and in the sidebar; never opens a window.",app.checkUpdates);
        push(UpdateVersionRow(),Key::Version);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) {
            if((keys_[i]==Key::PitLane && expanded_==LaneRow) || (keys_[i]==Key::PitService && expanded_==ServiceRow) || (keys_[i]==Key::SnapToGrid && expanded_==GridRow)) return static_cast<int>(i);
        }
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        editors_.Begin(row);
        if(expanded_==GridRow) {
            editors_.Label(L"Grid size",0,10,84); editors_.Edit(GridEditId,std::to_wstring(app.gridSize),88,10,64); editors_.Label(L"px  \u00B7  2 to 100",158,10,0);
            editors_.Position();
        }
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
            editors_.Position();
        }
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
    bool OnCommand(int id,int code,HWND control) override {
        if(id==PageRefreshId) { Refresh(); return true; }
        if(id==HotkeyId) {
            // Modifier-only intermediate states have no key yet; the first full combination is the new shortcut.
            const WORD hotkey=static_cast<WORD>(SendMessageW(control,HKM_GETHOTKEY,0,0));
            if(code==EN_CHANGE && capturing_>=0 && LOBYTE(hotkey)) { app.hotkeys[capturing_]=hotkey; Save(); EndCapture(); }
            return true;
        }
        if(id==GridEditId) {
            if(code==EN_KILLFOCUS) {
                const int size=_wtoi(editors_.Text(GridEditId).c_str());
                if(size>0) { app.gridSize=std::clamp(size,2,100); Save(); }
                Refresh(); BuildEditors();
            }
            return true;
        }
        if(id==LaneEditId || id==TiresEditId || id==RateEditId) {
            if(code==EN_KILLFOCUS) { ApplyPitTime(id); Refresh(); }
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
        case Key::TitleBar: if(code==RowList::kToggled) { SetShowTitleBar(on); Refresh(); } break;
        case Key::FuelReserve:
            if(pressed) {
                // In litres or in laps; a choice stores the amount in tenths and its unit.
                static constexpr struct { int tenths; bool laps; } choices[]={{0,false},{5,false},{10,false},{20,false},{30,false},{50,false},{5,true},{10,true},{20,true}};
                std::vector<std::wstring> items; int checked=-1;
                for(size_t i=0;i<std::size(choices);++i) {
                    const auto& c=choices[i];
                    items.push_back(!c.tenths?std::wstring(L"None"):Format(c.tenths/10.0,1)+(c.laps?(c.tenths==10?L" lap":L" laps"):L" L"));
                    if(c.tenths==app.reserveTenths && (c.laps==app.reserveInLaps || !c.tenths)) checked=static_cast<int>(i);
                }
                const int choice=ChooseFromMenu(items,checked);
                if(choice>=0) { app.reserveTenths=choices[choice].tenths; app.reserveInLaps=choices[choice].laps; InvalidateModel(); SettingChanged(); Refresh(); }
            }
            break;
        case Key::OpponentFuel:
            if(pressed) {
                std::vector<std::wstring> items; for(int i=0;i<=10;++i) items.push_back(L"+"+std::to_wstring(i)+(i==1?L" lap":L" laps"));
                const int choice=ChooseFromMenu(items,app.opponentFuelExtraLaps);
                if(choice>=0) { app.opponentFuelExtraLaps=choice; SettingChanged(); Refresh(); }
            }
            break;
        case Key::Hotkey:
            if(code==RowList::kIconButton) { app.hotkeys[hotkeys_[row]]=DefaultHotkeys[hotkeys_[row]]; RegisterHotkeys(); Save(); Refresh(); UpdateStatus(); }
            else if(pressed) StartCapture(hotkeys_[row]);
            break;
        case Key::PitLane: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(LaneRow); break;
        case Key::PitService: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(ServiceRow); break;
        case Key::CheckUpdates: if(code==RowList::kToggled) { SetCheckUpdates(on); Refresh(); } break;
        case Key::SnapToGrid:
            if(code==RowList::kToggled) { app.snapToGrid=on; Save(); Refresh(); }
            else if(code==RowList::kIconButton || code==RowList::kActivated) Expand(GridRow);
            break;
        case Key::SnapToOverlays: if(code==RowList::kToggled) { app.snapToOverlays=on; Save(); Refresh(); } break;
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
    int expanded_=-1, capturing_=-1; HWND capture_=nullptr; std::wstring startupError_;
};
HWND CreateGeneralPage(HWND parent) { return PageWindow::Show(std::make_unique<GeneralPage>(),app.instance,parent,560,360); }
