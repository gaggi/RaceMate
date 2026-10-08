// Settings page: startup, how overlays update, keyboard shortcuts, updates and quitting.
class GeneralPage : public PageWindow {
public:
    GeneralPage() : PageWindow(ui.heading,ui.text) {}
private:
    enum class Key { None, StartWithWindows, StartMinimized, RefreshRate, TitleBar, Hotkey, CheckUpdates, Version, Quit };
    static constexpr int ListId=100, HotkeyId=200, HotkeyOffId=201;
    static constexpr wchar_t ExpandGlyph=L'\uE70D', CollapseGlyph=L'\uE70E';
    static constexpr int Rates[]={2,5,10,20};

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
        RowList::Row rate; rate.title=L"Refresh rate"; rate.detail=std::to_wstring(app.rate)+L" Hz  \u00B7  how often overlays update while iRacing runs"; rate.button=L"Choose...";
        push(std::move(rate),Key::RefreshRate);
        toggle(Key::TitleBar,L"Title bar",L"Shows a title above Fuel Calculator, Pit Limiter and Weather, and switches the Relative and Standings headers.",app.showTitleBar);
        header(L"Shortcuts  \u00B7  work in every program");
        const wchar_t* names[]={L"Move overlays",L"Hide all overlays",L"Open RaceMate"};
        for(int i=0;i<HotkeyCount;++i) {
            RowList::Row row; row.title=names[i]; row.detail=HotkeyText(app.hotkeys[i]);
            if(app.hotkeyError.find(HotkeyKeys[i])!=std::wstring::npos) { row.pill=L"In use"; row.pillTone=RowList::Tone::Warning; }
            row.iconButtons={i==expanded_?CollapseGlyph:ExpandGlyph}; row.expandHeight=i==expanded_?48:0;
            push(std::move(row),Key::Hotkey,i);
        }
        header(L"Updates");
        toggle(Key::CheckUpdates,L"Check for updates on startup",L"Looks up the latest release on GitHub; never interrupts a running session.",app.checkUpdates);
        RowList::Row version; version.title=L"Version " RACEMATE_VERSION; version.detail=updates.status.empty()?L"Not checked yet":updates.status;
        version.button=UpdateAvailable()?L"Install...":L"Check now"; version.buttonEnabled=!updates.check.Running() && !updates.install.Running();
        if(UpdateAvailable()) { version.pill=L"Update"; version.pillTone=RowList::Tone::Active; }
        push(std::move(version),Key::Version);
        header(L"RaceMate");
        RowList::Row quit; quit.title=L"Quit RaceMate"; quit.detail=L"Closing this window keeps RaceMate running in the tray."; quit.button=L"Quit";
        push(std::move(quit),Key::Quit);
        list_.SetRows(std::move(rows));
    }
    int ExpandedRow() const {
        for(size_t i=0;i<keys_.size();++i) if(keys_[i]==Key::Hotkey && hotkeys_[i]==expanded_) return static_cast<int>(i);
        return -1;
    }
    void BuildEditors() {
        editors_.Clear();
        const int row=ExpandedRow(); if(row<0) return;
        editors_.Begin(row);
        const HWND field=editors_.Add(HOTKEY_CLASSW,L"",WS_TABSTOP|WS_BORDER,HotkeyId,0,10,200,26);
        // A shortcut always needs Ctrl or Alt so plain typing is never captured.
        SendMessageW(field,HKM_SETRULES,HKCOMB_NONE|HKCOMB_S,MAKELPARAM(HOTKEYF_CONTROL|HOTKEYF_ALT,0));
        SendMessageW(field,HKM_SETHOTKEY,app.hotkeys[expanded_],0); SetWindowSubclass(field,HotkeyFieldProc,1,0);
        editors_.Button(HotkeyOffId,L"Turn off",212,9,90);
        editors_.Label(L"Click the field and press a combination with Ctrl or Alt.",316,10,0);
        editors_.Position();
    }
    void Expand(int hotkey) {
        expanded_=expanded_==hotkey?-1:hotkey;
        Refresh(); BuildEditors();
        if(const int row=ExpandedRow(); row>=0) list_.ScrollIntoView(row);
    }
    void ChooseRate() {
        std::vector<std::wstring> items; int checked=-1;
        for(int i=0;i<4;++i) { items.push_back(std::to_wstring(Rates[i])+L" Hz"); if(Rates[i]==app.rate) checked=i; }
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
        if(id==HotkeyOffId) {
            if(code==BN_CLICKED && expanded_>=0) { app.hotkeys[expanded_]=0; SendMessageW(editors_.Get(HotkeyId),HKM_SETHOTKEY,0,0); RegisterHotkeys(); Save(); Refresh(); UpdateStatus(); }
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
        case Key::Hotkey: if(code==RowList::kIconButton || code==RowList::kActivated) Expand(hotkeys_[row]); break;
        case Key::CheckUpdates: if(code==RowList::kToggled) { SetCheckUpdates(on); Refresh(); } break;
        case Key::Version: if(pressed) { if(UpdateAvailable()) OfferUpdate(); else StartUpdateCheck(true); } break;
        case Key::Quit: if(pressed) DestroyWindow(app.main); break;
        default: break;
        }
        return true;
    }

    RowList list_; RowEditors editors_;
    std::vector<Key> keys_; std::vector<int> hotkeys_;
    int expanded_=-1; std::wstring startupError_;
};
HWND CreateGeneralPage(HWND parent) { return PageWindow::Show(std::make_unique<GeneralPage>(),app.instance,parent,560,360); }
