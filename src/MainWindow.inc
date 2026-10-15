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
HWND CreateRaceControlPage(HWND parent);
void RefreshPage() { if(ui.host.Content()) SendMessageW(ui.host.Content(),WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0); }
std::wstring HotkeyText(WORD hotkey) {
    if(!LOBYTE(hotkey)) return L"Off";
    const BYTE modifiers=HIBYTE(hotkey); std::wstring text;
    if(modifiers&HOTKEYF_CONTROL) text+=L"Ctrl+";
    if(modifiers&HOTKEYF_ALT) text+=L"Alt+";
    if(modifiers&HOTKEYF_SHIFT) text+=L"Shift+";
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
        detail+=L"  \u00B7  "+std::to_wstring(ReadRate())+L" Hz"; button=L"Move overlays";
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
        L"A reminder to switch the pit limiter on. Size, opacity and corner are on the Overlays page.",
        L"Choose and order its cards."};
    if(page==Page::Overlay) {
        SetWindowTextW(ui.title,Name(*ui.overlay).c_str()); SetWindowTextW(ui.hint,hints[ui.overlay->kind]);
    } else if(page==Page::RaceControl) {
        SetWindowTextW(ui.title,L"Race Control");
        SetWindowTextW(ui.hint,L"Full standings and an event feed in their own window. Events are recorded even while it is closed.");
    } else {
        SetWindowTextW(ui.title,page==Page::Overlays?L"Overlays":L"Settings");
        SetWindowTextW(ui.hint,page==Page::Overlays?L"Turn overlays on or off, add more of a kind, and set their size and place. Open one to choose what it shows."
            :L"Startup, refresh rate and keyboard shortcuts.");
    }
    ShowWindow(ui.back,page==Page::Overlay?SW_SHOW:SW_HIDE);
    ui.nav.SetSelected(page==Page::Settings?NavSettingsId:(page==Page::RaceControl?NavRaceControlId:NavOverlaysId));
    const HWND host=ui.host.Handle();
    ui.host.SetContent(page==Page::Overlays?CreateOverlaysPage(host):page==Page::RaceControl?CreateRaceControlPage(host):(page==Page::Settings?CreateGeneralPage(host):(IsTable(ui.overlay->kind)?CreateTablePage(host,*ui.overlay):CreateElementsPage(host,*ui.overlay))));
    LayoutMain();
}
void OpenOverlay(Overlay& o) { ShowPage(Page::Overlay,&o); }
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
// A further instance of a kind with default settings, or a copy of another instance placed a little lower right.
Overlay& NewInstance(int kind,const Overlay* copy=nullptr) {
    auto& o=AddInstance(kind,InstanceCount(kind)+1);
    ReadInstance(o); // A removed instance's section is gone, so this gives the defaults.
    if(copy) { o.table=copy->table; o.fuel=copy->fuel; o.weather=copy->weather; o.place=copy->place; o.place.x+=30; o.place.y+=30; }
    CreateOverlayWindow(o); SettingChanged(); return o;
}
// Moves an ID one place up (-1) or down (+1); false at either end.
bool MoveInOrder(std::vector<int>& order,int id,int step) {
    const auto at=std::find(order.begin(),order.end(),id); if(at==order.end()) return false;
    const auto index=at-order.begin(), target=index+step;
    if(target<0 || target>=static_cast<long long>(order.size())) return false;
    std::swap(order[index],order[target]); SettingChanged(); return true;
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
    for(auto& o:app.overlays) if(IsTable(o.kind)) o.table.header=on;
    for(auto& overlay:app.overlays) Fit(overlay);
    Save(); UpdateOverlays();
}
void ResetPositions() {
    for(auto& o:app.overlays) { const int offset=(o.number-1)*30; SetOverlayOrigin(o,DefaultPositions[o.kind].x+offset,DefaultPositions[o.kind].y+offset); Fit(o,true); }
    Save();
}
