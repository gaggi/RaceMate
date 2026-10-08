// Render checks and regression diagnostics; excluded from the production build.
bool Export(HWND window,const std::wstring& path) {
    RECT r{}; GetClientRect(window,&r); const int w=r.right,h=r.bottom;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(window),dc=CreateCompatibleDC(screen); void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0); auto old=SelectObject(dc,bitmap);
    if(window==app.main || window==app.overlaySettings || window==raceControl.window || window==columnOrderWindow || window==footerSettingsWindow || window==raceFieldsWindow) {
        FillRect(dc,&r,app.brush);
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            RECT bounds{}; GetWindowRect(child,&bounds); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
            const int saved=SaveDC(dc); SetViewportOrgEx(dc,bounds.left,bounds.top,nullptr);
            IntersectClipRect(dc,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
            SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
            RestoreDC(dc,saved);
        }
    }
    else SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader); file.bfSize=file.bfOffBits+w*h*4;
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&file),sizeof(file)); out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader)); out.write(static_cast<char*>(bits),static_cast<std::streamsize>(w)*h*4);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,screen); return out.good();
}
bool SettingsViewportDiagnostics(std::ostream& report) {
    bool ok=true;
    const int savedRows=app.rows;
    for(int kind=0; kind<OverlayCount; ++kind) {
        OpenOverlaySettings(kind);
        HWND window=app.overlaySettings;
        auto* view=reinterpret_cast<SettingsViewport*>(GetPropW(window,L"SettingsViewport"));
        if(!view) return false;
        RECT original{}; GetWindowRect(window,&original);
        SetWindowPos(window,nullptr,0,0,MulDiv(480,view->dpi,96),MulDiv(220,view->dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
        report<<"Viewport "<<kind<<": wheel y="<<view->y;
        ok=view->y>0 && ok;
        SendMessageW(GetDlgItem(window,IDOK),WM_SETFOCUS,0,0);
        RECT done{},client{}; GetWindowRect(GetDlgItem(window,IDOK),&done); GetClientRect(window,&client);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&done),2);
        report<<"; done="<<done.left<<","<<done.top<<","<<done.right<<","<<done.bottom<<" client="<<client.right<<","<<client.bottom;
        ok=done.left>=0 && done.top>=0 && done.right<=client.right && done.bottom<=client.bottom && ok;
        ok=Export(window,L"build\\qa\\settings-small-"+std::to_wstring(kind)+L".bmp") && ok;
        SetWindowPos(window,nullptr,0,0,original.right-original.left,original.bottom-original.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        ok=view->x==0 && view->y==0 && ok;
        report<<"; restored scroll="<<view->x<<","<<view->y<<"\n";
        if(kind==0) {
            SetDlgItemInt(window,400,15,FALSE);
            const int dpi=view->dpi;
            // DPI changes must keep the same dialog and any uncommitted edit text.
            for(int target:{96,192,dpi}) {
                RECT bounds{}; GetWindowRect(window,&bounds);
                SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(target,target),reinterpret_cast<LPARAM>(&bounds));
                report<<"DPI "<<target<<": same window="<<(app.overlaySettings==window)<<" rows="<<GetDlgItemInt(window,400,nullptr,FALSE)<<" dpi="<<view->dpi<<"\n";
                ok=app.overlaySettings==window && GetDlgItemInt(window,400,nullptr,FALSE)==15 && view->dpi==target && ok;
            }
        }
        SendMessageW(window,WM_CLOSE,0,0);
        if(kind==0) ok=app.rows==15 && ok;
    }
    app.rows=savedRows; Save();
    OpenOverlaySettings(0); OpenColumnOrder(app.overlaySettings,0);
    const HWND nested=columnOrderWindow;
    SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
    ok=!app.overlaySettings && !columnOrderWindow && !IsWindow(nested) && ok;
    report<<"Nested closed="<<(!app.overlaySettings && !columnOrderWindow && !IsWindow(nested))<<"\n";
    return ok;
}

int RunDiagnostics() {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    std::ofstream report("build/qa/controls.txt");
    for(int id:{210,220,231}) {
        wchar_t value[128]{}; HWND c=GetDlgItem(app.main,id);
        const auto index=SendMessageW(c,CB_GETCURSEL,0,0);
        SendMessageW(c,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(value));
        report<<id<<": index="<<index<<" count="<<SendMessageW(c,CB_GETCOUNT,0,0)<<" text-length="<<wcslen(value)<<"\n";
    }
    app.preview=true; app.edit=false; PreviewData(0);
    SendMessageW(GetDlgItem(app.main,PreviewId),BM_SETCHECK,BST_CHECKED,0);
    SetWindowTextW(app.status,L"Preview · sample data, no live telemetry");
    SetWindowPos(app.main,nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(app.main,SW_SHOWNOACTIVATE); UpdateWindow(app.main);
    bool ok=Export(app.main,L"build\\qa\\settings.bmp");
    const bool viewportOk=SettingsViewportDiagnostics(report);
    report<<"Settings scrolling, keyboard focus visibility, DPI edits and nested dialog cleanup: "<<(viewportOk?"PASS":"FAIL")<<"\n";
    ok=viewportOk && ok;
    {
        const bool savedFilter=app.excludeYellowFuelLaps;
        WritePrivateProfileStringW(L"Fuel Calculator",L"ExcludeYellowLaps",nullptr,app.config.c_str());
        Load();
        bool filterOk=!app.excludeYellowFuelLaps;
        for(bool enabled:{true,false}) {
            OpenOverlaySettings(2);
            SendDlgItemMessageW(app.overlaySettings,ExcludeYellowId,BM_SETCHECK,enabled?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(ExcludeYellowId,BN_CLICKED),0);
            Load(); filterOk=(app.excludeYellowFuelLaps==enabled) && filterOk;
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
            OpenOverlaySettings(2);
            filterOk=(SendDlgItemMessageW(app.overlaySettings,ExcludeYellowId,BM_GETCHECK,0,0)==(enabled?BST_CHECKED:BST_UNCHECKED)) && filterOk;
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        }
        app.excludeYellowFuelLaps=savedFilter; Save();
        report<<"Optional yellow fuel filter defaults off, persists and restores in settings: "<<(filterOk?"PASS":"FAIL")<<"\n";
        ok=filterOk && ok;
    }
    UpdateOverlays();
    for(auto& o:app.overlays) ok=Export(o.window,L"build\\qa\\overlay-"+std::to_wstring(o.kind)+L".bmp")&&ok;
    {
        const auto savedDriver=app.showDriver, savedStatus=app.showStatus, savedHeaders=app.tableHeaders, savedHeadings=app.columnHeaders;
        const auto savedFormats=app.nameFormat; const auto savedFooters=app.footers; const auto savedHeaderMetrics=app.headerMetrics; const auto savedFrame=app.frame; bool displayOk=true;
        app.frame.cars[1].outLap=true; app.frame.cars[1].sessionFlags=0x80000u; app.frame.cars[2].pit=true; app.frame.cars[2].sessionFlags=0x100000u;
        for(int kind=0;kind<2;++kind) {
            app.showDriver[kind]=app.showStatus[kind]=app.columnHeaders[kind]=true; app.tableHeaders[kind]=false; app.footers[kind].enabled=true; app.nameFormat[kind]=0; Save(); UpdateOverlays();
            OpenOverlaySettings(kind); Row test; test.driver.name="Daniel Fischer";
            for(int format=0;format<3;++format) {
                SendDlgItemMessageW(app.overlaySettings,511,CB_SETCURSEL,format,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(511,CBN_SELCHANGE),0);
                const wchar_t* expected[]={L"Daniel Fischer",L"D. Fischer",L"D Fischer"}; displayOk=DriverName(test,kind)==expected[format] && displayOk;
            }
            test.driver.name="Éric van der Meer"; app.nameFormat[kind]=1; displayOk=DriverName(test,kind)==L"É. van der Meer" && displayOk;
            Row status; status.car.pit=status.car.towing=status.car.outLap=true; status.car.sessionFlags=0x190000u;
            displayOk=OverlayStatus(status,kind)==L"PIT" && displayOk; status.car.pit=false;
            displayOk=OverlayStatus(status,kind)==L"TOW" && displayOk; status.car.towing=false;
            displayOk=OverlayStatus(status,kind)==L"BLACK" && displayOk; status.car.sessionFlags&=~0x10000u;
            displayOk=OverlayStatus(status,kind)==L"REPAIR" && displayOk; status.car.sessionFlags&=~0x100000u;
            displayOk=OverlayStatus(status,kind)==L"SLOW" && displayOk; status.car.sessionFlags=0;
            displayOk=OverlayStatus(status,kind)==L"OUT" && displayOk;
            SendDlgItemMessageW(app.overlaySettings,511,CB_SETCURSEL,1,0);
            int before=BaseHeight(kind); SendMessageW(app.overlaySettings,WM_COMMAND,449,0);
            displayOk=footerSettingsWindow && !IsWindowEnabled(app.overlaySettings) && displayOk;
            SendDlgItemMessageW(footerSettingsWindow,480,BM_SETCHECK,BST_CHECKED,0); SendMessageW(footerSettingsWindow,WM_COMMAND,MAKEWPARAM(480,BN_CLICKED),0); displayOk=BaseHeight(kind)==before+32 && displayOk;
            const int metrics[]={17,3,11}; for(int slot=0;slot<3;++slot) { SendDlgItemMessageW(footerSettingsWindow,481+slot,CB_SETCURSEL,metrics[slot],0); SendMessageW(footerSettingsWindow,WM_COMMAND,MAKEWPARAM(481+slot,CBN_SELCHANGE),0); }
            Load(); displayOk=app.headerMetrics[kind]==std::array<int,3>{17,3,11} && FooterValue(17,kind)==Titles[kind] && app.footers[kind].metrics==savedFooters[kind].metrics && displayOk;
            Export(footerSettingsWindow,L"build\\qa\\header-settings-"+std::to_wstring(kind)+L".bmp"); SendMessageW(footerSettingsWindow,WM_CLOSE,0,0);
            Export(app.overlaySettings,L"build\\qa\\integrated-settings-"+std::to_wstring(kind)+L".bmp"); Export(app.overlays[kind].window,L"build\\qa\\configured-header-status-"+std::to_wstring(kind)+L".bmp");
            const int expandedWidth=OverlayWidth(kind);
            SendDlgItemMessageW(app.overlaySettings,514,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(514,BN_CLICKED),0); displayOk=BaseHeight(kind)==before+5 && OverlayWidth(kind)<expandedWidth && displayOk;
            Export(app.overlays[kind].window,L"build\\qa\\compact-status-"+std::to_wstring(kind)+L".bmp");
            int width=OverlayWidth(kind);
            SendDlgItemMessageW(app.overlaySettings,510,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(510,BN_CLICKED),0); displayOk=!ColumnVisible(kind,2) && !IsWindowEnabled(GetDlgItem(app.overlaySettings,511)) && OverlayWidth(kind)==width-ColumnWidth(2,kind) && displayOk;
            width=OverlayWidth(kind);
            SendDlgItemMessageW(app.overlaySettings,512,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(512,BN_CLICKED),0); displayOk=!ColumnVisible(kind,15) && OverlayWidth(kind)==width-ColumnWidth(15,kind) && displayOk;
            SendMessageW(app.overlaySettings,WM_COMMAND,446,0); SendDlgItemMessageW(footerSettingsWindow,480,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(footerSettingsWindow,WM_COMMAND,MAKEWPARAM(480,BN_CLICKED),0); SendMessageW(footerSettingsWindow,WM_CLOSE,0,0);
            displayOk=BaseHeight(kind)==before+5-28 && displayOk;
            Load(); displayOk=!app.showDriver[kind] && !app.showStatus[kind] && !app.columnHeaders[kind] && app.tableHeaders[kind] && !app.footers[kind].enabled && app.nameFormat[kind]==1 && displayOk;
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        }
        app.showDriver=savedDriver; app.showStatus=savedStatus; app.tableHeaders=savedHeaders; app.columnHeaders=savedHeadings; app.nameFormat=savedFormats; app.footers=savedFooters; app.headerMetrics=savedHeaderMetrics; app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Integrated table settings, name formats, single status priority, configurable headers and independent footer persistence: "<<(displayOk?"PASS":"FAIL")<<"\n"; ok=displayOk && ok;
    }
    {
        const auto savedFlags=app.driverFlags; const auto savedFrame=app.frame; bool flagsOk=true;
        app.frame.cars[0].sessionFlags=0x10000u; app.frame.cars[1].sessionFlags=0x80000u; app.frame.cars[2].sessionFlags=0x100000u; app.frame.cars[3].sessionFlags=0x190000u;
        for(int kind=0;kind<2;++kind) {
            app.driverFlags[kind]={true,true,true}; Save(); OpenOverlaySettings(kind); UpdateOverlays();
            Export(app.overlaySettings,L"build\\qa\\driver-flags-settings-"+std::to_wstring(kind)+L".bmp"); Export(app.overlays[kind].window,L"build\\qa\\driver-flags-overlay-"+std::to_wstring(kind)+L".bmp");
            for(int i=0;i<3;++i) { SendDlgItemMessageW(app.overlaySettings,497+i,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(497+i,BN_CLICKED),0); }
            Load(); flagsOk=ColumnVisible(kind,15) && std::none_of(app.driverFlags[kind].begin(),app.driverFlags[kind].end(),[](bool v){return v;}) && flagsOk;
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        }
        app.driverFlags=savedFlags; app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Driver flag display settings, independent overlays and persisted disabled flags with PIT/OUT status retained: "<<(flagsOk?"PASS":"FAIL")<<"\n"; ok=flagsOk && ok;
    }
    {
        const auto savedShort=app.driverShortView; const auto savedColumns=app.driverColumns;
        bool shortOk=true;
        for(int kind=0;kind<2;++kind) {
            app.driverShortView[kind]={false,false}; app.driverColumns[kind][0]=app.driverColumns[kind][1]=true;
            OpenOverlaySettings(kind);
            for(int i=0;i<2;++i) {
                shortOk=IsWindowEnabled(GetDlgItem(app.overlaySettings,447+i)) && shortOk;
                SendDlgItemMessageW(app.overlaySettings,447+i,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(447+i,BN_CLICKED),0);
            }
            Load(); shortOk=app.driverShortView[kind][0] && app.driverShortView[kind][1] && shortOk;
            Row test; test.driver.license="A 3.45"; test.driver.rating=2400;
            shortOk=DriverValue(test,0,kind)==L"3,45" && DriverValue(test,1,kind)==L"2,4K" && DriverValue(test,0)==L"A 3.45" && shortOk;
            test.driver.license="Pro 4.99"; test.driver.rating=3450;
            shortOk=DriverValue(test,0,kind)==L"4,99" && DriverValue(test,1,kind)==L"3,5K" && shortOk;
            test.driver.license=""; test.driver.rating=0;
            shortOk=DriverValue(test,0,kind)==L"—" && DriverValue(test,1,kind)==L"—" && shortOk;
            Export(app.overlaySettings,L"build\\qa\\short-view-settings-"+std::to_wstring(kind)+L".bmp");
            UpdateOverlays(); Export(app.overlays[kind].window,L"build\\qa\\short-view-overlay-"+std::to_wstring(kind)+L".bmp");
            for(int i=0;i<2;++i) {
                SendDlgItemMessageW(app.overlaySettings,430+i,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(430+i,BN_CLICKED),0);
                shortOk=!IsWindowEnabled(GetDlgItem(app.overlaySettings,447+i)) && app.driverShortView[kind][i] && shortOk;
            }
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        }
        app.driverShortView=savedShort; app.driverColumns=savedColumns; Save(); UpdateOverlays();
        report<<"Independent short license/iRating views, formatting, missing values, dependent switches and persistence: "<<(shortOk?"PASS":"FAIL")<<"\n"; ok=shortOk && ok;
    }
    {
        const Frame originalFrame=app.frame; const Session originalSession=app.session;
        bool footerOk=true;
        for(int kind=0;kind<2;++kind) {
            OpenOverlaySettings(kind); SendMessageW(app.overlaySettings,WM_COMMAND,446,0);
            footerOk=footerSettingsWindow && !IsWindowEnabled(app.overlaySettings) && footerOk;
            footerOk=SendDlgItemMessageW(footerSettingsWindow,481,CB_GETCOUNT,0,0)==MetricCount && footerOk;
            const int values[]={3,8,6};
            for(int slot=0;slot<3;++slot) { SendDlgItemMessageW(footerSettingsWindow,481+slot,CB_SETCURSEL,values[slot],0); SendMessageW(footerSettingsWindow,WM_COMMAND,MAKEWPARAM(481+slot,CBN_SELCHANGE),0); }
            Save(); Load(); footerOk=app.footers[kind].metrics==std::array<int,3>{3,8,6} && footerOk;
            footerOk=Export(footerSettingsWindow,L"build\\qa\\footer-settings-"+std::to_wstring(kind)+L".bmp") && footerOk;
            footerOk=Export(app.overlays[kind].window,L"build\\qa\\footer-custom-"+std::to_wstring(kind)+L".bmp") && footerOk;
            const int before=HeightWithTitle(kind);
            SendDlgItemMessageW(footerSettingsWindow,480,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(footerSettingsWindow,WM_COMMAND,MAKEWPARAM(480,BN_CLICKED),0);
            Load(); footerOk=!app.footers[kind].enabled && HeightWithTitle(kind)==before-28 && footerOk;
            footerOk=Export(app.overlays[kind].window,L"build\\qa\\footer-hidden-"+std::to_wstring(kind)+L".bmp") && footerOk;
            SendMessageW(footerSettingsWindow,WM_CLOSE,0,0); footerOk=IsWindowEnabled(app.overlaySettings) && footerOk;
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0); app.footers[kind]={}; Save(); UpdateOverlays();
        }
        footerOk=FooterValue(9)==L"Lap 8 / ~20" && FooterValue(10)==L"~12.6 laps" && FooterValue(16).find(L"SOF ")==0 && footerOk;
        for(int metric=1;metric<MetricCount;++metric) footerOk=!FooterValue(metric).empty() && footerOk;
        app.frame.airTemperature=-5; app.frame.timeOfDay=86400+65*60;
        footerOk=FooterValue(4)==L"Air -5°C" && FooterValue(12)==L"Sim 01:05" && FooterValue(8)==L"Lap 8" && footerOk;
        app.frame.incidents=3; app.session.incidentLimit=17; footerOk=FooterValue(2)==L"3/17x" && footerOk;
        app.session.type="Practice"; footerOk=FooterValue(2)==L"3x" && footerOk;
        app.session.type="Qualify"; footerOk=FooterValue(2)==L"3x" && footerOk; app.session.type="Race";
        app.frame.onTrack=false; app.frame.camera=0; app.frame.cars[0].lap=12; app.session.drivers[0].incidentCount=4;
        footerOk=FooterValue(8)==L"Lap 12" && FooterValue(2)==L"4/17x" && footerOk;
        app.frame.trackWetness=7; footerOk=FooterValue(14)==L"Extremely Wet" && footerOk;
        app.frame.airTemperature=std::numeric_limits<double>::quiet_NaN(); footerOk=FooterValue(4)==L"Air —" && footerOk;
        app.frame.connected=false; footerOk=FooterValue(3)==L"—" && FooterValue(9)==L"—" && footerOk;
        app.frame=originalFrame; app.session=originalSession; UpdateOverlays();
        report<<"Independent footer dialogs, choices, persistence, resize, watched car, missing values and signed temperatures: "<<(footerOk?"PASS":"FAIL")<<"\n";
        ok=footerOk && ok;
    }
    {
        const auto saved=app.frame; constexpr double pi=3.141592653589793;
        bool windOk=OverlayWidth(4)==240 && BaseHeight(4)==WeatherHeight()-(app.showTitleBar?0:32);
        for(int direction=0;direction<5;++direction) {
            const double angle=direction==4?25*pi/180:direction*pi/2;
            app.frame.yawNorth=.4; app.frame.windDirection=-.4-angle; app.frame.windSpeed=5;
            UpdateOverlays(); windOk=std::abs(RelativeWindAngle(app.frame)-angle)<.001 && windOk;
            windOk=Export(app.overlays[4].window,L"build\\qa\\wind-"+std::to_wstring(direction)+L".bmp") && windOk;
        }
        app.frame.onTrack=false; UpdateOverlays(); windOk=Export(app.overlays[4].window,L"build\\qa\\wind-unavailable.bmp") && windOk;
        app.frame=saved; UpdateOverlays();
        report<<"Wind overlay cardinal and arbitrary source angles, speed, missing heading and rendering: "<<(windOk?"PASS":"FAIL")<<"\n"; ok=windOk && ok;
    }
    {
        bool weatherOk=true; OpenOverlaySettings(4);
        weatherOk=Export(app.overlaySettings,L"build\\qa\\weather-settings.bmp") && weatherOk;
        const auto originalOrder=app.weatherOrder;
        SendMessageW(app.overlaySettings,WM_COMMAND,445,0);
        weatherOk=columnOrderWindow && SendDlgItemMessageW(columnOrderWindow,450,LB_GETCOUNT,0,0)==5 && weatherOk;
        SendDlgItemMessageW(columnOrderWindow,450,LB_SETCURSEL,4,0);
        for(int step=0;step<4;++step) SendMessageW(columnOrderWindow,WM_COMMAND,451,0);
        Save(); Load(); weatherOk=app.weatherOrder.front()==originalOrder.back() && weatherOk;
        weatherOk=Export(app.overlays[4].window,L"build\\qa\\weather-reordered.bmp") && weatherOk;
        SendMessageW(columnOrderWindow,WM_CLOSE,0,0); app.weatherOrder=originalOrder; Save(); UpdateOverlays();
        for(int element=0;element<6;++element) {
            const int before=HeightWithTitle(4);
            SendDlgItemMessageW(app.overlaySettings,490+element,BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(490+element,BN_CLICKED),0);
            Load(); weatherOk=!app.weatherDisplay[element] && HeightWithTitle(4)==before-(element<WeatherCardCount?WeatherElements[element].height+8:0) && weatherOk;
            if(element==2) weatherOk=!IsWindowEnabled(GetDlgItem(app.overlaySettings,495)) && weatherOk;
        }
        weatherOk=!IsWindowVisible(app.overlays[4].window) && weatherOk;
        for(int element=0;element<6;++element) { SendDlgItemMessageW(app.overlaySettings,490+element,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(490+element,BN_CLICKED),0); }
        weatherOk=IsWindowEnabled(GetDlgItem(app.overlaySettings,495)) && weatherOk;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        app.weatherDisplay={false,false,true,false,false,true}; Save(); UpdateOverlays();
        weatherOk=Export(app.overlays[4].window,L"build\\qa\\weather-wind-only.bmp") && weatherOk;
        app.weatherDisplay.fill(true); Save(); UpdateOverlays();
        report<<"Weather card settings, element ordering, dependent direction switch, persistence, compact layout and all-hidden visibility: "<<(weatherOk?"PASS":"FAIL")<<"\n"; ok=weatherOk && ok;
    }
    {
        const auto rosterSession=app.session; const auto rosterFrame=app.frame; const auto rosterOptions=app.standings;
        app.session.type="Qualify"; app.session.results={}; for(auto& car:app.frame.cars) car=Car{};
        app.frame.camera=-1; app.frame.onTrack=true; app.standings.ownClass=false; app.standings.leaders=MaxCars;
        UpdateOverlays(); bool rosterOk=!Rows(1).empty() && Rows(0).size()==1 && Rows(0)[0].gap==0 && Rows(0)[0].car.position==0;
        Export(app.overlays[0].window,L"build\\qa\\relative-missing-progress.bmp"); Export(app.overlays[1].window,L"build\\qa\\qualifying-untimed.bmp");
        OpenOverlaySettings(1); SendDlgItemMessageW(app.overlaySettings,438,CB_SETCURSEL,1,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(438,CBN_SELCHANGE),0);
        Load(); rosterOk=app.standings.fallbackByRating && rosterOk;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        app.session=rosterSession; app.frame=rosterFrame; app.standings=rosterOptions; Save(); UpdateOverlays();
        report<<"Untimed qualifying roster, relative missing progress and sort preference persistence: "<<(rosterOk?"PASS":"FAIL")<<"\n"; ok=rosterOk && ok;
        const bool savedGained=app.standingsGained; const auto gainedGrid=app.session.gridPositions;
        app.standingsGained=false; Save(); const int gainedWidth=OverlayWidth(1);
        OpenOverlaySettings(1); SendDlgItemMessageW(app.overlaySettings,437,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(437,BN_CLICKED),0);
        Load(); bool gainedOk=app.standingsGained && ColumnVisible(1,14) && !ColumnVisible(0,14) && OverlayWidth(1)==gainedWidth+ColumnWidth(14,1);
        for(const auto& driver:app.session.drivers) app.session.gridPositions[driver.index]=driver.index+2;
        app.session.gridPositions[0]=3; app.session.gridPositions[1]=2;
        app.frame.cars[2].towing=true; UpdateOverlays();
        Export(app.overlays[1].window,L"build\\qa\\standings-gained-tow.bmp");
        app.frame.cars[2].towing=false;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0); app.standingsGained=savedGained; app.session.gridPositions=gainedGrid; Save();
        report<<"Standings gained column settings: "<<(gainedOk?"PASS":"FAIL")<<"\n"; ok=gainedOk && ok;
        const auto savedSession=app.session; const auto savedFrame=app.frame; const auto savedOrder=app.columnOrder[1]; const bool savedLaps=app.standingsLaps;
        app.standingsLaps=false; Save(); UpdateOverlays(); const int before=OverlayWidth(1);
        OpenOverlaySettings(1); SendDlgItemMessageW(app.overlaySettings,436,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(436,BN_CLICKED),0);
        Load(); bool lapsOk=app.standingsLaps && OverlayWidth(1)==before+ColumnWidth(13,1) && ColumnVisible(1,13) && !ColumnVisible(0,13);
        Row row; row.driver.index=0; row.car.completed=5; row.car.lap=7; lapsOk=CompletedLaps(row)==L"5" && lapsOk;
        row.car.completed=-1; row.car.lap=1; lapsOk=CompletedLaps(row)==L"0" && lapsOk;
        row.car.lap=-1; app.session.number=app.frame.session; app.session.results[0].completed=9; lapsOk=CompletedLaps(row)==L"9" && lapsOk;
        app.session.number=app.frame.session+1; lapsOk=CompletedLaps(row)==L"—" && lapsOk;
        app.session=savedSession;
        app.frame.cars[0].completed=6; app.frame.cars[0].lap=7;
        UpdateOverlays(); lapsOk=Export(app.overlays[1].window,L"build\\qa\\standings-laps.bmp") && lapsOk;
        lapsOk=Export(app.overlaySettings,L"build\\qa\\standings-laps-settings.bmp") && lapsOk;
        SendMessageW(app.overlaySettings,WM_COMMAND,445,0);
        const int index=static_cast<int>(std::find(app.columnOrder[1].begin(),app.columnOrder[1].end(),13)-app.columnOrder[1].begin());
        SendDlgItemMessageW(columnOrderWindow,450,LB_SETCURSEL,index,0); for(int step=0;step<index;++step) SendMessageW(columnOrderWindow,WM_COMMAND,451,0);
        Load(); lapsOk=app.columnOrder[1].front()==13 && app.standingsLaps && lapsOk;
        lapsOk=Export(app.overlays[1].window,L"build\\qa\\standings-laps-first.bmp") && lapsOk;
        SendMessageW(columnOrderWindow,WM_CLOSE,0,0);
        SendDlgItemMessageW(app.overlaySettings,436,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(436,BN_CLICKED),0);
        lapsOk=!app.standingsLaps && OverlayWidth(1)==before && lapsOk;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        app.session=savedSession; app.frame=savedFrame; app.columnOrder[1]=savedOrder; app.standingsLaps=savedLaps; Save(); UpdateOverlays();
        report<<"Standings completed laps: toggle, width, persistence, column order, live count, zero laps, session fallback and missing data: "<<(lapsOk?"PASS":"FAIL")<<"\n"; ok=lapsOk && ok;
    }
    {
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        for(int kind:{1,4}) {
            auto& overlay=app.overlays[kind]; InvalidateModel(); RefreshOverlay(overlay);
            const DWORD handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
            for(int pass=0;pass<300;++pass) { overlay.scene.clear(); InvalidateModel(); RefreshOverlay(overlay); }
            GdiFlush(); QueryPerformanceCounter(&end);
            report<<"Full scene + paint benchmark kind "<<kind<<": "<<(end.QuadPart-begin.QuadPart)*1000000.0/frequency.QuadPart/300<<" us; GDI handle delta "<<(static_cast<int>(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))-static_cast<int>(handles))<<"\n";
        }
    }
    {
        const auto savedFrame=app.frame; const auto savedSession=app.session; const int savedRows=app.rows; const bool savedHide=app.relativeHidePit;
        bool centerOk=true;
        app.rows=9; app.relativeHidePit=true; app.session.type="Practice";
        for(const auto& driver:app.session.drivers) if(driver.index!=app.frame.player) app.frame.cars[driver.index].surface=-1;
        app.frame.cars[app.frame.player].pit=true;
        UpdateOverlays(); centerOk=Export(app.overlays[0].window,L"build\\qa\\relative-centered-alone.bmp") && centerOk;
        auto& overlay=app.overlays[0]; const int centerDpi=GetDpiForWindow(overlay.window),factor=centerDpi*overlay.place.scale;
        const int midY=MulDiv(TableRowsTop(0)+(app.rows/2)*27+13-(ShowOverlayHeader(0)?0:32),factor,9600);
        centerOk=GetPixel(overlay.dc,MulDiv(8,factor,9600),midY)==RGB(54,49,30) && centerOk;
        app.frame=savedFrame; app.session=savedSession; app.session.type="Practice";
        app.frame.incidents=18; app.frame.incidentsKnown=true; app.session.incidentLimit=25; app.footers[0].metrics={1,0,2};
        UpdateOverlays(); centerOk=FooterValue(2)==L"18x" && centerOk;
        centerOk=Export(app.overlays[0].window,L"build\\qa\\relative-practice-colors.bmp") && centerOk;
        app.rows=savedRows; app.relativeHidePit=savedHide; app.frame=savedFrame; app.session=savedSession; Save(); UpdateOverlays();
        report<<"Relative center with empty slots and reference in pit; practice footer has incidents without race limit: "<<(centerOk?"PASS":"FAIL")<<"\n"; ok=centerOk && ok;
    }
    const double savedCapacity=app.session.fuelCapacity,savedFuel=app.frame.fuel;
    app.session.fuelCapacity=100; UpdateOverlays();
    auto fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption,Plan().prediction.ownPace);
    ok=fuelSummary.openNow && fuelSummary.timeEmpty>0 && fuelSummary.fuelAtEnd<0 && ok;
    ok=Export(app.overlays[2].window,L"build\\qa\\fuel-open.bmp") && ok;
    app.frame.fuel=50; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.stops==0 && fuelSummary.fuelAtEnd>0 && ok;
    ok=Export(app.overlays[2].window,L"build\\qa\\fuel-surplus.bmp") && ok;
    app.session.fuelCapacity=14; app.frame.fuel=12; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.stops==3 && ok;
    ok=Export(app.overlays[2].window,L"build\\qa\\fuel-three-stops.bmp") && ok;
    app.session.fuelCapacity=-1; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.status==PitWindowStatus::Unavailable && !fuelSummary.openNow && ok;
    ok=Export(app.overlays[2].window,L"build\\qa\\fuel-window-unknown.bmp") && ok;
    app.session.fuelCapacity=savedCapacity; app.frame.fuel=savedFuel; UpdateOverlays();
    report<<"Fuel time empty, signed finish fuel, open/closed text and three-stop UI: "<<(ok?"PASS":"FAIL")<<"\n";
    for(int kind=0;kind<OverlayCount;++kind) {
        SendMessageW(app.main,WM_COMMAND,240+kind,0);
        ok=(app.overlaySettings && app.settingsKind==kind)&&ok;
        if(app.overlaySettings) {
            ok=Export(app.overlaySettings,L"build\\qa\\settings-"+std::to_wstring(kind)+L".bmp")&&ok;
            if(kind==0) {
                SetDlgItemInt(app.overlaySettings,400,7,FALSE);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(400,EN_KILLFOCUS),0);
                ok=app.rows==7 && ok;
            } else if(kind==1) {
                const int originalWidth=OverlayWidth(1); app.showInterval=false; Fit(app.overlays[1]);
                SendDlgItemMessageW(app.overlaySettings,408,BM_SETCHECK,BST_CHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(408,BN_CLICKED),0);
                ok=app.showInterval && OverlayWidth(1)>=originalWidth && ok;
                SendDlgItemMessageW(app.overlaySettings,409,BM_SETCHECK,BST_CHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(409,BN_CLICKED),0);
                ok=app.standings.liveGaps && ok; Save(); Load(); ok=app.standings.liveGaps && ok;
                SendDlgItemMessageW(app.overlaySettings,409,BM_SETCHECK,BST_UNCHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(409,BN_CLICKED),0);
                ok=!app.standings.liveGaps && ok;

                ok=Export(app.overlays[1].window,L"build\\qa\\standings-interval.bmp") && ok;
                ok=Export(app.overlaySettings,L"build\\qa\\settings-1.bmp") && ok;
                SetDlgItemInt(app.overlaySettings,402,2,FALSE); SetDlgItemInt(app.overlaySettings,403,1,FALSE); SetDlgItemInt(app.overlaySettings,404,1,FALSE);
                SendDlgItemMessageW(app.overlaySettings,401,CB_SETCURSEL,1,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(401,CBN_SELCHANGE),0);
                const auto groups=SelectStandings(app.session,app.frame,app.standings);
                ok=(!app.standings.separateClasses && groups.size()==1 && groups[0].rows.size()==5 && app.rows==7)&&ok;
                ok=Export(app.overlays[1].window,L"build\\qa\\standings-combined.bmp")&&ok;
                SendDlgItemMessageW(app.overlaySettings,405,BM_SETCHECK,BST_CHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(405,BN_CLICKED),0);
                ok=app.standings.ownClass && Rows(0).size()<=7 && ok;
            } else if(kind==2) {
                SendDlgItemMessageW(app.overlaySettings,411,BM_SETCHECK,BST_UNCHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(411,BN_CLICKED),0);
                const int newElements=FuelElements[KeyNumbersElement].height+FuelElements[PitTimelineElement].height;
                ok=!app.fuelRows[1] && BaseHeight(2)==(app.showTitleBar?524:492)+newElements && ok;
                for(int i=0;i<FuelElementCount;++i) {
                    const int before=HeightWithTitle(2);
                    SendDlgItemMessageW(app.overlaySettings,FuelDisplayId+i,BM_SETCHECK,BST_UNCHECKED,0);
                    SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(FuelDisplayId+i,BN_CLICKED),0);
                    ok=!app.fuelDisplay[i] && HeightWithTitle(2)==before-FuelElements[i].height && ok;
                }
                ok=HeightWithTitle(2)==213 && OverlayWidth(2)==365 && ok;
                Save(); Load();
                ok=std::none_of(app.fuelDisplay.begin(),app.fuelDisplay.end(),[](bool value){return value;}) && ok;
                ok=Export(app.overlays[2].window,L"build\\qa\\fuel-compact.bmp") && ok;
                for(int i=0;i<FuelElementCount;++i) {
                    SendDlgItemMessageW(app.overlaySettings,FuelDisplayId+i,BM_SETCHECK,BST_CHECKED,0);
                    SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(FuelDisplayId+i,BN_CLICKED),0);
                }
                ok=Export(app.overlaySettings,L"build\\qa\\settings-2.bmp") && ok;
                const auto originalOrder=app.fuelOrder;
                OpenColumnOrder(app.overlaySettings,2);
                ok=columnOrderWindow && !IsWindowEnabled(app.overlaySettings) && ok;
                const int table=static_cast<int>(std::find(app.fuelOrder.begin(),app.fuelOrder.end(),ConsumptionTableElement)-app.fuelOrder.begin());
                SendDlgItemMessageW(columnOrderWindow,OrderListId,LB_SETCURSEL,table,0);
                SendMessageW(columnOrderWindow,WM_COMMAND,MoveUpId,0);
                ok=table>0 && app.fuelOrder[table-1]==ConsumptionTableElement && ok;
                Save(); Load(); ok=app.fuelOrder[table-1]==ConsumptionTableElement && ok;
                UpdateOverlays();
                ok=Export(app.overlays[2].window,L"build\\qa\\fuel-reordered.bmp") && ok;
                SendMessageW(columnOrderWindow,WM_CLOSE,0,0);
                app.fuelOrder=originalOrder; Save(); UpdateOverlays();
                const auto savedFrame=app.frame; const double savedDistance=app.fuelPlan.lapsToFinish;
                app.frame.lapCompleted=7; app.frame.lapPct=.2; app.fuelPlan.lapsToFinish=12.02;
                ok=FuelFinishLap()==20 && ok;
                app.frame.lapPct=.99; ok=FuelFinishLap()==21 && ok;
                app.fuelPlan.lapsToFinish=0; ok=FuelFinishLap()==8 && ok;
                app.fuelPlan.lapsToFinish=-1; ok=FuelFinishLap()==-1 && ok;
                app.frame=savedFrame; app.fuelPlan.lapsToFinish=savedDistance;


            }
            if(kind<2) {
                SendDlgItemMessageW(app.overlaySettings,435,BM_SETCHECK,BST_CHECKED,0);
                SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(435,BN_CLICKED),0);
                ok=app.driverColumns[kind][5] && DriverValue(Rows(kind).front(),5).find(L" s")!=std::wstring::npos && ok;
                ok=Export(app.overlays[kind].window,L"build\\qa\\pit-stop-"+std::to_wstring(kind)+L".bmp") && ok;
                ok=Export(app.overlaySettings,L"build\\qa\\settings-"+std::to_wstring(kind)+L".bmp") && ok;
            }
            SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
            ok=!app.overlaySettings && ok;
        }
    }
    OpenRaceControl(); UpdateRaceControl(true);
    ok=raceControl.window && !raceControl.rows.empty() && raceControl.events.size()==6 && ok;
    ok=raceControl.columns.size()>=9 && raceControl.columns[3]==8 && std::find(raceControl.columns.begin(),raceControl.columns.end(),5)!=raceControl.columns.end() && ok;
    for(const auto& row:raceControl.rows) {
        if(row.driver.index==1) ok=RaceCarStatus(row)==L"Outlap" && ok;
        if(row.driver.index==2) ok=RaceCarStatus(row)==L"Pit lane" && ok;
        if(row.driver.index==5) ok=RaceCarInactive(row) && RaceCarStatus(row)==L"Inactive" && ok;
    }
    const int chase=static_cast<int>(SendMessageW(raceControl.camera,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(L"Far Chase")));
    SendMessageW(raceControl.camera,CB_SETCURSEL,chase,0);
    SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(818,CBN_SELCHANGE),0);
    ok=chase>=0 && RaceCameraGroup()==11 && ok;
    ok=Export(raceControl.window,L"build\\qa\\race-control.bmp") && ok;
    if(raceControl.events.size()>0) {
        NMITEMACTIVATE action{}; action.hdr.hwndFrom=raceControl.feed; action.hdr.code=NM_CLICK; action.iItem=0; action.iSubItem=0;
        SendMessageW(raceControl.window,WM_NOTIFY,811,reinterpret_cast<LPARAM>(&action));
        ok=!raceControl.pendingNumber.empty() && raceControl.pendingGroup==11 && ok;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        SendMessageW(raceControl.camera,CB_SETCURSEL,2,0);
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(818,CBN_SELCHANGE),0);
        action.iSubItem=1; SendMessageW(raceControl.window,WM_NOTIFY,811,reinterpret_cast<LPARAM>(&action));
        ok=!raceControl.pendingNumber.empty() && raceControl.pendingGroup==12 && ok;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        CloseRaceControl(); OpenRaceControl(); ok=RaceCameraGroup()==12 && ok;
        SendMessageW(raceControl.camera,CB_SETCURSEL,chase,0);
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(818,CBN_SELCHANGE),0);
    }
    SendMessageW(raceControl.filter,CB_SETCURSEL,1,0); SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(812,CBN_SELCHANGE),0);
    ok=raceControl.events.size()==1 && raceControl.events.front().type==EventType::Offtrack && ok;
    SendMessageW(raceControl.window,WM_CLOSE,0,0); ok=!IsWindowVisible(raceControl.window) && ok;
    OpenRaceControl(); ok=IsWindowVisible(raceControl.window) && ok;
    NMITEMACTIVATE click{}; click.hdr.hwndFrom=raceControl.standings; click.hdr.code=NM_DBLCLK; click.iItem=0;
    SendMessageW(raceControl.window,WM_NOTIFY,810,reinterpret_cast<LPARAM>(&click));
    ok=raceControl.message.find(L"camera action simulated")!=std::wstring::npos && ok;
    report<<"Race Control standings, event filters, replay actions and close/reopen: "<<(ok?"PASS":"FAIL")<<"\n";
    {
        bool rcOk=true; const auto savedFields=raceControl.fields; const auto savedEvents=raceControl.eventFields; const auto savedOptions=app.standings; const auto savedDrivers=app.session.drivers;
        SendMessageW(raceControl.filter,CB_SETCURSEL,0,0); app.standings.ownClass=false; UpdateRaceControl(true);
        DWORD_PTR headerData=0; rcOk=GetWindowSubclass(ListView_GetHeader(raceControl.standings),RaceHeaderProc,1,&headerData) && rcOk;
        OpenRaceFields(); rcOk=raceFieldsWindow && !IsWindowEnabled(raceControl.window) && rcOk;
        SendDlgItemMessageW(raceFieldsWindow,906,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(raceFieldsWindow,WM_COMMAND,MAKEWPARAM(906,BN_CLICKED),0);
        SendDlgItemMessageW(raceFieldsWindow,920,BM_SETCHECK,BST_UNCHECKED,0); SendMessageW(raceFieldsWindow,WM_COMMAND,MAKEWPARAM(920,BN_CLICKED),0);
        rcOk=std::find(raceControl.columns.begin(),raceControl.columns.end(),6)==raceControl.columns.end() && raceControl.feedColumns.front()==1 && rcOk;
        Export(raceFieldsWindow,L"build\\qa\\race-manage-fields.bmp"); SendMessageW(raceFieldsWindow,WM_CLOSE,0,0);
        rcOk=IsWindowEnabled(raceControl.window) && rcOk; LoadRaceFields(); rcOk=!raceControl.fields[6] && !raceControl.eventFields[0] && rcOk;
        // Numeric iRating sorting, missing values last in either direction.
        for(auto& driver:app.session.drivers) driver.rating=driver.index==0?0:(driver.index==1?999:10000+driver.index);
        SetRaceSort(false,10,0); rcOk=raceControl.rows.front().driver.index==1 && raceControl.rows.back().driver.index==0 && rcOk;
        SetRaceSort(false,10,1); rcOk=raceControl.rows.front().driver.rating>=10000 && raceControl.rows.back().driver.index==0 && rcOk;
        // Actual column click toggles event order.
        NMLISTVIEW sort{}; sort.hdr.hwndFrom=raceControl.feed; sort.hdr.code=LVN_COLUMNCLICK; sort.iSubItem=static_cast<int>(std::find(raceControl.feedColumns.begin(),raceControl.feedColumns.end(),2)-raceControl.feedColumns.begin());
        SendMessageW(raceControl.window,WM_NOTIFY,811,reinterpret_cast<LPARAM>(&sort)); rcOk=raceControl.events.front().time<raceControl.events.back().time && rcOk;
        SendMessageW(raceControl.window,WM_NOTIFY,811,reinterpret_cast<LPARAM>(&sort)); rcOk=raceControl.events.front().time>raceControl.events.back().time && rcOk;
        int carItem=-1; for(int i=0;i<SendMessageW(raceControl.carFilter,CB_GETCOUNT,0,0);++i) if(SendMessageW(raceControl.carFilter,CB_GETITEMDATA,i,0)==2) carItem=i;
        SendMessageW(raceControl.carFilter,CB_SETCURSEL,carItem,0); SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(819,CBN_SELCHANGE),0);
        rcOk=raceControl.events.size()==1 && raceControl.events.front().car==2 && rcOk;
        NMITEMACTIVATE action{}; action.hdr.hwndFrom=raceControl.feed; action.hdr.code=NM_CLICK; action.iItem=0; action.iSubItem=0;
        SendMessageW(raceControl.window,WM_NOTIFY,811,reinterpret_cast<LPARAM>(&action)); rcOk=raceControl.pendingNumber==raceControl.events.front().number && rcOk;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        SendMessageW(raceControl.filter,CB_SETCURSEL,1,0); UpdateRaceControl(true); rcOk=raceControl.events.empty() && rcOk;
        Export(raceControl.window,L"build\\qa\\race-control-filtered.bmp");
        raceControl.fields=savedFields; raceControl.eventFields=savedEvents; app.standings=savedOptions; app.session.drivers=savedDrivers;
        raceControl.sortColumn=-1; raceControl.eventSortColumn=2; raceControl.eventSortDescending=true; SaveRaceFields();
        SendMessageW(raceControl.carFilter,CB_SETCURSEL,0,0); SendMessageW(raceControl.filter,CB_SETCURSEL,0,0); UpdateRaceControl(true);
        report<<"Race Control field visibility, numeric sorting, missing values, event sorting, car/type filters and remapped replay buttons: "<<(rcOk?"PASS":"FAIL")<<"\n"; ok=rcOk && ok;
    }
    SendDlgItemMessageW(app.main,250,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.main,WM_COMMAND,MAKEWPARAM(250,BN_CLICKED),0);
    SendDlgItemMessageW(app.main,251,BM_SETCHECK,BST_CHECKED,0); SendMessageW(app.main,WM_COMMAND,MAKEWPARAM(251,BN_CLICKED),0);
    Load();
    ok=app.driverColumns[0][5] && app.driverColumns[1][5] && app.showInterval && app.rows==7 && app.standings.leaders==2 && app.standings.ahead==1 && app.standings.behind==1 && app.standings.ownClass && !app.standings.separateClasses && !app.fuelRows[1] && app.startWithWindows && app.startMinimized && ok;
    ShowWindow(app.main,StartupShowCommand(true,SW_SHOW)); UpdateOverlays();
    ok=!IsWindowVisible(app.main) && IsWindowVisible(app.overlays[0].window) && ok;
    ShowWindow(app.main,StartupShowCommand(false,SW_SHOW)); ok=IsWindowVisible(app.main) && ok;
    const auto startup=StartupCommand(); ok=(!startup.empty() && startup.front()==L'"' && startup.back()==L'"') && ok;
    SendMessageW(app.main,WM_CLOSE,0,0); ok=!IsWindowVisible(app.main) && IsWindow(app.main) && ok;
    report<<"Overlay dialogs, independent options, class view switch, fuel rows, close-to-tray and persistence: "<<(ok?"PASS":"FAIL")<<"\n";
    app.rows=9; app.standings={}; app.fuelRows.fill(true); app.fuelDisplay.fill(true); app.startWithWindows=app.startMinimized=false;
    {
        const auto savedSession=app.session; const auto savedFrame=app.frame; const bool savedPreview=app.preview;
        app.preview=false; app.fuel.Reset(); app.predictor.Reset(); app.session.type="Race"; app.session.number=app.frame.session;
        std::erase_if(app.session.drivers,[](const Driver& driver){return driver.index!=app.frame.player;});
        app.frame.pit=app.frame.inStall=false; app.frame.onTrack=app.frame.connected=app.frame.fuelKnown=true;
        app.frame.flags=0; app.frame.time=0; app.frame.state=3; app.frame.fuel=50; app.frame.lapCompleted=0; app.frame.lapPct=0;
        app.fuel.Update(app.session,app.frame);
        app.frame.time=1; app.frame.state=4; app.fuel.Update(app.session,app.frame);
        app.frame.time=101; app.frame.lapCompleted=1; app.frame.fuel=46.8; app.frame.bestLap=app.frame.lastLap=100;
        auto& own=app.frame.cars[app.frame.player]; own.lap=2; own.completed=1; own.pct=0; own.estTime=0; own.best=own.last=100; own.position=own.classPosition=1; own.surface=3;
        app.fuel.Update(app.session,app.frame); UpdateOverlays();
        ok=Plan().estimates[0].samples==1 && std::abs(Plan().estimates[0].consumption-3.2)<.001 && Plan().estimates[5].samples==1 && ok;
        ok=Export(app.overlays[2].window,L"build\\qa\\fuel-first-race-lap.bmp") && ok;
        report<<"Fuel Last Lap and Average populated immediately after observed first race lap: "<<(ok?"PASS":"FAIL")<<"\n";
        app.session=savedSession; app.frame=savedFrame; app.preview=savedPreview; app.fuel.Reset(); app.predictor.Reset(); UpdateOverlays();
    }
    const auto demoSession=app.session; const auto demoFrame=app.frame;
    app.preview=false; app.session.type="Practice"; app.session.number=app.frame.session;
    for(const auto& driver:app.session.drivers) {
        auto& result=app.session.results[driver.index];
        result.position=driver.index+1; result.classPosition=driver.index+1;
        result.best=120.5+driver.index*.4; result.last=121.2+driver.index*.5;
        auto& car=app.frame.cars[driver.index];
        car.best=car.last=car.f2Time=-1; car.position=car.classPosition=0;
    }
    const auto historicalRows=Rows(1);
    ok=historicalRows.size()==9 && ok;
    for(const auto& row:historicalRows) ok=row.car.best>0 && row.car.last>0 && ok;
    UpdateOverlays(); ok=Export(app.overlays[1].window,L"build\\qa\\standings-history.bmp")&&ok;
    report<<"Late-join standings populated from session results only: "<<(ok?"PASS":"FAIL")<<"\n";
    {
        app.session.results={}; for(auto& car:app.frame.cars) { car.best=car.last=car.f2Time=-1; car.position=car.classPosition=0; car.surface=-1; }
        UpdateOverlays(); const auto practiceRows=Rows(1);
        ok=!practiceRows.empty() && std::none_of(practiceRows.begin(),practiceRows.end(),[](const Row& row){return row.car.best>0;}) && ok;
        ok=Export(app.overlays[1].window,L"build\\qa\\practice-no-times.bmp") && ok;
        app.frame.cars[9].best=125; UpdateOverlays();
        const auto rankedPractice=Standings(app.session,app.frame,MaxCars,false);
        ok=!rankedPractice.empty() && rankedPractice.front().driver.index==9 && ok;
        ok=Export(app.overlays[1].window,L"build\\qa\\practice-first-time.bmp") && ok;
        report<<"Untimed practice roster, car-number order and first timed lap: "<<(ok?"PASS":"FAIL")<<"\n";
    }
    app.session=demoSession; app.frame=demoFrame; app.preview=true;
    {
        app.preview=false; app.session.type="Race"; app.session.number=app.frame.session; app.frame.state=3;
        app.frame.bestLap=app.frame.lastLap=-1; app.standings.liveGaps=true;
        for(auto& driver:app.session.drivers) {
            driver.estimatedLapTime=140; app.session.results[driver.index]={};
            auto& car=app.frame.cars[driver.index]; car.position=car.classPosition=0; car.completed=-1; car.lap=0; car.best=car.last=car.f2Time=-1; car.estTime=car.pct*140;
        }
        UpdateOverlays();
        ok=!Rows(1).empty() && ok;
        const auto earlyRows=Rows(0); for(const auto& row:earlyRows) ok=std::isfinite(row.gap) && ok;
        ok=Export(app.overlays[0].window,L"build\\qa\\relative-formation.bmp") && ok;
        ok=Export(app.overlays[1].window,L"build\\qa\\standings-formation.bmp") && ok;
        app.frame.state=4; for(auto& car:app.frame.cars) if(car.surface>=0) { car.completed=0; car.lap=1; }
        UpdateOverlays(); ok=!Rows(1).empty() && ok;
        ok=Export(app.overlays[1].window,L"build\\qa\\standings-first-lap.bmp") && ok;
        report<<"Formation / first-lap standings, early relative gaps and live-gap settings: "<<(ok?"PASS":"FAIL")<<"\n";
        app.session=demoSession; app.frame=demoFrame; app.preview=true; app.standings.liveGaps=false;
    }
    app.frame.limiter=false; ok=Export(app.overlays[3].window,L"build\\qa\\pit-red.bmp")&&ok;
    {
        const auto savedFrame=app.frame; const auto savedSession=app.session; const auto savedStandings=app.standings;
        app.preview=false; app.session.type="Race"; app.frame.state=4; app.standings={}; app.standings.liveGaps=true;
        app.frame.cars[app.frame.player].pct=.52; app.frame.cars[app.frame.player].estTime=.52*138.2;
        UpdateOverlays(); const auto liveRows=Rows(1); const auto liveRelative=Rows(0);
        const auto player=std::find_if(liveRelative.begin(),liveRelative.end(),[](const Row& row){return row.player;});
        ok=player!=liveRelative.end() && player->car.position==1 && !liveRows.empty() && liveRows.front().player && liveRows.front().car.classPosition==1 && ok;
        ok=Export(app.overlays[0].window,L"build\\qa\\relative-live-overtake.bmp") && ok;
        ok=Export(app.overlays[1].window,L"build\\qa\\standings-live-overtake.bmp") && ok;
        app.frame=savedFrame; app.session=savedSession; app.standings=savedStandings; app.preview=true;
        report<<"Live overtake updates Relative P and Standings row order / class P: "<<(ok?"PASS":"FAIL")<<"\n";
    }
    app.pitVisibility.Reset();
    app.preview=false; app.hidden=false; app.frame.pit=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(app.overlays[i].window))==(i!=3))&&ok;
    const auto originalSurface=app.frame.cars[app.frame.player].surface;
    app.frame.cars[app.frame.player].surface=2; UpdateOverlays(); ok=IsWindowVisible(app.overlays[3].window) && ok;
    ok=Export(app.overlays[3].window,L"build\\qa\\pit-approach.bmp") && ok;
    app.frame.cars[app.frame.player].surface=3; app.frame.pit=true; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(app.overlays[3].window) && ok;
    app.frame.pit=false; app.frame.limiter=true; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(app.overlays[3].window) && ok;
    app.frame.limiter=false; app.frame.time+=1; UpdateOverlays(); ok=!IsWindowVisible(app.overlays[3].window) && ok;
    app.frame.cars[app.frame.player].surface=2; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(app.overlays[3].window) && ok;
    app.frame.cars[app.frame.player].surface=originalSurface;
    app.frame.pit=true; UpdateOverlays(); ok=IsWindowVisible(app.overlays[3].window)&&ok;
    app.frame.onTrack=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(app.overlays[i].window))==(i!=3))&&ok;
    app.frame.camera=4; app.frame.replay=true; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(app.overlays[i].window))==(i!=3))&&ok;
    ok=ReferenceCar(app.frame)==4 && !Rows(0).empty() && ok;
    ok=Export(app.overlays[0].window,L"build\\qa\\relative-replay.bmp")&&ok;
    app.frame.replay=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(app.overlays[i].window))==(i!=3))&&ok;
    app.frame.connected=false; UpdateOverlays();
    for(auto& o:app.overlays) ok=!IsWindowVisible(o.window)&&ok;
    app.preview=true; app.hidden=true; UpdateOverlays();
    for(auto& o:app.overlays) ok=!IsWindowVisible(o.window)&&ok;
    app.overlays[0].place.x=123; app.overlays[0].place.scale=130; Save();
    app.overlays[0].place.x=0; app.overlays[0].place.scale=50; Load();
    ok=(app.overlays[0].place.x==123 && app.overlays[0].place.scale==130 && !app.settingsError)&&ok;
    report<<"Visibility in car, spotting, replay, pause, disconnect, pit lane, hide and saved placement: "<<(ok?"PASS":"FAIL")<<"\n";
    app.preview=true; PreviewData(0);
    bool orderOk=true;
    for(int kind=0;kind<2;++kind) {
        const auto original=app.columnOrder[kind]; const auto other=app.columnOrder[1-kind];
        OpenOverlaySettings(kind); SendMessageW(app.overlaySettings,WM_COMMAND,445,0);
        HWND orderWindow=columnOrderWindow;
        orderOk=orderWindow && SendDlgItemMessageW(orderWindow,450,LB_GETCOUNT,0,0)==static_cast<LRESULT>(original.size()) && orderOk;
        SendDlgItemMessageW(orderWindow,450,LB_SETCURSEL,0,0); SendMessageW(orderWindow,WM_COMMAND,451,0);
        orderOk=app.columnOrder[kind]==original && orderOk;
        SendMessageW(orderWindow,WM_COMMAND,452,0);
        orderOk=app.columnOrder[kind][0]==original[1] && app.columnOrder[1-kind]==other && orderOk;
        SendDlgItemMessageW(orderWindow,450,LB_SETCURSEL,static_cast<WPARAM>(original.size()-1),0);
        for(size_t i=1;i<original.size();++i) SendMessageW(orderWindow,WM_COMMAND,451,0);
        const auto changed=app.columnOrder[kind]; Load(); orderOk=app.columnOrder[kind]==changed && changed.front()==original.back() && orderOk;
        ok=Export(orderWindow,L"build\\qa\\column-order-"+std::to_wstring(kind)+L".bmp") && ok;
        UpdateOverlays(); ok=Export(app.overlays[kind].window,L"build\\qa\\reordered-"+std::to_wstring(kind)+L".bmp") && ok;
        SendMessageW(orderWindow,WM_CLOSE,0,0); orderOk=!columnOrderWindow && IsWindowEnabled(app.overlaySettings) && orderOk;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0); app.columnOrder[kind]=original; Save(); UpdateOverlays();
    }
    ok=orderOk && ok; report<<"Independent column ordering, move boundaries, hidden-column positions and persistence: "<<(orderOk?"PASS":"FAIL")<<"\n";
    bool cornersOk=true;
    for(int kind=0;kind<OverlayCount;++kind) {
        auto& overlay=app.overlays[kind]; const auto original=overlay.place;
        overlay.place.growLeft=overlay.place.growUp=false; overlay.place.scale=100;
        SetOverlayOrigin(overlay,monitor.rcWork.left+300,monitor.rcWork.top+250); Fit(overlay);
        OpenOverlaySettings(kind);
        cornersOk=app.overlaySettings && SendDlgItemMessageW(app.overlaySettings,440,CB_GETCOUNT,0,0)==4 && cornersOk;
        for(int corner=0;corner<4;++corner) {
            RECT before{},grown{},shrunk{}; GetWindowRect(overlay.window,&before);
            SendDlgItemMessageW(app.overlaySettings,440,CB_SETCURSEL,corner,0);
            SendMessageW(app.overlaySettings,WM_COMMAND,MAKEWPARAM(440,CBN_SELCHANGE),0);
            GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
            overlay.place.scale=130; Fit(overlay); GetWindowRect(overlay.window,&grown);
            cornersOk=((corner&1)?grown.right==before.right:grown.left==before.left) && ((corner&2)?grown.bottom==before.bottom:grown.top==before.top) && cornersOk;
            cornersOk=grown.right-grown.left>before.right-before.left && grown.bottom-grown.top>before.bottom-before.top && cornersOk;
            overlay.place.scale=100; Fit(overlay); GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
        }
        ok=Export(app.overlaySettings,L"build\\qa\\fixed-corner-"+std::to_wstring(kind)+L".bmp") && ok;
        SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        const int anchorX=overlay.place.x,anchorY=overlay.place.y; Save();
        overlay.place.growLeft=overlay.place.growUp=false; overlay.place.x=overlay.place.y=0; Load();
        cornersOk=overlay.place.growLeft && overlay.place.growUp && overlay.place.x==anchorX && overlay.place.y==anchorY && cornersOk;
        overlay.place=original; Fit(overlay); Save();
    }
    ok=cornersOk && ok;
    report<<"Fixed corner: all four corners on every overlay, resize/shrink, no jump on selection and persisted anchors: "<<(cornersOk?"PASS":"FAIL")<<"\n";
    Frame liveFrame; Session liveSession;
    if(app.telemetry.Read(liveFrame,liveSession)) {
        app.preview=false; app.frame=liveFrame; app.session=liveSession; app.standings={}; raceControl.message.clear();
        OpenRaceControl(); UpdateRaceControl(true); ok=Export(raceControl.window,L"build\\qa\\race-control-live.bmp") && ok;
        int counters=0; for(const auto& d:liveSession.drivers) counters+=d.incidentCount>=0;
        report<<"Live Race Control: rows="<<raceControl.rows.size()<<"; incident counters="<<counters<<"; track meters="<<liveSession.trackMeters<<"; camera group="<<liveFrame.cameraGroup<<"; replay session="<<liveFrame.replaySession<<"\n";
    }
    // Exercise the actual window procedure with no tray, still using only the QA config.
    app.diagnostics=false; app.trayAvailable=false;
    ShowWindow(app.main,SW_SHOWNOACTIVATE);
    SendMessageW(app.main,WM_SIZE,SIZE_MINIMIZED,0);
    bool trayFallbackOk=IsWindowVisible(app.main)!=FALSE;
    SendMessageW(app.main,WM_CLOSE,0,0);
    trayFallbackOk=!IsWindow(app.main) && trayFallbackOk;
    report<<"Missing tray icon keeps minimize accessible and closes the app on exit: "<<(trayFallbackOk?"PASS":"FAIL")<<"\n";
    ok=trayFallbackOk && ok;
    if(IsWindow(app.main)) DestroyWindow(app.main);
    DeleteObject(app.font); DeleteObject(app.heading); DeleteObject(app.brush); return ok?0:2;
}
// Main-thread cost of one connected refresh with a 60-car multiclass race and every overlay visible.
int RunTickBenchmark() {
    // Move mode drops WS_EX_TRANSPARENT so UpdateWindow paints synchronously instead of deferring to the message loop.
    app.preview=false; app.edit=true; app.hidden=false;
    for(auto& o:app.overlays) { o.place.enabled=true; o.place.scale=100; }
    app.weatherDisplay.fill(true); app.fuelDisplay.fill(true); app.fuelRows.fill(true);
    app.showInterval=true; app.driverColumns[1][0]=app.driverColumns[1][1]=app.driverColumns[0][4]=true;
    Session session=DemoSession(); session.drivers.clear(); session.key="benchmark"; session.number=0; session.trackMeters=7004;
    const char* classes[]={"GTP","LMP2","GT3"}; const unsigned colors[]={0xffda59,0x33ceff,0xff5888};
    for(int i=0;i<60;++i) session.drivers.push_back({i,i%3+1,1500+i*37,colors[i%3],false,false,"Driver Number "+std::to_string(i),std::to_string(i+1),classes[i%3],"Car","car",0,"A 3.45"});
    session.player=7; app.session=session;
    Frame frame=DemoFrame(0); frame.player=7; frame.session=0; frame.state=4; frame.pit=false; frame.timeLeft=3600; frame.cars={};
    double progress[60];
    for(int i=0;i<60;++i) progress[i]=10+(60-i)*.02;
    LARGE_INTEGER frequency{},start{},end{}; QueryPerformanceFrequency(&frequency);
    FILETIME created{},exited{},kernelBefore{},userBefore{},kernelAfter{},userAfter{};
    constexpr int iterations=3000; double phases[3]{},paints[OverlayCount]{}; int changes[OverlayCount]{};
    auto step=[&](int t) {
        frame.time=1000+t*.1; frame.timeLeft=3600-t*.1;
        for(int i=0;i<60;++i) {
            progress[i]+=.1/(120+(i%3)*6+(i%7)*.3);
            auto& c=frame.cars[i]; c.completed=static_cast<int>(progress[i]); c.lap=c.completed+1; c.pct=progress[i]-c.completed;
            c.estTime=c.pct*120; c.best=119+i*.05; c.last=120+i*.05; c.surface=3; c.f2Time=(progress[0]-progress[i])*120+std::sin(t*.37+i)*.4;
            c.estTime+=std::sin(t*.29+i*1.7)*.3;
        }
        std::vector<int> order(60); for(int i=0;i<60;++i) order[i]=i;
        std::sort(order.begin(),order.end(),[&](int a,int b){return progress[a]>progress[b];});
        int classCount[4]{};
        for(int p=0;p<60;++p) { auto& c=frame.cars[order[p]]; c.position=p+1; c.classPosition=++classCount[order[p]%3+1]; }
        frame.lapCompleted=frame.cars[7].completed; frame.lapPct=frame.cars[7].pct; frame.fuel=60-frame.time*.02;
        std::array<std::vector<DrawCommand>,OverlayCount> previous; for(const auto& o:app.overlays) previous[o.kind]=o.scene;
        LARGE_INTEGER a{},b{},c{},d{};
        QueryPerformanceCounter(&a); app.frame=frame; ProcessFrame(true);
        QueryPerformanceCounter(&b); UpdateOverlays(); UpdateRaceControl();
        for(const auto& o:app.overlays) changes[o.kind]+=!(o.scene==previous[o.kind]);
        QueryPerformanceCounter(&c);
        for(auto& o:app.overlays) { LARGE_INTEGER before{},after{}; QueryPerformanceCounter(&before); UpdateWindow(o.window); QueryPerformanceCounter(&after); paints[o.kind]+=after.QuadPart-before.QuadPart; }
        QueryPerformanceCounter(&d); phases[0]+=b.QuadPart-a.QuadPart; phases[1]+=c.QuadPart-b.QuadPart; phases[2]+=d.QuadPart-c.QuadPart;
    };
    for(int t=0;t<100;++t) step(t);
    for(double& phase:phases) phase=0;
    for(double& paint:paints) paint=0;
    for(int& change:changes) change=0;
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelBefore,&userBefore); QueryPerformanceCounter(&start);
    for(int t=100;t<100+iterations;++t) step(t);
    QueryPerformanceCounter(&end); GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelAfter,&userAfter);
    auto ticks=[](FILETIME a,FILETIME b){ return (ULARGE_INTEGER{{b.dwLowDateTime,b.dwHighDateTime}}.QuadPart-ULARGE_INTEGER{{a.dwLowDateTime,a.dwHighDateTime}}.QuadPart)/10.0; };
    const double cpu=(ticks(kernelBefore,kernelAfter)+ticks(userBefore,userAfter))/iterations;
    const double wall=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/iterations;
    std::ofstream report("build/qa/benchmark.txt");
    report<<"60-car race, all overlays visible, "<<iterations<<" refreshes: "<<wall<<" us wall, "<<cpu<<" us CPU per refresh\n";
    report<<"  model "<<phases[0]*1e6/frequency.QuadPart/iterations<<" us, overlay update "<<phases[1]*1e6/frequency.QuadPart/iterations<<" us, paint "<<phases[2]*1e6/frequency.QuadPart/iterations<<" us\n";
    for(const auto& o:app.overlays) report<<"  overlay "<<o.kind<<": "<<changes[o.kind]<<" repaints, "<<paints[o.kind]*1e6/frequency.QuadPart/iterations<<" us per refresh"<<std::endl;
    return 0;
}
