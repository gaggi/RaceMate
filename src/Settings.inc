// Native per-overlay settings, included inside the application's private namespace.
std::wstring StartupCommand() {
    wchar_t path[32768]{}; const DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length || length>=32768) return {};
    return L"\""+std::wstring(path,length)+L"\"";
}
int StartupShowCommand(bool trayAvailable,int requested) {
    return app.startMinimized && trayAvailable?SW_HIDE:requested;
}
bool RegisterStartup(bool enabled) {
    if(app.diagnostics) return true;
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    LSTATUS result;
    if(enabled) {
        const auto command=StartupCommand();
        result=command.empty()?ERROR_BAD_PATHNAME:RegSetValueExW(key,L"RaceMate",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    } else { result=RegDeleteValueW(key,L"RaceMate"); if(result==ERROR_FILE_NOT_FOUND) result=ERROR_SUCCESS; }
    RegCloseKey(key); return result==ERROR_SUCCESS;
}
const wchar_t* FuelElementTitle(int id) { const wchar_t* names[]={L"Leader Distance",L"Final Lap Time",L"Time Empty",L"Fuel at End",L"Stops Remaining",L"Tank Gauge",L"Stint Timeline",L"Pit Window Panel",L"Consumption Table",L"Remaining Leader Distance",L"Refuel to End"}; return names[id]; }
bool FuelElementVisible(int id) { return id!=8?app.fuelDisplay[id]:std::any_of(app.fuelRows.begin(),app.fuelRows.end(),[](bool value){return value;}); }
void OpenOverlaySettings(int kind);
void ApplyOverlaySettings(HWND w,int kind) {
    auto number=[&](int id,int fallback,int minimum,int maximum=MaxCars) {
        BOOL valid=FALSE; const UINT value=GetDlgItemInt(w,id,&valid,FALSE);
        const int result=valid?static_cast<int>(std::min<UINT>(value,maximum)):fallback;
        SetDlgItemInt(w,id,std::max(minimum,result),FALSE); return std::max(minimum,result);
    };
    if(kind==0) { app.rows=RelativeRowCount(number(400,app.rows,1)); SetDlgItemInt(w,400,app.rows,FALSE); app.gapDecimals=number(406,app.gapDecimals,0,3); }
    if(kind==1) {
        app.standings.leaders=number(402,app.standings.leaders,0);
        app.standings.ahead=number(403,app.standings.ahead,0);
        app.standings.behind=number(404,app.standings.behind,0);
    }
    for(auto& overlay:app.overlays) Fit(overlay);
    Save(); UpdateOverlays();
}
const wchar_t* WeatherElementTitle(int id) { const wchar_t* names[]={L"Track Temp",L"Air Temp",L"Wind",L"Track Wetness",L"Track Usage"}; return names[id]; }
HWND columnOrderWindow=nullptr;
LRESULT CALLBACK ColumnOrderProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_NCCREATE) SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    const int kind=static_cast<int>(GetWindowLongPtrW(w,GWLP_USERDATA)); HWND list=GetDlgItem(w,450);
    if(m==WM_COMMAND && (LOWORD(wp)==451 || LOWORD(wp)==452)) {
        const int selected=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0)); const int next=selected+(LOWORD(wp)==451?-1:1); auto& order=kind==2?app.fuelOrder:(kind==4?app.weatherOrder:app.columnOrder[kind]);
        if(selected>=0 && next>=0 && next<static_cast<int>(order.size())) {
            std::swap(order[selected],order[next]); SendMessageW(list,LB_RESETCONTENT,0,0);
            for(int id:order) { std::wstring text=(kind==2?FuelElementTitle(id):(kind==4?WeatherElementTitle(id):ColumnTitle(id))); if(!(kind==2?FuelElementVisible(id):(kind==4?app.weatherDisplay[id]:ColumnVisible(kind,id)))) text+=L" (hidden)"; SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())); }
            SendMessageW(list,LB_SETCURSEL,next,0); Save(); UpdateOverlays();
        } return 0;
    }
    if(m==WM_COMMAND && (LOWORD(wp)==IDOK || LOWORD(wp)==IDCANCEL)) { DestroyWindow(w); return 0; }
    if(m==WM_CLOSE) { DestroyWindow(w); return 0; }
    if(m==WM_DESTROY) { EnableWindow(GetWindow(w,GW_OWNER),TRUE); columnOrderWindow=nullptr; return 0; }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenColumnOrder(HWND owner,int kind) {
    if(columnOrderWindow) { SetForegroundWindow(columnOrderWindow); return; }
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=ColumnOrderProc; cls.hInstance=app.instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.ColumnOrder"; RegisterClassExW(&cls);
    const int dpi=GetDpiForWindow(owner); auto d=[&](int v){return MulDiv(v,dpi,96);}; RECT r{}; GetWindowRect(owner,&r);
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,(kind==2 || kind==4)?L"Element Order":L"Column Order",WS_CAPTION|WS_SYSMENU,r.left+d(35),r.top+d(45),d(410),d(415),owner,nullptr,app.instance,reinterpret_cast<void*>(static_cast<INT_PTR>(kind)));
    if(!w) return; columnOrderWindow=w; EnableWindow(owner,FALSE);
    auto create=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style) { HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,d(x),d(y),d(width),d(height),w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr); SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetPropW(owner,L"SettingsFont")),TRUE); return c; };
    HWND list=create(L"LISTBOX",L"",450,16,16,250,320,LBS_NOTIFY|WS_BORDER|WS_VSCROLL|WS_TABSTOP);
    for(int id:(kind==2?app.fuelOrder:(kind==4?app.weatherOrder:app.columnOrder[kind]))) { std::wstring text=(kind==2?FuelElementTitle(id):(kind==4?WeatherElementTitle(id):ColumnTitle(id))); if(!(kind==2?FuelElementVisible(id):(kind==4?app.weatherDisplay[id]:ColumnVisible(kind,id)))) text+=L" (hidden)"; SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())); }
    SendMessageW(list,LB_SETCURSEL,0,0); create(L"BUTTON",L"Move Up",451,278,16,105,30,WS_TABSTOP); create(L"BUTTON",L"Move Down",452,278,54,105,30,WS_TABSTOP); create(L"BUTTON",L"Done",IDOK,278,305,105,30,WS_TABSTOP);
    MakeSettingsScrollable(w,410,356);
    ShowWindow(w,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
}
HWND footerSettingsWindow=nullptr;
LRESULT CALLBACK FooterSettingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_NCCREATE) SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    const int mode=static_cast<int>(GetWindowLongPtrW(w,GWLP_USERDATA)),kind=mode%2; const bool header=mode>=2;
    if(m==WM_COMMAND) {
        const int id=LOWORD(wp),code=HIWORD(wp);
        if(id==IDOK || id==IDCANCEL) { DestroyWindow(w); return 0; }
        if(id==480 && code==BN_CLICKED) (header?app.tableHeaders[kind]:app.footers[kind].enabled)=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id>=481 && id<=483 && code==CBN_SELCHANGE) {
            const int selected=static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0));
            if(selected>=0 && selected<FooterMetricCount) (header?app.headerMetrics[kind]:app.footers[kind].metrics)[id-481]=selected;
        } else return 0;
        Fit(app.overlays[kind]); Save(); UpdateOverlays(); return 0;
    }
    if(m==WM_CLOSE) { DestroyWindow(w); return 0; }
    if(m==WM_DESTROY) { EnableWindow(GetWindow(w,GW_OWNER),TRUE); footerSettingsWindow=nullptr; return 0; }
    if(m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN) { SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),Text); return reinterpret_cast<LRESULT>(app.brush); }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenFooterSettings(HWND owner,int kind,bool header=false) {
    if(footerSettingsWindow) { SetForegroundWindow(footerSettingsWindow); return; }
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=FooterSettingsProc; cls.hInstance=app.instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.FooterSettings"; RegisterClassExW(&cls);
    const int dpi=GetDpiForWindow(owner); auto d=[&](int v){return MulDiv(v,dpi,96);}; RECT r{}; GetWindowRect(owner,&r);
    constexpr DWORD style=WS_CAPTION|WS_SYSMENU; RECT size{0,0,d(520),d(305)}; AdjustWindowRectExForDpi(&size,style,FALSE,WS_EX_CONTROLPARENT,dpi);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=size.right-size.left,height=size.bottom-size.top;
    const int x=std::clamp(static_cast<int>(r.left)+d(20),static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width));
    const int y=std::clamp(static_cast<int>(r.top)+d(35),static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height));
    const std::wstring title=std::wstring(Titles[kind])+(header?L" – Header Settings":L" – Footer Settings");
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,title.c_str(),style,x,y,width,height,owner,nullptr,app.instance,reinterpret_cast<void*>(static_cast<INT_PTR>(kind+(header?2:0))));
    if(!w) return; footerSettingsWindow=w; EnableWindow(owner,FALSE);
    auto create=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,int height,DWORD flags=0) { HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|flags,d(x),d(y),d(width),d(height),w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr); SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetPropW(owner,L"SettingsFont")),TRUE); return c; };
    HWND check=create(L"BUTTON",header?L"Show Header":L"Show Footer",480,24,20,460,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(check,BM_SETCHECK,(header?app.tableHeaders[kind]:app.footers[kind].enabled)?BST_CHECKED:BST_UNCHECKED,0);
    const wchar_t* positions[]={L"Left",L"Center",L"Right"};
    for(int slot=0;slot<3;++slot) {
        create(L"STATIC",positions[slot],0,24,62+slot*44,90,24);
        HWND combo=create(L"COMBOBOX",L"",481+slot,125,60+slot*44,370,330,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        for(const auto* name:FooterTitles) SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(combo,CB_SETCURSEL,(header?app.headerMetrics[kind]:app.footers[kind].metrics)[slot],0);
    }
    create(L"STATIC",L"Lap, incidents and class follow your / the watched car.\nEstimates use ~. Unavailable values show —.",0,24,200,470,45);
    create(L"BUTTON",L"Done",IDOK,405,258,90,30,BS_DEFPUSHBUTTON|WS_TABSTOP);
    MakeSettingsScrollable(w,520,305);
    ShowWindow(w,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
}
LRESULT CALLBACK SettingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_NCCREATE) SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    const int kind=static_cast<int>(GetWindowLongPtrW(w,GWLP_USERDATA));
    switch(m) {
    case WM_COMMAND: {
        const int id=LOWORD(wp),code=HIWORD(wp);
        if(id==449 && kind<2) { OpenFooterSettings(w,kind,true); return 0; }
        if(id==446 && kind<2) { OpenFooterSettings(w,kind); return 0; }
        if(id==445 && (kind<3 || kind==4)) { OpenColumnOrder(w,kind); return 0; }
        if(id==IDOK) { ApplyOverlaySettings(w,kind); DestroyWindow(w); return 0; }
        if(id==IDCANCEL) { ApplyOverlaySettings(w,kind); DestroyWindow(w); return 0; }
        if((id==400 || id==406 || (id>=402 && id<=404)) && code==EN_KILLFOCUS) { ApplyOverlaySettings(w,kind); return 0; }
        if(id==440 && code==CBN_SELCHANGE) {
            auto& overlay=app.overlays[kind]; const int selected=static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0));
            overlay.place.growLeft=(selected&1)!=0; overlay.place.growUp=(selected&2)!=0;
            CaptureOverlayCorner(overlay);
        }
        else if(kind==4 && id>=490 && id<496 && code==BN_CLICKED) { app.weatherDisplay[id-490]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED; EnableWindow(GetDlgItem(w,495),app.weatherDisplay[2]); }
        else if(id==438 && kind==1 && code==CBN_SELCHANGE) app.standings.fallbackByRating=SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0)==1;
        else if(id==409 && kind==1 && code==BN_CLICKED) app.standings.liveGaps=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id==408 && kind==1 && code==BN_CLICKED) app.showInterval=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id==407 && kind==0 && code==BN_CLICKED) app.relativeHidePit=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id==401 && code==CBN_SELCHANGE) app.standings.separateClasses=SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0)==0;
        else if(id==405 && code==BN_CLICKED) app.standings.ownClass=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id>=410 && id<410+FuelMetricCount && code==BN_CLICKED) app.fuelRows[id-410]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind==2 && id>=460 && id<471 && code==BN_CLICKED) app.fuelDisplay[id-460]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind==2 && id==471 && code==BN_CLICKED) app.excludeYellowFuelLaps=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind==1 && id==437 && code==BN_CLICKED) app.standingsGained=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind==1 && id==436 && code==BN_CLICKED) app.standingsLaps=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && id==511 && code==CBN_SELCHANGE) app.nameFormat[kind]=static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0));
        else if(kind<2 && id==510 && code==BN_CLICKED) app.showDriver[kind]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && id==512 && code==BN_CLICKED) app.showStatus[kind]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && id==514 && code==BN_CLICKED) app.columnHeaders[kind]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && id>=497 && id<500 && code==BN_CLICKED) app.driverFlags[kind][id-497]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && (id==447 || id==448) && code==BN_CLICKED) app.driverShortView[kind][id-447]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(kind<2 && id>=430 && id<436 && code==BN_CLICKED) app.driverColumns[kind][id-430]=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else return 0;
        if(kind<2) { EnableWindow(GetDlgItem(w,511),app.showDriver[kind]); for(int i=0;i<3;++i) EnableWindow(GetDlgItem(w,497+i),app.showStatus[kind]); EnableWindow(GetDlgItem(w,447),app.driverColumns[kind][0]); EnableWindow(GetDlgItem(w,448),app.driverColumns[kind][1]); }
        ApplyOverlaySettings(w,kind); return 0;
    }
    case WM_CLOSE: ApplyOverlaySettings(w,kind); DestroyWindow(w); return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),Text); return reinterpret_cast<LRESULT>(app.brush);
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(w,&r); FillRect(reinterpret_cast<HDC>(wp),&r,app.brush); return 1; }
    case WM_DESTROY: {
        if(footerSettingsWindow && GetWindow(footerSettingsWindow,GW_OWNER)==w) DestroyWindow(footerSettingsWindow);
        if(columnOrderWindow && GetWindow(columnOrderWindow,GW_OWNER)==w) DestroyWindow(columnOrderWindow);
        app.overlaySettings=nullptr; app.settingsKind=-1; return 0;
    }
    }
    return DefWindowProcW(w,m,wp,lp);
}
void OpenOverlaySettings(int kind) {
    if(app.overlaySettings) {
        if(app.settingsKind==kind) { ShowWindow(app.overlaySettings,SW_RESTORE); SetForegroundWindow(app.overlaySettings); return; }
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
    }
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=SettingsProc; cls.hInstance=app.instance;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.OverlaySettings";
    RegisterClassExW(&cls);
    const int height=kind==0?642:(kind==1?803:(kind==2?510:(kind==4?360:228)));
    const int dpi=GetDpiForWindow(app.main); auto d=[&](int value){return MulDiv(value,dpi,96);};
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
    RECT size{0,0,d(540),d(height)},owner{}; AdjustWindowRectExForDpi(&size,style,FALSE,0,dpi); GetWindowRect(app.main,&owner);
    const std::wstring title=std::wstring(Titles[kind])+L" – Settings";
    int x=owner.left+d(45),y=owner.top+d(60);
    if(!app.diagnostics) {
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(app.main,MONITOR_DEFAULTTONEAREST),&monitor);
        x=std::clamp(x,static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right-size.right+size.left)));
        y=std::clamp(y,static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom-size.bottom+size.top)));
    }
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,title.c_str(),style,x,y,size.right-size.left,size.bottom-size.top,app.main,nullptr,app.instance,reinterpret_cast<void*>(static_cast<INT_PTR>(kind)));
    if(!w) return;
    app.overlaySettings=w; app.settingsKind=kind;
    HFONT font=CreateFontW(-d(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI"); SetPropW(w,L"SettingsFont",font);
    auto control=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,int h,DWORD flags=0) {
        HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|flags,d(x),d(y),d(width),d(h),w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return c;
    };
    auto label=[&](const wchar_t* text,int y){control(L"STATIC",text,0,24,y,490,24);};
    auto check=[&](const wchar_t* text,int id,int y,bool checked) {
        auto c=control(L"BUTTON",text,id,24,y,490,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
    };
    auto number=[&](const wchar_t* text,int id,int y,int value) {
        control(L"STATIC",text,0,24,y,360,24);
        auto c=control(L"EDIT",std::to_wstring(value).c_str(),id,416,y,90,25,ES_NUMBER|WS_BORDER|WS_TABSTOP);
        SendMessageW(c,EM_SETLIMITTEXT,3,0);
    };
    if(kind==0) {
        number(L"Rows (including your / the watched car)",400,28,app.rows);
        number(L"Gap decimal places (0–3)",406,71,app.gapDecimals);
        check(L"Hide Drivers in Pit",407,108,app.relativeHidePit);
        label(L"Odd rows (1–127); even entries round up. Your car stays centered.",147);
    } else if(kind==1) {
        control(L"STATIC",L"Class view",0,24,25,130,24);
        HWND combo=control(L"COMBOBOX",L"",401,170,25,336,180,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        for(const auto* item:{L"Separate classes",L"Combined overall standings"}) SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(combo,CB_SETCURSEL,app.standings.separateClasses?0:1,0);
        check(L"Show only driver's class",405,72,app.standings.ownClass);
        number(L"Top drivers (per class / overall)",402,118,app.standings.leaders);
        number(L"Drivers ahead of your / the watched car",403,159,app.standings.ahead);
        number(L"Drivers behind your / the watched car",404,200,app.standings.behind);
        check(L"Show Interval (INT)",408,236,app.showInterval);
        check(L"Live Positions / GAP / INT (estimated)",409,268,app.standings.liveGaps);
        control(L"STATIC",L"Untimed driver order",0,24,305,140,24);
        HWND fallback=control(L"COMBOBOX",L"",438,170,305,336,140,CBS_DROPDOWNLIST|WS_TABSTOP);
        for(const auto* item:{L"Car number (ascending)",L"iRating (descending)"}) SendMessageW(fallback,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(fallback,CB_SETCURSEL,app.standings.fallbackByRating?1:0,0);
    } else if(kind==2) {
        control(L"STATIC",L"Consumption rows",0,24,23,250,24);
        control(L"STATIC",L"Display elements",0,290,23,226,24);
        const wchar_t* labels[]={L"Last lap",L"Average of last 10 laps",L"Average of last 5 laps",L"Maximum",L"Minimum",L"Race average"};
        for(int i=0;i<FuelMetricCount;++i) {
            auto c=control(L"BUTTON",labels[i],410+i,24,62+i*35,250,24,BS_AUTOCHECKBOX|WS_TABSTOP);
            SendMessageW(c,BM_SETCHECK,app.fuelRows[i]?BST_CHECKED:BST_UNCHECKED,0);
        }
        const wchar_t* displayLabels[]={L"Leader distance",L"Time left at final lap start",L"Time until empty",L"Fuel at finish",L"Stops remaining",L"Tank gauge",L"Stint timeline",L"Pit window panel",L"Pit window column",L"Remaining leader distance",L"Refuel to finish"};
        for(int i=0;i<11;++i) {
            auto c=control(L"BUTTON",displayLabels[i],460+i,290,62+i*32,226,24,BS_AUTOCHECKBOX|WS_TABSTOP);
            SendMessageW(c,BM_SETCHECK,app.fuelDisplay[i]?BST_CHECKED:BST_UNCHECKED,0);
        }
        control(L"STATIC",L"Average uses all recorded laps.\nAverage 5 / 10 uses the latest laps.",0,24,282,250,55);
        HWND yellow=control(L"BUTTON",L"Exclude yellow / red flag laps",471,24,350,250,24,BS_AUTOCHECKBOX|WS_TABSTOP);
        SendMessageW(yellow,BM_SETCHECK,app.excludeYellowFuelLaps?BST_CHECKED:BST_UNCHECKED,0);
    } else if(kind==4) {
        const wchar_t* labels[]={L"Show Track Temperature",L"Show Air Temperature",L"Show Wind",L"Show Track Wetness",L"Show Track Usage",L"Show Wind Direction"};
        const int displayOrder[]={0,1,2,5,3,4};
        for(int slot=0;slot<6;++slot) {
            const int i=displayOrder[slot];
            HWND c=control(L"BUTTON",labels[i],490+i,i==5?44:24,24+slot*31,i==5?470:490,24,BS_AUTOCHECKBOX|WS_TABSTOP);
            SendMessageW(c,BM_SETCHECK,app.weatherDisplay[i]?BST_CHECKED:BST_UNCHECKED,0);
        }
        EnableWindow(GetDlgItem(w,495),app.weatherDisplay[2]);
        label(L"Wind direction follows your live car heading. Speed: km/h.",218);
    } else {
        label(L"Shown on pit approach, in pit lane or with limiter active.",28);
        label(L"Green: pit limiter active. Red: pit limiter not active.",65);
        label(L"Stays visible after pit exit until the limiter is switched off.",102);
    }
    const int growthY=kind==0?174:(kind==1?348:(kind==2?423:(kind==4?264:132)));
    control(L"STATIC",L"Fixed corner",0,24,growthY,140,24);
    HWND corner=control(L"COMBOBOX",L"",440,170,growthY,336,180,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(const auto* text:{L"Top Left",L"Top Right",L"Bottom Left",L"Bottom Right"}) SendMessageW(corner,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SendMessageW(corner,CB_SETCURSEL,(app.overlays[kind].place.growLeft?1:0)+(app.overlays[kind].place.growUp?2:0),0);
    if(kind<2) {
        const int headingY=kind==0?215:385;
        control(L"STATIC",L"Driver and status",0,24,headingY,240,24);
        control(L"STATIC",L"Additional columns",0,290,headingY,226,24);
        const int top=headingY+30;
        HWND c=control(L"BUTTON",L"Show Driver",510,24,top,240,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.showDriver[kind]?BST_CHECKED:BST_UNCHECKED,0);
        HWND names=control(L"COMBOBOX",L"",511,44,top+30,220,160,CBS_DROPDOWNLIST|WS_TABSTOP);
        for(const auto* name:{L"First name Last name",L"F. Last name",L"F Last name"}) SendMessageW(names,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(names,CB_SETCURSEL,app.nameFormat[kind],0); EnableWindow(names,app.showDriver[kind]);
        c=control(L"BUTTON",L"Show Status",512,24,top+64,240,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.showStatus[kind]?BST_CHECKED:BST_UNCHECKED,0);
        const wchar_t* flags[]={L"Show Black Flag",L"Show Slowdown",L"Show Required Repairs"};
        for(int i=0;i<3;++i) { c=control(L"BUTTON",flags[i],497+i,44,top+96+32*i,220,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.driverFlags[kind][i]?BST_CHECKED:BST_UNCHECKED,0); EnableWindow(c,app.showStatus[kind]); }
        c=control(L"BUTTON",L"Show Column Headings",514,24,top+192,240,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.columnHeaders[kind]?BST_CHECKED:BST_UNCHECKED,0);
        if(kind==1) {
            c=control(L"BUTTON",L"Show Positions Gained",437,24,top+224,240,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.standingsGained?BST_CHECKED:BST_UNCHECKED,0);
            c=control(L"BUTTON",L"Show Completed Laps",436,24,top+256,240,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.standingsLaps?BST_CHECKED:BST_UNCHECKED,0);
        }
        const wchar_t* labels[]={L"License / safety rating",L"iRating",L"Tires / compound",L"Last pit lap",L"Pit lane time",L"Pit stop time"};
        for(int i=0;i<6;++i) {
            const int columnY=top+32*(i+std::min(i,2));
            c=control(L"BUTTON",labels[i],430+i,290,columnY,226,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.driverColumns[kind][i]?BST_CHECKED:BST_UNCHECKED,0);
            if(i<2) { c=control(L"BUTTON",L"Short View",447+i,310,columnY+32,206,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,app.driverShortView[kind][i]?BST_CHECKED:BST_UNCHECKED,0); EnableWindow(c,app.driverColumns[kind][i]); }
        }
        control(L"BUTTON",L"Header Settings...",449,24,height-89,220,30,WS_TABSTOP);
        control(L"BUTTON",L"Footer Settings...",446,270,height-89,236,30,WS_TABSTOP);
        control(L"BUTTON",L"Column Order...",445,24,height-47,220,30,WS_TABSTOP);
    }
    if(kind==2 || kind==4) control(L"BUTTON",L"Element Order...",445,24,height-47,220,30,WS_TABSTOP);
    control(L"BUTTON",L"Done",IDOK,416,height-47,90,30,BS_DEFPUSHBUTTON|WS_TABSTOP);
    MakeSettingsScrollable(w,540,height);
    ShowWindow(w,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
    if(!app.diagnostics) { SetForegroundWindow(w); HWND first=GetNextDlgTabItem(w,nullptr,FALSE); if(first) SetFocus(first); }
}
