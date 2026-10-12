// Render checks and regression diagnostics; excluded from the production build.
bool Export(HWND window,const std::wstring& path) {
    RECT r{}; GetClientRect(window,&r); const int w=r.right,h=r.bottom;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(window),dc=CreateCompatibleDC(screen); void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0); auto old=SelectObject(dc,bitmap);
    if(window==app.main || window==raceControl.window) {
        // Let the freshly shown window and its custom controls paint before capturing it.
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        for(int pass=0;pass<5;++pass) { MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&message); Sleep(20); }
        PrintWindow(window,dc,PW_RENDERFULLCONTENT);
    }
    else SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader); file.bfSize=file.bfOffBits+w*h*4;
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&file),sizeof(file)); out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader)); out.write(static_cast<char*>(bits),static_cast<std::streamsize>(w)*h*4);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,screen); return out.good();
}
// A DPI change rebuilds fonts and the current page in place.
bool MainDpiDiagnostics(std::ostream& report) {
    bool ok=true;
    OpenOverlay(RelativeKind);
    for(UINT target:{96u,192u,GetDpiForWindow(app.main)}) {
        RECT bounds{}; GetWindowRect(app.main,&bounds);
        SendMessageW(app.main,WM_DPICHANGED,MAKEWPARAM(target,target),reinterpret_cast<LPARAM>(&bounds));
        const bool same=ui.page==Page::Overlay && ui.overlayKind==RelativeKind && ui.host.Content()!=nullptr && ui.text!=nullptr;
        report<<"DPI "<<target<<": page kept="<<same<<"\n"; ok=same && ok;
    }
    ShowPage(Page::Overlays);
    return ok;
}

int RunDiagnostics() {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    std::ofstream report("build/qa/controls.txt");
    // The page as built at startup, before anything refreshes it: one row per overlay, each with its own title.
    bool startupOk=false;
    if(const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr)) {
        std::vector<std::wstring> titles; for(const auto& row:reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA))->Rows()) if(!row.header) titles.push_back(row.title);
        startupOk=titles.size()>=OverlayCount; for(int kind=0;kind<OverlayCount && startupOk;++kind) startupOk=titles[kind]==Titles[kind];
    }
    report<<"Overlays page at startup lists every overlay by its own name: "<<(startupOk?"PASS":"FAIL")<<"\n";
    app.preview=true; app.edit=false; PreviewData(0); UpdateStatus(); RefreshPage();
    SetWindowPos(app.main,nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(app.main,SW_SHOWNOACTIVATE); UpdateWindow(app.main);
    bool ok=Export(app.main,L"build\\qa\\main-overlays.bmp");
    {
        ShowPage(Page::Settings); UpdateWindow(app.main); ok=Export(app.main,L"build\\qa\\main-settings.bmp") && ok;
        bool pagesOk=ui.page==Page::Settings && ui.host.Content()!=nullptr;
        for(int kind=0;kind<OverlayCount;++kind) {
            OpenOverlay(kind); UpdateWindow(app.main);
            pagesOk=ui.page==Page::Overlay && ui.overlayKind==kind && IsWindowVisible(ui.back) && pagesOk;
            ok=Export(app.main,L"build\\qa\\main-"+std::to_wstring(kind)+L".bmp") && ok;
        }
        ShowPage(Page::Overlays); UpdateWindow(app.main); pagesOk=!IsWindowVisible(ui.back) && pagesOk;
        report<<"Main window pages, banner and navigation: "<<(pagesOk && ui.page==Page::Overlays?"PASS":"FAIL")<<"\n"; ok=pagesOk && ok;
    }
    const bool viewportOk=MainDpiDiagnostics(report);
    report<<"Main window DPI changes keep the current page: "<<(viewportOk?"PASS":"FAIL")<<"\n";
    ok=viewportOk && ok;
    {
        // Save() writes only changed keys; after Load() or without a file it writes all again.
        Save(); const unsigned before=app.settingsWrites; Save();
        bool saveOk=app.settingsWrites==before;
        app.showTitleBar=!app.showTitleBar; Save(); saveOk=app.settingsWrites==before+1 && saveOk;
        const bool title=app.showTitleBar; Load(); saveOk=app.showTitleBar==title && saveOk;
        app.showTitleBar=!app.showTitleBar; Save(); Load(); saveOk=app.showTitleBar!=title && saveOk;
        const unsigned full=app.settingsWrites; Save(); saveOk=app.settingsWrites-full>100 && saveOk;
        report<<"Settings save only changed keys and survive a reload: "<<(saveOk?"PASS":"FAIL")<<"\n"; ok=saveOk && ok;
    }
    {
        const bool savedFilter=app.excludeYellowFuelLaps;
        WritePrivateProfileStringW(L"Fuel Calculator",L"ExcludeYellowLaps",nullptr,app.config.c_str());
        Load();
        bool filterOk=!app.excludeYellowFuelLaps;
        for(bool enabled:{true,false}) {
            app.excludeYellowFuelLaps=enabled; SettingChanged();
            Load(); filterOk=(app.excludeYellowFuelLaps==enabled) && filterOk;
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
            Row test; test.driver.name="Daniel Fischer";
            for(int format=0;format<3;++format) {
                app.nameFormat[kind]=format; SettingChanged();
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
            app.nameFormat[kind]=1; SettingChanged();
            int before=BaseHeight(kind);
            app.tableHeaders[kind]=true; SettingChanged(); displayOk=BaseHeight(kind)==before+32 && displayOk;
            app.headerMetrics[kind]={OverlayNameMetric,TrackTempMetric,LocalTimeMetric}; SettingChanged();
            Load(); displayOk=app.headerMetrics[kind]==Slots{OverlayNameMetric,TrackTempMetric,LocalTimeMetric} && FooterValue(OverlayNameMetric,kind)==Titles[kind] && app.footers[kind].metrics==savedFooters[kind].metrics && displayOk;
            OpenOverlay(kind); Export(app.main,L"build\\qa\\integrated-settings-"+std::to_wstring(kind)+L".bmp"); Export(app.overlays[kind].window,L"build\\qa\\configured-header-status-"+std::to_wstring(kind)+L".bmp");
            const int expandedWidth=OverlayWidth(kind);
            app.columnHeaders[kind]=false; SettingChanged(); displayOk=BaseHeight(kind)==before+5 && OverlayWidth(kind)<expandedWidth && displayOk;
            Export(app.overlays[kind].window,L"build\\qa\\compact-status-"+std::to_wstring(kind)+L".bmp");
            int width=OverlayWidth(kind);
            SetColumnVisible(kind,DriverColumn,false); displayOk=!ColumnVisible(kind,DriverColumn) && OverlayWidth(kind)==width-ColumnWidth(DriverColumn,kind) && displayOk;
            width=OverlayWidth(kind);
            SetColumnVisible(kind,StatusColumn,false); displayOk=!ColumnVisible(kind,StatusColumn) && OverlayWidth(kind)==width-ColumnWidth(StatusColumn,kind) && displayOk;
            app.footers[kind].enabled=false; SettingChanged();
            displayOk=BaseHeight(kind)==before+5-28 && displayOk;
            Load(); displayOk=!app.showDriver[kind] && !app.showStatus[kind] && !app.columnHeaders[kind] && app.tableHeaders[kind] && !app.footers[kind].enabled && app.nameFormat[kind]==1 && displayOk;
        }
        app.showDriver=savedDriver; app.showStatus=savedStatus; app.tableHeaders=savedHeaders; app.columnHeaders=savedHeadings; app.nameFormat=savedFormats; app.footers=savedFooters; app.headerMetrics=savedHeaderMetrics; app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Table page settings, name formats, single status priority, configurable headers and independent footer persistence: "<<(displayOk?"PASS":"FAIL")<<"\n"; ok=displayOk && ok;
    }
    {
        const auto savedFlags=app.driverFlags; const auto savedFrame=app.frame; bool flagsOk=true;
        app.frame.cars[0].sessionFlags=0x10000u; app.frame.cars[1].sessionFlags=0x80000u; app.frame.cars[2].sessionFlags=0x100000u; app.frame.cars[3].sessionFlags=0x190000u;
        for(int kind=0;kind<2;++kind) {
            app.driverFlags[kind]={true,true,true}; SettingChanged(); OpenOverlay(kind);
            Export(app.main,L"build\\qa\\driver-flags-settings-"+std::to_wstring(kind)+L".bmp"); Export(app.overlays[kind].window,L"build\\qa\\driver-flags-overlay-"+std::to_wstring(kind)+L".bmp");
            app.driverFlags[kind]={false,false,false}; SettingChanged();
            Load(); flagsOk=ColumnVisible(kind,StatusColumn) && std::none_of(app.driverFlags[kind].begin(),app.driverFlags[kind].end(),[](bool v){return v;}) && flagsOk;
        }
        app.driverFlags=savedFlags; app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Driver flag display settings, independent overlays and persisted disabled flags with PIT/OUT status retained: "<<(flagsOk?"PASS":"FAIL")<<"\n"; ok=flagsOk && ok;
    }
    {
        const auto savedShort=app.driverShortView; const auto savedColumns=app.driverColumns;
        bool shortOk=true;
        for(int kind=0;kind<2;++kind) {
            app.driverShortView[kind]={false,false}; app.driverColumns[kind][0]=app.driverColumns[kind][1]=true;
            app.driverShortView[kind]={true,true}; SettingChanged();
            Load(); shortOk=app.driverShortView[kind][0] && app.driverShortView[kind][1] && shortOk;
            Row test; test.driver.license="A 3.45"; test.driver.rating=2400;
            shortOk=DriverValue(test,0,kind)==L"3,45" && DriverValue(test,1,kind)==L"2,4K" && DriverValue(test,0)==L"A 3.45" && shortOk;
            test.driver.license="Pro 4.99"; test.driver.rating=3450;
            shortOk=DriverValue(test,0,kind)==L"4,99" && DriverValue(test,1,kind)==L"3,5K" && shortOk;
            test.driver.license=""; test.driver.rating=0;
            shortOk=DriverValue(test,0,kind)==L"—" && DriverValue(test,1,kind)==L"—" && shortOk;
            OpenOverlay(kind); Export(app.main,L"build\\qa\\short-view-settings-"+std::to_wstring(kind)+L".bmp");
            UpdateOverlays(); Export(app.overlays[kind].window,L"build\\qa\\short-view-overlay-"+std::to_wstring(kind)+L".bmp");
            // Hiding the column keeps its short-view choice.
            for(int i=0;i<2;++i) { SetColumnVisible(kind,LicenseColumn+i,false); shortOk=!ColumnVisible(kind,LicenseColumn+i) && app.driverShortView[kind][i] && shortOk; }
        }
        app.driverShortView=savedShort; app.driverColumns=savedColumns; Save(); UpdateOverlays();
        report<<"Independent short license/iRating views, formatting, missing values, kept choice when hidden and persistence: "<<(shortOk?"PASS":"FAIL")<<"\n"; ok=shortOk && ok;
    }
    {
        const Frame originalFrame=app.frame; const Session originalSession=app.session;
        bool footerOk=true;
        for(int kind=0;kind<2;++kind) {
            app.footers[kind].metrics={TrackTempMetric,CurrentLapMetric,TimeRemainingMetric}; SettingChanged();
            Save(); Load(); footerOk=app.footers[kind].metrics==Slots{TrackTempMetric,CurrentLapMetric,TimeRemainingMetric} && footerOk;
            footerOk=Export(app.overlays[kind].window,L"build\\qa\\footer-custom-"+std::to_wstring(kind)+L".bmp") && footerOk;
            const int before=HeightWithTitle(kind);
            app.footers[kind].enabled=false; SettingChanged();
            Load(); footerOk=!app.footers[kind].enabled && HeightWithTitle(kind)==before-28 && footerOk;
            footerOk=Export(app.overlays[kind].window,L"build\\qa\\footer-hidden-"+std::to_wstring(kind)+L".bmp") && footerOk;
            app.footers[kind]={}; SettingChanged();
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
        report<<"Independent footers, choices, persistence, resize, watched car, missing values and signed temperatures: "<<(footerOk?"PASS":"FAIL")<<"\n";
        ok=footerOk && ok;
    }
    {
        const auto saved=app.frame; constexpr double pi=3.141592653589793;
        bool windOk=OverlayWidth(4)==240 && BaseHeight(4)==WeatherHeight()-(app.showTitleBar?0:32);
        for(int direction=0;direction<5;++direction) {
            const double angle=direction==4?25*pi/180:direction*pi/2;
            app.frame.yawNorth=.4; app.frame.windDirection=.4+angle; app.frame.windSpeed=5;
            UpdateOverlays(); windOk=std::abs(RelativeWindAngle(app.frame)-angle)<.001 && windOk;
            windOk=Export(app.overlays[4].window,L"build\\qa\\wind-"+std::to_wstring(direction)+L".bmp") && windOk;
        }
        app.frame.onTrack=false; UpdateOverlays(); windOk=Export(app.overlays[4].window,L"build\\qa\\wind-unavailable.bmp") && windOk;
        app.frame=saved; UpdateOverlays();
        report<<"Wind overlay cardinal and arbitrary source angles, speed, missing heading and rendering: "<<(windOk?"PASS":"FAIL")<<"\n"; ok=windOk && ok;
    }
    {
        bool weatherOk=true; OpenOverlay(WeatherKind);
        weatherOk=Export(app.main,L"build\\qa\\weather-settings.bmp") && weatherOk;
        const auto originalOrder=app.weatherOrder;
        for(int step=0;step<4;++step) weatherOk=MoveInOrder(app.weatherOrder,originalOrder.back(),-1) && weatherOk;
        weatherOk=!MoveInOrder(app.weatherOrder,originalOrder.back(),-1) && weatherOk;
        Save(); Load(); weatherOk=app.weatherOrder.front()==originalOrder.back() && weatherOk;
        weatherOk=Export(app.overlays[4].window,L"build\\qa\\weather-reordered.bmp") && weatherOk;
        app.weatherOrder=originalOrder; SettingChanged();
        for(int element=0;element<=WindDirectionOption;++element) {
            const int before=HeightWithTitle(4);
            app.weatherDisplay[element]=false; SettingChanged();
            Load(); weatherOk=!app.weatherDisplay[element] && HeightWithTitle(4)==before-(element<WeatherCardCount?WeatherElements[element].height+8:0) && weatherOk;
        }
        weatherOk=!IsWindowVisible(app.overlays[4].window) && weatherOk;
        app.weatherDisplay.fill(true); SettingChanged();
        app.weatherDisplay={false,false,true,false,false,true}; Save(); UpdateOverlays();
        weatherOk=Export(app.overlays[4].window,L"build\\qa\\weather-wind-only.bmp") && weatherOk;
        app.weatherDisplay.fill(true); Save(); UpdateOverlays();
        report<<"Weather card settings, card ordering, direction switch, persistence, compact layout and all-hidden visibility: "<<(weatherOk?"PASS":"FAIL")<<"\n"; ok=weatherOk && ok;
    }
    {
        const auto rosterSession=app.session; const auto rosterFrame=app.frame; const auto rosterOptions=app.standings;
        app.session.type="Qualify"; app.session.results={}; for(auto& car:app.frame.cars) car=Car{};
        app.frame.camera=-1; app.frame.onTrack=true; app.standings.ownClass=false; app.standings.leaders=MaxCars;
        UpdateOverlays(); bool rosterOk=!Rows(1).empty() && Rows(0).size()==1 && Rows(0)[0].gap==0 && Rows(0)[0].car.position==0;
        Export(app.overlays[0].window,L"build\\qa\\relative-missing-progress.bmp"); Export(app.overlays[1].window,L"build\\qa\\qualifying-untimed.bmp");
        app.standings.fallbackByRating=true; SettingChanged();
        Load(); rosterOk=app.standings.fallbackByRating && rosterOk;
        app.session=rosterSession; app.frame=rosterFrame; app.standings=rosterOptions; Save(); UpdateOverlays();
        report<<"Untimed qualifying roster, relative missing progress and sort preference persistence: "<<(rosterOk?"PASS":"FAIL")<<"\n"; ok=rosterOk && ok;
        const bool savedGained=app.standingsGained; const auto gainedGrid=app.session.gridPositions;
        app.standingsGained=false; Save(); const int gainedWidth=OverlayWidth(1);
        SetColumnVisible(StandingsKind,GainedColumn,true);
        Load(); bool gainedOk=app.standingsGained && ColumnVisible(1,14) && !ColumnVisible(0,14) && OverlayWidth(1)==gainedWidth+ColumnWidth(14,1);
        for(const auto& driver:app.session.drivers) app.session.gridPositions[driver.index]=driver.index+2;
        app.session.gridPositions[0]=3; app.session.gridPositions[1]=2;
        app.frame.cars[2].towing=true; UpdateOverlays();
        Export(app.overlays[1].window,L"build\\qa\\standings-gained-tow.bmp");
        app.frame.cars[2].towing=false;
        app.standingsGained=savedGained; app.session.gridPositions=gainedGrid; Save();
        report<<"Standings gained column settings: "<<(gainedOk?"PASS":"FAIL")<<"\n"; ok=gainedOk && ok;
        const auto savedSession=app.session; const auto savedFrame=app.frame; const auto savedOrder=app.columnOrder[1]; const bool savedLaps=app.standingsLaps;
        app.standingsLaps=false; Save(); UpdateOverlays(); const int before=OverlayWidth(1);
        SetColumnVisible(StandingsKind,LapsColumn,true);
        Load(); bool lapsOk=app.standingsLaps && OverlayWidth(1)==before+ColumnWidth(13,1) && ColumnVisible(1,13) && !ColumnVisible(0,13);
        Row row; row.driver.index=0; row.car.completed=5; row.car.lap=7; lapsOk=CompletedLaps(row)==L"5" && lapsOk;
        row.car.completed=-1; row.car.lap=1; lapsOk=CompletedLaps(row)==L"0" && lapsOk;
        row.car.lap=-1; app.session.number=app.frame.session; app.session.results[0].completed=9; lapsOk=CompletedLaps(row)==L"9" && lapsOk;
        app.session.number=app.frame.session+1; lapsOk=CompletedLaps(row)==L"—" && lapsOk;
        app.session=savedSession;
        app.frame.cars[0].completed=6; app.frame.cars[0].lap=7;
        UpdateOverlays(); lapsOk=Export(app.overlays[1].window,L"build\\qa\\standings-laps.bmp") && lapsOk;
        OpenOverlay(StandingsKind); lapsOk=Export(app.main,L"build\\qa\\standings-laps-settings.bmp") && lapsOk;
        const int index=static_cast<int>(std::find(app.columnOrder[1].begin(),app.columnOrder[1].end(),LapsColumn)-app.columnOrder[1].begin());
        for(int step=0;step<index;++step) MoveInOrder(app.columnOrder[1],LapsColumn,-1);
        Load(); lapsOk=app.columnOrder[1].front()==13 && app.standingsLaps && lapsOk;
        lapsOk=Export(app.overlays[1].window,L"build\\qa\\standings-laps-first.bmp") && lapsOk;
        SetColumnVisible(StandingsKind,LapsColumn,false);
        lapsOk=!app.standingsLaps && OverlayWidth(1)==before && lapsOk;
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
        OpenOverlay(kind);
        ok=ui.page==Page::Overlay && ui.overlayKind==kind && ok;
        ok=Export(app.main,L"build\\qa\\settings-"+std::to_wstring(kind)+L".bmp")&&ok;
        if(kind==RelativeKind) {
            app.rows=RelativeRowCount(6); SettingChanged();
            ok=app.rows==7 && ok;
        } else if(kind==StandingsKind) {
            const int originalWidth=OverlayWidth(1); app.showInterval=false; Fit(app.overlays[1]);
            SetColumnVisible(StandingsKind,IntervalColumn,true);
            ok=app.showInterval && OverlayWidth(1)>=originalWidth && ok;
            app.standings.liveGaps=true; SettingChanged();
            ok=app.standings.liveGaps && ok; Save(); Load(); ok=app.standings.liveGaps && ok;
            app.standings.liveGaps=false; SettingChanged();
            ok=!app.standings.liveGaps && ok;
            ok=Export(app.overlays[1].window,L"build\\qa\\standings-interval.bmp") && ok;
            app.standings.leaders=2; app.standings.ahead=1; app.standings.behind=1; app.standings.separateClasses=false; SettingChanged();
            const auto groups=SelectStandings(app.session,app.frame,app.standings);
            ok=(!app.standings.separateClasses && groups.size()==1 && groups[0].rows.size()==5 && app.rows==7)&&ok;
            ok=Export(app.overlays[1].window,L"build\\qa\\standings-combined.bmp")&&ok;
            app.standings.ownClass=true; SettingChanged();
            ok=app.standings.ownClass && Rows(0).size()<=7 && ok;
        } else if(kind==FuelKind) {
            app.fuelRows[1]=false; SettingChanged();
            const int newElements=FuelElements[KeyNumbersElement].height+FuelElements[PitTimelineElement].height;
            ok=!app.fuelRows[1] && BaseHeight(2)==(app.showTitleBar?524:492)+newElements && ok;
            for(int i=0;i<FuelElementCount;++i) {
                const int before=HeightWithTitle(2);
                app.fuelDisplay[i]=false; SettingChanged();
                ok=!app.fuelDisplay[i] && HeightWithTitle(2)==before-FuelElements[i].height && ok;
            }
            ok=HeightWithTitle(2)==213 && OverlayWidth(2)==365 && ok;
            Save(); Load();
            ok=std::none_of(app.fuelDisplay.begin(),app.fuelDisplay.end(),[](bool value){return value;}) && ok;
            ok=Export(app.overlays[2].window,L"build\\qa\\fuel-compact.bmp") && ok;
            app.fuelDisplay.fill(true); SettingChanged();
            const auto originalOrder=app.fuelOrder;
            const int table=static_cast<int>(std::find(app.fuelOrder.begin(),app.fuelOrder.end(),ConsumptionTableElement)-app.fuelOrder.begin());
            ok=MoveInOrder(app.fuelOrder,ConsumptionTableElement,-1) && ok;
            ok=table>0 && app.fuelOrder[table-1]==ConsumptionTableElement && ok;
            Save(); Load(); ok=app.fuelOrder[table-1]==ConsumptionTableElement && ok;
            UpdateOverlays();
            ok=Export(app.overlays[2].window,L"build\\qa\\fuel-reordered.bmp") && ok;
            app.fuelOrder=originalOrder; SettingChanged();
            const auto savedFrame=app.frame; const double savedDistance=app.fuelPlan.lapsToFinish;
            app.frame.lapCompleted=7; app.frame.lapPct=.2; app.fuelPlan.lapsToFinish=12.02;
            ok=FuelFinishLap()==20 && ok;
            app.frame.lapPct=.99; ok=FuelFinishLap()==21 && ok;
            app.fuelPlan.lapsToFinish=0; ok=FuelFinishLap()==8 && ok;
            app.fuelPlan.lapsToFinish=-1; ok=FuelFinishLap()==-1 && ok;
            app.frame=savedFrame; app.fuelPlan.lapsToFinish=savedDistance;
        }
        if(IsTable(kind)) {
            SetColumnVisible(kind,PitStopColumn,true);
            ok=app.driverColumns[kind][5] && DriverValue(Rows(kind).front(),5).find(L" s")!=std::wstring::npos && ok;
            ok=Export(app.overlays[kind].window,L"build\\qa\\pit-stop-"+std::to_wstring(kind)+L".bmp") && ok;
        }
    }
    ShowPage(Page::Overlays);
    OpenRaceControl(); UpdateRaceControl(true);
    ok=raceControl.window && !raceControl.rows.empty() && raceControl.events.size()==6 && ok;
    auto shown=[](int field){ return std::find(raceControl.columns.begin(),raceControl.columns.end(),field)!=raceControl.columns.end(); };
    ok=shown(5) && shown(8) && shown(RaceIncidentsField) && ok;
    for(const auto& row:raceControl.rows) {
        if(row.driver.index==1) ok=RaceCarStatus(row).text==L"Out" && ok;
        if(row.driver.index==2) ok=RaceCarStatus(row).text==L"Pit" && ok;
        if(row.driver.index==3) ok=RaceCarStatus(row).text.empty() && ok;
        if(row.driver.index==5) ok=RaceCarInactive(row) && RaceCarStatus(row).text==L"Inactive" && ok;
    }
    int chase=-1; for(int i=0;i<static_cast<int>(raceControl.cameras.size());++i) if(raceControl.cameras[i].name=="Far Chase") chase=i;
    SetRaceCamera(chase); ok=chase>=0 && RaceCameraGroup()==11 && ok;
    ok=Export(raceControl.window,L"build\\qa\\race-control.bmp") && ok;
    if(raceControl.events.size()>0) {
        ReplayRaceEvent(0,5); ok=!raceControl.pendingNumber.empty() && raceControl.pendingGroup==11 && ok;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        SetRaceCamera(2);
        // Replay from 10 s, then a double-click on the selected event.
        SetRaceReplayMode(2); ListView_SetItemState(raceControl.feed,0,LVIS_SELECTED,LVIS_SELECTED);
        NMITEMACTIVATE open{}; open.hdr.hwndFrom=raceControl.feed; open.hdr.code=NM_DBLCLK; open.iItem=0;
        SendMessageW(raceControl.window,WM_NOTIFY,RaceFeedId,reinterpret_cast<LPARAM>(&open));
        ok=SelectedEventIndex()==0 && raceControl.pendingNumber==raceControl.events[0].number && raceControl.pendingGroup==12 && ok;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        CloseRaceControl(); OpenRaceControl(); ok=RaceCameraGroup()==12 && raceControl.replayMode==2 && raceControl.replay.Selected()==2 && ok;
        SetRaceReplayMode(1);
        SetRaceCamera(chase);
    }
    SetRaceEventFilter(1);
    ok=raceControl.events.size()==3 && std::all_of(raceControl.events.begin(),raceControl.events.end(),[](const RaceEvent& e){return EventCategory(e.type)==1;}) && ok;
    SetRaceEventFilter(3); ok=raceControl.events.size()==1 && raceControl.events.front().type==EventType::Overtake && ok;
    SetRaceEventFilter(0);
    SetRaceOnTop(true); ok=(GetWindowLongPtrW(raceControl.window,GWL_EXSTYLE)&WS_EX_TOPMOST) && ok;
    SetRaceOnTop(false); ok=!(GetWindowLongPtrW(raceControl.window,GWL_EXSTYLE)&WS_EX_TOPMOST) && ok;
    LoadRaceFields(); ok=!raceControl.onTop && ok;
    {
        // Watching live from outside the car is not a replay; behind live or another session is.
        Frame f; f.replay=true; f.session=f.replaySession=2; f.time=100; f.replayTime=99.5; ok=!ReviewingReplay(f) && ok;
        f.replayTime=90; ok=ReviewingReplay(f) && ok; f.replaySession=1; f.replayTime=99.5; ok=ReviewingReplay(f) && ok;
        // A paused replay may clear IsReplayPlaying; in the car is never a replay.
        f.replay=false; f.replaySession=2; f.replayTime=90; ok=ReviewingReplay(f) && ok;
        f.onTrack=true; f.replayTime=-1; ok=!ReviewingReplay(f) && ok;
    }
    // A DPI change swaps fonts; the segmented controls must follow instead of keeping a deleted one.
    RaceControlFonts(); ApplyRaceControlFonts();
    for(HWND segmented:{raceControl.filter.Handle(),raceControl.replay.Handle()}) ok=reinterpret_cast<HFONT>(SendMessageW(segmented,WM_GETFONT,0,0))==raceControl.font && ok;
    SendMessageW(raceControl.window,WM_CLOSE,0,0); ok=!IsWindowVisible(raceControl.window) && ok;
    OpenRaceControl(); ok=IsWindowVisible(raceControl.window) && ok;
    NMITEMACTIVATE click{}; click.hdr.hwndFrom=raceControl.standings; click.hdr.code=NM_DBLCLK; click.iItem=0;
    ListView_SetItemState(raceControl.standings,0,LVIS_SELECTED,LVIS_SELECTED);
    // 5 s (default): replay that car from the moment on screen; Live: camera only.
    SendMessageW(raceControl.window,WM_NOTIFY,RaceStandingsId,reinterpret_cast<LPARAM>(&click));
    ok=raceControl.pendingNumber==raceControl.rows[0].driver.number && raceControl.message.find(L"replay action simulated")!=std::wstring::npos && ok;
    KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
    SetRaceReplayMode(0); SendMessageW(raceControl.window,WM_NOTIFY,RaceStandingsId,reinterpret_cast<LPARAM>(&click));
    ok=raceControl.pendingNumber.empty() && raceControl.message.find(L"camera action simulated")!=std::wstring::npos && ok;
    SetRaceReplayMode(1);
    report<<"Race Control banner, standings status, event filters, replay bar, keep on top and close/reopen: "<<(ok?"PASS":"FAIL")<<"\n";
    {
        bool rcOk=true; const auto savedFields=raceControl.fields; const auto savedEvents=raceControl.eventFields; const auto savedOptions=app.standings; const auto savedDrivers=app.session.drivers;
        app.standings.ownClass=false; UpdateRaceControl(true);
        DWORD_PTR headerData=0; rcOk=GetWindowSubclass(ListView_GetHeader(raceControl.standings),RaceHeaderProc,1,&headerData) && rcOk;
        ShowRaceControlPage(); rcOk=ui.page==Page::RaceControl && rcOk;
        rcOk=SetRaceField(false,6,false) && SetRaceField(true,2,false) && !SetRaceField(true,0,true) && rcOk;
        rcOk=std::find(raceControl.columns.begin(),raceControl.columns.end(),6)==raceControl.columns.end() && raceControl.feedColumns.front()==3 && rcOk;
        {
            auto only=raceControl.fields; raceControl.fields.fill(false); raceControl.fields[2]=true;
            rcOk=!SetRaceField(false,2,false) && raceControl.fields[2] && rcOk; raceControl.fields=only; SaveRaceFields();
        }
        RefreshPage(); Export(app.main,L"build\\qa\\main-race-control.bmp"); ShowPage(Page::Overlays);
        LoadRaceFields(); rcOk=!raceControl.fields[6] && !raceControl.eventFields[2] && rcOk;
        SetRaceField(true,2,true);
        // Numeric iRating sorting, missing values last in either direction.
        for(auto& driver:app.session.drivers) driver.rating=driver.index==0?0:(driver.index==1?999:10000+driver.index);
        SetRaceSort(false,10,0); rcOk=raceControl.rows.front().driver.index==1 && raceControl.rows.back().driver.index==0 && rcOk;
        SetRaceSort(false,10,1); rcOk=raceControl.rows.front().driver.rating>=10000 && raceControl.rows.back().driver.index==0 && rcOk;
        // Actual column click toggles event order.
        NMLISTVIEW sort{}; sort.hdr.hwndFrom=raceControl.feed; sort.hdr.code=LVN_COLUMNCLICK; sort.iSubItem=static_cast<int>(std::find(raceControl.feedColumns.begin(),raceControl.feedColumns.end(),2)-raceControl.feedColumns.begin());
        SendMessageW(raceControl.window,WM_NOTIFY,RaceFeedId,reinterpret_cast<LPARAM>(&sort)); rcOk=raceControl.events.front().time<raceControl.events.back().time && rcOk;
        SendMessageW(raceControl.window,WM_NOTIFY,RaceFeedId,reinterpret_cast<LPARAM>(&sort)); rcOk=raceControl.events.front().time>raceControl.events.back().time && rcOk;
        // Car filter from a standings row and cleared from its pill.
        int carRow=-1; for(int i=0;i<static_cast<int>(raceControl.rows.size());++i) if(raceControl.rows[i].driver.index==2) carRow=i;
        ListView_SetItemState(raceControl.standings,carRow,LVIS_SELECTED,LVIS_SELECTED);
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RaceCarEventsId,BN_CLICKED),0);
        rcOk=raceControl.car==2 && raceControl.events.size()==1 && raceControl.events.front().car==2 && IsWindowVisible(RaceItem(RaceCarPillId)) && rcOk;
        ReplayRaceEvent(0,5); rcOk=raceControl.pendingNumber==raceControl.events.front().number && rcOk;
        KillTimer(raceControl.window,1); raceControl.pendingNumber.clear();
        SetRaceEventFilter(3); rcOk=raceControl.events.empty() && rcOk;
        Export(raceControl.window,L"build\\qa\\race-control-filtered.bmp");
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RaceCarPillId,BN_CLICKED),0); rcOk=raceControl.car<0 && !IsWindowVisible(RaceItem(RaceCarPillId)) && rcOk;
        raceControl.fields=savedFields; raceControl.eventFields=savedEvents; app.standings=savedOptions; app.session.drivers=savedDrivers;
        raceControl.sortColumn=-1; raceControl.eventSortColumn=2; raceControl.eventSortDescending=true; SaveRaceFields();
        SetRaceEventFilter(0);
        report<<"Race Control field visibility, numeric sorting, missing values, event sorting, car/type filters and replay for the filtered event: "<<(rcOk?"PASS":"FAIL")<<"\n"; ok=rcOk && ok;
    }
    // Kept apart from ok, which later checks reuse for their own line; folded in at the end.
    bool listOk=true;
    {
        // The first click into a scrolled, unfocused list must hit the row under the mouse.
        RowList list; listOk=list.Create(app.instance,app.main,9990,ui.heading,ui.text);
        std::vector<RowList::Row> rows(30); for(size_t i=0;i<rows.size();++i) rows[i].title=L"Row "+std::to_wstring(i);
        list.SetRows(std::move(rows)); SetWindowPos(list.Handle(),HWND_TOP,0,0,400,240,SWP_NOACTIVATE);
        SetFocus(app.main); SendMessageW(list.Handle(),WM_VSCROLL,SB_BOTTOM,0);
        RECT client{}; GetClientRect(list.Handle(),&client); const LPARAM at=MAKELPARAM(20,client.bottom-20);
        SendMessageW(list.Handle(),WM_LBUTTONDOWN,MK_LBUTTON,at); SendMessageW(list.Handle(),WM_LBUTTONUP,0,at);
        listOk=GetFocus()==list.Handle() && list.NotifiedRow()==29 && listOk;
        DestroyWindow(list.Handle());
        report<<"First click into a scrolled list hits the row under the mouse: "<<(listOk?"PASS":"FAIL")<<"\n";
    }
    // Version tags compare numerically, a leading v is ignored, a suffix such as -beta sorts first; diagnostics never go online.
    bool updateOk=UpdateChecker::CurrentVersion()==Wide(RACEMATE_VERSION) && UpdateChecker::IsNewerVersion(L"v0.1.1",L"0.1.0") && UpdateChecker::IsNewerVersion(L"0.10.0",L"0.9.0")
        && !UpdateChecker::IsNewerVersion(L"v0.1.0",L"0.1.0") && !UpdateChecker::IsNewerVersion(L"0.0.9",L"0.1.0") && UpdateChecker::IsNewerVersion(L"0.1.0",L"0.1.0-beta");
    StartUpdateCheck(false); updateOk=!updates.check.Running() && updates.phase==Updates::Phase::Idle && updateOk;
    {
        // The Settings version row carries the whole update flow.
        updates.phase=Updates::Phase::Available; updates.release.versionDisplay=L"0.2.0";
        updates.release.assetDownloadUrl=L"https://example.invalid/RaceMate-0.2.0-windows-x64.exe"; updates.release.releasePageUrl=L"https://example.invalid/release";
        const auto available=UpdateVersionRow(); updateOk=available.button==L"Install update" && available.pill==L"Update available" && available.iconButtons.size()==1 && updateOk;
        updates.release.assetDownloadUrl.clear(); updateOk=UpdateVersionRow().button==L"Open release" && updateOk;
        updates.phase=Updates::Phase::Downloading; updateOk=!UpdateVersionRow().buttonEnabled && updateOk;
        updates.phase=Updates::Phase::Failed; updates.message=L"Offline"; updateOk=UpdateVersionRow().pill==L"Not updated" && UpdateVersionRow().detail==L"Offline" && updateOk;
        updates.phase=Updates::Phase::Idle; updates.release={}; updates.message.clear();
    }
    report<<"Update version comparison, version row states, no network in diagnostics: "<<(updateOk?"PASS":"FAIL")<<"\n";
    SetStartWithWindows(true); SetStartMinimized(true);
    Load();
    ok=app.driverColumns[0][5] && app.driverColumns[1][5] && app.showInterval && app.rows==7 && app.standings.leaders==2 && app.standings.ahead==1 && app.standings.behind==1 && app.standings.ownClass && !app.standings.separateClasses && !app.fuelRows[1] && app.startWithWindows && app.startMinimized && ok;
    ShowWindow(app.main,StartupShowCommand(true,SW_SHOW)); UpdateOverlays();
    ok=!IsWindowVisible(app.main) && IsWindowVisible(app.overlays[0].window) && ok;
    ShowWindow(app.main,StartupShowCommand(false,SW_SHOW)); ok=IsWindowVisible(app.main) && ok;
    const auto startup=StartupCommand(); ok=(!startup.empty() && startup.front()==L'"' && startup.back()==L'"') && ok;
    SendMessageW(app.main,WM_CLOSE,0,0); ok=!IsWindowVisible(app.main) && IsWindow(app.main) && ok;
    report<<"Overlay pages, independent options, class view switch, fuel rows, close-to-tray and persistence: "<<(ok?"PASS":"FAIL")<<"\n";
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
        orderOk=!MoveInOrder(app.columnOrder[kind],original.front(),-1) && app.columnOrder[kind]==original && orderOk;
        orderOk=MoveInOrder(app.columnOrder[kind],original.front(),1) && orderOk;
        orderOk=app.columnOrder[kind][0]==original[1] && app.columnOrder[1-kind]==other && orderOk;
        app.columnOrder[kind]=original;
        for(size_t i=1;i<original.size();++i) MoveInOrder(app.columnOrder[kind],original.back(),-1);
        const auto changed=app.columnOrder[kind]; Load(); orderOk=app.columnOrder[kind]==changed && changed.front()==original.back() && orderOk;
        OpenOverlay(kind); ok=Export(app.main,L"build\\qa\\column-order-"+std::to_wstring(kind)+L".bmp") && ok;
        UpdateOverlays(); ok=Export(app.overlays[kind].window,L"build\\qa\\reordered-"+std::to_wstring(kind)+L".bmp") && ok;
        app.columnOrder[kind]=original; SettingChanged();
    }
    ShowPage(Page::Overlays);
    ok=orderOk && ok; report<<"Independent column ordering, move boundaries, hidden-column positions and persistence: "<<(orderOk?"PASS":"FAIL")<<"\n";
    bool cornersOk=true;
    for(int kind=0;kind<OverlayCount;++kind) {
        auto& overlay=app.overlays[kind]; const auto original=overlay.place;
        overlay.place.growLeft=overlay.place.growUp=false; overlay.place.scale=100;
        SetOverlayOrigin(overlay,monitor.rcWork.left+300,monitor.rcWork.top+250); Fit(overlay);
        for(int corner=0;corner<4;++corner) {
            RECT before{},grown{},shrunk{}; GetWindowRect(overlay.window,&before);
            SetOverlayCorner(kind,corner);
            GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
            overlay.place.scale=130; Fit(overlay); GetWindowRect(overlay.window,&grown);
            cornersOk=((corner&1)?grown.right==before.right:grown.left==before.left) && ((corner&2)?grown.bottom==before.bottom:grown.top==before.top) && cornersOk;
            cornersOk=grown.right-grown.left>before.right-before.left && grown.bottom-grown.top>before.bottom-before.top && cornersOk;
            overlay.place.scale=100; Fit(overlay); GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
        }
        ok=Export(app.overlays[kind].window,L"build\\qa\\fixed-corner-"+std::to_wstring(kind)+L".bmp") && ok;
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
    ok=trayFallbackOk && listOk && updateOk && startupOk && ok;
    if(IsWindow(app.main)) DestroyWindow(app.main);
    DeleteObject(app.brush); return ok?0:2;
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
