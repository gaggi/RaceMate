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
void OpenOverlaySettings(int kind);
void ApplyOverlaySettings(HWND w,int kind) {
    auto number=[&](int id,int fallback,int minimum,int maximum=MaxCars) {
        BOOL valid=FALSE; const UINT value=GetDlgItemInt(w,id,&valid,FALSE);
        const int result=valid?static_cast<int>(std::min<UINT>(value,maximum)):fallback;
        SetDlgItemInt(w,id,std::max(minimum,result),FALSE); return std::max(minimum,result);
    };
    if(kind==RelativeKind) { app.rows=RelativeRowCount(number(RowsId,app.rows,1)); SetDlgItemInt(w,RowsId,app.rows,FALSE); app.gapDecimals=number(GapDecimalsId,app.gapDecimals,0,3); }
    if(kind==StandingsKind) {
        app.standings.leaders=number(LeadersId,app.standings.leaders,0);
        app.standings.ahead=number(AheadId,app.standings.ahead,0);
        app.standings.behind=number(BehindId,app.standings.behind,0);
    }
    for(auto& overlay:app.overlays) Fit(overlay);
    Save(); UpdateOverlays();
}
// Column Order for Relative/Standings, Element Order for Fuel Calculator and Weather.
std::vector<int>& EditableOrder(int kind) { return kind==FuelKind?app.fuelOrder:(kind==WeatherKind?app.weatherOrder:app.columnOrder[kind]); }
void FillOrderList(HWND list,int kind) {
    SendMessageW(list,LB_RESETCONTENT,0,0);
    for(int id:EditableOrder(kind)) {
        std::wstring text=kind==FuelKind?FuelElements[id].title:(kind==WeatherKind?WeatherElements[id].title:ColumnTitle(id));
        if(!(kind==FuelKind?FuelElementVisible(id):(kind==WeatherKind?app.weatherDisplay[id]:ColumnVisible(kind,id)))) text+=L" (hidden)";
        SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
    }
}
HWND columnOrderWindow=nullptr;
LRESULT CALLBACK ColumnOrderProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_NCCREATE) SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    const int kind=static_cast<int>(GetWindowLongPtrW(w,GWLP_USERDATA)); HWND list=GetDlgItem(w,OrderListId);
    if(m==WM_COMMAND && (LOWORD(wp)==MoveUpId || LOWORD(wp)==MoveDownId)) {
        const int selected=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0)); const int next=selected+(LOWORD(wp)==MoveUpId?-1:1); auto& order=EditableOrder(kind);
        if(selected>=0 && next>=0 && next<static_cast<int>(order.size())) {
            std::swap(order[selected],order[next]); FillOrderList(list,kind);
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
    HWND w=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,IsTable(kind)?L"Column Order":L"Element Order",WS_CAPTION|WS_SYSMENU,r.left+d(35),r.top+d(45),d(410),d(415),owner,nullptr,app.instance,reinterpret_cast<void*>(static_cast<INT_PTR>(kind)));
    if(!w) return; columnOrderWindow=w; EnableWindow(owner,FALSE);
    auto create=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style) { HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,d(x),d(y),d(width),d(height),w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr); SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetPropW(owner,L"SettingsFont")),TRUE); return c; };
    HWND list=create(L"LISTBOX",L"",OrderListId,16,16,250,320,LBS_NOTIFY|WS_BORDER|WS_VSCROLL|WS_TABSTOP);
    FillOrderList(list,kind);
    SendMessageW(list,LB_SETCURSEL,0,0); create(L"BUTTON",L"Move Up",MoveUpId,278,16,105,30,WS_TABSTOP); create(L"BUTTON",L"Move Down",MoveDownId,278,54,105,30,WS_TABSTOP); create(L"BUTTON",L"Done",IDOK,278,305,105,30,WS_TABSTOP);
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
        if(id==SlotEnabledId && code==BN_CLICKED) (header?app.tableHeaders[kind]:app.footers[kind].enabled)=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id>=SlotMetricId && id<SlotMetricId+3 && code==CBN_SELCHANGE) {
            const int selected=static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0));
            if(selected>=0 && selected<MetricCount) (header?app.headerMetrics[kind]:app.footers[kind].metrics)[id-SlotMetricId]=selected;
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
    HWND check=create(L"BUTTON",header?L"Show Header":L"Show Footer",SlotEnabledId,24,20,460,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(check,BM_SETCHECK,(header?app.tableHeaders[kind]:app.footers[kind].enabled)?BST_CHECKED:BST_UNCHECKED,0);
    const wchar_t* positions[]={L"Left",L"Center",L"Right"};
    for(int slot=0;slot<3;++slot) {
        create(L"STATIC",positions[slot],0,24,62+slot*44,90,24);
        HWND combo=create(L"COMBOBOX",L"",SlotMetricId+slot,125,60+slot*44,370,330,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        for(const auto* name:MetricTitles) SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
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
        auto checked=[&]{ return SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED; };
        auto selection=[&]{ return static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0)); };
        if(id==HeaderButtonId && IsTable(kind)) { OpenFooterSettings(w,kind,true); return 0; }
        if(id==FooterButtonId && IsTable(kind)) { OpenFooterSettings(w,kind); return 0; }
        if(id==OrderButtonId && kind!=PitLimiterKind) { OpenColumnOrder(w,kind); return 0; }
        if(id==IDOK || id==IDCANCEL) { ApplyOverlaySettings(w,kind); DestroyWindow(w); return 0; }
        if((id==RowsId || id==GapDecimalsId || (id>=LeadersId && id<=BehindId)) && code==EN_KILLFOCUS) { ApplyOverlaySettings(w,kind); return 0; }
        const bool click=code==BN_CLICKED, select=code==CBN_SELCHANGE;
        if(id==CornerId && select) {
            auto& overlay=app.overlays[kind]; const int corner=selection();
            overlay.place.growLeft=(corner&1)!=0; overlay.place.growUp=(corner&2)!=0;
            CaptureOverlayCorner(overlay);
        }
        else if(kind==WeatherKind && id>=WeatherDisplayId && id<=WeatherDisplayId+WindDirectionOption && click) { app.weatherDisplay[id-WeatherDisplayId]=checked(); EnableWindow(GetDlgItem(w,WeatherDisplayId+WindDirectionOption),app.weatherDisplay[WindElement]); }
        else if(kind==StandingsKind && id==UntimedOrderId && select) app.standings.fallbackByRating=selection()==1;
        else if(kind==StandingsKind && id==LiveGapsId && click) app.standings.liveGaps=checked();
        else if(kind==StandingsKind && id==IntervalId && click) app.showInterval=checked();
        else if(kind==RelativeKind && id==HidePitId && click) app.relativeHidePit=checked();
        else if(id==ClassViewId && select) app.standings.separateClasses=selection()==0;
        else if(id==OwnClassId && click) app.standings.ownClass=checked();
        else if(id>=FuelRowId && id<FuelRowId+FuelMetricCount && click) app.fuelRows[id-FuelRowId]=checked();
        else if(kind==FuelKind && id>=FuelDisplayId && id<FuelDisplayId+FuelElementCount && click) app.fuelDisplay[id-FuelDisplayId]=checked();
        else if(kind==FuelKind && id==ExcludeYellowId && click) app.excludeYellowFuelLaps=checked();
        else if(kind==StandingsKind && id==GainedColumnId && click) app.standingsGained=checked();
        else if(kind==StandingsKind && id==LapsColumnId && click) app.standingsLaps=checked();
        else if(IsTable(kind) && id==NameFormatId && select) app.nameFormat[kind]=selection();
        else if(IsTable(kind) && id==ShowDriverId && click) app.showDriver[kind]=checked();
        else if(IsTable(kind) && id==ShowStatusId && click) app.showStatus[kind]=checked();
        else if(IsTable(kind) && id==ColumnHeadingsId && click) app.columnHeaders[kind]=checked();
        else if(IsTable(kind) && id>=StatusFlagId && id<StatusFlagId+3 && click) app.driverFlags[kind][id-StatusFlagId]=checked();
        else if(IsTable(kind) && id>=ShortViewId && id<ShortViewId+2 && click) app.driverShortView[kind][id-ShortViewId]=checked();
        else if(IsTable(kind) && id>=DriverColumnId && id<DriverColumnId+DriverColumnCount && click) app.driverColumns[kind][id-DriverColumnId]=checked();
        else return 0;
        if(IsTable(kind)) {
            EnableWindow(GetDlgItem(w,NameFormatId),app.showDriver[kind]);
            for(int i=0;i<3;++i) EnableWindow(GetDlgItem(w,StatusFlagId+i),app.showStatus[kind]);
            for(int i=0;i<2;++i) EnableWindow(GetDlgItem(w,ShortViewId+i),app.driverColumns[kind][i]);
        }
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
    constexpr int heights[]={642,803,573,228,360};
    const int height=heights[kind];
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
    auto check=[&](const wchar_t* text,int id,int x,int y,int width,bool checked,bool enabled=true) {
        HWND c=control(L"BUTTON",text,id,x,y,width,24,BS_AUTOCHECKBOX|WS_TABSTOP);
        SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0); EnableWindow(c,enabled); return c;
    };
    auto number=[&](const wchar_t* text,int id,int y,int value) {
        control(L"STATIC",text,0,24,y,360,24);
        auto c=control(L"EDIT",std::to_wstring(value).c_str(),id,416,y,90,25,ES_NUMBER|WS_BORDER|WS_TABSTOP);
        SendMessageW(c,EM_SETLIMITTEXT,3,0);
    };
    auto combo=[&](int id,int x,int y,int width,std::initializer_list<const wchar_t*> items,int selected) {
        HWND c=control(L"COMBOBOX",L"",id,x,y,width,180,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        for(const auto* item:items) SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(c,CB_SETCURSEL,selected,0); return c;
    };
    if(kind==RelativeKind) {
        number(L"Rows (including your / the watched car)",RowsId,28,app.rows);
        number(L"Gap decimal places (0–3)",GapDecimalsId,71,app.gapDecimals);
        check(L"Hide Drivers in Pit",HidePitId,24,108,490,app.relativeHidePit);
        label(L"Odd rows (1–127); even entries round up. Your car stays centered.",147);
    } else if(kind==StandingsKind) {
        control(L"STATIC",L"Class view",0,24,25,130,24);
        combo(ClassViewId,170,25,336,{L"Separate classes",L"Combined overall standings"},app.standings.separateClasses?0:1);
        check(L"Show only driver's class",OwnClassId,24,72,490,app.standings.ownClass);
        number(L"Top drivers (per class / overall)",LeadersId,118,app.standings.leaders);
        number(L"Drivers ahead of your / the watched car",AheadId,159,app.standings.ahead);
        number(L"Drivers behind your / the watched car",BehindId,200,app.standings.behind);
        check(L"Show Interval (INT)",IntervalId,24,236,490,app.showInterval);
        check(L"Live Positions / GAP / INT (estimated)",LiveGapsId,24,268,490,app.standings.liveGaps);
        control(L"STATIC",L"Untimed driver order",0,24,305,140,24);
        combo(UntimedOrderId,170,305,336,{L"Car number (ascending)",L"iRating (descending)"},app.standings.fallbackByRating?1:0);
    } else if(kind==FuelKind) {
        control(L"STATIC",L"Consumption rows",0,24,23,250,24);
        control(L"STATIC",L"Display elements",0,290,23,226,24);
        const wchar_t* labels[]={L"Last lap",L"Average of last 10 laps",L"Average of last 5 laps",L"Maximum",L"Minimum",L"Race average"};
        for(int i=0;i<FuelMetricCount;++i) check(labels[i],FuelRowId+i,24,62+i*35,250,app.fuelRows[i]);
        // Listed in the default display order rather than by stored ID.
        for(int slot=0;slot<FuelElementCount;++slot) { const int i=DefaultFuelOrder[slot]; check(FuelElements[i].option,FuelDisplayId+i,290,62+slot*32,226,app.fuelDisplay[i]); }
        control(L"STATIC",L"Average uses all recorded laps.\nAverage 5 / 10 uses the latest laps.",0,24,282,250,55);
        check(L"Exclude yellow / red flag laps",ExcludeYellowId,24,350,250,app.excludeYellowFuelLaps);
    } else if(kind==WeatherKind) {
        // Wind direction is listed and indented directly below the wind card it belongs to.
        constexpr int listOrder[]={TrackTempElement,AirTempElement,WindElement,WindDirectionOption,WetnessElement,UsageElement};
        for(int slot=0;slot<6;++slot) {
            const int i=listOrder[slot]; const bool nested=i==WindDirectionOption;
            check(WeatherElements[i].option,WeatherDisplayId+i,nested?44:24,24+slot*31,nested?470:490,app.weatherDisplay[i],!nested || app.weatherDisplay[WindElement]);
        }
        label(L"Wind direction follows your live car heading. Speed: km/h.",218);
    } else {
        label(L"Shown on pit approach, in pit lane or with limiter active.",28);
        label(L"Green: pit limiter active. Red: pit limiter not active.",65);
        label(L"Stays visible after pit exit until the limiter is switched off.",102);
    }
    constexpr int cornerY[]={174,348,486,132,264};
    control(L"STATIC",L"Fixed corner",0,24,cornerY[kind],140,24);
    combo(CornerId,170,cornerY[kind],336,{L"Top Left",L"Top Right",L"Bottom Left",L"Bottom Right"},(app.overlays[kind].place.growLeft?1:0)+(app.overlays[kind].place.growUp?2:0));
    if(IsTable(kind)) {
        const int headingY=kind==RelativeKind?215:385;
        control(L"STATIC",L"Driver and status",0,24,headingY,240,24);
        control(L"STATIC",L"Additional columns",0,290,headingY,226,24);
        const int top=headingY+30;
        check(L"Show Driver",ShowDriverId,24,top,240,app.showDriver[kind]);
        EnableWindow(combo(NameFormatId,44,top+30,220,{L"First name Last name",L"F. Last name",L"F Last name"},app.nameFormat[kind]),app.showDriver[kind]);
        check(L"Show Status",ShowStatusId,24,top+64,240,app.showStatus[kind]);
        const wchar_t* flags[]={L"Show Black Flag",L"Show Slowdown",L"Show Required Repairs"};
        for(int i=0;i<3;++i) check(flags[i],StatusFlagId+i,44,top+96+32*i,220,app.driverFlags[kind][i],app.showStatus[kind]);
        check(L"Show Column Headings",ColumnHeadingsId,24,top+192,240,app.columnHeaders[kind]);
        if(kind==StandingsKind) {
            check(L"Show Positions Gained",GainedColumnId,24,top+224,240,app.standingsGained);
            check(L"Show Completed Laps",LapsColumnId,24,top+256,240,app.standingsLaps);
        }
        const wchar_t* labels[]={L"License / safety rating",L"iRating",L"Tires / compound",L"Last pit lap",L"Pit lane time",L"Pit stop time"};
        for(int i=0;i<DriverColumnCount;++i) {
            // License and iRating each have a Short View switch below them.
            const int columnY=top+32*(i+std::min(i,2));
            check(labels[i],DriverColumnId+i,290,columnY,226,app.driverColumns[kind][i]);
            if(i<2) check(L"Short View",ShortViewId+i,310,columnY+32,206,app.driverShortView[kind][i],app.driverColumns[kind][i]);
        }
        control(L"BUTTON",L"Header Settings...",HeaderButtonId,24,height-89,220,30,WS_TABSTOP);
        control(L"BUTTON",L"Footer Settings...",FooterButtonId,270,height-89,236,30,WS_TABSTOP);
    }
    if(kind!=PitLimiterKind) control(L"BUTTON",IsTable(kind)?L"Column Order...":L"Element Order...",OrderButtonId,24,height-47,220,30,WS_TABSTOP);
    control(L"BUTTON",L"Done",IDOK,416,height-47,90,30,BS_DEFPUSHBUTTON|WS_TABSTOP);
    MakeSettingsScrollable(w,540,height);
    ShowWindow(w,app.diagnostics?SW_SHOWNOACTIVATE:SW_SHOWNORMAL);
    if(!app.diagnostics) { SetForegroundWindow(w); HWND first=GetNextDlgTabItem(w,nullptr,FALSE); if(first) SetFocus(first); }
}
