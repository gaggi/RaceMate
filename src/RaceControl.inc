// Race Control window: live/replay banner, event feed and full standings. The replay bar
// sets what a double-click on an event or a driver does: watch live, or replay 5 / 10 s back. Event recording runs independently of the window.
// Append stored field IDs so existing configurations keep their meaning.
enum { RaceIncidentsField=18, RaceFuelField, RaceStintLapsField, RaceStintTimeField, RaceStopsField, RaceRatingChangeField, RaceBrandField, RaceFieldCount };
constexpr int FirstEventField=2, RaceEventLapField=7, RaceEventFieldCount=8;
enum { RaceLeaderDistanceInfo, RaceWhiteFlagInfo, RaceInfoCount };
const wchar_t* RaceInfoKeys[]={L"ShowLeaderDistance",L"ShowWhiteFlagTime"};
struct RaceControlWindow {
    HWND window=nullptr, standings=nullptr, feed=nullptr, status=nullptr, splitter=nullptr;
    HFONT font=nullptr, heading=nullptr;
    StatusPanel banner; SegmentedControl filter, replay;
    std::vector<Row> rows; std::vector<RaceEvent> events;
    std::vector<int> columns, feedColumns;
    std::array<bool,RaceFieldCount> fields{}; std::array<bool,RaceEventFieldCount> eventFields{};
    std::array<bool,RaceInfoCount> info{};
    int sortColumn=-1, eventSortColumn=2; bool sortDescending=false, eventSortDescending=true;
    // Event filter: 0 all, 1 incidents (offtracks, incidents, crashes), 2 pit, 3 passes; car -1 for all cars.
    int category=0, car=-1; uint64_t selectedEvent=0;
    // What a double-click does: 0 watch live, 1 replay from 5 s before, 2 from 10 s before.
    int replayMode=1; bool onTop=false;
    int splitPermille=560, splitBeforeDrag=560, splitGrab=0;
    bool splitDragging=false;
    std::vector<CameraGroup> cameras; std::wstring cameraName;
    std::wstring signature, message, feedKey; size_t total=0;
    ULONGLONG updated=0;
    // A jump under way: the car and camera to show once iRacing is there. Target session and time (-1 for the live
    // end), and when to stop waiting.
    std::string pendingNumber; int pendingGroup=1, pendingSession=-1; double pendingTime=-1; ULONGLONG pendingUntil=0;
} raceControl;
enum RaceControlId {
    RaceStandingsId=810, RaceFeedId=811, RaceMessageId=816, RaceGoLiveId=820, RaceFilterId=821, RaceCarPillId=822, RaceCountId=823,
    RaceCameraId=824, RaceOnTopId=825, RaceReplayLabelId=829, RaceReplayId=830, RaceOnlyCarId=833, RaceSelectedId=834, RaceWatchId=840, RaceCarEventsId=841, RaceSplitterId=842, RaceInfoId=843,
    RacePreviousId=844, RaceNextId=845
};
constexpr int RaceReplayLeads[]={0,5,10};
const wchar_t* EventName(EventType type) {
    switch(type) {
    case EventType::Offtrack:return L"Offtrack";
    case EventType::Incident:return L"Incident";
    case EventType::Crash:return L"Crash";
    case EventType::PitEntry:return L"Pit entry";
    case EventType::PitStop:return L"Pit stop";
    case EventType::Overtake:return L"Pass";
    }
    return L"Event";
}
int EventCategory(EventType type) {
    switch(type) {
    case EventType::Offtrack: case EventType::Incident: case EventType::Crash: return 1;
    case EventType::PitEntry: case EventType::PitStop: return 2;
    default: return 3;
    }
}
struct PillColors { COLORREF fill, text; };
PillColors EventColors(EventType type) {
    switch(type) {
    case EventType::Crash: return {RGB(252,235,235),RGB(121,31,31)};
    case EventType::Offtrack: case EventType::Incident: return {RGB(250,238,218),RGB(133,79,11)};
    case EventType::Overtake: return {RGB(230,241,251),RGB(12,68,124)};
    default: return {RGB(241,239,232),RGB(95,94,90)};
    }
}
std::wstring EventClock(double time) {
    const int seconds=static_cast<int>(std::max(0.0,time)); wchar_t value[64];
    swprintf_s(value,L"%d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60); return value;
}
bool SendSimMessage(const SimMessage& message) {
    if(app.preview || app.diagnostics) return true;
    return SendIRacingMessage(message);
}
// Static texts repaint on every SetWindowText; the window refreshes twice a second.
void SetRaceText(HWND item,const std::wstring& text) {
    wchar_t current[256]{}; GetWindowTextW(item,current,static_cast<int>(std::size(current)));
    if(text!=current) SetWindowTextW(item,text.c_str());
}
void RaceControlStatus(const std::wstring& message) {
    raceControl.message=message; if(raceControl.status) SetRaceText(raceControl.status,message);
}
bool CanControlSim() {
    if(app.preview || app.diagnostics) { RaceControlStatus(L"Preview: camera / replay action simulated."); return true; }
    if(!app.frame.connected) { RaceControlStatus(L"Connect to iRacing to use the camera or replay."); return false; }
    if(app.frame.onTrack) { RaceControlStatus(L"Leave the car to control the spectator camera or replay."); return false; }
    return true;
}
HWND RaceItem(int id) { return GetDlgItem(raceControl.window,id); }
bool ReviewingReplay(const Frame& f) { return View(f)==ViewMode::Replay; }
void UpdateCameraButton() {
    if(!raceControl.window) return;
    const std::wstring text=L"Camera: "+(raceControl.cameraName.empty()?std::wstring(L"\u2014"):raceControl.cameraName)+L"  \u25BE";
    SetWindowTextW(RaceItem(RaceCameraId),text.c_str());
    EnableWindow(RaceItem(RaceCameraId),!raceControl.cameras.empty());
}
void UpdateRaceCameras() {
    std::vector<CameraGroup> groups;
    for(const auto& group:app.session.cameras) if(!group.scenic && !group.name.empty()) groups.push_back(group);
    bool same=groups.size()==raceControl.cameras.size();
    if(same) for(size_t i=0;i<groups.size();++i) if(groups[i].name!=raceControl.cameras[i].name || groups[i].number!=raceControl.cameras[i].number) same=false;
    if(same) return;
    raceControl.cameras=std::move(groups);
    // Keep the remembered camera when the session offers it, else prefer Far Chase.
    const bool known=std::any_of(raceControl.cameras.begin(),raceControl.cameras.end(),[](const CameraGroup& g){return Wide(g.name)==raceControl.cameraName;});
    if(!known && !raceControl.cameras.empty()) {
        const auto chase=std::find_if(raceControl.cameras.begin(),raceControl.cameras.end(),[](const CameraGroup& g){return g.name=="Far Chase";});
        raceControl.cameraName=Wide((chase!=raceControl.cameras.end()?*chase:raceControl.cameras.front()).name);
    }
    UpdateCameraButton();
}
// index into the session's non-scenic cameras; remembered by name across sessions.
void SetRaceCamera(int index) {
    if(index<0 || index>=static_cast<int>(raceControl.cameras.size())) return;
    raceControl.cameraName=Wide(raceControl.cameras[index].name);
    WritePrivateProfileStringW(L"RaceControl",L"Camera",raceControl.cameraName.c_str(),app.config.c_str());
    UpdateCameraButton();
}
void ChooseRaceCamera() {
    HMENU menu=CreatePopupMenu();
    for(int i=0;i<static_cast<int>(raceControl.cameras.size());++i) {
        const auto name=Wide(raceControl.cameras[i].name);
        AppendMenuW(menu,MF_STRING|(name==raceControl.cameraName?MF_CHECKED:0),i+1,name.c_str());
    }
    RECT button{}; GetWindowRect(RaceItem(RaceCameraId),&button);
    const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,button.left,button.bottom,0,raceControl.window,nullptr);
    DestroyMenu(menu);
    if(choice>0) SetRaceCamera(choice-1);
}
int RaceCameraGroup() {
    for(const auto& group:raceControl.cameras) if(Wide(group.name)==raceControl.cameraName) return group.number;
    RaceControlStatus(L"Waiting for iRacing camera groups. Choose a camera before jumping."); return -1;
}
void FocusRaceCar(const std::string& number) {
    if(!CanControlSim()) return;
    if(CameraNumber(number)<0) { RaceControlStatus(L"This car has no usable camera number."); return; }
    const int group=RaceCameraGroup(); if(group<0) return;
    if(SendSimMessage(CameraMessage(number,group))) RaceControlStatus(app.preview || app.diagnostics?L"Preview: camera action simulated.":L"Camera requested for #"+Wide(number));
    else RaceControlStatus(L"Could not send the camera command to iRacing.");
}
// Pauses, sends the jump, then switches the camera and plays once iRacing has moved there. A search through a long
// replay can take seconds; a camera and play sent before it ends are lost, and the replay stays on a still frame.
// So the replay position is watched, and only after a while does it play from wherever it is.
constexpr ULONGLONG RaceJumpWait=12000;
void SendRaceJump(const std::string& number,const SimMessage& jump,const std::wstring& done,int session=-1,double time=-1) {
    if(CameraNumber(number)<0) { RaceControlStatus(L"This car has no usable camera number."); return; }
    const int group=RaceCameraGroup(); if(group<0) return;
    if(!SendSimMessage({3,0,0}) || !SendSimMessage(jump)) { RaceControlStatus(L"Could not send the replay command to iRacing."); return; }
    raceControl.pendingNumber=number; raceControl.pendingGroup=group; raceControl.pendingSession=session; raceControl.pendingTime=time;
    raceControl.pendingUntil=GetTickCount64()+RaceJumpWait;
    SetTimer(raceControl.window,1,100,nullptr);
    RaceControlStatus(app.preview || app.diagnostics?L"Preview: replay action simulated.":done);
}
// Whether iRacing has reached the jump's target: within 2 s of its time, or near the live end for a live jump.
bool RaceJumpArrived(const Frame& f) {
    if(app.preview || app.diagnostics) return true;
    if(raceControl.pendingTime<0) return !ReviewingReplay(f);
    return f.replaySession==raceControl.pendingSession && f.replayTime>=0 && std::abs(f.replayTime-raceControl.pendingTime)<=2;
}
void FinishRaceJump(bool arrived) {
    KillTimer(raceControl.window,1);
    if(CanControlSim() && !raceControl.pendingNumber.empty()) {
        SendSimMessage(CameraMessage(raceControl.pendingNumber,raceControl.pendingGroup)); SendSimMessage({3,1,0});
        if(!arrived) RaceControlStatus(L"iRacing did not reach that moment within "+std::to_wstring(RaceJumpWait/1000)+L" s; it plays from where it is. The footage may be older than iRacing keeps in its replay buffer.");
    }
    raceControl.pendingNumber.clear();
}
// Live: camera on the car, leaving the replay first if needed.
void WatchRaceCarLive(const std::string& number) {
    if(!CanControlSim()) return;
    if(!ReviewingReplay(app.frame)) { FocusRaceCar(number); return; }
    SendRaceJump(number,{5,1,0},L"Live view requested for #"+Wide(number));
}
void ReplayRaceEvent(int selected,int lead) {
    if(selected<0 || selected>=static_cast<int>(raceControl.events.size())) { RaceControlStatus(L"Choose an event to replay."); return; }
    const auto& event=raceControl.events[selected];
    if(lead<=0) { WatchRaceCarLive(event.number); return; }
    if(!CanControlSim()) return;
    if(event.session<0) { RaceControlStatus(L"No replay target is available for this event."); return; }
    // Keep all 32 timestamp bits: the SDK packs them directly into LPARAM.
    SendRaceJump(event.number,ReplayMessage(event.session,event.time,lead),
        L"Replay requested: #"+Wide(event.number)+L" at "+EventClock(std::max(0.0,event.time-lead))+L". Footage must be in iRacing's replay buffer.",
        event.session,std::max(0.0,event.time-lead));
}
// From the moment on screen: the replay position while reviewing, else the live session time.
void ReplayRaceCar(const std::string& number,int lead) {
    if(lead<=0) { WatchRaceCarLive(number); return; }
    if(!CanControlSim()) return;
    const auto& f=app.frame;
    const bool review=ReviewingReplay(f); const int session=review?f.replaySession:f.session; const double time=review?f.replayTime:f.time;
    SendRaceJump(number,ReplayMessage(std::max(0,session),time,lead),L"Replay requested: #"+Wide(number)+L", "+std::to_wstring(lead)+L" s back.",std::max(0,session),std::max(0.0,time-lead));
}
int RaceReplayLead() { return RaceReplayLeads[std::clamp(raceControl.replayMode,0,2)]; }
void SetRaceReplayMode(int mode) {
    raceControl.replayMode=std::clamp(mode,0,2); raceControl.replay.SetSelected(raceControl.replayMode);
    WritePrivateProfileStringW(L"RaceControl",L"ReplayFrom",std::to_wstring(raceControl.replayMode).c_str(),app.config.c_str());
}
int SelectedEventIndex() {
    for(int i=0;i<static_cast<int>(raceControl.events.size());++i) if(raceControl.events[i].id==raceControl.selectedEvent) return i;
    return -1;
}
int SelectedRowIndex() { return raceControl.standings?ListView_GetNextItem(raceControl.standings,-1,LVNI_SELECTED):-1; }
void ActOnSelectedEvent() { ReplayRaceEvent(SelectedEventIndex(),RaceReplayLead()); }
void UpdateRaceSelection();
// Selects the event before (-1) or after (+1) the selected one in time, among those shown, and replays it.
// Without a selection, Previous starts at the latest and Next at the earliest.
void StepRaceEvent(int step) {
    const int count=static_cast<int>(raceControl.events.size());
    if(!count) { RaceControlStatus(L"No events to step through."); return; }
    std::vector<int> order(count); for(int i=0;i<count;++i) order[i]=i;
    std::stable_sort(order.begin(),order.end(),[](int a,int b){ const auto& x=raceControl.events[a]; const auto& y=raceControl.events[b]; return x.time!=y.time?x.time<y.time:x.id<y.id; });
    const int selected=SelectedEventIndex();
    const int at=selected<0?(step>0?-1:count):static_cast<int>(std::find(order.begin(),order.end(),selected)-order.begin());
    const int target=at+step;
    if(target<0 || target>=count) { RaceControlStatus(step<0?L"This is the earliest event shown.":L"This is the latest event shown."); return; }
    const int index=order[target];
    raceControl.selectedEvent=raceControl.events[index].id;
    ListView_SetItemState(raceControl.feed,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemState(raceControl.feed,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_EnsureVisible(raceControl.feed,index,FALSE);
    UpdateRaceSelection(); ReplayRaceEvent(index,RaceReplayLead());
}
void ActOnSelectedDriver() { if(const int row=SelectedRowIndex(); row>=0 && row<static_cast<int>(raceControl.rows.size())) ReplayRaceCar(raceControl.rows[row].driver.number,RaceReplayLead()); }
void ApplyRaceOnTop() {
    if(!raceControl.window) return;
    SetWindowPos(raceControl.window,raceControl.onTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SendMessageW(RaceItem(RaceOnTopId),BM_SETCHECK,raceControl.onTop?BST_CHECKED:BST_UNCHECKED,0);
}
void SetRaceOnTop(bool on) {
    raceControl.onTop=on;
    WritePrivateProfileStringW(L"RaceControl",L"AlwaysOnTop",on?L"1":L"0",app.config.c_str());
    ApplyRaceOnTop(); RefreshPage();
}
void UpdateRaceControl(bool force);
void SetRaceEventFilter(int category) { raceControl.category=std::clamp(category,0,3); raceControl.filter.SetSelected(raceControl.category); UpdateRaceControl(true); }
void SetRaceCarFilter(int car) { raceControl.car=car; UpdateRaceControl(true); }
void RaceControlFonts() {
    const int dpi=GetDpiForWindow(raceControl.window);
    if(raceControl.font) DeleteObject(raceControl.font);
    if(raceControl.heading) DeleteObject(raceControl.heading);
    raceControl.font=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    raceControl.heading=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
}
void ApplyRaceControlFonts() {
    for(HWND child=GetWindow(raceControl.window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(raceControl.font),TRUE);
    raceControl.banner.SetFonts(raceControl.heading,raceControl.font);
}
// Sizes in DIPs.
void LayoutRaceControl() {
    if(!raceControl.window) return;
    RECT client{}; GetClientRect(raceControl.window,&client); const int dpi=GetDpiForWindow(raceControl.window);
    auto d=[&](int n){return MulDiv(n,dpi,96);};
    auto place=[&](HWND window,int x,int y,int w,int h){ if(window) SetWindowPos(window,nullptr,x,y,std::max(1,w),std::max(1,h),SWP_NOZORDER|SWP_NOACTIVATE); };
    const int margin=d(12), width=client.right, height=client.bottom;
    place(raceControl.banner.Handle(),margin,margin,width-2*margin,d(56));
    const bool info=std::any_of(raceControl.info.begin(),raceControl.info.end(),[](bool on){return on;});
    const int infoHeight=info?d(30):0;
    ShowWindow(RaceItem(RaceInfoId),info?SW_SHOW:SW_HIDE);
    if(info) place(RaceItem(RaceInfoId),margin,d(74),width-2*margin,d(26));
    const int toolbar=d(80)+infoHeight;
    const int segmented=raceControl.filter.IdealWidth();
    place(raceControl.filter.Handle(),margin,toolbar,segmented,d(30));
    const int available=std::max(0,width-3*margin), minimum=std::min(d(240),available/2);
    const int left=std::clamp(MulDiv(available,raceControl.splitPermille,1000),minimum,available-minimum), right=available-left;
    // Toolbars span the window independently of the table split. Wrap the event
    // controls when a car filter would otherwise overlap the camera buttons.
    const int cameraX=width-margin-d(118)-d(8)-d(200), onlyCarX=cameraX-d(8)-d(118);
    const bool pill=IsWindowVisible(RaceItem(RaceCarPillId))!=FALSE;
    const bool wrap=margin+segmented+d(10)+(pill?d(200):0)+d(40)+d(8)>onlyCarX;
    const int eventY=toolbar+(wrap?d(36):0);
    int x=margin+segmented+d(10);
    if(wrap) x=margin;
    if(pill) { place(RaceItem(RaceCarPillId),x,eventY,d(190),d(30)); x+=d(200); }
    const int onlyCar=wrap?width-margin-d(118):onlyCarX;
    place(RaceItem(RaceCountId),x,eventY,std::max(d(40),onlyCar-d(8)-x),d(30));
    place(RaceItem(RaceOnlyCarId),onlyCar,eventY,d(118),d(30));
    place(RaceItem(RaceOnTopId),width-margin-d(118),toolbar,d(118),d(30));
    place(RaceItem(RaceCameraId),cameraX,toolbar,d(200),d(30));
    const int top=d(118)+infoHeight+(wrap?d(36):0), bar=d(32), message=d(22);
    const int listHeight=height-top-bar-d(8)-message-margin;
    place(raceControl.feed,margin,top,left,listHeight);
    place(raceControl.standings,2*margin+left,top,right,listHeight);
    place(raceControl.splitter,margin+left,top,margin,listHeight);
    const int barY=top+listHeight+d(6);
    int bx=margin;
    place(RaceItem(RaceReplayLabelId),bx,barY,d(76),d(28)); bx+=d(78);
    const int replay=raceControl.replay.IdealWidth();
    place(raceControl.replay.Handle(),bx,barY,replay,d(28)); bx+=replay+d(12);
    place(RaceItem(RacePreviousId),bx,barY,d(88),d(28)); bx+=d(92);
    place(RaceItem(RaceNextId),bx,barY,d(88),d(28)); bx+=d(100);
    const int watchX=width-margin-d(228);
    place(RaceItem(RaceSelectedId),bx,barY,watchX-d(8)-bx,d(28));
    place(RaceItem(RaceWatchId),watchX,barY,d(80),d(28));
    place(RaceItem(RaceCarEventsId),watchX+d(88),barY,d(140),d(28));
    place(raceControl.status,margin,height-margin-message,width-2*margin,message);
}
void SaveRaceSplit() {
    const auto value=std::to_wstring(raceControl.splitPermille);
    WritePrivateProfileStringW(L"RaceControl",L"SplitPermille",value.c_str(),app.config.c_str());
}
void SetRaceSplit(int value,bool save) {
    value=std::clamp(value,100,900);
    if(value!=raceControl.splitPermille) {
        raceControl.splitPermille=value; LayoutRaceControl();
        RedrawWindow(raceControl.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    if(save) SaveRaceSplit();
}
LRESULT CALLBACK RaceSplitterProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_SIZEWE)); return TRUE;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
        SetFocus(w); raceControl.splitBeforeDrag=raceControl.splitPermille;
        raceControl.splitGrab=GET_X_LPARAM(lp); raceControl.splitDragging=true;
        SetCapture(w); InvalidateRect(w,nullptr,FALSE); return 0;
    case WM_MOUSEMOVE:
        if(raceControl.splitDragging && GetCapture()==w) {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; MapWindowPoints(w,raceControl.window,&point,1);
            RECT client{}; GetClientRect(raceControl.window,&client);
            const int margin=MulDiv(12,GetDpiForWindow(w),96), available=client.right-3*margin;
            if(available>0) {
                const int minimum=std::min(MulDiv(240,GetDpiForWindow(w),96),available/2);
                const int left=std::clamp<int>(point.x-margin-raceControl.splitGrab,minimum,available-minimum);
                SetRaceSplit(MulDiv(left,1000,available),false);
            }
        }
        return 0;
    case WM_LBUTTONUP: if(GetCapture()==w) ReleaseCapture(); return 0;
    case WM_CAPTURECHANGED:
        if(raceControl.splitDragging) { raceControl.splitDragging=false; SaveRaceSplit(); InvalidateRect(w,nullptr,FALSE); }
        return 0;
    case WM_CANCELMODE:
        if(raceControl.splitDragging) { SetRaceSplit(raceControl.splitBeforeDrag,false); ReleaseCapture(); }
        return 0;
    case WM_LBUTTONDBLCLK: SetRaceSplit(560,true); return 0;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE && raceControl.splitDragging) { SetRaceSplit(raceControl.splitBeforeDrag,false); ReleaseCapture(); return 0; }
        if(wp==VK_LEFT || wp==VK_RIGHT) { SetRaceSplit(raceControl.splitPermille+(wp==VK_LEFT?-10:10),true); return 0; }
        if(wp==VK_HOME) { SetRaceSplit(560,true); return 0; }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(w,nullptr,FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const HDC dc=BeginPaint(w,&paint); RECT bounds{}; GetClientRect(w,&bounds);
        FillRect(dc,&bounds,app.brush);
        const int dpi=GetDpiForWindow(w), center=bounds.right/2, middle=bounds.bottom/2;
        const int thickness=std::max(1,MulDiv(2,dpi,96)), half=MulDiv(16,dpi,96);
        RECT grip{center-thickness/2,std::max(0,middle-half),center+(thickness+1)/2,std::min<int>(bounds.bottom,middle+half)};
        SetDCBrushColor(dc,GetFocus()==w || raceControl.splitDragging?RGB(46,135,220):RGB(171,178,187));
        FillRect(dc,&grip,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(GetFocus()==w) { InflateRect(&grip,MulDiv(3,dpi,96),MulDiv(3,dpi,96)); DrawFocusRect(dc,&grip); }
        EndPaint(w,&paint); return 0;
    }
    }
    return DefWindowProcW(w,m,wp,lp);
}
bool RaceCarInactive(const Row& row) { return row.car.surface<0 && !row.car.pit; }
// One status per car: flags first, then where the car is. Empty while it simply drives.
struct RaceStatus { std::wstring text; int tone=0; }; // tone: 1 red, 2 amber, 3 gray
RaceStatus RaceCarStatus(const Row& row) {
    if(!app.frame.connected) return {};
    const unsigned flags=row.car.sessionFlags;
    if(flags&SessionFlag::Disqualify) return {L"DQ",1};
    if(flags&SessionFlag::Black) return {L"Black",1};
    if(flags&SessionFlag::Repair) return {L"Repair",2};
    if(flags&SessionFlag::Furled) return {L"Slow",2};
    if(row.car.towing) return {L"Tow",2};
    if(row.car.surface==1) return {L"Stall",3};
    if(row.car.pit) return {L"Pit",3};
    if(row.car.surface==2) return {L"Pit in",3};
    if(row.car.surface==0) return {L"Off track",2};
    if(row.car.outLap) return {L"Out",3};
    if(row.car.surface==3) return {};
    return {L"Inactive",3};
}
// Incident points: always known for your own car, for others only when the server shares them.
int RaceIncidents(const Row& row) {
    if(row.driver.index==app.frame.player && app.frame.incidentsKnown) return app.frame.incidents;
    return row.driver.incidentCount;
}
bool RaceIncidentsHigh(int incidents) {
    const int limit=app.session.incidentLimit;
    return incidents>=0 && (limit>0?incidents*2>=limit:incidents>=10);
}
double RaceStintSeconds(const Row& row) {
    const double missing=std::numeric_limits<double>::quiet_NaN();
    if(!app.frame.connected || app.session.type!="Race" || View(app.frame)==ViewMode::Replay || row.car.pit || row.car.towing || row.car.surface<0) return missing;
    if(app.preview) return row.car.stintLaps>=0?row.car.stintLaps*139.4:missing;
    const double start=row.car.stintStartTime;
    return std::isfinite(start) && start>=0 && std::isfinite(app.frame.time) && app.frame.time>=start?app.frame.time-start:missing;
}
int RaceRatingChange(const Row& row) {
    const int index=row.driver.index;
    return index>=0 && index<MaxCars?RatingChanges()[index]:NoRatingChange;
}
std::wstring RaceStandingCell(const Row& row,int column) {
    switch(column) {
    case 0: { const int p=First(StandingsKind).table.standings.separateClasses?row.car.classPosition:row.car.position; return p>0?std::to_wstring(p):L"\u2014"; }
    case 1:return Wide(row.driver.number);
    case 2:return Wide(row.driver.name);
    case 3:return Wide(row.driver.className);
    case 4:return Gap(row,First(StandingsKind));
    case 5:return Interval(row,First(StandingsKind).table);
    case 6:return Lap(row.car.best);
    case 7:return Lap(row.car.last);
    case 8:return RaceCarStatus(row).text;
    case 15: { const auto flags=Wide(DriverFlags(row.car.sessionFlags)); return flags.empty()?L"\u2014":flags; }
    case 16:return CompletedLaps(row);
    case 17: { const int gained=PositionsGained(app.session,row,First(StandingsKind).table.standings.separateClasses); return gained==-MaxCars?L"\u2014":(gained>0?L"+":L"")+std::to_wstring(gained); }
    case 18: { const int incidents=RaceIncidents(row); return incidents>=0?std::to_wstring(incidents):L"\u2014"; }
    case RaceFuelField: { const auto status=FuelFinishStatus(row.driver.index); return status==OpponentFuelStatus::End?L"END":(status==OpponentFuelStatus::Save?L"SAVE":L""); }
    case RaceStintLapsField: return row.car.stintLaps>=0?std::to_wstring(row.car.stintLaps):L"\u2014";
    case RaceStintTimeField: return FooterDuration(RaceStintSeconds(row));
    case RaceStopsField: return row.car.stops>=0?std::to_wstring(row.car.stops):L"\u2014";
    case RaceRatingChangeField: { const int change=RaceRatingChange(row); return change==NoRatingChange?L"\u2014":(change>0?L"+":L"")+std::to_wstring(change); }
    case RaceBrandField: return row.driver.brand.empty()?L"\u2014":Wide(row.driver.brand);
    default:return DriverValue(row,column-9);
    }
}
#include "RaceControlFields.inc"
std::wstring RaceCarName(int car) {
    for(const auto& d:app.session.drivers) if(d.index==car) return L"#"+Wide(d.number)+L" "+Wide(d.name);
    for(const auto& e:app.events.Events()) if(e.car==car) return L"#"+Wide(e.number)+L" "+Wide(e.driver);
    return L"Car";
}
void UpdateRaceBanner() {
    using Tone=StatusPanel::Tone;
    const auto& f=app.frame;
    std::wstring detail=Wide(app.session.type);
    auto add=[&](const std::wstring& part) { if(!part.empty() && part!=L"\u2014") detail+=(detail.empty()?L"":L"  \u00B7  ")+part; };
    if(app.preview) { raceControl.banner.SetState(Tone::Busy,L"Preview",L"Sample data; camera and replay actions are simulated.",L"Go live",true); return; }
    if(!f.connected) { raceControl.banner.SetState(Tone::Neutral,L"Waiting for iRacing",L"Recorded events stay available for this session.",L"Go live",false); return; }
    add(FooterValue(TimeRemainingMetric)); add(FooterValue(CurrentLapMetric));
    if(ReviewingReplay(f)) raceControl.banner.SetState(Tone::Busy,L"Replay  \u00B7  "+EventClock(f.replayTime),detail,L"Go live",true);
    else raceControl.banner.SetState(Tone::Active,L"Live",detail,L"Go live",!f.onTrack);
}
void UpdateRaceInfo() {
    std::wstring text;
    auto add=[&](const std::wstring& value) { if(!text.empty()) text+=L"   \u00B7   "; text+=value; };
    if(raceControl.info[RaceLeaderDistanceInfo]) add(L"Leader projection: "+LeaderDistanceValue()+L" laps");
    if(raceControl.info[RaceWhiteFlagInfo]) add(L"Clock at leader's final lap: "+WhiteFlagTimeValue());
    SetRaceText(RaceItem(RaceInfoId),text);
}
void UpdateRaceSelection() {
    const int index=SelectedEventIndex();
    std::wstring text;
    if(index>=0) { const auto& e=raceControl.events[index]; text=std::wstring(EventName(e.type))+L"  \u00B7  #"+Wide(e.number)+L" "+Wide(e.driver)+L"  \u00B7  "+EventClock(e.time); }
    SetRaceText(RaceItem(RaceSelectedId),text);
    EnableWindow(RaceItem(RaceOnlyCarId),index>=0);
    EnableWindow(RaceItem(RacePreviousId),!raceControl.events.empty()); EnableWindow(RaceItem(RaceNextId),!raceControl.events.empty());
    const int row=SelectedRowIndex();
    EnableWindow(RaceItem(RaceWatchId),row>=0); EnableWindow(RaceItem(RaceCarEventsId),row>=0);
}
void UpdateRaceControl(bool force) {
    if(!raceControl.window || (!force && (!IsWindowVisible(raceControl.window) || IsIconic(raceControl.window)))) return;
    const auto now=GetTickCount64(); if(!force && now-raceControl.updated<500) return; raceControl.updated=now;
    UpdateRaceCameras(); UpdateRaceBanner(); UpdateRaceInfo();
    auto options=First(StandingsKind).table.standings; options.leaders=MaxCars; options.ahead=options.behind=0;
    std::vector<Row> rows; for(const auto& group:SelectStandings(app.session,app.frame,options)) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
    const int reference=ReferenceCar(app.frame);
    int referenceClass=-1; for(const auto& d:app.session.drivers) if(d.index==reference) referenceClass=d.classId;
    for(const auto& driver:app.session.drivers) {
        if(driver.index<0 || driver.index>=MaxCars || (options.ownClass && (driver.classId!=referenceClass))) continue;
        if(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.driver.index==driver.index;})) {
            Row row{driver,app.frame.cars[driver.index]}; row.player=driver.index==reference; rows.push_back(row);
        }
    }
    SortRaceRows(rows);
    // The feed is filtered and sorted again only when the events, the filter or the sort change.
    const std::wstring feedKey=std::to_wstring(app.preview?0:app.events.Revision()+1)+L":"+std::to_wstring(raceControl.category)+L":"+std::to_wstring(raceControl.car)+L":"+std::to_wstring(raceControl.eventSortColumn)+std::to_wstring(raceControl.eventSortDescending);
    if(force || feedKey!=raceControl.feedKey) {
        std::vector<RaceEvent> all;
        if(app.preview) {
            const EventType types[]={EventType::Offtrack,EventType::Incident,EventType::Crash,EventType::PitEntry,EventType::PitStop,EventType::Overtake};
            for(int i=5;i>=0;--i) {
                const auto& driver=app.session.drivers[i]; RaceEvent e;
                e.id=i+1; e.type=types[i]; e.session=0; e.car=driver.index; e.time=600+i*42; e.lap=4+i/2; e.number=driver.number; e.driver=driver.name;
                e.estimated=i==2 || i==5; e.detail=i==2?"New repair flag; possible crash":(i==4?"Pit lane visit: 42 s":(i==5?"Passed #12 Luca Weber":"Observed during this session"));
                all.push_back(e);
            }
        } else { const auto& recorded=app.events.Events(); all.assign(recorded.rbegin(),recorded.rend()); }
        auto& events=raceControl.events; events.clear();
        for(const auto& e:all) if((raceControl.category==0 || EventCategory(e.type)==raceControl.category) && (raceControl.car<0 || e.car==raceControl.car)) events.push_back(e);
        SortRaceEvents(events); raceControl.total=all.size(); raceControl.feedKey=feedKey;
    }
    const auto& events=raceControl.events;
    std::vector<int> columns; for(int id:RaceFieldOrder) if(raceControl.fields[id]) columns.push_back(id);
    std::vector<int> feedColumns; for(int id:RaceEventOrder) if(raceControl.eventFields[id]) feedColumns.push_back(id);
    const std::wstring count=std::to_wstring(events.size())+L" of "+std::to_wstring(raceControl.total)+L" events";
    SetRaceText(RaceItem(RaceCountId),count);
    const bool pill=raceControl.car>=0;
    if(pill!=(IsWindowVisible(RaceItem(RaceCarPillId))!=FALSE)) { ShowWindow(RaceItem(RaceCarPillId),pill?SW_SHOW:SW_HIDE); LayoutRaceControl(); }
    if(pill) SetRaceText(RaceItem(RaceCarPillId),RaceCarName(raceControl.car)+L"  \u00D7");
    std::wstring signature=std::to_wstring(raceControl.car)+L":"+std::to_wstring(raceControl.category)+L":"+std::to_wstring(raceControl.sortColumn)+std::to_wstring(raceControl.sortDescending)+std::to_wstring(raceControl.eventSortColumn)+std::to_wstring(raceControl.eventSortDescending)+std::to_wstring(First(StandingsKind).table.standings.separateClasses)+std::to_wstring(app.frame.connected)+std::to_wstring(app.frame.onTrack);
    for(int col:columns) signature+=L"|"+std::to_wstring(col); for(int col:feedColumns) signature+=L"E"+std::to_wstring(col);
    for(const auto& row:rows) { signature+=std::to_wstring(row.driver.index); for(int col:columns) signature+=L"|"+RaceStandingCell(row,col); }
    signature+=L"#"+feedKey;
    if(!force && signature==raceControl.signature) return;
    raceControl.signature=std::move(signature);
    const int oldRow=SelectedRowIndex();
    const int selectedCar=oldRow>=0 && oldRow<static_cast<int>(raceControl.rows.size())?raceControl.rows[oldRow].driver.index:-1;
    SendMessageW(raceControl.standings,WM_SETREDRAW,FALSE,0); SendMessageW(raceControl.feed,WM_SETREDRAW,FALSE,0);
    const int dpi=GetDpiForWindow(raceControl.window);
    if(columns!=raceControl.columns || force) {
        while(ListView_DeleteColumn(raceControl.standings,0)) {}
        for(int i=0;i<static_cast<int>(columns.size());++i) {
            const int c=columns[i]; LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
            col.pszText=const_cast<wchar_t*>(RaceFieldTitle(c)); col.cx=MulDiv(RaceFieldWidth(c),dpi,96); col.fmt=c==2 || c==3 || c==8 || c==15 || c==RaceBrandField?LVCFMT_LEFT:LVCFMT_RIGHT;
            ListView_InsertColumn(raceControl.standings,i,&col);
        }
        raceControl.columns=columns;
    }
    if(feedColumns!=raceControl.feedColumns || force) {
        while(ListView_DeleteColumn(raceControl.feed,0)) {}
        for(int i=0;i<static_cast<int>(feedColumns.size());++i) {
            const int c=feedColumns[i]; LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT; col.fmt=c==4 || c==RaceEventLapField?LVCFMT_RIGHT:LVCFMT_LEFT;
            col.pszText=const_cast<wchar_t*>(RaceEventTitles[c]); col.cx=MulDiv(RaceEventWidths[c],dpi,96); ListView_InsertColumn(raceControl.feed,i,&col);
        }
        raceControl.feedColumns=feedColumns;
    }
    UpdateRaceSortIndicators();
    raceControl.rows=std::move(rows);
    ListView_SetItemCountEx(raceControl.standings,static_cast<int>(raceControl.rows.size()),LVSICF_NOSCROLL);
    ListView_SetItemCountEx(raceControl.feed,static_cast<int>(raceControl.events.size()),LVSICF_NOSCROLL);
    ListView_SetItemState(raceControl.standings,-1,0,LVIS_SELECTED); ListView_SetItemState(raceControl.feed,-1,0,LVIS_SELECTED);
    for(int i=0;i<static_cast<int>(raceControl.rows.size());++i) if(raceControl.rows[i].driver.index==selectedCar) ListView_SetItemState(raceControl.standings,i,LVIS_SELECTED,LVIS_SELECTED);
    if(const int index=SelectedEventIndex(); index>=0) ListView_SetItemState(raceControl.feed,index,LVIS_SELECTED,LVIS_SELECTED);
    SendMessageW(raceControl.standings,WM_SETREDRAW,TRUE,0); SendMessageW(raceControl.feed,WM_SETREDRAW,TRUE,0);
    InvalidateRect(raceControl.standings,nullptr,FALSE); InvalidateRect(raceControl.feed,nullptr,FALSE);
    UpdateRaceSelection();
    if(raceControl.message.empty()) RaceControlStatus(L"Choose Live, 5 s or 10 s, then double-click (or Enter) an event or a driver. F shows one car's events.");
}
// Owner-drawn cells: class strip, status and event pills. Everything else uses the list's own text drawing.
void DrawRacePill(HDC dc,RECT cell,const std::wstring& text,PillColors colors,HFONT font,bool center) {
    if(text.empty()) return;
    const auto old=SelectObject(dc,font); SIZE size{}; GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
    const int dpi=GetDpiForWindow(raceControl.window), pad=MulDiv(7,dpi,96), height=MulDiv(18,dpi,96);
    const int width=std::min<int>(size.cx+2*pad,cell.right-cell.left-MulDiv(8,dpi,96));
    const int left=center?(cell.left+cell.right-width)/2:cell.left+MulDiv(5,dpi,96), top=(cell.top+cell.bottom-height)/2;
    HBRUSH brush=CreateSolidBrush(colors.fill); HPEN pen=CreatePen(PS_SOLID,1,colors.fill);
    const auto oldBrush=SelectObject(dc,brush); const auto oldPen=SelectObject(dc,pen);
    RoundRect(dc,left,top,left+width,top+height,height,height);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
    RECT label{left+pad,top,left+width-pad,top+height};
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,colors.text);
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&label,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);
}
COLORREF RaceRowBackground(HWND list,size_t index,const Row* row) {
    if(ListView_GetItemState(list,static_cast<int>(index),LVIS_SELECTED)) return RGB(230,241,251);
    if(row && row->player) return RGB(250,244,228);
    return index%2?Background:RGB(255,255,255);
}
LRESULT RaceCustomDraw(NMLVCUSTOMDRAW& draw) {
    const HWND list=draw.nmcd.hdr.hwndFrom; const bool feed=list==raceControl.feed;
    if(draw.nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    const size_t index=draw.nmcd.dwItemSpec;
    if(feed?index>=raceControl.events.size():index>=raceControl.rows.size()) return CDRF_DODEFAULT;
    const Row* row=feed?nullptr:&raceControl.rows[index];
    if(draw.nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
        // Draw the selection as a light tint instead of the system highlight.
        draw.nmcd.uItemState&=~(CDIS_SELECTED|CDIS_FOCUS);
        draw.clrTextBk=RaceRowBackground(list,index,row); draw.clrText=Text;
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    if(draw.nmcd.dwDrawStage!=(CDDS_ITEMPREPAINT|CDDS_SUBITEM)) return CDRF_DODEFAULT;
    const auto& columns=feed?raceControl.feedColumns:raceControl.columns;
    if(draw.iSubItem<0 || draw.iSubItem>=static_cast<int>(columns.size())) return CDRF_DODEFAULT;
    const int column=columns[draw.iSubItem];
    draw.clrTextBk=RaceRowBackground(list,index,row);
    RECT cell{}; ListView_GetSubItemRect(list,static_cast<int>(index),draw.iSubItem,LVIR_BOUNDS,&cell);
    if(draw.iSubItem==0) cell.right=cell.left+ListView_GetColumnWidth(list,0);
    const HDC dc=draw.nmcd.hdc; const int dpi=GetDpiForWindow(raceControl.window);
    auto fillCell=[&]{ SetDCBrushColor(dc,draw.clrTextBk); FillRect(dc,&cell,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); };
    if(feed) {
        draw.clrText=Text;
        if(column!=3) return CDRF_NEWFONT;
        const auto& e=raceControl.events[index];
        fillCell(); DrawRacePill(dc,cell,std::wstring(EventName(e.type))+(e.estimated?L"?":L""),EventColors(e.type),raceControl.font,false);
        return CDRF_SKIPDEFAULT;
    }
    const bool inactive=RaceCarInactive(*row) || !app.frame.connected;
    draw.clrText=inactive?RGB(145,150,157):(row->player?RGB(140,96,0):Text);
    if(column==9) { const unsigned c=row->driver.licenseColor; draw.clrText=RGB((c>>16)&255,(c>>8)&255,c&255); }
    if(column==11) draw.clrText=TireTextColor(*row);
    if(column==15 && row->car.sessionFlags) draw.clrText=(row->car.sessionFlags&(SessionFlag::Black|SessionFlag::Disqualify))?RGB(180,40,35):RGB(155,90,15);
    if(column==RaceIncidentsField && RaceIncidentsHigh(RaceIncidents(*row))) draw.clrText=RGB(163,45,45);
    if(column==RaceRatingChangeField && !inactive) {
        const int change=RaceRatingChange(*row);
        if(change!=NoRatingChange && change) draw.clrText=change>0?RGB(25,125,65):RGB(175,45,40);
    }
    if(GetRValue(draw.clrText)+GetGValue(draw.clrText)+GetBValue(draw.clrText)>500) draw.clrText=RGB(GetRValue(draw.clrText)/2,GetGValue(draw.clrText)/2,GetBValue(draw.clrText)/2);
    if(column==RaceFuelField) {
        const auto status=FuelFinishStatus(row->driver.index);
        const PillColors colors=status==OpponentFuelStatus::End?PillColors{Ink::Green,Ink::Panel}:PillColors{Ink::Yellow,Ink::Panel};
        fillCell();
        if(draw.iSubItem==0) {
            const unsigned color=row->driver.classColor; RECT strip{cell.left,cell.top+1,cell.left+MulDiv(4,dpi,96),cell.bottom-1};
            SetDCBrushColor(dc,RGB((color>>16)&255,(color>>8)&255,color&255)); FillRect(dc,&strip,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        DrawRacePill(dc,cell,RaceStandingCell(*row,column),colors,raceControl.font,true);
        return CDRF_SKIPDEFAULT;
    }
    if(draw.iSubItem==0) {
        // Class color strip on the left edge of each row.
        fillCell();
        const unsigned c=row->driver.classColor; RECT strip{cell.left,cell.top+1,cell.left+MulDiv(4,dpi,96),cell.bottom-1};
        SetDCBrushColor(dc,RGB((c>>16)&255,(c>>8)&255,c&255)); FillRect(dc,&strip,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT text=cell; text.left=strip.right+MulDiv(4,dpi,96); text.right-=MulDiv(6,dpi,96);
        const auto value=RaceStandingCell(*row,column);
        const auto old=SelectObject(dc,raceControl.font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,draw.clrText);
        DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS|(column==2 || column==3 || column==8 || column==15 || column==RaceBrandField?DT_LEFT:DT_RIGHT));
        SelectObject(dc,old); return CDRF_SKIPDEFAULT;
    }
    if(column==8) {
        const auto status=RaceCarStatus(*row);
        const PillColors colors[]={{0,0},{RGB(252,235,235),RGB(121,31,31)},{RGB(250,238,218),RGB(133,79,11)},{RGB(241,239,232),RGB(95,94,90)}};
        fillCell(); DrawRacePill(dc,cell,status.text,colors[status.tone],raceControl.font,false);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_NEWFONT;
}
LRESULT CALLBACK RaceControlProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_SIZE:
        LayoutRaceControl(); if(wp!=SIZE_MINIMIZED) UpdateRaceControl(true);
        // Moved controls leave their old pixels behind otherwise.
        RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        return 0;
    case WM_GETMINMAXINFO: { auto* size=reinterpret_cast<MINMAXINFO*>(lp); const int dpi=GetDpiForWindow(w); size->ptMinTrackSize={MulDiv(900,dpi,96),MulDiv(520,dpi,96)}; return 0; }
    case WM_COMMAND: {
        const int id=LOWORD(wp),code=HIWORD(wp);
        if(id==RaceFilterId && code==SegmentedControl::kChanged) SetRaceEventFilter(raceControl.filter.Selected());
        else if(id==RaceCarPillId) SetRaceCarFilter(-1);
        else if(id==RaceCameraId) ChooseRaceCamera();
        else if(id==RaceOnTopId) SetRaceOnTop(SendMessageW(RaceItem(RaceOnTopId),BM_GETCHECK,0,0)==BST_CHECKED);
        else if(id==RaceReplayId && code==SegmentedControl::kChanged) SetRaceReplayMode(raceControl.replay.Selected());
        else if(id==RaceOnlyCarId) { if(const int index=SelectedEventIndex(); index>=0) SetRaceCarFilter(raceControl.events[index].car); }
        else if(id==RaceWatchId) ActOnSelectedDriver();
        else if(id==RacePreviousId || id==RaceNextId) StepRaceEvent(id==RaceNextId?1:-1);
        else if(id==RaceCarEventsId) { if(const int row=SelectedRowIndex(); row>=0 && row<static_cast<int>(raceControl.rows.size())) SetRaceCarFilter(raceControl.rows[row].driver.index); }
        else if(id==RaceGoLiveId && CanControlSim()) {
            KillTimer(w,1); raceControl.pendingNumber.clear(); SendSimMessage({5,1,0}); SendSimMessage({3,1,0}); RaceControlStatus(app.preview?L"Preview: live action simulated.":L"Live replay view requested.");
        }
        return 0;
    }
    case WM_TIMER:
        if(wp==1) {
            // Telemetry keeps coming in between; arrived, or waited long enough, the camera and play follow.
            const bool arrived=RaceJumpArrived(app.frame);
            if(arrived || GetTickCount64()>=raceControl.pendingUntil || raceControl.pendingNumber.empty()) FinishRaceJump(arrived);
        }
        return 0;
    case WM_NOTIFY: {
        const auto* header=reinterpret_cast<NMHDR*>(lp);
        const bool feed=header->hwndFrom==raceControl.feed, standings=header->hwndFrom==raceControl.standings;
        if(!feed && !standings) return 0;
        switch(header->code) {
        case LVN_COLUMNCLICK: {
            const auto* click=reinterpret_cast<NMLISTVIEW*>(lp);
            const auto& columns=feed?raceControl.feedColumns:raceControl.columns;
            if(click->iSubItem>=0 && click->iSubItem<static_cast<int>(columns.size())) SetRaceSort(feed,columns[click->iSubItem]);
            return 0;
        }
        case LVN_GETDISPINFOW: {
            auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp); std::wstring text;
            const int index=info->item.iItem; int column=info->item.iSubItem;
            if(standings && index>=0 && index<static_cast<int>(raceControl.rows.size()) && column<static_cast<int>(raceControl.columns.size())) text=RaceStandingCell(raceControl.rows[index],raceControl.columns[column]);
            if(feed && index>=0 && index<static_cast<int>(raceControl.events.size()) && column<static_cast<int>(raceControl.feedColumns.size())) {
                const auto& e=raceControl.events[index];
                switch(raceControl.feedColumns[column]) {
                case 2: text=EventClock(e.time); break;
                case 3: text=std::wstring(EventName(e.type))+(e.estimated?L"?":L""); break;
                case 4: text=Wide(e.number); break;
                case 5: text=Wide(e.driver); break;
                case 6: text=Wide(e.detail); break;
                case RaceEventLapField: text=e.lap>0?std::to_wstring(e.lap):L"\u2014"; break;
                }
            }
            if(info->item.mask&LVIF_TEXT) lstrcpynW(info->item.pszText,text.c_str(),info->item.cchTextMax);
            return 0;
        }
        case LVN_ITEMCHANGED: {
            const auto* change=reinterpret_cast<NMLISTVIEW*>(lp);
            if(feed && (change->uNewState&LVIS_SELECTED) && change->iItem>=0 && change->iItem<static_cast<int>(raceControl.events.size())) raceControl.selectedEvent=raceControl.events[change->iItem].id;
            UpdateRaceSelection(); return 0;
        }
        case NM_DBLCLK: case NM_RETURN: {
            if(feed) ActOnSelectedEvent(); else ActOnSelectedDriver();
            return 0;
        }
        case LVN_KEYDOWN: {
            const WORD key=reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey;
            if(key==VK_ESCAPE) SetRaceCarFilter(-1);
            else if(key=='F' && standings) { if(const int row=SelectedRowIndex(); row>=0 && row<static_cast<int>(raceControl.rows.size())) SetRaceCarFilter(raceControl.rows[row].driver.index); }
            else if(key=='F' && feed) { if(const int index=SelectedEventIndex(); index>=0) SetRaceCarFilter(raceControl.events[index].car); }
            return 0;
        }
        case NM_CUSTOMDRAW: return RaceCustomDraw(*reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
        }
        return 0;
    }
    case WM_CLOSE:
        if(raceControl.splitDragging) SendMessageW(raceControl.splitter,WM_CANCELMODE,0,0);
        ShowWindow(w,SW_HIDE); RefreshPage(); return 0; // Updates the page's Open pill.
    case WM_DPICHANGED: {
        RaceControlFonts(); ApplyRaceControlFonts();
        const auto* rect=reinterpret_cast<RECT*>(lp); SetWindowPos(w,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);
        LayoutRaceControl(); UpdateRaceControl(true); return 0;
    }
    case WM_CTLCOLORSTATIC: SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)==raceControl.status?RGB(110,112,118):Text); return reinterpret_cast<LRESULT>(app.brush);
    case WM_DESTROY:
        KillTimer(w,1); if(raceControl.font) DeleteObject(raceControl.font); if(raceControl.heading) DeleteObject(raceControl.heading);
        // The controls own windows; reset the plain data and leave the control objects to their windows.
        raceControl.window=raceControl.standings=raceControl.feed=raceControl.status=raceControl.splitter=nullptr; raceControl.font=raceControl.heading=nullptr;
        raceControl.splitDragging=false;
        raceControl.rows.clear(); raceControl.events.clear(); raceControl.columns.clear(); raceControl.feedColumns.clear(); raceControl.cameras.clear();
        raceControl.signature.clear(); raceControl.feedKey.clear(); raceControl.message.clear(); raceControl.pendingNumber.clear(); raceControl.car=-1; raceControl.selectedEvent=0;
        return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenRaceControl() {
    if(raceControl.window) { ShowWindow(raceControl.window,SW_RESTORE); UpdateRaceControl(true); SetForegroundWindow(raceControl.window); RefreshPage(); return; }
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=RaceControlProc; cls.hInstance=app.instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush; cls.style=CS_HREDRAW|CS_VREDRAW;
    cls.hIcon=LoadIconW(app.instance,MAKEINTRESOURCEW(101)); cls.lpszClassName=L"RaceMate.RaceControl"; RegisterClassExW(&cls);
    WNDCLASSEXW splitterClass{sizeof(splitterClass)}; splitterClass.lpfnWndProc=RaceSplitterProc; splitterClass.hInstance=app.instance;
    splitterClass.hCursor=LoadCursorW(nullptr,IDC_SIZEWE); splitterClass.hbrBackground=app.brush; splitterClass.style=CS_DBLCLKS;
    splitterClass.lpszClassName=L"RaceMate.RaceSplitter"; RegisterClassExW(&splitterClass);
    const int dpi=GetDpiForWindow(app.main); auto d=[&](int n){return MulDiv(n,dpi,96);};
    raceControl.window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"RaceMate - Race Control",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,d(1240),d(680),nullptr,nullptr,app.instance,nullptr);
    if(!raceControl.window) return;
    RaceControlFonts();
    auto create=[&](const wchar_t* type,const wchar_t* title,int id,DWORD style) {
        HWND child=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,0,0,0,0,raceControl.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(raceControl.font),TRUE); return child;
    };
    raceControl.banner.Create(app.instance,raceControl.window,RaceGoLiveId,raceControl.heading,raceControl.font);
    create(L"STATIC",L"",RaceInfoId,SS_CENTERIMAGE|SS_ENDELLIPSIS);
    raceControl.filter.Create(app.instance,raceControl.window,RaceFilterId,raceControl.font,{L"All",L"Incidents",L"Pit",L"Passes"});
    raceControl.filter.SetSelected(raceControl.category);
    create(L"BUTTON",L"",RaceCarPillId,WS_TABSTOP); ShowWindow(RaceItem(RaceCarPillId),SW_HIDE);
    create(L"STATIC",L"",RaceCountId,SS_CENTERIMAGE);
    create(L"BUTTON",L"Camera",RaceCameraId,WS_TABSTOP);
    create(L"BUTTON",L"Keep on top",RaceOnTopId,WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE);
    raceControl.feed=create(WC_LISTVIEWW,L"",RaceFeedId,LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);
    raceControl.standings=create(WC_LISTVIEWW,L"",RaceStandingsId,LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);
    raceControl.splitter=create(splitterClass.lpszClassName,L"Resize event and standings tables",RaceSplitterId,WS_TABSTOP);
    for(HWND list:{raceControl.standings,raceControl.feed}) { ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER); ListView_SetBkColor(list,RGB(255,255,255)); ListView_SetTextBkColor(list,RGB(255,255,255)); ListView_SetTextColor(list,Text); }
    create(L"STATIC",L"Replay from",RaceReplayLabelId,SS_CENTERIMAGE);
    raceControl.replay.Create(app.instance,raceControl.window,RaceReplayId,raceControl.font,{L"Live",L"5 s",L"10 s"});
    create(L"BUTTON",L"Only this car",RaceOnlyCarId,WS_TABSTOP);
    create(L"BUTTON",L"\u2039 Previous",RacePreviousId,WS_TABSTOP);
    create(L"BUTTON",L"Next \u203A",RaceNextId,WS_TABSTOP);
    create(L"STATIC",L"",RaceSelectedId,SS_CENTERIMAGE|SS_ENDELLIPSIS);
    create(L"BUTTON",L"Watch",RaceWatchId,WS_TABSTOP);
    create(L"BUTTON",L"Events of this car",RaceCarEventsId,WS_TABSTOP);
    raceControl.status=create(L"STATIC",L"",RaceMessageId,SS_CENTERIMAGE|SS_ENDELLIPSIS);
    LoadRaceFields(); raceControl.replay.SetSelected(raceControl.replayMode);
    for(HWND list:{raceControl.standings,raceControl.feed}) SetWindowSubclass(ListView_GetHeader(list),RaceHeaderProc,1,reinterpret_cast<DWORD_PTR>(list));
    wchar_t cameraName[256]{}; GetPrivateProfileStringW(L"RaceControl",L"Camera",L"Far Chase",cameraName,256,app.config.c_str()); raceControl.cameraName=cameraName;
    UiTheme::Apply(raceControl.window);
    UpdateCameraButton(); ApplyRaceOnTop();
    LayoutRaceControl(); UpdateRaceControl(true); ShowWindow(raceControl.window,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL); RefreshPage();
    if(!app.diagnostics) SetForegroundWindow(raceControl.window);
}
void CloseRaceControl() { if(raceControl.window) DestroyWindow(raceControl.window); RefreshPage(); }
