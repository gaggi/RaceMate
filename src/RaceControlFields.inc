// Independent Race Control columns, sorting and event car filter.
constexpr int RaceFieldCount=18;
const wchar_t* RaceEventTitles[]={L"-5s",L"-10s",L"TIME",L"EVENT",L"#",L"DRIVER",L"DETAILS"};
const int RaceEventWidths[]={48,48,78,142,42,155,310};
const wchar_t* RaceFieldTitle(int id) {
    const wchar_t* titles[]={L"P",L"#",L"DRIVER",L"CLASS",L"GAP",L"INT",L"BEST",L"LAST",L"STATUS",L"LICENSE",L"iRATING",L"TIRES",L"PIT LAP",L"PIT TIME",L"PIT STOP",L"FLAGS",L"LAPS",L"GAIN"};
    return titles[id];
}
int RaceFieldWidth(int id) { const int widths[]={32,38,130,50,60,60,78,78,90,82,72,100,70,80,88,158,54,54}; return widths[id]; }
void SaveRaceFields() {
    auto write=[&](const std::wstring& key,int value) { const auto text=std::to_wstring(value); WritePrivateProfileStringW(L"RaceControl",key.c_str(),text.c_str(),app.config.c_str()); };
    for(int i=0;i<RaceFieldCount;++i) write(L"Field"+std::to_wstring(i),raceControl.fields[i]);
    for(int i=0;i<7;++i) write(L"EventField"+std::to_wstring(i),raceControl.eventFields[i]);
    write(L"SortColumn",raceControl.sortColumn); write(L"SortDescending",raceControl.sortDescending);
    write(L"EventSortColumn",raceControl.eventSortColumn); write(L"EventSortDescending",raceControl.eventSortDescending);
}
void LoadRaceFields() {
    for(int i=0;i<RaceFieldCount;++i) { const bool fallback=i<9 || i==15 || (i>=9 && i<15 && app.driverColumns[1][i-9]); raceControl.fields[i]=ReadInt(L"RaceControl",(L"Field"+std::to_wstring(i)).c_str(),fallback)!=0; }
    for(int i=0;i<7;++i) raceControl.eventFields[i]=ReadInt(L"RaceControl",(L"EventField"+std::to_wstring(i)).c_str(),1)!=0;
    if(std::none_of(raceControl.fields.begin(),raceControl.fields.end(),[](bool v){return v;})) raceControl.fields[2]=true;
    if(std::none_of(raceControl.eventFields.begin(),raceControl.eventFields.end(),[](bool v){return v;})) raceControl.eventFields[5]=true;
    raceControl.sortColumn=std::clamp(ReadInt(L"RaceControl",L"SortColumn",-1),-1,RaceFieldCount-1);
    raceControl.eventSortColumn=std::clamp(ReadInt(L"RaceControl",L"EventSortColumn",2),2,6);
    raceControl.sortDescending=ReadInt(L"RaceControl",L"SortDescending",0)!=0;
    raceControl.eventSortDescending=ReadInt(L"RaceControl",L"EventSortDescending",1)!=0;
    if(raceControl.sortColumn>=0 && !raceControl.fields[raceControl.sortColumn]) raceControl.sortColumn=-1;
}
int RaceFilterCar() {
    const int index=static_cast<int>(SendMessageW(raceControl.carFilter,CB_GETCURSEL,0,0));
    return index<0?-1:static_cast<int>(SendMessageW(raceControl.carFilter,CB_GETITEMDATA,index,0));
}
double RaceNumber(const std::string& value);
void UpdateRaceCarFilter() {
    std::vector<Driver> drivers=app.session.drivers;
    for(const auto& e:app.events.Events()) if(std::none_of(drivers.begin(),drivers.end(),[&](const Driver& d){return d.index==e.car;})) { Driver d; d.index=e.car; d.number=e.number; d.name=e.driver; drivers.push_back(d); }
    std::stable_sort(drivers.begin(),drivers.end(),[](const Driver& a,const Driver& b){ const double an=RaceNumber(a.number),bn=RaceNumber(b.number); const bool av=std::isfinite(an),bv=std::isfinite(bn); if(av!=bv) return av; return av && an!=bn?an<bn:a.number<b.number; });
    std::wstring signature=Wide(app.session.key);
    for(const auto& d:drivers) signature+=std::to_wstring(d.index)+L":"+Wide(d.number)+L":"+Wide(d.name)+L"|";
    if(signature==raceControl.carFilterSignature) return;
    const int selected=raceControl.carFilterSession==app.session.key?RaceFilterCar():-1;
    raceControl.carFilterSignature=signature; raceControl.carFilterSession=app.session.key;
    SendMessageW(raceControl.carFilter,CB_RESETCONTENT,0,0);
    SendMessageW(raceControl.carFilter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All cars")); SendMessageW(raceControl.carFilter,CB_SETITEMDATA,0,-1);
    int chosen=0;
    for(const auto& d:drivers) {
        const auto name=L"#"+Wide(d.number)+L"  "+Wide(d.name);
        const int item=static_cast<int>(SendMessageW(raceControl.carFilter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str())));
        SendMessageW(raceControl.carFilter,CB_SETITEMDATA,item,d.index); if(d.index==selected) chosen=item;
    }
    SendMessageW(raceControl.carFilter,CB_SETCURSEL,chosen,0);
}
double RaceNumber(const std::string& value) {
    if(value.empty() || !std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0' && c<='9';})) return std::numeric_limits<double>::quiet_NaN();
    return std::strtod(value.c_str(),nullptr);
}
double RaceSortNumber(const Row& row,int col) {
    const double missing=std::numeric_limits<double>::quiet_NaN();
    auto positive=[&](double v){return v>0?v:missing;}; auto known=[&](double v){return v>=0?v:missing;};
    switch(col) {
    case 0:return positive(app.standings.separateClasses?row.car.classPosition:row.car.position);
    case 1:return RaceNumber(row.driver.number);
    case 4:return row.lapDifference>0?row.lapDifference*86400.0:row.gap;
    case 5:return row.intervalLaps>0?row.intervalLaps*86400.0:row.interval;
    case 6:return positive(row.car.best); case 7:return positive(row.car.last);
    case 9: { const auto start=row.driver.license.find_first_of("0123456789"); return start==std::string::npos?missing:std::strtod(row.driver.license.c_str()+start,nullptr); }
    case 10:return positive(row.driver.rating); case 11:return known(row.car.tireCompound); case 12:return known(row.car.lastPitLap);
    case 13:return known(row.car.lastPitDuration); case 14:return known(row.car.pit?row.car.pitStationary:row.car.lastPitStationary);
    case 16: { int laps=row.car.completed; if(laps<0 && row.car.lap>=0) laps=std::max(0,row.car.lap-1); if(laps<0 && app.session.number==app.frame.session) laps=app.session.results[row.driver.index].completed; return known(laps); }
    case 17: { int gained=PositionsGained(app.session,row,app.standings.separateClasses); return gained==-MaxCars?missing:gained; }
    }
    return missing;
}
void SortRaceRows(std::vector<Row>& rows) {
    const int col=raceControl.sortColumn; if(col<0) return;
    std::stable_sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b) {
        if(col==2 || col==3 || col==8 || col==15) { const auto av=RaceStandingCell(a,col),bv=RaceStandingCell(b,col); return raceControl.sortDescending?av>bv:av<bv; }
        const double av=RaceSortNumber(a,col),bv=RaceSortNumber(b,col); const bool ak=std::isfinite(av),bk=std::isfinite(bv);
        if(ak!=bk) return ak; if(!ak) return col==1? (raceControl.sortDescending?a.driver.number>b.driver.number:a.driver.number<b.driver.number):false;
        return raceControl.sortDescending?av>bv:av<bv;
    });
}
void SortRaceEvents(std::vector<RaceEvent>& events) {
    const int col=raceControl.eventSortColumn;
    std::stable_sort(events.begin(),events.end(),[&](const RaceEvent& a,const RaceEvent& b) {
        if(col==2 || col==4) {
            const double av=col==2?a.time:RaceNumber(a.number),bv=col==2?b.time:RaceNumber(b.number);
            const bool ak=std::isfinite(av),bk=std::isfinite(bv); if(ak!=bk) return ak; if(!ak) return false;
            return raceControl.eventSortDescending?av>bv:av<bv;
        }
        const auto av=col==3?std::wstring(EventName(a.type)):Wide(col==5?a.driver:a.detail);
        const auto bv=col==3?std::wstring(EventName(b.type)):Wide(col==5?b.driver:b.detail);
        return raceControl.eventSortDescending?av>bv:av<bv;
    });
}
void UpdateRaceSortIndicators() {
    for(bool feed:{false,true}) {
        HWND header=ListView_GetHeader(feed?raceControl.feed:raceControl.standings);
        const auto& columns=feed?raceControl.feedColumns:raceControl.columns;
        const int sort=feed?raceControl.eventSortColumn:raceControl.sortColumn; const bool desc=feed?raceControl.eventSortDescending:raceControl.sortDescending;
        for(int i=0;i<static_cast<int>(columns.size());++i) { HDITEMW item{}; item.mask=HDI_FORMAT; Header_GetItem(header,i,&item); item.fmt&=~(HDF_SORTUP|HDF_SORTDOWN); if(columns[i]==sort) item.fmt|=desc?HDF_SORTDOWN:HDF_SORTUP; Header_SetItem(header,i,&item); }
    }
}
void SetRaceSort(bool feed,int col,int direction=-1) {
    auto& sort=feed?raceControl.eventSortColumn:raceControl.sortColumn; auto& descending=feed?raceControl.eventSortDescending:raceControl.sortDescending;
    descending=direction>=0?direction!=0:(sort==col?!descending:false); sort=col; SaveRaceFields(); UpdateRaceControl(true);
}
HWND raceFieldsWindow=nullptr;
LRESULT CALLBACK RaceFieldsProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_COMMAND) {
        const int id=LOWORD(wp);
        if(id==IDOK || id==IDCANCEL) { DestroyWindow(w); return 0; }
        bool changed=false;
        if(id>=900 && id<900+RaceFieldCount) {
            auto& fields=raceControl.fields; fields[id-900]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
            if(std::none_of(fields.begin(),fields.end(),[](bool v){return v;})) { fields[id-900]=true; SendDlgItemMessageW(w,id,BM_SETCHECK,BST_CHECKED,0); }
            if(raceControl.sortColumn>=0 && !fields[raceControl.sortColumn]) raceControl.sortColumn=-1;
            changed=true;
        }
        if(id>=920 && id<927) {
            auto& fields=raceControl.eventFields; fields[id-920]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
            if(std::none_of(fields.begin(),fields.end(),[](bool v){return v;})) { fields[id-920]=true; SendDlgItemMessageW(w,id,BM_SETCHECK,BST_CHECKED,0); }
            changed=true;
        }
        if(changed) { SaveRaceFields(); UpdateRaceControl(true); }
        return 0;
    }
    if(m==WM_CLOSE) { DestroyWindow(w); return 0; }
    if(m==WM_DESTROY) { EnableWindow(raceControl.window,TRUE); raceFieldsWindow=nullptr; return 0; }
    if(m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN) { SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),Text); return reinterpret_cast<LRESULT>(app.brush); }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenRaceFields() {
    if(raceFieldsWindow) { SetForegroundWindow(raceFieldsWindow); return; }
    WNDCLASSW cls{}; cls.lpfnWndProc=RaceFieldsProc; cls.hInstance=app.instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.RaceFields"; RegisterClassW(&cls);
    const int dpi=GetDpiForWindow(raceControl.window); auto d=[&](int n){return MulDiv(n,dpi,96);};
    RECT owner{}; GetWindowRect(raceControl.window,&owner); RECT size{0,0,d(550),d(584)}; constexpr DWORD style=WS_CAPTION|WS_SYSMENU;
    AdjustWindowRectExForDpi(&size,style,FALSE,0,dpi); MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(raceControl.window,MONITOR_DEFAULTTONEAREST),&monitor);
    int x=std::clamp(static_cast<int>(owner.left)+d(30),static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right-size.right)));
    int y=std::clamp(static_cast<int>(owner.top)+d(30),static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom-size.bottom)));
    raceFieldsWindow=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Race Control - Manage Fields",style,x,y,size.right-size.left,size.bottom-size.top,raceControl.window,nullptr,app.instance,nullptr);
    if(!raceFieldsWindow) return; EnableWindow(raceControl.window,FALSE);
    auto control=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,DWORD flags=0) { HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|flags,d(x),d(y),d(width),d(24),raceFieldsWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr); SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(raceControl.font),TRUE); return c; };
    control(L"STATIC",L"Standings",0,24,16,230); control(L"STATIC",L"Events",0,290,16,230);
    const wchar_t* labels[]={L"Position",L"Car Number",L"Driver",L"Class",L"Gap",L"Interval",L"Best Lap",L"Last Lap",L"Track / Pit Status",L"License / Safety Rating",L"iRating",L"Tires / Compound",L"Last Pit Lap",L"Pit Lane Time",L"Pit Stop Time",L"Flags / Repairs",L"Completed Laps",L"Positions Gained"};
    for(int i=0;i<RaceFieldCount;++i) { HWND c=control(L"BUTTON",labels[i],900+i,24,46+i*27,240,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,raceControl.fields[i]?BST_CHECKED:BST_UNCHECKED,0); }
    for(int i=0;i<7;++i) { HWND c=control(L"BUTTON",RaceEventTitles[i],920+i,290,46+i*27,236,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,raceControl.eventFields[i]?BST_CHECKED:BST_UNCHECKED,0); }
    control(L"STATIC",L"Click a column heading to sort.",0,290,260,236);
    control(L"BUTTON",L"Done",IDOK,436,544,90,BS_DEFPUSHBUTTON|WS_TABSTOP);
    MakeSettingsScrollable(raceFieldsWindow,550,584);
    ShowWindow(raceFieldsWindow,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
}
LRESULT CALLBACK RaceHeaderProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    if(m==WM_RBUTTONUP) {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(w,&point);
        SendMessageW(w,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(w),MAKELPARAM(point.x,point.y)); return 0;
    }
    if(m==WM_CONTEXTMENU) {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const bool feed=reinterpret_cast<HWND>(data)==raceControl.feed;
        HDHITTESTINFO hit{}; int col=-1;
        if(point.x==-1 && point.y==-1) { RECT rect{}; GetWindowRect(w,&rect); point={rect.left+10,rect.bottom}; }
        else { hit.pt=point; ScreenToClient(w,&hit.pt); const int index=static_cast<int>(SendMessageW(w,HDM_HITTEST,0,reinterpret_cast<LPARAM>(&hit))); const auto& cols=feed?raceControl.feedColumns:raceControl.columns; if(index>=0 && index<static_cast<int>(cols.size())) col=cols[index]; }
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Manage Fields...");
        if(col>=0 && (!feed || col>=2)) { AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,2,L"Sort ascending"); AppendMenuW(menu,MF_STRING,3,L"Sort descending"); }
        if(!feed) AppendMenuW(menu,MF_STRING,4,L"Restore race order");
        const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,raceControl.window,nullptr); DestroyMenu(menu);
        if(command==1) OpenRaceFields(); if(command==2 || command==3) SetRaceSort(feed,col,command==3?1:0); if(command==4) SetRaceSort(false,-1,0);
        return 0;
    }
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,RaceHeaderProc,id);
    return DefSubclassProc(w,m,wp,lp);
}
