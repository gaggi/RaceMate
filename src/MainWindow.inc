// Main window in the shared LaunchMate style (docs/ui-design.md there): sidebar,
// status banner, page title with a one-line hint, and the current page below.
enum class Page { Overlays, Settings, Overlay, RaceControl };
constexpr int NavOverlaysId=900, NavRaceControlId=901, NavSettingsId=902, StatusButtonId=903, BackId=904;
// Sent to the current page as WM_COMMAND when state changes outside it (shortcuts, tray, banner).
constexpr int PageRefreshId=905;
struct MainUi {
    NavBar nav; StatusPanel status; ScrollHost host;
    HFONT text=nullptr, heading=nullptr;
    HWND title=nullptr, hint=nullptr, back=nullptr;
    Page page=Page::Overlays; Overlay* overlay=nullptr; // The overlay whose own page is open.
} ui;
HWND CreateOverlaysPage(HWND parent);
HWND CreateGeneralPage(HWND parent);
HWND CreateTablePage(HWND parent,Overlay& overlay);
HWND CreateElementsPage(HWND parent,Overlay& overlay);
HWND CreateTrackMapPage(HWND parent,Overlay& overlay);
HWND CreateRaceControlPage(HWND parent);
void RefreshPage() { if(ui.host.Content()) SendMessageW(ui.host.Content(),WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0); }
std::wstring HotkeyText(WORD hotkey) {
    if(!LOBYTE(hotkey)) return L"Off";
    const BYTE modifiers=HIBYTE(hotkey); std::wstring text;
    if(modifiers&HOTKEYF_CONTROL) text+=L"Ctrl+";
    if(modifiers&HOTKEYF_ALT) text+=L"Alt+";
    if(modifiers&HOTKEYF_SHIFT) text+=L"Shift+";
    // The arrows as arrows: Windows names them in its own language ("NACH-LINKS"), the rest of the window is English.
    switch(LOBYTE(hotkey)) { case VK_LEFT: return text+L"←"; case VK_RIGHT: return text+L"→"; case VK_UP: return text+L"↑"; case VK_DOWN: return text+L"↓"; }
    LONG key=static_cast<LONG>(MapVirtualKeyW(LOBYTE(hotkey),MAPVK_VK_TO_VSC))<<16; if(modifiers&HOTKEYF_EXT) key|=1<<24;
    wchar_t name[64]{}; GetKeyNameTextW(key,name,64);
    return text+(name[0]?std::wstring(name):std::to_wstring(LOBYTE(hotkey)));
}
void UpdateStatus() {
    if(!ui.status.Handle()) return;
    using Tone=StatusPanel::Tone;
    Tone tone=Tone::Neutral; std::wstring title,detail,button;
    if(app.edit) { tone=Tone::Busy; title=L"Moving overlays"; detail=L"Drag them where you want them. Other programs don't get mouse clicks on them meanwhile."; button=L"Done"; }
    else if(app.preview) { tone=Tone::Busy; title=L"Preview with sample data"; detail=L"No live telemetry is read while the preview runs."; button=L"Stop preview"; }
    else if(app.frame.connected) {
        tone=Tone::Active; title=L"Connected to iRacing";
        detail=Wide(app.session.track); if(!app.session.type.empty()) detail+=L"  \u00B7  "+Wide(app.session.type);
        detail+=L"  \u00B7  "+std::to_wstring(app.frame.tickRate>0?app.frame.tickRate:60)+L" Hz"; button=L"Move overlays";
    }
    else { title=L"Waiting for iRacing"; detail=L"RaceMate connects as soon as a session is loaded."; button=L"Preview"; }
    std::wstring problems;
    if(!app.trayAvailable && !app.diagnostics) problems=L"No tray icon: closing this window exits RaceMate";
    if(!app.hotkeyError.empty()) problems+=(problems.empty()?L"":L"  \u00B7  ")+std::wstring(L"Shortcut in use by another program: ")+app.hotkeyError;
    if(!problems.empty()) { if(tone==Tone::Neutral) tone=Tone::Busy; detail=problems; }
    ui.status.SetState(tone,title,detail,button,true);
}
void StatusButton() {
    if(app.edit || (!app.preview && app.frame.connected)) ToggleEdit();
    else TogglePreview();
}
int Dip(int value) { return MulDiv(value,GetDpiForWindow(app.main),96); }
void CreateUiFonts() {
    const int dpi=GetDpiForWindow(app.main);
    if(ui.text) DeleteObject(ui.text);
    if(ui.heading) DeleteObject(ui.heading);
    ui.text=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.heading=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
}
// Sizes in DIPs, see the layout table in LaunchMate's docs/ui-design.md.
void LayoutMain() {
    if(!ui.title) return;
    RECT client{}; GetClientRect(app.main,&client);
    const int width=MulDiv(client.right,96,GetDpiForWindow(app.main)), height=MulDiv(client.bottom,96,GetDpiForWindow(app.main));
    auto place=[](HWND window,int x,int y,int w,int h) { if(window) MoveWindow(window,Dip(x),Dip(y),Dip(std::max(0,w)),Dip(std::max(0,h)),TRUE); };
    constexpr int nav=184, margin=24;
    const int left=nav+margin, content=width-left-margin;
    place(ui.nav.Handle(),0,0,nav,height);
    place(ui.status.Handle(),left,20,content,56);
    // A sub-page shows its way back left of the title.
    const int titleLeft=IsWindowVisible(ui.back)?left+124:left;
    place(ui.back,left,90,112,28);
    place(ui.title,titleLeft,92,content-(titleLeft-left),24);
    place(ui.hint,left,116,content,20);
    place(ui.host.Handle(),left,148,content,height-148-20);
}
void ShowPage(Page page,Overlay* overlay=nullptr) {
    ui.page=page; if(overlay) ui.overlay=overlay;
    const wchar_t* hints[]={L"Choose how many cars it shows, its columns and its header and footer.",
        L"Class leaders and the cars around you: rows, columns, header and footer.",
        L"Choose and order what it shows, and which laps count toward consumption.",
        L"The pit limiter, your way through the pit lane to your box with where to brake, and what you changed in the car since the setup.",
        L"Choose and order what it shows.",L"The official iRacing track map with every car: layout, markers, pit exit and what is drawn.",
        L"Bars at the screen edges that light up while a car is beside you.",
        L"The cars around yours from above, with the lanes beside you estimated from the spotter.",
        L"The cars of faster classes close behind you, each with its gap."};
    if(page==Page::Overlay) {
        SetWindowTextW(ui.title,Name(*ui.overlay).c_str()); SetWindowTextW(ui.hint,hints[ui.overlay->kind]);
    } else if(page==Page::RaceControl) {
        SetWindowTextW(ui.title,L"Race Control");
        SetWindowTextW(ui.hint,L"Full standings and an event feed in their own window. Events are recorded even while it is closed.");
    } else {
        SetWindowTextW(ui.title,page==Page::Overlays?L"Overlays":L"Settings");
        SetWindowTextW(ui.hint,page==Page::Overlays?L"Turn overlays on or off, add more of a kind, and set their size and place. Open one to choose what it shows."
            :L"Startup, update rates, fuel and pit stop estimates, shortcuts and updates.");
    }
    ShowWindow(ui.back,page==Page::Overlay?SW_SHOW:SW_HIDE);
    UpdateOverlays(); // The edited overlay's border follows the page.
    ui.nav.SetSelected(page==Page::Settings?NavSettingsId:(page==Page::RaceControl?NavRaceControlId:NavOverlaysId));
    const HWND host=ui.host.Handle();
    ui.host.SetContent(page==Page::Overlays?CreateOverlaysPage(host):page==Page::RaceControl?CreateRaceControlPage(host):(page==Page::Settings?CreateGeneralPage(host):(IsTable(ui.overlay->kind)?CreateTablePage(host,*ui.overlay):ui.overlay->kind==TrackMapKind?CreateTrackMapPage(host,*ui.overlay):CreateElementsPage(host,*ui.overlay))));
    LayoutMain();
}
void OpenOverlay(Overlay& o) { ShowPage(Page::Overlay,&o); }
bool Editing(const Overlay& o) { return ui.page==Page::Overlay && ui.overlay==&o && IsWindowVisible(app.main) && !IsIconic(app.main); }
void ShowRaceControlPage() { ShowSettings(); ShowPage(Page::RaceControl); }
void CreateMainUi() {
    CreateUiFonts();
    ui.nav.Create(app.instance,app.main,L"RaceMate",L"Version " RACEMATE_VERSION,{
        {L'\uE81E',L"Overlays",NavOverlaysId,true},
        {L'\uE7C1',L"Race Control",NavRaceControlId,true},
        {L'\uE713',L"Settings",NavSettingsId,true}},ui.heading,ui.text);
    ui.status.Create(app.instance,app.main,StatusButtonId,ui.heading,ui.text);
    ui.title=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE|SS_ENDELLIPSIS,0,0,10,10,app.main,nullptr,app.instance,nullptr);
    ui.hint=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE|SS_ENDELLIPSIS,0,0,10,10,app.main,nullptr,app.instance,nullptr);
    SendMessageW(ui.title,WM_SETFONT,reinterpret_cast<WPARAM>(ui.heading),TRUE);
    SendMessageW(ui.hint,WM_SETFONT,reinterpret_cast<WPARAM>(ui.text),TRUE);
    ui.back=CreateWindowExW(0,L"BUTTON",L"\u2190  Overlays",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,0,0,10,10,app.main,reinterpret_cast<HMENU>(static_cast<INT_PTR>(BackId)),app.instance,nullptr);
    SendMessageW(ui.back,WM_SETFONT,reinterpret_cast<WPARAM>(ui.text),TRUE);
    ui.host.Create(app.instance,app.main); ShowWindow(ui.host.Handle(),SW_SHOW);
    UpdateStatus(); ShowPage(Page::Overlays);
}
void ApplyUiDpi() {
    CreateUiFonts();
    ui.nav.SetFonts(ui.heading,ui.text); ui.status.SetFonts(ui.heading,ui.text);
    SendMessageW(ui.title,WM_SETFONT,reinterpret_cast<WPARAM>(ui.heading),TRUE);
    SendMessageW(ui.hint,WM_SETFONT,reinterpret_cast<WPARAM>(ui.text),TRUE);
    SendMessageW(ui.back,WM_SETFONT,reinterpret_cast<WPARAM>(ui.text),TRUE);
    ShowPage(ui.page);
}
// The saved normal position is used only while it is still on a monitor.
void RestoreWindowPlacement(int show) {
    WINDOWPLACEMENT placement{sizeof(placement)}; GetWindowPlacement(app.main,&placement);
    const RECT& saved=app.windowRect;
    if(saved.right-saved.left>=200 && saved.bottom-saved.top>=200 && MonitorFromRect(&saved,MONITOR_DEFAULTTONULL)) placement.rcNormalPosition=saved;
    placement.showCmd=show==SW_HIDE?SW_HIDE:(app.windowMaximized?SW_SHOWMAXIMIZED:show);
    SetWindowPlacement(app.main,&placement);
}
void RememberWindowPlacement() {
    WINDOWPLACEMENT placement{sizeof(placement)};
    if(!app.main || !GetWindowPlacement(app.main,&placement)) return;
    app.windowRect=placement.rcNormalPosition; app.windowMaximized=placement.showCmd==SW_SHOWMAXIMIZED || (placement.flags&WPF_RESTORETOMAXIMIZED);
}
// Setting changes shared by the pages and the diagnostics.
void SettingChanged() { Save(); UpdateOverlays(); }
// A further instance of a kind with default settings.
Overlay& NewInstance(int kind) {
    auto& o=AddInstance(kind,InstanceCount(kind)+1);
    ReadInstance(o); // A removed instance's section is gone, so this gives the defaults.
    o.place.enabled=true;
    CreateOverlayWindow(o); SettingChanged(); return o;
}
// Makes another layout the active one: the current one stays saved as it is, the other's overlays take
// their places and settings. Instances the other layout lacks go, ones it has more of are created.
void SwitchLayout(int id) {
    if(app.layoutIdle && id==app.layout) { app.layoutIdle=false; UpdateOverlays(); RefreshPage(); return; }
    app.layoutIdle=false;
    if(id==app.layout || std::none_of(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;})) return;
    Save(); app.layout=id;
    LoadOverlays();
    for(auto& o:app.overlays) if(o.window) { Fit(o); o.due=0; }
    SettingChanged();
    // An open overlay page is rebuilt for the new settings, or closed when its overlay is gone.
    if(ui.page==Page::Overlay) {
        const bool kept=std::any_of(app.overlays.begin(),app.overlays.end(),[](const Overlay& o){return &o==ui.overlay;});
        if(kept) ShowPage(Page::Overlay,ui.overlay); else ShowPage(Page::Overlays);
    } else RefreshPage();
}
// Every section of a layout's overlays, and what Save() remembers of them.
void DeleteLayoutSections(int id) {
    const auto prefix=LayoutPrefix(id); if(prefix.empty()) return;
    std::vector<wchar_t> buffer(65536); GetPrivateProfileSectionNamesW(buffer.data(),static_cast<DWORD>(buffer.size()),app.config.c_str());
    std::vector<std::wstring> names;
    for(const wchar_t* name=buffer.data();*name;name+=wcslen(name)+1) if(std::wstring_view(name).starts_with(prefix)) names.emplace_back(name);
    for(const auto& name:names) DeleteSection(name);
}
// A new layout at the end of the list: a copy of the active one, or default overlays.
void AddLayout(bool copy) {
    int id=1; for(const auto& l:app.layouts) id=std::max(id,l.id+1);
    DeleteLayoutSections(id); // Nothing of an earlier layout with this number may come back.
    app.layouts.push_back({id,L"Layout "+std::to_wstring(app.layouts.size()+1),AlwaysCondition});
    if(copy) { Save(); app.layout=id; SettingChanged(); RefreshPage(); } // Save() writes the overlays as they are under the new names.
    else SwitchLayout(id);
}
// Removes a layout with all its overlays' settings; the first layout stays.
void RemoveLayout(int id) {
    if(id==1) return;
    const auto at=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;}); if(at==app.layouts.end()) return;
    if(app.layout==id) SwitchLayout(1);
    DeleteLayoutSections(id);
    DeleteSection(L"Layout "+std::to_wstring(id));
    app.layouts.erase(std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;}));
    Save();
}
bool LayoutApplies(int condition,int situation) {
    switch(condition) {
    case InCarCondition: return situation==InCarSituation;
    case OutOfCarCondition: return situation==SpottingSituation || situation==ReplaySituation;
    case GarageCondition: return situation==GarageSituation;
    default: return true;
    }
}
// When the situation changes and holds for a second, the first layout in the list whose condition applies
// becomes active; with none, the overlays hide (a single In car layout shows only while driving).
// A layout chosen by hand stays until the situation changes again.
void AutoLayout() {
    if(app.preview || !app.frame.connected) { app.pendingSituation=-1; return; }
    const int situation=CurrentSituation(app.frame);
    if(situation!=app.pendingSituation) { app.pendingSituation=situation; app.pendingSince=app.now; return; }
    if(situation==app.layoutSituation || app.now-app.pendingSince<1000) return;
    app.layoutSituation=situation;
    for(const auto& l:app.layouts) if(LayoutApplies(l.condition,situation)) { SwitchLayout(l.id); return; }
    if(!app.layoutIdle) { app.layoutIdle=true; UpdateOverlays(); RefreshPage(); }
}
// After a condition or the order changed: the next tick picks the layout for the current situation again.
void ReconsiderLayout() { app.layoutSituation=-1; }
// Moves an ID one place up (-1) or down (+1); false at either end.
bool MoveInOrder(std::vector<int>& order,int id,int step) {
    const auto at=std::find(order.begin(),order.end(),id); if(at==order.end()) return false;
    const auto index=at-order.begin(), target=index+step;
    if(target<0 || target>=static_cast<long long>(order.size())) return false;
    std::swap(order[index],order[target]); SettingChanged(); return true;
}
// Moves an ID to where another one stands, the ones between moving up or down a place; false when nothing moves.
bool MoveToPlace(std::vector<int>& order,int id,int place) {
    const auto from=std::find(order.begin(),order.end(),id), to=std::find(order.begin(),order.end(),place);
    if(from==order.end() || to==order.end() || from==to) return false;
    if(from<to) std::rotate(from,from+1,to+1); else std::rotate(to,from,from+1);
    SettingChanged(); return true;
}
// Gives a table the columns of another, also one of the other kind: their order, which show and their options
// (name format, short views, logo and name, status badges, headings, class colours, signs). Columns only the target
// has keep their setting and stand before the column they stood before. Switches and options only one kind has go
// between two of that kind: Best, Last and the Position option between Relatives (the Standings always show Best and
// Last, and their positions follow the class view), Laps and Gained between Standings. Rows and gap decimals stay.
void CopyColumns(const Overlay& from,Overlay& to) {
    const auto& f=from.table; auto& t=to.table;
    auto has=[](const std::vector<int>& order,int id) { return std::find(order.begin(),order.end(),id)!=order.end(); };
    std::vector<int> order; for(int id:f.columnOrder) if(has(t.columnOrder,id)) order.push_back(id);
    for(size_t i=0;i<t.columnOrder.size();++i) if(!has(order,t.columnOrder[i])) {
        auto next=order.end();
        for(size_t j=i+1;j<t.columnOrder.size() && next==order.end();++j) next=std::find(order.begin(),order.end(),t.columnOrder[j]);
        order.insert(next,t.columnOrder[i]);
    }
    t.columnOrder=order; StatusAfterDriver(t.columnOrder);
    t.driverColumns=f.driverColumns; t.extraColumns=f.extraColumns; t.driverShortView=f.driverShortView; t.driverFlags=f.driverFlags;
    t.showDriver=f.showDriver; t.showStatus=f.showStatus; t.nameFormat=f.nameFormat; t.columnHeaders=f.columnHeaders;
    t.classColors=f.classColors; t.brandLogo=f.brandLogo; t.brandName=f.brandName; t.gapSigns=f.gapSigns; t.interval=f.interval; t.sectorParts=f.sectorParts;
    if(from.kind==to.kind && to.kind==RelativeKind) { t.best=f.best; t.last=f.last; t.classPositions=f.classPositions; }
    if(from.kind==to.kind && to.kind==StandingsKind) { t.laps=f.laps; t.gained=f.gained; }
    Fit(to); SettingChanged();
}
void SetColumnVisible(Overlay& o,int id,bool on) {
    auto& t=o.table;
    switch(id) {
    case DriverColumn: t.showDriver=on; break;
    case StatusColumn: t.showStatus=on; break;
    case IntervalColumn: t.interval=on; break;
    case LapsColumn: t.laps=on; break;
    case GainedColumn: t.gained=on; break;
    case BestColumn: if(o.kind==RelativeKind) t.best=on; break;
    case LastColumn: if(o.kind==RelativeKind) t.last=on; break;
    default:
        if(id>=BrandColumn) t.extraColumns[id-BrandColumn]=on;
        else if(id>=LicenseColumn && id<=PitStopColumn) t.driverColumns[id-LicenseColumn]=on;
    }
    SettingChanged();
}
// corner: bit 0 right, bit 1 bottom. The overlay keeps its place; later growth starts from that corner.
void SetOverlayCorner(Overlay& o,int corner) {
    o.place.growLeft=(corner&1)!=0; o.place.growUp=(corner&2)!=0;
    CaptureOverlayCorner(o); Fit(o); SettingChanged();
}
bool SetStartWithWindows(bool on) {
    if(!RegisterStartup(on)) return false;
    app.startWithWindows=on; Save(); return true;
}
void SetStartMinimized(bool on) { app.startMinimized=on; Save(); }
void SetCheckUpdates(bool on) { app.checkUpdates=on; Save(); }
void SetGapSigns(Overlay& o,bool on) { o.table.gapSigns=on; SettingChanged(); }
void SetShowTitleBar(bool on) {
    app.showTitleBar=on;
    for(auto& o:app.overlays) o.bands.header=on;
    for(auto& overlay:app.overlays) Fit(overlay);
    Save(); UpdateOverlays();
}
void ResetPositions() {
    for(auto& o:app.overlays) { const int offset=(o.number-1)*30; SetOverlayOrigin(o,DefaultPositions[o.kind].x+offset,DefaultPositions[o.kind].y+offset); Fit(o,true); }
    Save();
}
