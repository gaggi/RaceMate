// Main window in the shared LaunchMate style (docs/ui-design.md there): sidebar,
// status banner, page title with a one-line hint, and the current page below.
enum class Page { Overlays, Settings, Overlay, RaceControl };
constexpr int NavOverlaysId=900, NavRaceControlId=901, NavSettingsId=902, StatusButtonId=903;
// Sent to the current page as WM_COMMAND when state changes outside it (shortcuts, tray, banner).
constexpr int PageRefreshId=905;
struct MainUi {
    NavBar nav; StatusPanel status; ScrollHost host;
    HFONT text=nullptr, heading=nullptr;
    HWND title=nullptr, hint=nullptr;
    HWND pane=nullptr; // Beside an overlay's page and the layouts: the layout's overlays, to go from one to the next.
    Page page=Page::Overlays; Overlay* overlay=nullptr; // The overlay whose own page is open.
} ui;
HWND CreateOverlaysPage(HWND parent);
HWND CreateGeneralPage(HWND parent);
HWND CreateTablePage(HWND parent,Overlay& overlay);
HWND CreateElementsPage(HWND parent,Overlay& overlay);
HWND CreateTrackMapPage(HWND parent,Overlay& overlay);
HWND CreateRaceControlPage(HWND parent);
HWND CreateOverlayPane(HWND parent);
void RefreshPage() {
    if(ui.host.Content()) SendMessageW(ui.host.Content(),WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0);
    if(ui.pane && IsWindowVisible(ui.pane)) SendMessageW(ui.pane,WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0);
}
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
    Tone tone=Tone::Neutral; std::wstring title,detail;
    if(app.edit) { tone=Tone::Busy; title=L"Moving overlays"; detail=L"Drag them where you want them. Other programs don't get mouse clicks on them meanwhile."; }
    else if(app.preview || (!app.frame.connected && !app.previewLayouts.empty())) { tone=Tone::Busy; title=L"Layout preview"; detail=L"Sample previews stay on screen. Live telemetry and OBS output are unchanged."; }
    else if(app.frame.connected) {
        tone=Tone::Active; title=L"Connected to iRacing";
        detail=Wide(app.session.track); if(!app.session.type.empty()) detail+=L"  \u00B7  "+Wide(app.session.type);
        detail+=L"  \u00B7  "+std::to_wstring(app.frame.tickRate>0?app.frame.tickRate:60)+L" Hz";
    }
    else { title=L"Waiting for iRacing"; detail=L"RaceMate connects as soon as a session is loaded."; }
    std::wstring problems;
    if(!app.trayAvailable && !app.diagnostics) problems=L"No tray icon: closing this window exits RaceMate";
    if(!app.hotkeyError.empty()) problems+=(problems.empty()?L"":L"  \u00B7  ")+std::wstring(L"Shortcut in use by another program: ")+app.hotkeyError;
    if(!problems.empty()) { if(tone==Tone::Neutral) tone=Tone::Busy; detail=problems; }
    ui.status.SetState(tone,title,detail);
    ui.status.SetToggles({L"Move",L"Hide",L"Pause OBS"},(app.edit?1:0)|(app.hidden?2:0)|(app.obsPaused?4:0));
}
void ShowPage(Page page,Overlay* overlay);
// Move, Hide and Pause OBS in the status bar, for every overlay at once.
void StatusButton(int which) {
    if(which==0) ToggleEdit(); else if(which==1) ToggleHidden(); else if(which==2) ToggleObsPaused();
    UpdateStatus();
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
    // Narrow enough for 1366 x 768 at 125 %.
    constexpr int nav=152, margin=16, pane=240;
    // An overlay's page and the layouts have the overlay list beside them, the full height under the banner.
    const bool listed=ui.pane && IsWindowVisible(ui.pane);
    const int left=nav+margin+(listed?pane+margin:0), content=width-left-margin;
    place(ui.nav.Handle(),0,0,nav,height);
    if(listed) place(ui.pane,nav+margin,92,pane,height-92-20);
    place(ui.status.Handle(),nav+margin,20,width-nav-2*margin,56);
    // An overlay's page has only its name above it, no hint.
    const int top=IsWindowVisible(ui.hint)?148:128;
    place(ui.title,left,92,content,24);
    place(ui.hint,left,116,content,20);
    place(ui.host.Handle(),left,top,content,height-top-20);
}
// An overlay's page title: its layout, then its name, as two layouts can have overlays of the same name.
std::wstring OverlayTitle(const Overlay& o) {
    for(const auto& l:app.layouts) if(l.id==o.layout) return l.name+L"  \u203A  "+Name(o);
    return Name(o);
}
void ShowPage(Page page,Overlay* overlay=nullptr) {
    ui.page=page; if(overlay) ui.overlay=overlay;
    if(page==Page::Overlay) {
        SetWindowTextW(ui.title,OverlayTitle(*ui.overlay).c_str()); SetWindowTextW(ui.hint,L"");
    } else if(page==Page::RaceControl) {
        SetWindowTextW(ui.title,L"Race Control");
        SetWindowTextW(ui.hint,L"Full standings and an event feed in their own window. Events are recorded even while it is closed.");
    } else {
        const Layout* layout=ActiveLayout();
        SetWindowTextW(ui.title,page==Page::Settings?L"Settings":(layout?layout->name.c_str():L"Layouts"));
        SetWindowTextW(ui.hint,page==Page::Settings?L"Startup, update rates, fuel and pit stop estimates, shortcuts and updates.":L"");
    }
    // Beside the list a page has only its name above it.
    const bool listed=page==Page::Overlay || page==Page::Overlays;
    ShowWindow(ui.hint,listed?SW_HIDE:SW_SHOW);
    if(ui.pane) { ShowWindow(ui.pane,listed?SW_SHOW:SW_HIDE); if(listed) SendMessageW(ui.pane,WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0); }
    UpdateOverlays(); // The edited overlay's border follows the page.
    ui.nav.SetSelected(page==Page::Settings?NavSettingsId:(page==Page::RaceControl?NavRaceControlId:NavOverlaysId));
    const HWND host=ui.host.Handle();
    ui.host.SetContent(page==Page::Overlays?CreateOverlaysPage(host):page==Page::RaceControl?CreateRaceControlPage(host):(page==Page::Settings?CreateGeneralPage(host):(IsTable(ui.overlay->kind)?CreateTablePage(host,*ui.overlay):ui.overlay->kind==TrackMapKind?CreateTrackMapPage(host,*ui.overlay):CreateElementsPage(host,*ui.overlay))));
    LayoutMain();
}
void OpenOverlay(Overlay& o) { if(app.layout!=o.layout) { Save(); app.layout=o.layout; } ShowPage(Page::Overlay,&o); }
// The Overlays entry: the overlay open last, else the layout's page, so that none is marked on screen until chosen.
void OpenOverlays() {
    for(auto& o:app.overlays) if(&o==ui.overlay && o.layout==app.layout) { ShowPage(Page::Overlay,&o); return; }
    ShowPage(Page::Overlays);
}
bool Editing(const Overlay& o) {
    return IsWindowVisible(app.main) && !IsIconic(app.main) &&
        ui.page==Page::Overlay && ui.overlay==&o;
}
// Dark as Windows' apps are set: AppsUseLightTheme 0.
bool WindowsAppsDark() {
    DWORD light=1,size=sizeof(light);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size)==ERROR_SUCCESS && light==0;
}
// The settings and Race Control windows in the chosen theme; the overlays keep their own dark look.
void ApplyTheme(bool rebuild=true) {
    UiTheme::SetDark(app.theme==2 || (app.theme==0 && WindowsAppsDark()));
    app.brush=UiTheme::BackgroundBrush();
    if(rebuild && ui.page==Page::Overlay && ui.overlay) ShowPage(ui.page,ui.overlay); else if(rebuild) ShowPage(ui.page);
    if(app.main) UiTheme::RefreshWindow(app.main);
    if(raceControl.window) {
        for(HWND list:{raceControl.standings,raceControl.feed}) if(list) { ListView_SetBkColor(list,UiTheme::Surface); ListView_SetTextBkColor(list,UiTheme::Surface); ListView_SetTextColor(list,Text); }
        UiTheme::RefreshWindow(raceControl.window);
    }
}
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
    ui.host.Create(app.instance,app.main); ShowWindow(ui.host.Handle(),SW_SHOW);
    ui.pane=CreateOverlayPane(app.main);
    UpdateStatus(); OpenOverlays();
}
void ApplyUiDpi() {
    CreateUiFonts();
    ui.nav.SetFonts(ui.heading,ui.text); ui.status.SetFonts(ui.heading,ui.text);
    SendMessageW(ui.title,WM_SETFONT,reinterpret_cast<WPARAM>(ui.heading),TRUE);
    SendMessageW(ui.hint,WM_SETFONT,reinterpret_cast<WPARAM>(ui.text),TRUE);
    // The overlay list holds the old fonts: made again with the new ones; ShowPage shows it where it belongs.
    if(ui.pane) { DestroyWindow(ui.pane); ui.pane=CreateOverlayPane(app.main); }
    if(ui.page==Page::Overlay && ui.overlay) ShowPage(ui.page,ui.overlay); else ShowPage(ui.page);
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
// The overlay list follows, as a switch on an overlay's page changes it there too.
void SettingChanged() {
    Save(); AutoLayout(); UpdateOverlays();
    if(ui.pane && IsWindowVisible(ui.pane)) SendMessageW(ui.pane,WM_COMMAND,MAKEWPARAM(PageRefreshId,0),0);
}
// A further instance of a kind with default settings.
Overlay& NewInstance(int kind) {
    auto& o=AddInstance(kind,InstanceCount(kind)+1);
    ReadInstance(o); // A removed instance's section is gone, so this gives the defaults.
    // The Stream Cover goes to OBS only: on your screen it would hide the setup you are working on.
    if(kind==StreamCoverKind) { o.place.enabled=false; o.place.spout=true; } else o.place.enabled=true;
    if(LayoutRunning(o.layout)) CreateOverlayWindow(o); SettingChanged(); return o;
}
// Makes another layout the active one: the current one stays saved as it is, the other's overlays take
// their places and settings. Instances the other layout lacks go, ones it has more of are created.
void SwitchLayout(int id) {
    if(id==app.layout || std::none_of(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;})) return;
    Save(); app.layout=id;
    SettingChanged();
    // An open overlay page is rebuilt for the new settings, or closed when its overlay is gone.
    if(ui.page==Page::Overlay) {
        const bool kept=std::any_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return &o==ui.overlay && o.layout==id;});
        if(kept) ShowPage(Page::Overlay,ui.overlay); else ShowPage(Page::Overlays);
    } else RefreshPage();
}
// Every section of a layout's overlays, and what Save() remembers of them.
void DeleteLayoutSections(int id) {
    const auto prefix=LayoutPrefix(id);
    std::vector<wchar_t> buffer(65536); GetPrivateProfileSectionNamesW(buffer.data(),static_cast<DWORD>(buffer.size()),app.config.c_str());
    std::vector<std::wstring> names;
    for(const wchar_t* name=buffer.data();*name;name+=wcslen(name)+1) {
        const std::wstring_view section(name);
        bool owned=!prefix.empty() && section.starts_with(prefix);
        if(id==1) for(const auto* base:Sections) {
            const std::wstring_view kind(base);
            if(section==kind) owned=true;
            else if(section.starts_with(std::wstring(kind)+L" ")) {
                const auto number=section.substr(kind.size()+1);
                if(!number.empty() && std::all_of(number.begin(),number.end(),[](wchar_t c){return c>=L'0' && c<=L'9';})) owned=true;
            }
        }
        if(owned) names.emplace_back(name);
    }
    for(const auto& name:names) DeleteSection(name);
}
// A new layout at the end of the list: a copy of the active one, or default overlays.
void AddLayout(bool copy) {
    Save();
    int id=1; for(const auto& l:app.layouts) id=std::max(id,l.id+1);
    DeleteLayoutSections(id); // Nothing of an earlier layout with this number may come back.
    if(copy) for(const auto& o:app.overlays) if(o.layout==app.layout) {
        std::vector<wchar_t> contents(32768);
        GetPrivateProfileSectionW(Section(o).c_str(),contents.data(),static_cast<DWORD>(contents.size()),app.config.c_str());
        const auto target=LayoutPrefix(id)+Numbered(Sections[o.kind],o.number);
        // A whole section at once; remembered, so the next Save() does not write its keys one by one again.
        if(WritePrivateProfileSectionW(target.c_str(),contents.data(),app.config.c_str())) {
            size_t length=0; while(length+1<contents.size() && (contents[length] || contents[length+1])) ++length;
            RememberSection(target,contents.data(),length+1);
        }
    }
    app.layouts.push_back({id,L"Layout "+std::to_wstring(app.layouts.size()+1),AlwaysCondition});
    LoadOverlays(); SwitchLayout(id);
}
// A layout as a file of its own, to share or to take to another computer: a header with its name and conditions,
// then its overlays' sections under their plain names, as layout 1 keeps them.
constexpr const wchar_t* LayoutFileHeader=L"RaceMate layout";
// The double-zero-ended "key=value" list of a section, as the profile functions read and write it.
std::vector<wchar_t> ReadSection(const std::wstring& section,const std::wstring& file) {
    std::vector<wchar_t> contents(32768);
    const DWORD length=GetPrivateProfileSectionW(section.c_str(),contents.data(),static_cast<DWORD>(contents.size()),file.c_str());
    contents.resize(length+2,0); return contents;
}
bool ExportLayout(int id,const std::wstring& path) {
    const auto layout=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){ return l.id==id; });
    if(layout==app.layouts.end()) return false;
    Save();
    // Started as UTF-16, so names in any script come back as they were.
    { std::ofstream file(path,std::ios::binary|std::ios::trunc); if(!file) return false; file.write("\xFF\xFE",2); if(!file) return false; }
    bool ok=WritePrivateProfileStringW(LayoutFileHeader,L"Version",L"1",path.c_str()) && WritePrivateProfileStringW(LayoutFileHeader,L"Name",layout->name.c_str(),path.c_str())
        && WritePrivateProfileStringW(LayoutFileHeader,L"Condition",std::to_wstring(layout->condition).c_str(),path.c_str())
        && WritePrivateProfileStringW(LayoutFileHeader,L"Session",std::to_wstring(layout->session).c_str(),path.c_str());
    for(const auto& o:app.overlays) if(o.layout==id) ok=WritePrivateProfileSectionW(Numbered(Sections[o.kind],o.number).c_str(),ReadSection(Section(o),app.config).data(),path.c_str()) && ok;
    return ok;
}
// An overlay's section name in a layout file: a kind's name, numbered from 2 for the kinds that have instances.
bool OverlaySectionName(const std::wstring& name) {
    for(int kind=0;kind<OverlayCount;++kind) {
        const std::wstring base=Sections[kind];
        if(name==base) return true;
        if(name.size()<=base.size()+1 || name.compare(0,base.size()+1,base+L" ")) continue;
        const auto number=name.substr(base.size()+1);
        if(number.size()<=2 && std::all_of(number.begin(),number.end(),iswdigit) && std::stoi(number)>=2) return true;
    }
    return false;
}
// A new layout from a file, at the end of the list; 0 if the file holds none. Only overlay sections are taken, never
// general settings. Without positions the overlays start where new ones do.
int ImportLayout(const std::wstring& path,bool positions) {
    if(GetPrivateProfileIntW(LayoutFileHeader,L"Version",0,path.c_str())!=1) return 0;
    Save();
    int id=1; for(const auto& l:app.layouts) id=std::max(id,l.id+1);
    DeleteLayoutSections(id);
    std::vector<wchar_t> names(65536);
    GetPrivateProfileSectionNamesW(names.data(),static_cast<DWORD>(names.size()),path.c_str());
    for(const wchar_t* name=names.data();*name;name+=wcslen(name)+1) {
        if(!OverlaySectionName(name)) continue;
        std::vector<wchar_t> kept;
        const auto contents=ReadSection(name,path);
        for(const wchar_t* line=contents.data();*line;line+=wcslen(line)+1) {
            if(!positions && (!wcsncmp(line,L"X=",2) || !wcsncmp(line,L"Y=",2))) continue;
            kept.insert(kept.end(),line,line+wcslen(line)+1);
        }
        kept.push_back(0); if(kept.size()<2) kept.push_back(0);
        const auto target=LayoutPrefix(id)+name;
        if(WritePrivateProfileSectionW(target.c_str(),kept.data(),app.config.c_str())) RememberSection(target,kept.data(),kept.size()-1);
    }
    wchar_t name[128]{}; GetPrivateProfileStringW(LayoutFileHeader,L"Name",L"Imported layout",name,128,path.c_str());
    app.layouts.push_back({id,name[0]?name:L"Imported layout",std::clamp(static_cast<int>(GetPrivateProfileIntW(LayoutFileHeader,L"Condition",AlwaysCondition,path.c_str())),0,ConditionCount-1),
        std::clamp(static_cast<int>(GetPrivateProfileIntW(LayoutFileHeader,L"Session",AnySession,path.c_str())),0,SessionConditionCount-1)});
    LoadOverlays(); SwitchLayout(id); Save();
    return id;
}
// Every layout can be removed, including the original and the last remaining one.
void RemoveLayout(int id) {
    const auto at=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;}); if(at==app.layouts.end()) return;
    if(std::any_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return &o==ui.overlay && o.layout==id;})) {
        if(ui.page==Page::Overlay) ShowPage(Page::Overlays);
        ui.overlay=nullptr;
    }
    if(app.layout==id) {
        const auto next=std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id!=id;});
        app.layout=next==app.layouts.end()?0:next->id;
    }
    for(auto it=app.overlays.begin();it!=app.overlays.end();) {
        if(it->layout==id) { DestroyOverlay(*it); it=app.overlays.erase(it); } else ++it;
    }
    DeleteLayoutSections(id);
    DeleteSection(L"Layout "+std::to_wstring(id));
    app.layouts.erase(std::find_if(app.layouts.begin(),app.layouts.end(),[&](const Layout& l){return l.id==id;}));
    std::erase(app.activeLayouts,id);
    std::erase(app.previewLayouts,id);
    std::erase(app.openLayoutGroups,id);
    Save(); UpdateOverlays();
}
// Both conditions must hold: where you are, and the kind of session (any, or the one running).
bool LayoutApplies(const Layout& layout,int situation,int session) {
    if(layout.session!=AnySession && layout.session!=session) return false;
    switch(layout.condition) {
    case InCarCondition: return situation==InCarSituation;
    case OutOfCarCondition: return situation==SpottingSituation || situation==ReplaySituation;
    case GarageCondition: return situation==GarageSituation;
    default: return true;
    }
}
// Activate all matching layouts. Selecting a layout in the editor never overrides conditions.
void AutoLayout() {
    const int situation=app.frame.connected?CurrentSituation(app.frame):-1, session=app.frame.connected?SessionKind(app.session.type):-1;
    std::vector<int> matches;
    for(const auto& l:app.layouts) if(LayoutApplies(l,situation,session)) matches.push_back(l.id);
    const bool live=app.frame.connected && !app.preview;
    if(matches==app.activeLayouts && live==app.layoutsLive) return;
    app.layoutsLive=live;
    app.activeLayouts=std::move(matches);
    UpdateOverlays(); RefreshPage();
}
// After a condition or the order changed: the next tick picks the layout for the current situation again.
void ReconsiderLayout() { AutoLayout(); }
void ToggleLayoutPreview(int id) {
    if(LayoutPreviewRequested(id)) std::erase(app.previewLayouts,id);
    else if(!LayoutLive(id)) app.previewLayouts.push_back(id);
    UpdateOverlays(); UpdateStatus(); Timer(); RefreshPage();
}
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
    t.showPosition=f.showPosition; t.showNumber=f.showNumber; t.showGap=f.showGap; t.showDriver=f.showDriver; t.showStatus=f.showStatus; t.nameFormat=f.nameFormat; t.columnHeaders=f.columnHeaders;
    t.classColors=f.classColors; t.brandLogo=f.brandLogo; t.brandName=f.brandName; t.gapSigns=f.gapSigns; t.interval=f.interval; t.sectorParts=f.sectorParts;
    if(from.kind==to.kind) { t.best=f.best; t.last=f.last; }
    if(from.kind==to.kind && to.kind==RelativeKind) t.classPositions=f.classPositions;
    if(from.kind==to.kind && to.kind==StandingsKind) { t.laps=f.laps; t.gained=f.gained; }
    Fit(to); SettingChanged();
}
void SetColumnVisible(Overlay& o,int id,bool on) {
    auto& t=o.table;
    switch(id) {
    case PositionColumn: t.showPosition=on; break;
    case NumberColumn: t.showNumber=on; break;
    case DriverColumn: t.showDriver=on; break;
    case GapColumn: t.showGap=on; break;
    case StatusColumn: t.showStatus=on; break;
    case IntervalColumn: t.interval=on; break;
    case LapsColumn: t.laps=on; break;
    case GainedColumn: t.gained=on; break;
    case BestColumn: t.best=on; break;
    case LastColumn: t.last=on; break;
    default:
        if(id>=BrandColumn) t.extraColumns[id-BrandColumn]=on;
        else if(id>=LicenseColumn && id<=PitStopColumn) t.driverColumns[id-LicenseColumn]=on;
    }
    SettingChanged();
}
// corner: bit 0 right, bit 1 bottom. The overlay keeps its place; later growth starts from that corner.
void SetOverlayCorner(Overlay& o,int corner) {
    if(!o.window) {
        const int dpi=GetDpiForSystem();
        const int x=o.place.x-(o.place.growLeft?MulDiv(OverlayWidth(o),dpi*o.place.scale,9600):0);
        const int y=o.place.y-(o.place.growUp?MulDiv(BaseHeight(o),dpi*o.place.scale,9600):0);
        o.place.growLeft=(corner&1)!=0; o.place.growUp=(corner&2)!=0;
        SetOverlayOrigin(o,x,y); SettingChanged(); return;
    }
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
    for(auto& o:app.overlays) if(o.layout==app.layout) { const POINT origin=DefaultOrigin(o); SetOverlayOrigin(o,origin.x,origin.y); Fit(o,true); }
    Save();
}
