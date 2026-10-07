// Native, resizable race control window. Event recording runs independently of visibility.
struct RaceControlWindow {
    HWND window=nullptr, standings=nullptr, feed=nullptr, status=nullptr, filter=nullptr, camera=nullptr, carFilter=nullptr;
    HFONT font=nullptr;
    std::vector<Row> rows; std::vector<RaceEvent> events;
    std::vector<int> columns, feedColumns;
    std::array<bool,18> fields{}; std::array<bool,7> eventFields{};
    int sortColumn=-1, eventSortColumn=2; bool sortDescending=false, eventSortDescending=true;
    std::wstring carFilterSignature; std::string carFilterSession;
    std::vector<CameraGroup> cameras; std::wstring cameraName;
    std::wstring signature, message;
    ULONGLONG updated=0;
    std::string pendingNumber; int pendingGroup=1;
} raceControl;
const wchar_t* EventName(EventType type) {
    switch(type) {
    case EventType::Offtrack:return L"Offtrack";
    case EventType::Incident:return L"Incident";
    case EventType::Crash:return L"Possible crash";
    case EventType::PitEntry:return L"Pit entry";
    case EventType::PitStop:return L"Pit stop";
    case EventType::Overtake:return L"Overtake (estimated)";
    }
    return L"Event";
}
std::wstring EventClock(double time) {
    const int seconds=static_cast<int>(std::max(0.0,time)); wchar_t value[64];
    swprintf_s(value,L"%d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60); return value;
}
bool SendSimMessage(const SimMessage& message) {
    if(app.preview || app.diagnostics) return true;
    return SendIRacingMessage(message);
}
void RaceControlStatus(const std::wstring& message) {
    raceControl.message=message; if(raceControl.status) SetWindowTextW(raceControl.status,message.c_str());
}
bool CanControlSim() {
    if(app.preview || app.diagnostics) { RaceControlStatus(L"Preview: camera / replay action simulated."); return true; }
    if(!app.frame.connected) { RaceControlStatus(L"Connect to iRacing to use the camera or replay."); return false; }
    if(app.frame.onTrack) { RaceControlStatus(L"Leave the car to control the spectator camera or replay."); return false; }
    return true;
}
void UpdateRaceCameras() {
    std::vector<CameraGroup> groups;
    for(const auto& group:app.session.cameras) if(!group.scenic && !group.name.empty()) groups.push_back(group);
    bool same=groups.size()==raceControl.cameras.size();
    if(same) for(size_t i=0;i<groups.size();++i) if(groups[i].name!=raceControl.cameras[i].name || groups[i].number!=raceControl.cameras[i].number) same=false;
    if(same) return;
    raceControl.cameras=std::move(groups); SendMessageW(raceControl.camera,CB_RESETCONTENT,0,0);
    int selected=-1, fallback=-1;
    for(int i=0;i<static_cast<int>(raceControl.cameras.size());++i) {
        const auto name=Wide(raceControl.cameras[i].name);
        SendMessageW(raceControl.camera,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
        if(name==raceControl.cameraName) selected=i;
        if(name==L"Far Chase") fallback=i;
    }
    if(selected<0) selected=fallback>=0?fallback:(raceControl.cameras.empty()?-1:0);
    SendMessageW(raceControl.camera,CB_SETCURSEL,selected,0);
    EnableWindow(raceControl.camera,!raceControl.cameras.empty());
}
int RaceCameraGroup() {
    const int index=static_cast<int>(SendMessageW(raceControl.camera,CB_GETCURSEL,0,0));
    if(index>=0 && index<static_cast<int>(raceControl.cameras.size())) return raceControl.cameras[index].number;
    RaceControlStatus(L"Waiting for iRacing camera groups. Choose a camera before jumping."); return -1;
}
void FocusRaceCar(const std::string& number) {
    if(!CanControlSim()) return;
    if(CameraNumber(number)<0) { RaceControlStatus(L"This car has no usable camera number."); return; }
    const int group=RaceCameraGroup(); if(group<0) return;
    if(SendSimMessage(CameraMessage(number,group))) RaceControlStatus(app.preview || app.diagnostics?L"Preview: camera action simulated.":L"Camera requested for #"+Wide(number));
    else RaceControlStatus(L"Could not send the camera command to iRacing.");
}
void ReplayRaceEvent(int selected,int lead) {
    if(selected<0 || selected>=static_cast<int>(raceControl.events.size())) { RaceControlStatus(L"Click -5s or -10s beside an event."); return; }
    if(!CanControlSim()) return;
    const auto& event=raceControl.events[selected];
    if(CameraNumber(event.number)<0 || event.session<0) { RaceControlStatus(L"No replay target is available for this event."); return; }
    const int group=RaceCameraGroup(); if(group<0) return;
    // Keep all 32 timestamp bits: the SDK packs them directly into LPARAM.
    const bool success=SendSimMessage({3,0,0}) && SendSimMessage(ReplayMessage(event.session,event.time,lead));
    if(!success) { RaceControlStatus(L"Could not send the replay command to iRacing."); return; }
    raceControl.pendingNumber=event.number; raceControl.pendingGroup=group;
    SetTimer(raceControl.window,1,250,nullptr);
    RaceControlStatus(app.preview || app.diagnostics?L"Preview: replay action simulated.":L"Replay requested: #"+Wide(event.number)+L" at "+EventClock(std::max(0.0,event.time-lead))+L". Footage must be in iRacing's replay buffer.");
}
void RaceControlFont() {
    if(!raceControl.window) return;
    const int dpi=GetDpiForWindow(raceControl.window);
    HFONT font=CreateFontW(-MulDiv(13,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    if(!font) return;
    for(HWND child=GetWindow(raceControl.window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(raceControl.font) DeleteObject(raceControl.font); raceControl.font=font;
    const int widths[]={48,48,78,142,42,155,310};
    if(raceControl.feed) for(int i=0;i<static_cast<int>(raceControl.feedColumns.size());++i) ListView_SetColumnWidth(raceControl.feed,i,MulDiv(widths[raceControl.feedColumns[i]],dpi,96));
}
void LayoutRaceControl() {
    if(!raceControl.window) return;
    RECT r{}; GetClientRect(raceControl.window,&r); const int dpi=GetDpiForWindow(raceControl.window);
    auto d=[&](int n){return MulDiv(n,dpi,96);};
    const int margin=d(12),left=(r.right-3*margin)/2,right=r.right-left-3*margin;
    auto place=[&](int id,int x,int y,int w,int h){SetWindowPos(GetDlgItem(raceControl.window,id),nullptr,x,y,std::max(1,w),std::max(1,h),SWP_NOZORDER|SWP_NOACTIVATE);};
    place(800,margin,d(14),left,d(24)); place(801,2*margin+left,d(14),right,d(24));
    place(810,margin,d(44),left,r.bottom-d(124)); place(811,2*margin+left,d(84),right,r.bottom-d(164));
    const int filterWidth=std::max(d(100),(right-d(8))/2);
    place(812,2*margin+left,d(44),filterWidth,d(240)); place(819,2*margin+left+filterWidth+d(8),d(44),right-filterWidth-d(8),d(320));
    place(817,margin,r.bottom-d(64),d(60),d(24)); place(818,margin+d(65),r.bottom-d(68),d(260),d(250));
    place(815,margin+d(335),r.bottom-d(68),d(110),d(28));
    place(816,margin,r.bottom-d(34),r.right-2*margin,d(24));
}
bool RaceCarInactive(const Row& row) { return row.car.surface<0 && !row.car.pit; }
std::wstring RaceCarStatus(const Row& row) {
    if(!app.frame.connected) return L"Unknown";
    if(row.car.towing) return L"Towing";
    if(row.car.surface==1) return L"Pit stall";
    if(row.car.pit) return L"Pit lane";
    if(row.car.surface==2) return L"Pit approach";
    if(row.car.surface==0) return L"Off track";
    if(row.car.outLap) return L"Outlap";
    if(row.car.surface==3) return L"On track";
    return L"Inactive";
}
std::wstring RaceStandingCell(const Row& row,int column) {
    switch(column) {
    case 0: { const int p=app.standings.separateClasses?row.car.classPosition:row.car.position; return p>0?std::to_wstring(p):L"—"; }
    case 1:return Wide(row.driver.number);
    case 2:return Wide(row.driver.name);
    case 3:return Wide(row.driver.className);
    case 4:return Gap(row,1);
    case 5:return Interval(row);
    case 6:return Lap(row.car.best);
    case 7:return Lap(row.car.last);
    case 8:return RaceCarStatus(row);
    case 15: { const auto flags=Wide(DriverFlags(row.car.sessionFlags)); return flags.empty()?L"—":flags; }
    case 16:return CompletedLaps(row);
    case 17: { const int gained=PositionsGained(app.session,row,app.standings.separateClasses); return gained==-MaxCars?L"—":(gained>0?L"+":L"")+std::to_wstring(gained); }
    default:return DriverValue(row,column-9);
    }
}
#include "RaceControlFields.inc"
void UpdateRaceControl(bool force) {
    if(!raceControl.window || (!force && (!IsWindowVisible(raceControl.window) || IsIconic(raceControl.window)))) return;
    const auto now=GetTickCount64(); if(!force && now-raceControl.updated<500) return; raceControl.updated=now;
    UpdateRaceCameras();
    auto options=app.standings; options.leaders=MaxCars; options.ahead=options.behind=0;
    std::vector<Row> rows; for(const auto& group:SelectStandings(app.session,app.frame,options)) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
    const int reference=ReferenceCar(app.frame);
    int referenceClass=-1; for(const auto& d:app.session.drivers) if(d.index==reference) referenceClass=d.classId;
    for(const auto& driver:app.session.drivers) {
        if(driver.index<0 || driver.index>=MaxCars || (options.ownClass && (driver.classId!=referenceClass))) continue;
        if(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.driver.index==driver.index;})) {
            Row row{driver,app.frame.cars[driver.index]}; row.player=driver.index==ReferenceCar(app.frame); rows.push_back(row);
        }
    }
    UpdateRaceCarFilter();
    const int carIndex=RaceFilterCar();
    SortRaceRows(rows);
    std::vector<RaceEvent> events;
    const int filter=static_cast<int>(SendMessageW(raceControl.filter,CB_GETCURSEL,0,0));
    const auto& recorded=app.events.Events();
    if(app.preview) {
        const EventType types[]={EventType::Offtrack,EventType::Incident,EventType::Crash,EventType::PitEntry,EventType::PitStop,EventType::Overtake};
        for(int i=5;i>=0;--i) if(filter<=0 || filter==i+1) {
            const auto& driver=app.session.drivers[i]; RaceEvent e;
            e.id=i+1; e.type=types[i]; e.session=0; e.car=driver.index; e.time=600+i*42; e.number=driver.number; e.driver=driver.name;
            e.estimated=i==2 || i==5; e.detail=i==2?"New repair flag; possible crash":(i==4?"Pit lane visit: 42 s":(i==5?"Passed #12 Luca Weber":"Observed during this session")); if(carIndex<0 || e.car==carIndex) events.push_back(e);
        }
    } else for(auto it=recorded.rbegin();it!=recorded.rend();++it) if((filter<=0 || static_cast<int>(it->type)+1==filter) && (carIndex<0 || it->car==carIndex)) events.push_back(*it);
    SortRaceEvents(events);
    std::vector<int> columns; for(int id:{0,1,2,8,15,3,4,5,6,7,16,17,9,10,11,12,13,14}) if(raceControl.fields[id]) columns.push_back(id);
    std::vector<int> feedColumns; for(int id=0;id<7;++id) if(raceControl.eventFields[id]) feedColumns.push_back(id);
    std::wstring signature=std::to_wstring(carIndex)+L":"+std::to_wstring(raceControl.sortColumn)+std::to_wstring(raceControl.sortDescending)+std::to_wstring(raceControl.eventSortColumn)+std::to_wstring(raceControl.eventSortDescending)+std::to_wstring(filter)+std::to_wstring(app.standings.separateClasses)+std::to_wstring(app.frame.connected)+std::to_wstring(app.frame.onTrack);
    for(int col:columns) signature+=L"|"+std::to_wstring(col); for(int col:feedColumns) signature+=L"E"+std::to_wstring(col);
    for(const auto& row:rows) { signature+=std::to_wstring(row.driver.index); for(int col:columns) signature+=L"|"+RaceStandingCell(row,col); }
    for(const auto& event:events) signature+=L"|"+std::to_wstring(event.id)+Wide(event.driver);
    if(!force && signature==raceControl.signature) return;
    raceControl.signature=std::move(signature);
    const int oldRow=ListView_GetNextItem(raceControl.standings,-1,LVNI_SELECTED),oldEvent=ListView_GetNextItem(raceControl.feed,-1,LVNI_SELECTED);
    const int selectedCar=oldRow>=0 && oldRow<static_cast<int>(raceControl.rows.size())?raceControl.rows[oldRow].driver.index:-1;
    const uint64_t selectedEvent=oldEvent>=0 && oldEvent<static_cast<int>(raceControl.events.size())?raceControl.events[oldEvent].id:0;
    SendMessageW(raceControl.standings,WM_SETREDRAW,FALSE,0); SendMessageW(raceControl.feed,WM_SETREDRAW,FALSE,0);
    if(columns!=raceControl.columns || force) {
        while(ListView_DeleteColumn(raceControl.standings,0)) {}
        const int dpi=GetDpiForWindow(raceControl.window);
        for(int i=0;i<static_cast<int>(columns.size());++i) {
            const int c=columns[i]; LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
            col.pszText=const_cast<wchar_t*>(RaceFieldTitle(c)); col.cx=MulDiv(RaceFieldWidth(c),dpi,96); col.fmt=c==2 || c==3 || c==8 || c==15?LVCFMT_LEFT:LVCFMT_RIGHT;
            ListView_InsertColumn(raceControl.standings,i,&col);
        }
        raceControl.columns=columns;
    }
    if(feedColumns!=raceControl.feedColumns || force) {
        while(ListView_DeleteColumn(raceControl.feed,0)) {}
        const int dpi=GetDpiForWindow(raceControl.window);
        for(int i=0;i<static_cast<int>(feedColumns.size());++i) { const int c=feedColumns[i]; LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH; col.pszText=const_cast<wchar_t*>(RaceEventTitles[c]); col.cx=MulDiv(RaceEventWidths[c],dpi,96); ListView_InsertColumn(raceControl.feed,i,&col); }
        raceControl.feedColumns=feedColumns;
    }
    UpdateRaceSortIndicators();
    raceControl.rows=std::move(rows); raceControl.events=std::move(events);
    ListView_SetItemCountEx(raceControl.standings,static_cast<int>(raceControl.rows.size()),LVSICF_NOSCROLL);
    ListView_SetItemCountEx(raceControl.feed,static_cast<int>(raceControl.events.size()),LVSICF_NOSCROLL);
    ListView_SetItemState(raceControl.standings,-1,0,LVIS_SELECTED); ListView_SetItemState(raceControl.feed,-1,0,LVIS_SELECTED);
    for(int i=0;i<static_cast<int>(raceControl.rows.size());++i) if(raceControl.rows[i].driver.index==selectedCar) ListView_SetItemState(raceControl.standings,i,LVIS_SELECTED,LVIS_SELECTED);
    for(int i=0;i<static_cast<int>(raceControl.events.size());++i) if(raceControl.events[i].id==selectedEvent) ListView_SetItemState(raceControl.feed,i,LVIS_SELECTED,LVIS_SELECTED);
    SendMessageW(raceControl.standings,WM_SETREDRAW,TRUE,0); SendMessageW(raceControl.feed,WM_SETREDRAW,TRUE,0);
    InvalidateRect(raceControl.standings,nullptr,FALSE); InvalidateRect(raceControl.feed,nullptr,FALSE);
    if(raceControl.message.empty()) RaceControlStatus(app.preview?L"Preview. Double-click a driver; click -5s or -10s beside an event. Actions are simulated.":(app.frame.connected?L"Double-click a driver to switch camera. Click -5s or -10s beside an event. * = estimated event.":L"Waiting for iRacing. Recorded events remain available for this session."));
}
LRESULT CALLBACK RaceControlProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_SIZE: LayoutRaceControl(); if(wp!=SIZE_MINIMIZED) UpdateRaceControl(true); return 0;
    case WM_GETMINMAXINFO: { auto* size=reinterpret_cast<MINMAXINFO*>(lp); const int dpi=GetDpiForWindow(w); size->ptMinTrackSize={MulDiv(780,dpi,96),MulDiv(420,dpi,96)}; return 0; }
    case WM_COMMAND:
        if(LOWORD(wp)==818 && HIWORD(wp)==CBN_SELCHANGE) {
            const int index=static_cast<int>(SendMessageW(raceControl.camera,CB_GETCURSEL,0,0));
            if(index>=0 && index<static_cast<int>(raceControl.cameras.size())) {
                raceControl.cameraName=Wide(raceControl.cameras[index].name);
                WritePrivateProfileStringW(L"RaceControl",L"Camera",raceControl.cameraName.c_str(),app.config.c_str());
            }
        }
        else if((LOWORD(wp)==812 || LOWORD(wp)==819) && HIWORD(wp)==CBN_SELCHANGE) UpdateRaceControl(true);
        else if(LOWORD(wp)==815 && CanControlSim()) {
            KillTimer(w,1); raceControl.pendingNumber.clear(); SendSimMessage({5,1,0}); SendSimMessage({3,1,0}); RaceControlStatus(app.preview?L"Preview: live action simulated.":L"Live replay view requested.");
        }
        return 0;
    case WM_TIMER:
        if(wp==1) { KillTimer(w,1); if(CanControlSim() && !raceControl.pendingNumber.empty()) { SendSimMessage(CameraMessage(raceControl.pendingNumber,raceControl.pendingGroup)); SendSimMessage({3,1,0}); } raceControl.pendingNumber.clear(); }
        return 0;
    case WM_NOTIFY: {
        const auto* header=reinterpret_cast<NMHDR*>(lp);
        if(header->code==LVN_COLUMNCLICK) {
            const auto* click=reinterpret_cast<NMLISTVIEW*>(lp); const bool feed=header->hwndFrom==raceControl.feed;
            const auto& columns=feed?raceControl.feedColumns:raceControl.columns;
            if(click->iSubItem>=0 && click->iSubItem<static_cast<int>(columns.size())) {
                const int col=columns[click->iSubItem]; if(!feed || col>=2) SetRaceSort(feed,col);
            }
            return 0;
        }
        if(header->code==LVN_GETDISPINFOW) {
            auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp); std::wstring text;
            const int index=info->item.iItem; int column=info->item.iSubItem;
            if(header->hwndFrom==raceControl.standings && index>=0 && index<static_cast<int>(raceControl.rows.size()) && column<static_cast<int>(raceControl.columns.size())) text=RaceStandingCell(raceControl.rows[index],raceControl.columns[column]);
            if(header->hwndFrom==raceControl.feed && index>=0 && index<static_cast<int>(raceControl.events.size())) {
                column=column>=0 && column<static_cast<int>(raceControl.feedColumns.size())?raceControl.feedColumns[column]:-1;
                const auto& e=raceControl.events[index];
                if(column==0) text=L"-5s"; if(column==1) text=L"-10s"; if(column==2) text=EventClock(e.time); if(column==3) text=std::wstring(EventName(e.type))+(e.estimated?L" *":L""); if(column==4) text=Wide(e.number); if(column==5) text=Wide(e.driver); if(column==6) text=Wide(e.detail);
            }
            if(info->item.mask&LVIF_TEXT) lstrcpynW(info->item.pszText,text.c_str(),info->item.cchTextMax); return 0;
        }
        if(header->code==NM_DBLCLK && header->hwndFrom==raceControl.standings) {
            const auto* click=reinterpret_cast<NMITEMACTIVATE*>(lp);
            if(click->iItem>=0 && click->iItem<static_cast<int>(raceControl.rows.size())) FocusRaceCar(raceControl.rows[click->iItem].driver.number);
            return 0;
        }
        if(header->code==NM_CLICK && header->hwndFrom==raceControl.feed) {
            const auto* click=reinterpret_cast<NMITEMACTIVATE*>(lp);
            const int col=click->iSubItem>=0 && click->iSubItem<static_cast<int>(raceControl.feedColumns.size())?raceControl.feedColumns[click->iSubItem]:-1;
            if(col==0 || col==1) ReplayRaceEvent(click->iItem,col==0?5:10);
            return 0;
        }
        if(header->code==NM_CUSTOMDRAW) {
            auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                draw->clrText=Text; draw->clrTextBk=draw->nmcd.dwItemSpec%2?Background:RGB(255,255,255);
                return CDRF_NOTIFYSUBITEMDRAW;
            }
            if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && header->hwndFrom==raceControl.feed && draw->iSubItem>=0 && draw->iSubItem<static_cast<int>(raceControl.feedColumns.size()) && raceControl.feedColumns[draw->iSubItem]<2) {
                RECT rect{}; ListView_GetSubItemRect(raceControl.feed,static_cast<int>(draw->nmcd.dwItemSpec),draw->iSubItem,LVIR_BOUNDS,&rect);
                if(draw->iSubItem==0) rect.right=rect.left+ListView_GetColumnWidth(raceControl.feed,0);
                InflateRect(&rect,-2,-1); DrawFrameControl(draw->nmcd.hdc,&rect,DFC_BUTTON,DFCS_BUTTONPUSH);
                SetBkMode(draw->nmcd.hdc,TRANSPARENT); SetTextColor(draw->nmcd.hdc,Text);
                DrawTextW(draw->nmcd.hdc,raceControl.feedColumns[draw->iSubItem]==0?L"-5s":L"-10s",-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                return CDRF_SKIPDEFAULT;
            }
            if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM) && header->hwndFrom==raceControl.standings && draw->nmcd.dwItemSpec<raceControl.rows.size() && draw->iSubItem>=0 && draw->iSubItem<static_cast<int>(raceControl.columns.size())) {
                const auto& row=raceControl.rows[draw->nmcd.dwItemSpec]; const int c=raceControl.columns[draw->iSubItem];
                draw->clrText=row.player?RGB(140,96,0):Text;
                if(c==1 || c==3) { const unsigned color=row.driver.classColor; draw->clrText=RGB((color>>16)&255,(color>>8)&255,color&255); }
                if(c==9) { const unsigned color=row.driver.licenseColor; draw->clrText=RGB((color>>16)&255,(color>>8)&255,color&255); }
                if(c==11) draw->clrText=TireTextColor(row);
                if(c==15 && row.car.sessionFlags) draw->clrText=(row.car.sessionFlags&0x30000u)?RGB(180,40,35):RGB(155,90,15);
                if(GetRValue(draw->clrText)+GetGValue(draw->clrText)+GetBValue(draw->clrText)>500) draw->clrText=RGB(GetRValue(draw->clrText)/2,GetGValue(draw->clrText)/2,GetBValue(draw->clrText)/2);
                if(RaceCarInactive(row) || !app.frame.connected) draw->clrText=RGB(145,150,157);
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        return 0;
    }
    case WM_CLOSE: ShowWindow(w,SW_HIDE); return 0;
    case WM_DPICHANGED: { RaceControlFont(); const auto* rect=reinterpret_cast<RECT*>(lp); SetWindowPos(w,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER); LayoutRaceControl(); UpdateRaceControl(true); return 0; }
    case WM_CTLCOLORSTATIC: SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),Text); return reinterpret_cast<LRESULT>(app.brush);
    case WM_DESTROY: if(raceFieldsWindow) DestroyWindow(raceFieldsWindow); KillTimer(w,1); if(raceControl.font) DeleteObject(raceControl.font); raceControl={}; return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenRaceControl() {
    if(raceControl.window) { ShowWindow(raceControl.window,SW_RESTORE); UpdateRaceControl(true); SetForegroundWindow(raceControl.window); return; }
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=RaceControlProc; cls.hInstance=app.instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush;
    cls.hIcon=LoadIconW(app.instance,MAKEINTRESOURCEW(101)); cls.lpszClassName=L"RaceMate.RaceControl"; RegisterClassExW(&cls);
    const int dpi=GetDpiForWindow(app.main); auto d=[&](int n){return MulDiv(n,dpi,96);};
    raceControl.window=CreateWindowExW(0,cls.lpszClassName,L"RaceMate - Race Control",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,d(1240),d(650),nullptr,nullptr,app.instance,nullptr);
    if(!raceControl.window) return;
    raceControl.font=CreateFontW(-d(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    auto create=[&](const wchar_t* type,const wchar_t* title,int id,DWORD style) {
        HWND child=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,0,0,0,0,raceControl.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(raceControl.font),TRUE); return child;
    };
    create(L"STATIC",L"Standings - double-click a driver to follow",800,0); create(L"STATIC",L"Events - click -5s or -10s to replay",801,0);
    raceControl.standings=create(WC_LISTVIEWW,L"",810,LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);
    raceControl.feed=create(WC_LISTVIEWW,L"",811,LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);
    for(HWND list:{raceControl.standings,raceControl.feed}) { ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER); ListView_SetBkColor(list,RGB(255,255,255)); ListView_SetTextBkColor(list,RGB(255,255,255)); ListView_SetTextColor(list,Text); }
    LoadRaceFields();
    for(HWND list:{raceControl.standings,raceControl.feed}) SetWindowSubclass(ListView_GetHeader(list),RaceHeaderProc,1,reinterpret_cast<DWORD_PTR>(list));
    raceControl.filter=create(L"COMBOBOX",L"",812,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    for(const auto* item:{L"All events",L"Offtracks",L"Incidents",L"Possible crashes",L"Pit entries",L"Pit stops",L"Overtakes"}) SendMessageW(raceControl.filter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
    SendMessageW(raceControl.filter,CB_SETCURSEL,0,0);
    raceControl.carFilter=create(L"COMBOBOX",L"",819,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    create(L"STATIC",L"Camera",817,0); raceControl.camera=create(L"COMBOBOX",L"",818,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    wchar_t cameraName[256]{}; GetPrivateProfileStringW(L"RaceControl",L"Camera",L"Far Chase",cameraName,256,app.config.c_str()); raceControl.cameraName=cameraName; create(L"BUTTON",L"Go Live",815,WS_TABSTOP); raceControl.status=create(L"STATIC",L"",816,0);
    LayoutRaceControl(); UpdateRaceControl(true); ShowWindow(raceControl.window,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
    if(!app.diagnostics) SetForegroundWindow(raceControl.window);
}
void CloseRaceControl() { if(raceControl.window) DestroyWindow(raceControl.window); }
