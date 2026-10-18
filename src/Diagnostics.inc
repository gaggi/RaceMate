// Render checks and regression diagnostics; excluded from the production build.
// Both tables' settings, saved and restored around checks that change many of them.
struct TableSnapshot { TableSettings table; Bands bands; };
std::array<TableSnapshot,2> Tables() { return {TableSnapshot{First(RelativeKind).table,First(RelativeKind).bands},TableSnapshot{First(StandingsKind).table,First(StandingsKind).bands}}; }
void RestoreTables(const std::array<TableSnapshot,2>& tables) { for(int kind:{RelativeKind,StandingsKind}) { First(kind).table=tables[kind].table; First(kind).bands=tables[kind].bands; } }
bool Export(HWND window,const std::wstring& path,bool transparentChecker=false) {
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
    if(transparentChecker) {
        auto* pixels=static_cast<uint32_t*>(bits);
        // Premultiplied pixels over a checkerboard, as the desktop would show them.
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const uint32_t p=pixels[y*w+x], keep=255-(p>>24), back=((x/18+y/18)%2)?0x303840u:0x46515cu;
            uint32_t out=0; for(int shift:{0,8,16}) out|=std::min(255u,((p>>shift)&255)+((back>>shift)&255)*keep/255)<<shift;
            pixels[y*w+x]=out;
        }
    }
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader); file.bfSize=file.bfOffBits+w*h*4;
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<char*>(&file),sizeof(file)); out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader)); out.write(static_cast<char*>(bits),static_cast<std::streamsize>(w)*h*4);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,screen); return out.good();
}
// A DPI change rebuilds fonts and the current page in place.
bool MainDpiDiagnostics(std::ostream& report) {
    bool ok=true;
    OpenOverlay(First(RelativeKind));
    for(UINT target:{96u,192u,GetDpiForWindow(app.main)}) {
        RECT bounds{}; GetWindowRect(app.main,&bounds);
        SendMessageW(app.main,WM_DPICHANGED,MAKEWPARAM(target,target),reinterpret_cast<LPARAM>(&bounds));
        const bool same=ui.page==Page::Overlay && ui.overlay==&First(RelativeKind) && ui.host.Content()!=nullptr && ui.text!=nullptr;
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
        std::vector<std::wstring> titles; for(const auto& row:reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA))->Rows()) if(row.button==L"Settings") titles.push_back(row.title);
        startupOk=titles.size()>=OverlayCount; for(int kind=0;kind<OverlayCount && startupOk;++kind) startupOk=titles[kind]==Titles[kind];
    }
    report<<"Overlays page at startup lists every overlay by its own name: "<<(startupOk?"PASS":"FAIL")<<"\n";
    app.preview=true; app.edit=false; PreviewData(0); UpdateStatus(); RefreshPage();
    SetWindowPos(app.main,nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(app.main,SW_SHOWNOACTIVATE); UpdateWindow(app.main);
    bool ok=Export(app.main,L"build\\qa\\main-overlays.bmp");
    {
        auto& o=First(TrackMapKind); const auto settings=o.map; const auto savedSession=app.session; const auto savedFrame=app.frame;
        bool mapOk=true, exercised=false;
        for(int id:{353,166,168}) {
            const auto cache=TrackMapCache(); std::error_code error;
            if(!std::filesystem::exists(cache/std::to_wstring(id)/L"source.txt",error)) continue;
            exercised=true;
            const std::atomic_bool cancelled=false;
            const bool haveTurns=std::filesystem::exists(cache/std::to_wstring(id)/L"turns.svg",error);
            auto loaded=trackmap::Load(*trackmap::FindSource(id),cache,cancelled,haveTurns);
            mapOk=loaded.fromCache && loaded.map && loaded.map.geometry.direction!=0 && mapOk;
            if(!loaded.map) continue;
            app.preview=false; app.session.trackId=id; app.session.trackName=id==353?"limerock 2019 gp":(id==168?"suzuka grandprix":"okayama full");
            app.session.track=id==353?"Lime Rock Park":(id==168?"Suzuka International Racing Course":"Okayama International Circuit"); app.session.trackConfig=id==166?"Full Course":"Grand Prix";
            if(id==168) mapOk=loaded.map.geometry.bridged && loaded.map.geometry.length>4500 && loaded.map.geometry.length<5000 && mapOk;
            for(const auto& d:app.session.drivers) app.frame.cars[d.index].pct=std::fmod(d.index*.091+.05,1.);
            trackMap.task.Cancel(); trackMap.identity=std::to_string(id)+":"+trackmap::NormalizeName(app.session.trackName); trackMap.id=id;
            trackMap.geometry=std::move(loaded.map.geometry); trackMap.ready=trackMap.cached=true; ++trackMap.revision;
            o.map={}; UpdateOverlays();
            const auto bitmap=o.mapBitmap;
            mapOk=bitmap && Export(o.window,L"build\\qa\\trackmap-"+std::to_wstring(id)+L".bmp") && mapOk;
            auto markers=[&] { return std::count_if(o.scene.begin(),o.scene.end(),[](const DrawCommand& c){return c.type==DrawCommand::MapCar;}); };
            const auto all=markers(); o.map.ownClass=true; UpdateOverlays();
            const auto sprites=o.mapMarkers.size();
            app.frame.cars[0].pct+=.001; UpdateOverlays();
            mapOk=markers()<all && o.mapBitmap==bitmap && sprites>0 && o.mapMarkers.size()==sprites && mapOk;
            // Layer visibility must invalidate the static bitmap without changing
            // the prepared route, start/finish alignment or live marker positions.
            auto mapPixels=[&] {
                std::vector<uint32_t> pixels(static_cast<size_t>(o.mapWidth)*o.mapHeight);
                BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),o.mapWidth,-o.mapHeight,1,32,BI_RGB};
                GetDIBits(o.dc,o.mapBitmap,0,o.mapHeight,pixels.data(),&info,DIB_RGB_COLORS);
                for(auto& pixel:pixels) pixel&=0xffffff;
                return pixels;
            };
            if(!trackMap.geometry.labels.empty()) {
                const auto basePixels=mapPixels(); const auto routePoint=trackMap.geometry.At(.37);
                o.map.cornerNumbers=true; trackMap.identity+=":labels"; UpdateOverlays();
                mapOk=basePixels!=mapPixels() && (o.mapStyle&8)!=0 && mapOk;
                mapOk=Export(o.window,L"build\\qa\\trackmap-corner-numbers-"+std::to_wstring(id)+L".bmp") && mapOk;
                o.map.cornerNames=true; o.map.labelScale=125; Save(); Load(); UpdateOverlays();
                mapOk=o.map.cornerNumbers && o.map.cornerNames && o.map.labelScale==125 && o.map.labelContrast && trackMap.geometry.At(.37).x==routePoint.x && mapOk;
                mapOk=Export(o.window,L"build\\qa\\trackmap-corner-labels-"+std::to_wstring(id)+L".bmp") && mapOk;
                const auto labeledBitmap=o.mapBitmap; app.frame.cars[0].pct+=.001; UpdateOverlays();
                mapOk=o.mapBitmap==labeledBitmap && mapOk;
                o.map.background=false; UpdateOverlays();
                mapOk=Export(o.window,L"build\\qa\\trackmap-corner-transparent-"+std::to_wstring(id)+L".bmp",true) && mapOk;
                OpenOverlay(o);
                if(const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr)) {
                    auto* rows=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA));
                    for(size_t row=0;row<rows->Rows().size();++row) if(rows->Rows()[row].title==L"Label size") {
                        rows->PressIconButton(static_cast<int>(row),0); rows->ScrollIntoView(static_cast<int>(row));
                        const HWND combo=GetDlgItem(list,240);
                        mapOk=combo && SendMessageW(combo,CB_GETCOUNT,0,0)==6 && mapOk;
                        if(combo) { SendMessageW(combo,CB_SETCURSEL,3,0); SendMessageW(ui.host.Content(),WM_COMMAND,MAKEWPARAM(240,CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo)); }
                        mapOk=o.map.labelScale==150 && mapOk;
                        mapOk=Export(app.main,L"build\\qa\\trackmap-corner-settings.bmp") && mapOk;
                        break;
                    }
                } else mapOk=false;
                ShowPage(Page::Overlays);
                o.map.cornerNumbers=o.map.cornerNames=false; o.map.labelScale=100; o.map.background=true;
                UpdateOverlays(); mapOk=trackMap.ready && !trackMap.task.Running() && mapOk;
                o.map.cornerNames=true; UpdateOverlays();
                mapOk=trackMap.ready && !trackMap.task.Running() && mapOk;
                o.map.cornerNames=false; UpdateOverlays();
            }
            const auto start=trackMap.geometry.At(0); const auto revision=trackMap.revision; const auto withLayers=mapPixels();
            o.map.pitLane=false; UpdateOverlays(); const auto withoutPit=mapPixels();
            mapOk=(trackMap.geometry.pit.shapes.empty() || withLayers!=withoutPit) && mapOk;
            o.map.startFinish=false; UpdateOverlays();
            mapOk=withoutPit!=mapPixels() && trackMap.revision==revision && trackMap.geometry.At(0).x==start.x && trackMap.geometry.At(0).y==start.y && mapOk;
            o.map.background=false; Save(); Load(); UpdateOverlays();
            COLORREF key=0; BYTE alpha=0; DWORD flags=0;
            // Per-pixel alpha: an empty corner is fully transparent, the window takes its content from the buffer.
            mapOk=!o.map.background && !o.map.pitLane && !o.map.startFinish && o.layeredAlpha && o.pixels && (o.pixels[0]>>24)==0
                && !GetLayeredWindowAttributes(o.window,&key,&alpha,&flags) && BaseHeight(o)==240
                && !ShowOverlayHeader(o) && FooterHeight(o)==0 && std::none_of(o.scene.begin(),o.scene.end(),[](const DrawCommand& c){return c.type==DrawCommand::Outline;}) && mapOk;
            mapOk=Export(o.window,L"build\\qa\\trackmap-transparent-"+std::to_wstring(id)+L".bmp",true) && mapOk;
            o.map.background=true; UpdateOverlays();
            GetLayeredWindowAttributes(o.window,&key,&alpha,&flags);
            mapOk=!o.layeredAlpha && (flags&LWA_ALPHA) && !(flags&LWA_COLORKEY) && ShowOverlayHeader(o)==o.bands.header && mapOk;
            o.map.numbers=false; o.map.pit=false; o.map.details=false; Save(); Load(); UpdateOverlays();
            mapOk=!o.map.numbers && o.map.ownClass && !o.map.pit && !o.map.details && Export(o.window,L"build\\qa\\trackmap-dots-"+std::to_wstring(id)+L".bmp") && mapOk;
        }
        app.preview=false; app.session.trackId=999999; app.session.trackName="unknown layout"; UpdateOverlays();
        mapOk=!trackMap.ready && !trackMap.task.Running() && std::none_of(o.scene.begin(),o.scene.end(),[](const DrawCommand& c){return c.type==DrawCommand::Map;}) && mapOk;
        o.map=settings; app.preview=true; app.session=savedSession; app.frame=savedFrame; RetryTrackMap(); UpdateOverlays(); Save();
        OpenOverlay(o); mapOk=Export(app.main,L"build\\qa\\trackmap-settings.bmp") && mapOk;
        report<<"Track map: cached SVGs/markers, class/pit filters, transparent background, optional pit lane/start-finish, corner labels/size editor, persistence and unchanged geometry: "<<(mapOk?(exercised?"PASS":"PASS (no cached real SVGs to render)"):"FAIL")<<"\n";
        ok=mapOk && ok;
        ShowPage(Page::Overlays);
    }
    {
        // --profile: ticks are measured per part and written to profile.txt beside settings.ini.
        bool profileOk=true; auto& map=First(TrackMapKind); const auto placement=map.place; map.place.enabled=true; SettingChanged();
        profiler.on=true; profiler.entries.clear(); profiler.start=GetTickCount64(); profiler.cpuStart=ProcessCpuMicroseconds(); profiler.ticks=0;
        for(int i=0;i<20;++i) { for(auto& o:app.overlays) o.due=0; Tick(); }
        profiler.start=GetTickCount64()-10001; Tick();
        std::ifstream file(std::filesystem::path(app.config).parent_path()/L"profile.txt"); const std::string text((std::istreambuf_iterator<char>(file)),{});
        profileOk=text.find("Process CPU")!=std::string::npos && text.find("tick (everything below)")!=std::string::npos
            && text.find("overlay Track Map: update")!=std::string::npos && text.find("session YAML parsing")!=std::string::npos && profileOk;
        profiler.on=false; profiler.entries.clear(); map.place=placement; SettingChanged();
        report<<"Profile: parts measured and written every 10 s: "<<(profileOk?"PASS":"FAIL")<<"\n"; ok=profileOk && ok;
        if(!profileOk) report<<text;
    }
    {
        // Pit exit in the preview: 22 s lane loss plus 20 s service; metric, Fuel module and the ring on the track map.
        bool pitOk=true; InvalidateModel();
        const auto& rejoin=PitExit(); const std::wstring position=L"P"+std::to_wstring(rejoin.classPosition);
        pitOk=rejoin.valid && rejoin.loss==42 && rejoin.classPosition>=1 && FooterValue(PitExitMetric)==L"Pit exit "+position+L" · 42 s" && pitOk;
        auto& fuel=First(FuelKind); const auto fuelSettings=fuel.fuel;
        fuel.fuel.display[PitExitModule]=true; SettingChanged();
        pitOk=std::any_of(fuel.scene.begin(),fuel.scene.end(),[&](const DrawCommand& c){ return c.type==DrawCommand::Text && c.text==position; }) && Export(fuel.window,L"build\\qa\\fuel-pit-exit.bmp") && pitOk;
        fuel.fuel=fuelSettings; SettingChanged();
        auto& map=First(TrackMapKind); const auto placement=map.place; map.place.enabled=true; SettingChanged();
        if(trackMap.ready) pitOk=std::any_of(map.scene.begin(),map.scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::MapCar && c.color==Ink::Accent; }) && Export(map.window,L"build\\qa\\trackmap-pit-exit.bmp") && pitOk;
        // Circle and line: no SVG needed, every car placed, the ring included; their own sizes.
        for(int layout:{CircleLayout,LineLayout}) {
            map.map.layout=layout; Fit(map); SettingChanged();
            const auto markers=std::count_if(map.scene.begin(),map.scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::MapCar; });
            pitOk=OverlayWidth(map)==TrackMapWidthOf(map.map) && markers>=static_cast<long long>(app.session.drivers.size()) && std::any_of(map.scene.begin(),map.scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::MapLabel && c.text==L"PIT"; }) && pitOk;
            pitOk=Export(map.window,layout==CircleLayout?L"build\\qa\\trackmap-circle.bmp":L"build\\qa\\trackmap-line.bmp") && pitOk;
            // Your car is drawn last, marker then number, above every other car and number.
            std::vector<const DrawCommand*> drawn; for(const auto& c:map.scene) if(c.type==DrawCommand::MapCar || c.type==DrawCommand::MapLabel) drawn.push_back(&c);
            pitOk=drawn.size()>=2 && drawn.back()->type==DrawCommand::MapLabel && drawn[drawn.size()-2]->type==DrawCommand::MapCar && drawn[drawn.size()-2]->color==Ink::Yellow && pitOk;
            // The other cars from the back to the front, each number right after its own marker, so the leader is on top.
            int previousPosition=MaxCars+2; const DrawCommand* previous=nullptr;
            for(size_t i=0;i+2<drawn.size();++i) if(drawn[i]->type==DrawCommand::MapLabel && drawn[i]->text!=L"PIT") {
                const auto d=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[&](const Driver& x){ return Wide(x.number)==drawn[i]->text; });
                const int standing=d!=app.session.drivers.end() && app.frame.cars[d->index].position>0?app.frame.cars[d->index].position:MaxCars+1;
                pitOk=standing<=previousPosition && i>0 && drawn[i-1]->type==DrawCommand::MapCar && pitOk;
                previousPosition=standing; previous=drawn[i];
            }
            pitOk=previous!=nullptr && pitOk;
        }
        // Positions instead of car numbers: in class and overall, as the cars stand.
        for(int mode:{1,2}) {
            map.map.markerText=mode; SettingChanged();
            const auto sample=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[](const Driver& d){ return d.index!=ReferenceCar(app.frame) && app.frame.cars[d.index].position>0 && app.frame.cars[d.index].surface>=0; });
            const int standing=sample==app.session.drivers.end()?-1:(mode==1?app.frame.cars[sample->index].classPosition:app.frame.cars[sample->index].position);
            pitOk=standing>0 && std::any_of(map.scene.begin(),map.scene.end(),[&](const DrawCommand& c){ return c.type==DrawCommand::MapLabel && c.text==std::to_wstring(standing); }) && pitOk;
            pitOk=std::none_of(map.scene.begin(),map.scene.end(),[&](const DrawCommand& c){ return c.type==DrawCommand::MapLabel && c.text==Wide(sample->number) && Wide(sample->number)!=std::to_wstring(standing); }) && pitOk;
        }
        map.map.markerText=0; SettingChanged();
        // Three-digit numbers keep all digits: the label is as wide as the text, centred on the marker.
        {
            const auto savedDrivers=app.session.drivers;
            for(auto& d:app.session.drivers) d.number=std::to_string(100+d.index*11);
            map.map.layout=LineLayout; Fit(map); SettingChanged();
            const auto label=std::find_if(map.scene.begin(),map.scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::MapLabel && c.text.size()==3; });
            pitOk=label!=map.scene.end() && label->r.right-label->r.left>=[&]{ SIZE size{}; SelectObject(map.dc,map.fonts[SmallBold]); GetTextExtentPoint32W(map.dc,label->text.c_str(),3,&size); return static_cast<LONG>(size.cx); }() && Export(map.window,L"build\\qa\\trackmap-three-digits.bmp") && pitOk;
            app.session.drivers=savedDrivers;
        }
        // A car in the pits beside the start straight sits on the drawn pit lane, not on the track.
        map.map.layout=MapLayout; Fit(map); SettingChanged();
        // Circle and line released the SVG map; it loads again from the cache in the background.
        for(int wait=0;wait<300 && !trackMap.ready && trackMap.task.Running();++wait) { Sleep(10); UpdateOverlays(); }
        if(trackMap.ready && !trackMap.geometry.pitPaths.empty()) {
            const auto savedNow=app.now; const auto pitting=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[](const Driver& d){ return app.frame.cars[d.index].pit; });
            if(pitting!=app.session.drivers.end()) {
                app.now=static_cast<ULONGLONG>((1-std::fmod(pitting->index*.091,1.))*90000); UpdateOverlays();
                const double lap=std::fmod(app.now/90000.+pitting->index*.091,1.);
                const auto onPit=trackMap.geometry.PitAt(lap), onTrack=trackMap.geometry.At(lap);
                pitOk=(onPit.x!=onTrack.x || onPit.y!=onTrack.y) && Export(map.window,L"build\\qa\\trackmap-pit-lane.bmp") && pitOk;
                app.now=savedNow; UpdateOverlays();
            }
            // iRacing's background and the other configurations under the track, where the layout has them.
            const int style=MapStyle(map.map);
            map.map.surroundings=map.map.allConfigs=true; SettingChanged();
            pitOk=MapStyle(map.map)!=style && map.mapStyle==MapStyle(map.map) && Export(map.window,L"build\\qa\\trackmap-layers.bmp") && pitOk;
            report<<"  track map layers: background "<<trackMap.geometry.background.shapes.size()<<" shapes, other configurations "<<trackMap.geometry.inactive.shapes.size()<<" shapes\n";
            map.map.surroundings=map.map.allConfigs=false; SettingChanged();
        }
        map.place=placement; map.map.layout=MapLayout; Fit(map); SettingChanged();
        // Standings: a faded row of its own at the predicted position, only when switched on.
        auto& standings=First(StandingsKind); const auto table=standings.table;
        const auto withoutExit=Rows(standings);
        pitOk=std::none_of(withoutExit.begin(),withoutExit.end(),[](const Row& r){ return r.pitExit; }) && pitOk;
        standings.table.pitExit=true; SettingChanged();
        const auto withExit=Rows(standings);
        pitOk=std::count_if(withExit.begin(),withExit.end(),[](const Row& r){ return r.pitExit; })==1 && pitOk;
        pitOk=std::any_of(standings.scene.begin(),standings.scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::Fill && c.color==Ink::PitExitRow; }) && Export(standings.window,L"build\\qa\\standings-pit-exit.bmp") && pitOk;
        // Right behind the car just ahead after the stop, even where the usual selection shows only the leader:
        // two cars ahead and two behind the row are added.
        standings.table.standings.separateClasses=true; standings.table.standings.leaders=1; standings.table.standings.ahead=standings.table.standings.behind=0; SettingChanged();
        {
            const auto rows=Rows(standings); const auto row=std::find_if(rows.begin(),rows.end(),[](const Row& r){ return r.pitExit; });
            const auto& r=PitExit();
            if(row!=rows.end()) { auto previous=row; while(previous!=rows.begin() && (previous-1)->player) --previous; pitOk=(r.classAhead<0 || (previous!=rows.begin() && (previous-1)->driver.index==r.classAhead)) && pitOk; }
            else pitOk=false;
            int own=0; for(const auto& x:rows) if(!x.pitExit && x.driver.classId==row->driver.classId && !x.player) ++own;
            // Only the leader would be shown besides you; the cars around the row make it at least three.
            pitOk=own>=3 && pitOk;
            pitOk=Export(standings.window,L"build\\qa\\standings-pit-exit-around.bmp") && pitOk;
        }
        standings.table=table; SettingChanged();
        // Leaving the pits or replays give no estimate.
        app.frame.pit=true; InvalidateModel(); pitOk=!PitExit().valid && FooterValue(PitExitMetric)==L"Pit exit —" && pitOk;
        app.frame.pit=false; InvalidateModel();
        // Settings: lane loss and service times, the service row expanded to its two fields.
        ShowPage(Page::Settings);
        if(const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr)) {
            auto* rows=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA)); int service=-1, lane=-1;
            for(size_t i=0;i<rows->Rows().size();++i) { if(rows->Rows()[i].title==L"Pit lane loss") lane=static_cast<int>(i); if(rows->Rows()[i].title.rfind(L"Service times",0)==0) service=static_cast<int>(i); }
            pitOk=lane>=0 && service>=0 && pitOk;
            if(service>=0) {
                rows->PressIconButton(service,0); rows->ScrollIntoView(service);
                RedrawWindow(app.main,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
                pitOk=Export(app.main,L"build\\qa\\settings-pit-stop.bmp") && pitOk;
                rows->PressIconButton(service,0);
            }
        } else pitOk=false;
        ShowPage(Page::Overlays);
        report<<"Pit exit estimate: metric, Fuel module, track map ring and settings: "<<(pitOk?"PASS":"FAIL")<<"\n"; ok=pitOk && ok;
    }
    {
        bool forecastOk=true;
        InvalidateModel(); Plan();
        const auto savedPlan=app.fuelPlan;
        auto& finish=app.fuelPlan.prediction.finish;
        finish.leaderTotalDistance=19.08; finish.finalLapTimeLeft=13;
        forecastOk=LeaderDistanceValue()==L"~19.08" && WhiteFlagTimeValue()==L"~13 s" && forecastOk;
        forecastOk=FooterValue(LapLeaderDistanceMetric)==L"Lap 8 / ~19.08" && forecastOk;
        const Frame savedFrame=app.frame;
        app.frame.onTrack=false; app.frame.camera=0; app.frame.cars[0].lap=12;
        forecastOk=FooterValue(LapLeaderDistanceMetric)==L"Lap 12 / ~19.08" && forecastOk;
        app.frame=savedFrame;
        finish.finalLapTimeLeft=73; forecastOk=WhiteFlagTimeValue()==L"~1:13" && forecastOk;
        finish.finalLapTimeLeft=0; forecastOk=WhiteFlagTimeValue()==L"~0 s" && forecastOk;
        finish.finalLapTimeLeft=std::numeric_limits<double>::quiet_NaN(); forecastOk=WhiteFlagTimeValue()==L"—" && forecastOk;
        finish.valid=false; forecastOk=LeaderDistanceValue()==L"—" && WhiteFlagTimeValue()==L"—" && forecastOk;
        forecastOk=FooterValue(LapLeaderDistanceMetric)==L"Lap 8 / —" && forecastOk;
        app.fuelPlan=savedPlan;
        // One registry and renderer for both bands of every kind, including the Pit Limiter.
        for(int kind=0;kind<OverlayCount;++kind) {
            auto& o=First(kind); const Bands bands=o.bands;
            o.bands.header=true; o.bands.footer.enabled=true;
            o.bands.headerMetrics={LeaderDistanceMetric,WhiteFlagTimeMetric,NoMetric,NoMetric};
            o.bands.footer.metrics={WhiteFlagTimeMetric,LeaderDistanceMetric,NoMetric,NoMetric};
            Save(); Load(); UpdateOverlays();
            forecastOk=o.bands.header && o.bands.footer.enabled && o.bands.headerMetrics[0]==LeaderDistanceMetric && o.bands.footer.metrics[1]==LeaderDistanceMetric && forecastOk;
            bool leader=false,time=false;
            for(const auto& command:o.scene) { leader=leader || command.text==LeaderDistanceValue(); time=time || command.text==WhiteFlagTimeValue(); }
            forecastOk=leader && time && Export(o.window,L"build\\qa\\forecast-bands-"+std::to_wstring(kind)+L".bmp") && forecastOk;
            OpenOverlay(o);
            const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
            auto* rows=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA));
            int header=-1; for(size_t i=0;rows && i<rows->Rows().size();++i) if(rows->Rows()[i].title==L"Header") header=static_cast<int>(i);
            if(header<0) forecastOk=false;
            else {
                rows->PressIconButton(header,0);
                int combos=0;
                for(HWND child=GetWindow(list,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
                    wchar_t name[32]{}; GetClassNameW(child,name,32);
                    if(wcscmp(name,L"ComboBox")==0) { ++combos; forecastOk=SendMessageW(child,CB_GETCOUNT,0,0)==MetricCount && forecastOk; }
                }
                forecastOk=combos==SlotCount && forecastOk;
            }
            ShowPage(Page::Overlays); o.bands=bands; Save();
        }
        auto& fuel=First(FuelKind); const FuelSettings settings=fuel.fuel;
        fuel.fuel.strip=false; fuel.fuel.display.fill(false); fuel.fuel.display[LeaderDistanceModule]=fuel.fuel.display[WhiteFlagTimeModule]=true;
        fuel.fuel.order=FuelOrder; Save(); Load(); UpdateOverlays();
        forecastOk=FuelHeight(fuel)==64 && Export(fuel.window,L"build\\qa\\fuel-leader-forecast.bmp") && forecastOk;
        fuel.fuel.strip=true; Save(); Load(); UpdateOverlays();
        forecastOk=FuelStripCount(fuel)==2 && InStrip(FuelKind,LeaderDistanceModule) && InStrip(FuelKind,WhiteFlagTimeModule) && Export(fuel.window,L"build\\qa\\fuel-leader-forecast-strip.bmp") && forecastOk;
        fuel.fuel=settings; Save(); UpdateOverlays();
        report<<"Leader forecast precision, final-lap clock, independent Fuel modules and universal header/footer choices: "<<(forecastOk?"PASS":"FAIL")<<"\n"; ok=forecastOk && ok;
    }
    {
        const auto savedTables=Tables(); const int savedExtra=app.opponentFuelExtraLaps;
        bool fuelOk=true;
        app.opponentFuelExtraLaps=4; Save(); app.opponentFuelExtraLaps=0; Load();
        fuelOk=app.opponentFuelExtraLaps==4 && fuelOk;
        for(int kind:{RelativeKind,StandingsKind}) {
            auto& o=First(kind); auto& t=o.table;
            // Old saved orders gain the appended column without renumbering existing columns.
            t.columnOrder=DefaultColumns[kind]; t.columnOrder.pop_back(); Save(); Load();
            fuelOk=t.columnOrder==DefaultColumns[kind] && fuelOk;
            SetColumnVisible(o,FuelColumn,false); const int before=OverlayWidth(o);
            SetColumnVisible(o,FuelColumn,true); Save(); Load();
            fuelOk=ColumnVisible(o,FuelColumn) && OverlayWidth(o)==before+ColumnWidth(o,FuelColumn) && fuelOk;
            {
                Row rated; rated.driver.rating=2456; rated.driver.license="A 3.45"; Cell badge{{},Ink::White}; CellRenderers[CombinedRatingColumn](rated,o,badge);
                rated.driver.rating=0; Cell none{{},Ink::White}; CellRenderers[CombinedRatingColumn](rated,o,none);
                fuelOk=badge.text==L"2456" && badge.box==Cell::Filled && badge.boxColor==LicenseFill(rated.driver) && none.text==L"—" && none.box!=Cell::Filled && fuelOk;
                SetColumnVisible(o,CombinedRatingColumn,true); const int wide=OverlayWidth(o); SetColumnVisible(o,CombinedRatingColumn,false);
                fuelOk=wide==OverlayWidth(o)+ColumnWidth(o,CombinedRatingColumn) && fuelOk;
            }
            Row row; row.driver.index=0; Cell end{{},Ink::White}; CellRenderers[FuelColumn](row,o,end);
            row.driver.index=4; Cell save{{},Ink::White}; CellRenderers[FuelColumn](row,o,save);
            row.driver.index=6; Cell unknown{{},Ink::White}; CellRenderers[FuelColumn](row,o,unknown);
            fuelOk=end.text==L"END" && end.box==Cell::Filled && end.boxColor==Ink::Green && save.text==L"SAVE" && save.boxColor==Ink::Yellow && unknown.text.empty() && unknown.box==Cell::Plain && fuelOk;
            t.extraColumns.fill(false); t.extraColumns[FuelColumn-BrandColumn]=true;
            t.driverColumns.fill(false); t.driverColumns[PitStopColumn-LicenseColumn]=true;
            t.columnHeaders=true; t.best=t.last=false; t.interval=kind==StandingsKind;
            t.columnOrder=kind==RelativeKind?std::vector<int>{PositionColumn,NumberColumn,DriverColumn,GapColumn,PitStopColumn,FuelColumn}:
                std::vector<int>{PositionColumn,NumberColumn,DriverColumn,GapColumn,IntervalColumn,BestColumn,LastColumn,PitStopColumn,FuelColumn};
            o.bands.header=false; o.bands.footer.enabled=true;
            UpdateOverlays();
            fuelOk=Export(o.window,L"build\\qa\\fuel-column-"+std::to_wstring(kind)+L".bmp") && fuelOk;
            OpenOverlay(o);
            const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
            auto* rows=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA));
            bool found=false; if(rows) for(const auto& item:rows->Rows()) if(item.title==L"Fuel to finish") found=item.toggle==1;
            fuelOk=found && Export(app.main,L"build\\qa\\fuel-column-settings-"+std::to_wstring(kind)+L".bmp") && fuelOk;
            // Each overlay's switch remains independent.
            SetColumnVisible(o,FuelColumn,false);
        }
        RestoreTables(savedTables); app.opponentFuelExtraLaps=savedExtra; Save(); UpdateOverlays();
        report<<"Opponent fuel column: END/SAVE colors, blank unknowns, independent visibility, old order migration and shared saving setting: "<<(fuelOk?"PASS":"FAIL")<<"\n";
        ok=fuelOk && ok;
    }
    {
        ShowPage(Page::Settings); UpdateWindow(app.main); ok=Export(app.main,L"build\\qa\\main-settings.bmp") && ok;
        bool pagesOk=ui.page==Page::Settings && ui.host.Content()!=nullptr;
        for(int kind=0;kind<OverlayCount;++kind) {
            OpenOverlay(First(kind)); UpdateWindow(app.main);
            pagesOk=ui.page==Page::Overlay && ui.overlay==&First(kind) && IsWindowVisible(ui.back) && pagesOk;
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
        // Caution laps never count now, so the fuel page has a layout and modules instead of a filter.
        const auto savedFuel=First(FuelKind).fuel; bool fuelOk=true;
        First(FuelKind).fuel.strip=true; First(FuelKind).fuel.display[TwoLongerRow]=true; SettingChanged(); Load();
        fuelOk=First(FuelKind).fuel.strip && First(FuelKind).fuel.display[TwoLongerRow] && OverlayWidth(First(FuelKind))==FuelStripCount(First(FuelKind))*FuelStripCell && HeightWithTitle(First(FuelKind))==32+50 && fuelOk;
        First(FuelKind).fuel=savedFuel; SettingChanged();
        report<<"Fuel layout and modules persist; the strip's width follows its values: "<<(fuelOk?"PASS":"FAIL")<<"\n"; ok=fuelOk && ok;
    }
    UpdateOverlays();
    for(auto& o:app.overlays) ok=Export(o.window,L"build\\qa\\overlay-"+std::to_wstring(o.kind)+L".bmp")&&ok;
    {
        const auto savedTables=Tables();
        const auto savedFrame=app.frame; bool displayOk=true;
        app.frame.cars[1].outLap=true; app.frame.cars[1].sessionFlags=0x80000u; app.frame.cars[2].pit=true; app.frame.cars[2].sessionFlags=0x100000u;
        for(int kind=0;kind<2;++kind) {
            First(kind).table.showDriver=First(kind).table.showStatus=First(kind).table.columnHeaders=true; First(kind).bands.header=false; First(kind).bands.footer.enabled=true; First(kind).table.nameFormat=0; Save(); UpdateOverlays();
            Row test; test.driver.name="Daniel Fischer";
            for(int format=0;format<3;++format) {
                First(kind).table.nameFormat=format; SettingChanged();
                const wchar_t* expected[]={L"Daniel Fischer",L"D. Fischer",L"D Fischer"}; displayOk=DriverName(test,First(kind).table)==expected[format] && displayOk;
            }
            test.driver.name="Éric van der Meer"; First(kind).table.nameFormat=1; displayOk=DriverName(test,First(kind).table)==L"É. van der Meer" && displayOk;
            Row status; status.car.pit=status.car.towing=status.car.outLap=true; status.car.sessionFlags=0x190000u;
            displayOk=OverlayStatus(status,First(kind).table)==L"PIT" && displayOk; status.car.pit=false;
            displayOk=OverlayStatus(status,First(kind).table)==L"TOW" && displayOk; status.car.towing=false;
            displayOk=OverlayStatus(status,First(kind).table)==L"BLACK" && displayOk; status.car.sessionFlags&=~0x10000u;
            displayOk=OverlayStatus(status,First(kind).table)==L"REPAIR" && displayOk; status.car.sessionFlags&=~0x100000u;
            displayOk=OverlayStatus(status,First(kind).table)==L"SLOW" && displayOk; status.car.sessionFlags=0;
            displayOk=OverlayStatus(status,First(kind).table)==L"OUT" && displayOk;
            First(kind).table.nameFormat=1; SettingChanged();
            int before=BaseHeight(First(kind));
            First(kind).bands.header=true; SettingChanged(); displayOk=BaseHeight(First(kind))==before+32 && displayOk;
            First(kind).bands.headerMetrics={OverlayNameMetric,TrackTempMetric,LocalTimeMetric}; SettingChanged();
            Load(); displayOk=First(kind).bands.headerMetrics==Slots{OverlayNameMetric,TrackTempMetric,LocalTimeMetric} && FooterValue(OverlayNameMetric,&First(kind))==Titles[kind] && First(kind).bands.footer.metrics==savedTables[kind].bands.footer.metrics && displayOk;
            OpenOverlay(First(kind)); Export(app.main,L"build\\qa\\integrated-settings-"+std::to_wstring(kind)+L".bmp"); Export(First(kind).window,L"build\\qa\\configured-header-status-"+std::to_wstring(kind)+L".bmp");
            const int expandedWidth=OverlayWidth(First(kind));
            First(kind).table.columnHeaders=false; SettingChanged(); displayOk=BaseHeight(First(kind))==before+5 && OverlayWidth(First(kind))<expandedWidth && displayOk;
            Export(First(kind).window,L"build\\qa\\compact-status-"+std::to_wstring(kind)+L".bmp");
            int width=OverlayWidth(First(kind));
            SetColumnVisible(First(kind),DriverColumn,false); displayOk=!ColumnVisible(First(kind),DriverColumn) && OverlayWidth(First(kind))==width-ColumnWidth(First(kind),DriverColumn) && displayOk;
            width=OverlayWidth(First(kind));
            SetColumnVisible(First(kind),StatusColumn,false); displayOk=!First(kind).table.showStatus && OverlayWidth(First(kind))==width && displayOk;
            First(kind).bands.footer.enabled=false; SettingChanged();
            displayOk=BaseHeight(First(kind))==before+5-28 && displayOk;
            Load(); displayOk=!First(kind).table.showDriver && !First(kind).table.showStatus && !First(kind).table.columnHeaders && First(kind).bands.header && !First(kind).bands.footer.enabled && First(kind).table.nameFormat==1 && displayOk;
        }
        RestoreTables(savedTables); app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Table page settings, name formats, single status priority, configurable headers and independent footer persistence: "<<(displayOk?"PASS":"FAIL")<<"\n"; ok=displayOk && ok;
    }
    {
        // Best and last lap: optional in the Relative (off by default), always in the Standings.
        bool lapsOk=!ColumnVisible(First(RelativeKind),BestColumn) && !ColumnVisible(First(RelativeKind),LastColumn) && ColumnVisible(First(StandingsKind),BestColumn) && ColumnVisible(First(StandingsKind),LastColumn);
        const int width=OverlayWidth(First(RelativeKind));
        SetColumnVisible(First(RelativeKind),BestColumn,true); SetColumnVisible(First(RelativeKind),LastColumn,true);
        lapsOk=OverlayWidth(First(RelativeKind))==width+ColumnWidth(First(RelativeKind),BestColumn)+ColumnWidth(First(RelativeKind),LastColumn) && lapsOk;
        const auto columns=VisibleColumns(First(RelativeKind));
        const auto gap=std::find(columns.begin(),columns.end(),GapColumn);
        lapsOk=gap!=columns.end() && gap+2<columns.end() && gap[1]==BestColumn && gap[2]==LastColumn && lapsOk;
        UpdateOverlays(); lapsOk=Export(First(RelativeKind).window,L"build\\qa\\relative-lap-times.bmp") && lapsOk;
        SetColumnVisible(First(StandingsKind),BestColumn,false); lapsOk=ColumnVisible(First(StandingsKind),BestColumn) && lapsOk;
        Load(); lapsOk=First(RelativeKind).table.best && First(RelativeKind).table.last && lapsOk;
        SetColumnVisible(First(RelativeKind),BestColumn,false); SetColumnVisible(First(RelativeKind),LastColumn,false);
        Load(); lapsOk=!First(RelativeKind).table.best && !First(RelativeKind).table.last && OverlayWidth(First(RelativeKind))==width && lapsOk;
        report<<"Relative best and last lap columns: "<<(lapsOk?"PASS":"FAIL")<<"\n"; ok=lapsOk && ok;
    }
    {
        // Gap signs: off by default; the row order tells ahead from behind.
        const auto savedTables=Tables();
        First(StandingsKind).table.standings.separateClasses=false;
        Row ahead; ahead.gap=-7.5; Row behind; behind.gap=2.5;
        Row standing; standing.car.position=3; standing.gap=2.6; standing.interval=1.2;
        Row lapped=standing; lapped.lapDifference=1; lapped.intervalLaps=1;
        bool signsOk=!First(RelativeKind).table.gapSigns && !First(StandingsKind).table.gapSigns;
        signsOk=Gap(ahead,First(RelativeKind))==Format(7.5,First(RelativeKind).table.gapDecimals) && Gap(behind,First(RelativeKind))==Format(2.5,First(RelativeKind).table.gapDecimals) && signsOk;
        signsOk=Gap(standing,First(StandingsKind))==L"2.6" && Interval(standing,First(StandingsKind).table)==L"1.2" && Gap(lapped,First(StandingsKind))==L"1 L" && Interval(lapped,First(StandingsKind).table)==L"1 L" && signsOk;
        SetGapSigns(First(RelativeKind),true);
        signsOk=Gap(ahead,First(RelativeKind))==L"−"+Format(7.5,First(RelativeKind).table.gapDecimals) && Gap(behind,First(RelativeKind))==L"+"+Format(2.5,First(RelativeKind).table.gapDecimals) && Gap(standing,First(StandingsKind))==L"2.6" && signsOk;
        SetGapSigns(First(StandingsKind),true);
        signsOk=Gap(standing,First(StandingsKind))==L"+2.6" && Interval(standing,First(StandingsKind).table)==L"+1.2" && Gap(lapped,First(StandingsKind))==L"+1 L" && Interval(lapped,First(StandingsKind).table)==L"+1 L" && signsOk;
        Load(); signsOk=First(RelativeKind).table.gapSigns && First(StandingsKind).table.gapSigns && signsOk;
        RestoreTables(savedTables); Save();
        report<<"Gap signs switch per table, off by default: "<<(signsOk?"PASS":"FAIL")<<"\n"; ok=signsOk && ok;
    }
    {
        const auto savedTables=Tables(); const auto savedFrame=app.frame; bool flagsOk=true;
        app.frame.cars[0].sessionFlags=0x10000u; app.frame.cars[1].sessionFlags=0x80000u; app.frame.cars[2].sessionFlags=0x100000u; app.frame.cars[3].sessionFlags=0x190000u;
        for(int kind=0;kind<2;++kind) {
            First(kind).table.driverFlags={true,true,true}; SettingChanged(); OpenOverlay(First(kind));
            Export(app.main,L"build\\qa\\driver-flags-settings-"+std::to_wstring(kind)+L".bmp"); Export(First(kind).window,L"build\\qa\\driver-flags-overlay-"+std::to_wstring(kind)+L".bmp");
            First(kind).table.driverFlags={false,false,false}; SettingChanged();
            Load(); flagsOk=First(kind).table.showStatus && std::none_of(First(kind).table.driverFlags.begin(),First(kind).table.driverFlags.end(),[](bool v){return v;}) && flagsOk;
        }
        RestoreTables(savedTables); app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Driver flag display settings, independent overlays and persisted disabled flags with PIT/OUT status retained: "<<(flagsOk?"PASS":"FAIL")<<"\n"; ok=flagsOk && ok;
    }
    {
        // Every new column, the status badge and class colors on and off; the previews are for review.
        const auto savedTables=Tables();
        const auto savedFrame=app.frame; bool columnsOk=true;
        app.frame.cars[1].outLap=true; app.frame.cars[2].pit=true; app.frame.cars[2].best=app.frame.cars[2].last;
        for(int i=0;i<10;++i) app.frame.cars[i].stintLaps=i==4?-1:i;
        for(int kind=0;kind<2;++kind) {
            const int before=OverlayWidth(First(kind));
            for(auto& on:First(kind).table.extraColumns) on=true;
            First(kind).table.driverColumns[0]=First(kind).table.driverColumns[1]=First(kind).table.driverColumns[2]=true; First(kind).bands.header=true; SettingChanged();
            columnsOk=OverlayWidth(First(kind))>before && Export(First(kind).window,L"build\\qa\\columns-all-"+std::to_wstring(kind)+L".bmp") && columnsOk;
            const int both=OverlayWidth(First(kind));
            First(kind).table.brandName=false; SettingChanged(); columnsOk=OverlayWidth(First(kind))<both && columnsOk;
            columnsOk=Export(First(kind).window,L"build\\qa\\columns-logo-only-"+std::to_wstring(kind)+L".bmp") && columnsOk;
            First(kind).table.brandName=true; First(kind).table.brandLogo=false; SettingChanged(); Load(); columnsOk=!First(kind).table.brandLogo && First(kind).table.brandName && OverlayWidth(First(kind))<both && columnsOk;
            First(kind).table.brandLogo=true; SettingChanged();
            First(kind).table.classColors=false; SettingChanged();
            columnsOk=Export(First(kind).window,L"build\\qa\\columns-neutral-"+std::to_wstring(kind)+L".bmp") && columnsOk;
            Load(); columnsOk=!First(kind).table.classColors && std::all_of(First(kind).table.extraColumns.begin(),First(kind).table.extraColumns.end(),[](bool on){return on;}) && columnsOk;
        }
        columnsOk=RatingChanges()[0]>0 && RatingChanges()[9]<0 && columnsOk;
        // Logos for every brand the demo uses, decoded once per size and the atlas released after painting.
        columnsOk=BrandLogo("BMW")>=0 && BrandLogo("Aston Martin")>=0 && BrandLogo("Unknown")<0 && !First(0).logos.empty() && !logoAtlas.bitmap && columnsOk;
        RestoreTables(savedTables); app.frame=savedFrame; Save(); UpdateOverlays();
        report<<"Car, iRating change and stint columns, license pills, status badge and neutral class colors: "<<(columnsOk?"PASS":"FAIL")<<"\n"; ok=columnsOk && ok;
    }
    {
        const auto savedTables=Tables();
        bool shortOk=true;
        for(int kind=0;kind<2;++kind) {
            First(kind).table.driverShortView={false,false}; First(kind).table.driverColumns[0]=First(kind).table.driverColumns[1]=true;
            First(kind).table.driverShortView={true,true}; SettingChanged();
            Load(); shortOk=First(kind).table.driverShortView[0] && First(kind).table.driverShortView[1] && shortOk;
            Row test; test.driver.license="A 3.45"; test.driver.rating=2400;
            shortOk=DriverValue(test,0,&First(kind).table)==L"3,45" && DriverValue(test,1,&First(kind).table)==L"2,4K" && DriverValue(test,0)==L"A 3.45" && shortOk;
            test.driver.license="Pro 4.99"; test.driver.rating=3450;
            shortOk=DriverValue(test,0,&First(kind).table)==L"4,99" && DriverValue(test,1,&First(kind).table)==L"3,5K" && shortOk;
            test.driver.license=""; test.driver.rating=0;
            shortOk=DriverValue(test,0,&First(kind).table)==L"—" && DriverValue(test,1,&First(kind).table)==L"—" && shortOk;
            OpenOverlay(First(kind)); Export(app.main,L"build\\qa\\short-view-settings-"+std::to_wstring(kind)+L".bmp");
            UpdateOverlays(); Export(First(kind).window,L"build\\qa\\short-view-overlay-"+std::to_wstring(kind)+L".bmp");
            // Hiding the column keeps its short-view choice.
            for(int i=0;i<2;++i) { SetColumnVisible(First(kind),LicenseColumn+i,false); shortOk=!ColumnVisible(First(kind),LicenseColumn+i) && First(kind).table.driverShortView[i] && shortOk; }
        }
        RestoreTables(savedTables); Save(); UpdateOverlays();
        report<<"Independent short license/iRating views, formatting, missing values, kept choice when hidden and persistence: "<<(shortOk?"PASS":"FAIL")<<"\n"; ok=shortOk && ok;
    }
    {
        const Frame originalFrame=app.frame; const Session originalSession=app.session;
        bool footerOk=true;
        for(int kind=0;kind<2;++kind) {
            First(kind).bands.footer.metrics={TrackTempMetric,CurrentLapMetric,TimeRemainingMetric}; SettingChanged();
            Save(); Load(); footerOk=First(kind).bands.footer.metrics==Slots{TrackTempMetric,CurrentLapMetric,TimeRemainingMetric} && footerOk;
            footerOk=Export(First(kind).window,L"build\\qa\\footer-custom-"+std::to_wstring(kind)+L".bmp") && footerOk;
            const int before=HeightWithTitle(First(kind));
            First(kind).bands.footer.enabled=false; SettingChanged();
            Load(); footerOk=!First(kind).bands.footer.enabled && HeightWithTitle(First(kind))==before-28 && footerOk;
            footerOk=Export(First(kind).window,L"build\\qa\\footer-hidden-"+std::to_wstring(kind)+L".bmp") && footerOk;
            First(kind).bands.footer={}; SettingChanged();
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
        PreviewWeather(); bool windOk=OverlayWidth(First(4))==WeatherPanelWidth && BaseHeight(First(4))==32+WeatherHeight(First(WeatherKind));
        for(int direction=0;direction<5;++direction) {
            const double angle=direction==4?25*pi/180:direction*pi/2;
            app.frame.yawNorth=.4; app.frame.windDirection=.4+angle; app.frame.windSpeed=5;
            UpdateOverlays(); windOk=std::abs(RelativeWindAngle(app.frame)-angle)<.001 && windOk;
            windOk=Export(First(4).window,L"build\\qa\\wind-"+std::to_wstring(direction)+L".bmp") && windOk;
        }
        app.frame.onTrack=false; UpdateOverlays(); windOk=Export(First(4).window,L"build\\qa\\wind-unavailable.bmp") && windOk;
        app.frame=saved; UpdateOverlays();
        report<<"Wind overlay cardinal and arbitrary source angles, speed, missing heading and rendering: "<<(windOk?"PASS":"FAIL")<<"\n"; ok=windOk && ok;
    }
    {
        bool weatherOk=true; OpenOverlay(First(WeatherKind));
        weatherOk=Export(app.main,L"build\\qa\\weather-settings.bmp") && weatherOk;
        for(int module=0;module<WeatherModuleCount;++module) { First(WeatherKind).weather.display[module]=false; SettingChanged(); Load(); weatherOk=!First(WeatherKind).weather.display[module] && weatherOk; }
        First(WeatherKind).bands.header=false; SettingChanged();
        weatherOk=!IsWindowVisible(First(4).window) && BaseHeight(First(4))==40 && weatherOk;
        First(WeatherKind).weather.display.fill(true); SettingChanged(); PreviewWeather(); UpdateOverlays();
        weatherOk=IsWindowVisible(First(4).window) && BaseHeight(First(4))==84+36+62+76+108+32+32 && weatherOk;
        First(WeatherKind).bands.header=true; First(WeatherKind).bands.footer.enabled=true; SettingChanged();
        weatherOk=BaseHeight(First(4))==32+84+36+62+76+108+32+32+28 && Export(First(4).window,L"build\\qa\\weather-header-footer.bmp") && weatherOk;
        First(WeatherKind).bands=DefaultBands(WeatherKind); SettingChanged();
        weatherOk=Export(First(4).window,L"build\\qa\\weather-all.bmp") && weatherOk;
        // Only the wind rose: the overlay narrows to the ring.
        const auto allModules=First(WeatherKind).weather.display;
        First(WeatherKind).weather.display.fill(false); First(WeatherKind).weather.display[WindModule]=First(WeatherKind).weather.display[WindRose]=First(WeatherKind).weather.display[WindRelative]=true; First(WeatherKind).bands.header=false; SettingChanged(); Load();
        weatherOk=WindRoseOnly(First(WeatherKind)) && OverlayWidth(First(4))==WindRoseWidth && BaseHeight(First(4))==108 && Export(First(4).window,L"build\\qa\\weather-wind-rose.bmp") && weatherOk;
        First(WeatherKind).weather.display[WindRose]=false; First(WeatherKind).weather.display[WindText]=true; SettingChanged();
        weatherOk=!WindRoseOnly(First(WeatherKind)) && OverlayWidth(First(4))==WeatherPanelWidth && BaseHeight(First(4))==76 && Export(First(4).window,L"build\\qa\\weather-wind-text.bmp") && weatherOk;
        First(WeatherKind).weather.display=allModules; First(WeatherKind).bands=DefaultBands(WeatherKind); SettingChanged();
        First(WeatherKind).bands.header=false; First(WeatherKind).weather.strip=true; SettingChanged(); Load();
        weatherOk=First(WeatherKind).weather.strip && OverlayWidth(First(4))==WeatherStripCount(First(WeatherKind))*WeatherStripCell && Export(First(4).window,L"build\\qa\\weather-strip.bmp") && weatherOk;
        // The page marks what the strip leaves out.
        OpenOverlay(First(WeatherKind));
        if(const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr)) for(const auto& row:reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA))->Rows()) {
            if(row.title==WeatherModules[DetailsModule].title) weatherOk=row.pill==L"Panel only" && weatherOk;
            if(row.title==WeatherModules[RainModule].title) weatherOk=row.pill.empty() && weatherOk;
            // No arrow beyond either end of the order.
            if(row.title==WeatherModules[ConditionModule].title) weatherOk=row.iconButtons.size()==3 && row.iconButtons[1] && !row.iconButtons[2] && weatherOk;
            if(row.title==WeatherModules[UsageModule].title) weatherOk=row.iconButtons.size()==3 && !row.iconButtons[1] && row.iconButtons[2] && weatherOk;
        } else weatherOk=false;
        First(WeatherKind).weather.strip=false; First(WeatherKind).bands=DefaultBands(WeatherKind); SettingChanged();
        report<<"Weather modules, strip layout, persistence and all-hidden visibility: "<<(weatherOk?"PASS":"FAIL")<<"\n"; ok=weatherOk && ok;
    }
    {
        // A second Weather reduced to the wind rose, a second Relative with defaults; both survive a reload and go again.
        bool instancesOk=true;
        auto& wind=NewInstance(WeatherKind);
        instancesOk=InstanceCount(WeatherKind)==2 && wind.number==2 && Name(wind)==L"Weather 2" && wind.window && wind.place.enabled && instancesOk;
        wind.weather.display.fill(false); wind.weather.display[WindModule]=wind.weather.display[WindRose]=true; wind.bands.header=false; SettingChanged();
        instancesOk=OverlayWidth(wind)==WindRoseWidth && OverlayWidth(First(WeatherKind))==WeatherPanelWidth && instancesOk;
        auto& second=NewInstance(RelativeKind);
        instancesOk=second.table.columnOrder==DefaultColumns[RelativeKind] && second.table.rows==7 && !Rows(second).empty() && instancesOk;
        Load(); instancesOk=InstanceCount(WeatherKind)==2 && InstanceCount(RelativeKind)==2 && WindRoseOnly(wind) && instancesOk;
        PreviewWeather(); UpdateOverlays();
        instancesOk=Export(wind.window,L"build\\qa\\weather-2-wind-rose.bmp") && instancesOk;
        ShowPage(Page::Overlays); instancesOk=Export(app.main,L"build\\qa\\main-instances.bmp") && instancesOk;
        // Removing an added instance takes its settings section along.
        RemoveInstance(second); RemoveInstance(wind);
        wchar_t left[8]{}; GetPrivateProfileStringW(L"Weather 2",L"X",L"",left,8,app.config.c_str());
        instancesOk=InstanceCount(WeatherKind)==1 && InstanceCount(RelativeKind)==1 && !left[0] && instancesOk;
        Load(); instancesOk=InstanceCount(WeatherKind)==1 && InstanceCount(RelativeKind)==1 && instancesOk;
        report<<"Further instances: defaults, own size, persistence, removal of settings: "<<(instancesOk?"PASS":"FAIL")<<"\n"; ok=instancesOk && ok;
    }
    {
        // Layouts: a copy starts with the same overlays and keeps its own places and settings; switching brings
        // each back, also after a reload; conditions switch once a situation held for a second; a layout chosen
        // by hand stays until the situation changes; removal takes its overlays' sections along.
        bool layoutOk=true;
        const auto savedFrame=app.frame; const auto savedSession=app.session; const bool savedPreview=app.preview; const ULONGLONG savedNow=app.now;
        const Placement place=First(RelativeKind).place; const int rows=First(RelativeKind).table.rows;
        AddLayout(true); const int second=app.layout;
        layoutOk=app.layouts.size()==2 && second!=1 && First(RelativeKind).place.x==place.x && First(RelativeKind).table.rows==rows && layoutOk;
        First(RelativeKind).place.x+=200; First(RelativeKind).table.rows=5; Fit(First(RelativeKind)); SettingChanged();
        auto& extra=NewInstance(WeatherKind); (void)extra; layoutOk=InstanceCount(WeatherKind)==2 && layoutOk;
        SwitchLayout(1);
        layoutOk=app.layout==1 && First(RelativeKind).place.x==place.x && First(RelativeKind).table.rows==rows && InstanceCount(WeatherKind)==1 && layoutOk;
        RECT window{}; GetWindowRect(First(RelativeKind).window,&window); layoutOk=window.left==place.x && layoutOk;
        SwitchLayout(second);
        layoutOk=First(RelativeKind).place.x==place.x+200 && First(RelativeKind).table.rows==5 && InstanceCount(WeatherKind)==2 && layoutOk;
        Load(); layoutOk=app.layout==second && app.layouts.size()==2 && First(RelativeKind).place.x==place.x+200 && InstanceCount(WeatherKind)==2 && layoutOk;
        // The second layout in the car, ahead of the default for everything else.
        for(auto& l:app.layouts) if(l.id==second) l.condition=InCarCondition;
        std::swap(app.layouts[0],app.layouts[1]); SwitchLayout(1);
        app.preview=false; app.frame.connected=app.frame.onTrack=true; app.frame.replay=app.frame.garage=false; app.layoutSituation=app.pendingSituation=-1;
        app.now=1000000; AutoLayout(); app.now+=500; AutoLayout(); layoutOk=app.layout==1 && layoutOk;
        app.now+=600; AutoLayout(); layoutOk=app.layout==second && layoutOk;
        app.frame.onTrack=false; app.now+=100; AutoLayout(); app.now+=1100; AutoLayout(); layoutOk=app.layout==1 && layoutOk;
        SwitchLayout(second); app.now+=5000; AutoLayout(); layoutOk=app.layout==second && layoutOk;
        app.frame.garage=true; app.now+=100; AutoLayout(); app.now+=1100; AutoLayout(); layoutOk=app.layout==1 && layoutOk;
        ShowPage(Page::Overlays); layoutOk=Export(app.main,L"build\\qa\\main-layouts.bmp") && layoutOk;
        RemoveLayout(second);
        wchar_t left[8]{}; GetPrivateProfileStringW((L"Layout "+std::to_wstring(second)+L": Relative").c_str(),L"X",L"",left,8,app.config.c_str());
        layoutOk=app.layouts.size()==1 && app.layout==1 && !left[0] && InstanceCount(WeatherKind)==1 && First(RelativeKind).place.x==place.x && layoutOk;
        Load(); layoutOk=app.layouts.size()==1 && layoutOk;
        app.frame=savedFrame; app.session=savedSession; app.preview=savedPreview; app.now=savedNow; app.layoutSituation=app.pendingSituation=-1; UpdateOverlays();
        report<<"Layouts: own overlays, places and settings, switching, reload, conditions, manual choice and removal: "<<(layoutOk?"PASS":"FAIL")<<"\n"; ok=layoutOk && ok;
    }
    {
        // The Safety and iRating badge beside License and iRating, over drivers of every license class.
        auto& relative=First(RelativeKind); const auto savedTable=relative.table; const auto savedDrivers=app.session.drivers;
        const char* licenses[]={"A 3.45","B 2.81","C 3.12","D 1.97","R 2.50","A 4.20","B 3.66","C 2.04"}; const int ratings[]={3120,2215,1640,1105,860,4870,2790,1390};
        for(size_t i=0;i<app.session.drivers.size();++i) { app.session.drivers[i].license=licenses[i%8]; app.session.drivers[i].rating=ratings[i%8]; }
        relative.table.driverColumns[0]=relative.table.driverColumns[1]=true; relative.table.extraColumns[CombinedRatingColumn-BrandColumn]=true;
        relative.table.columnOrder={PositionColumn,NumberColumn,DriverColumn,LicenseColumn,IRatingColumn,CombinedRatingColumn,GapColumn};
        InvalidateModel(); Fit(relative); UpdateOverlays();
        const bool shot=Export(relative.window,L"build\\qa\\relative-ratings.bmp");
        relative.table=savedTable; app.session.drivers=savedDrivers; InvalidateModel(); Fit(relative); UpdateOverlays();
        report<<"Relative with License, iRating and Safety and iRating exported: "<<(shot?"PASS":"FAIL")<<"\n"; ok=shot && ok;
    }
    {
        // At the narrowest window every editor of an expanded row lies inside the visible list: the overlay's
        // size row, and header and footer of a table and of Fuel and Weather.
        bool fitOk=true; WINDOWPLACEMENT saved{sizeof(saved)}; GetWindowPlacement(app.main,&saved);
        ShowWindow(app.main,SW_RESTORE); SetWindowPos(app.main,nullptr,0,0,1,1,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE); // Clamped to the minimum size.
        auto Ascii=[](const std::wstring& text){ std::string ascii; for(wchar_t c:text) ascii+=static_cast<char>(c<128?c:'?'); return ascii; };
        auto check=[&](const std::wstring& title,int expected=8) {
            const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
            auto* rows=list?reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA)):nullptr; int index=-1;
            if(rows) for(size_t i=0;i<rows->Rows().size() && index<0;++i) if(rows->Rows()[i].title==title) index=static_cast<int>(i);
            if(index<0) { fitOk=false; report<<"  no row "<<Ascii(title)<<"\n"; return; }
            rows->PressIconButton(index,0);
            RECT visible{},host{}; GetClientRect(list,&visible); MapWindowPoints(list,nullptr,reinterpret_cast<POINT*>(&visible),2);
            GetClientRect(ui.host.Handle(),&host); MapWindowPoints(ui.host.Handle(),nullptr,reinterpret_cast<POINT*>(&host),2);
            visible.right=std::min(visible.right,host.right); int editors=0;
            for(HWND child=GetWindow(list,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if(IsWindowVisible(child)) {
                RECT r{}; GetWindowRect(child,&r); ++editors;
                if(r.left<visible.left || r.right>visible.right) { fitOk=false; report<<"  "<<Ascii(title)<<": editor ends at "<<r.right-visible.left<<" px of "<<visible.right-visible.left<<"\n"; }
            }
            if(editors!=expected) { fitOk=false; report<<"  "<<Ascii(title)<<": "<<editors<<" editors\n"; }
            rows->PressIconButton(index,0); // And collapsed again.
        };
        // The overlays, and a layout with its name and condition.
        ShowPage(Page::Overlays); for(auto& o:app.overlays) if(o.number==1) check(Name(o));
        check(ActiveLayout()->name,4);
        for(int kind:{RelativeKind,FuelKind,WeatherKind}) { OpenOverlay(First(kind)); check(L"Header"); check(L"Footer"); }
        SetWindowPlacement(app.main,&saved); ShowPage(Page::Overlays);
        report<<"Expanded editors fit the narrowest window: "<<(fitOk?"PASS":"FAIL")<<"\n"; ok=fitOk && ok;
    }
    {
        // Shortcuts change in their row: click, press a combination; Esc cancels, Backspace turns it off, the reset arrow
        // appears only on a changed one and restores the default.
        bool keysOk=true; const auto savedKeys=app.hotkeys;
        ShowPage(Page::Settings);
        const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
        auto* rows=list?reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA)):nullptr;
        const HWND field=FindWindowExW(ui.host.Content(),nullptr,HOTKEY_CLASSW,nullptr);
        auto row=[&](const wchar_t* title){ if(rows) for(int i=0;i<static_cast<int>(rows->Rows().size());++i) if(rows->Rows()[i].title==title) return i; return -1; };
        const int move=row(L"Move overlays");
        keysOk=rows && field && move>=0 && row(L"Open settings")>=0 && rows->Rows()[move].button==HotkeyText(DefaultHotkeys[0]) && rows->Rows()[move].iconButtons==std::vector<wchar_t>{0} && keysOk;
        if(keysOk) {
            rows->ScrollIntoView(row(L"Next event in Race Control"));
            rows->PressButton(move); keysOk=GetFocus()==field && rows->Rows()[move].button.find(L"Press")==0 && keysOk;
            keysOk=Export(app.main,L"build\\qa\\main-shortcuts.bmp") && keysOk;
            const WORD chosen=static_cast<WORD>(((HOTKEYF_CONTROL|HOTKEYF_ALT)<<8)|'J');
            SendMessageW(field,HKM_SETHOTKEY,chosen,0); SendMessageW(ui.host.Content(),WM_COMMAND,MAKEWPARAM(200,EN_CHANGE),reinterpret_cast<LPARAM>(field));
            keysOk=app.hotkeys[0]==chosen && GetFocus()!=field && rows->Rows()[move].button==HotkeyText(chosen) && rows->Rows()[move].iconButtons[0]==Glyph::Reset && keysOk;
            rows->PressButton(move); SendMessageW(field,WM_KEYDOWN,VK_ESCAPE,0); keysOk=app.hotkeys[0]==chosen && GetFocus()!=field && keysOk;
            rows->PressButton(move); SendMessageW(field,WM_KEYDOWN,VK_BACK,0); keysOk=app.hotkeys[0]==0 && rows->Rows()[move].button==L"Off" && keysOk;
            rows->PressIconButton(move,0); keysOk=app.hotkeys[0]==DefaultHotkeys[0] && rows->Rows()[move].iconButtons[0]==0 && keysOk;
        }
        app.hotkeys=savedKeys; RegisterHotkeys(); Save(); ShowPage(Page::Overlays);
        report<<"Shortcuts change, cancel, turn off and reset in their row: "<<(keysOk?"PASS":"FAIL")<<"\n"; ok=keysOk && ok;
    }
    {
        // Design rule: from the right expand, sort arrows, toggle, then a text button, each in one column per group;
        // a row with only a text button (Add, Reset) has it at the far right.
        bool alignOk=true; int toggles=0;
        auto aligned=[&](const char* page) {
            const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
            auto* rows=list?reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA)):nullptr;
            if(!rows) { alignOk=false; report<<"  no list on "<<page<<"\n"; return; }
            LONG column=-1,buttons=-1,rightmost=0;
            const int count=static_cast<int>(rows->Rows().size());
            for(int i=0;i<count;++i) { const RECT toggle=rows->ToggleRect(i); rightmost=std::max(rightmost,toggle.right); }
            for(int i=0;i<count;++i) {
                const auto& row=rows->Rows()[i];
                if(row.header) { column=buttons=-1; continue; }
                const RECT toggle=rows->ToggleRect(i), button=rows->ButtonRect(i);
                if(toggle.right>toggle.left) {
                    ++toggles;
                    if(column<0) column=toggle.right;
                    else if(toggle.right!=column) { alignOk=false; report<<"  "<<page<<": toggle in row "<<i<<" ends at "<<toggle.right<<", others at "<<column<<"\n"; }
                    if(button.right>button.left && button.right>toggle.left) { alignOk=false; report<<"  "<<page<<": button in row "<<i<<" is not left of its toggle\n"; }
                }
                if(button.right<=button.left) continue;
                if(row.toggle<0 && row.iconButtons.empty()) {
                    // Only a button: at the far right, past every toggle.
                    if(button.right<rightmost) { alignOk=false; report<<"  "<<page<<": button-only row "<<i<<" is not at the far right\n"; }
                    continue;
                }
                if(buttons<0) buttons=button.right;
                else if(button.right!=buttons) { alignOk=false; report<<"  "<<page<<": button in row "<<i<<" ends at "<<button.right<<", others at "<<buttons<<"\n"; }
            }
        };
        ShowPage(Page::Overlays); aligned("Overlays");
        ShowPage(Page::Settings); aligned("Settings");
        constexpr const char* pages[]={"Relative","Standings","Fuel Calculator","Pit Limiter","Weather","Track Map"};
        for(int kind=0;kind<OverlayCount;++kind) { OpenOverlay(First(kind)); aligned(pages[kind]); }
        ShowPage(Page::Overlays);
        report<<"Expand, sort arrows, toggle and button in order and in columns in every settings list: "<<(alignOk && toggles>40?"PASS":"FAIL")<<"\n"; ok=alignOk && toggles>40 && ok;
    }
    {
        // Every telemetry and overlay rate combination keeps its rate over a minute of jittered ticks, and a pause restarts without a burst.
        bool ratesOk=true;
        for(int telemetry:{1,2,5,10,15,20}) for(int rate:OverlayRates) {
            const int timer=1000/std::max(telemetry,rate), period=1000/rate; ULONGLONG due=0; int count=0;
            for(int tick=0;tick*timer<60000;++tick) { if(RefreshDue(due,static_cast<ULONGLONG>(tick*timer+(tick*7919)%(timer/4+1)),period,timer/2)) ++count; }
            const double hz=count/60.0;
            if(std::abs(hz-1000.0/period)>.03*rate) { ratesOk=false; report<<"  rate "<<rate<<" Hz on a "<<timer<<" ms timer: "<<hz<<" Hz\n"; }
            // Five seconds without ticks: the next tick refreshes once and the following ticks keep the rate.
            const ULONGLONG resume=60000+5000; int burst=0;
            for(int tick=0;tick<3;++tick) burst+=RefreshDue(due,resume+tick,period,timer/2);
            ratesOk=burst==1 && ratesOk;
        }
        // A background rate saved below 5 Hz, from earlier versions, loads as 5 Hz.
        const int savedRate=app.rate; WritePrivateProfileStringW(L"General",L"RefreshHz",L"2",app.config.c_str()); Load();
        ratesOk=app.rate==5 && ratesOk; app.rate=savedRate; Save(); Timer();
        report<<"Overlay rates on every telemetry rate, with jitter and pauses: "<<(ratesOk?"PASS":"FAIL")<<"\n"; ok=ratesOk && ok;
    }
    {
        // Rings outside the overlay: red on the one whose page is open, dashed yellow while moving; none on the Pit Limiter; square corners on request.
        bool bordersOk=true; auto& relative=First(RelativeKind);
        auto ringAround=[](const Overlay& o) {
            RECT inner{},outer{}; GetWindowRect(o.window,&inner); GetWindowRect(o.frame,&outer); const int thick=MulDiv(FrameWidth,GetDpiForWindow(o.window),96);
            return o.frame && IsWindowVisible(o.frame) && outer.left==inner.left-thick && outer.top==inner.top-thick && outer.right==inner.right+thick && outer.bottom==inner.bottom+thick;
        };
        OpenOverlay(relative); UpdateWindow(app.main);
        bordersOk=Editing(relative) && !Editing(First(StandingsKind)) && FrameStyleOf(relative)==EditingFrame && ringAround(relative) && bordersOk;
        bordersOk=(!First(StandingsKind).frame || !IsWindowVisible(First(StandingsKind).frame)) && Export(relative.frame,L"build\\qa\\border-editing.bmp") && bordersOk;
        ShowPage(Page::Overlays); bordersOk=!Editing(relative) && !IsWindowVisible(relative.frame) && bordersOk;
        app.edit=true; UpdateOverlays(); bordersOk=FrameStyleOf(relative)==MovingFrame && ringAround(relative) && ringAround(First(StandingsKind)) && Export(relative.frame,L"build\\qa\\border-moving.bmp") && Export(relative.window,L"build\\qa\\border-overlay.bmp") && bordersOk;
        app.edit=false; UpdateOverlays(); bordersOk=!IsWindowVisible(relative.frame) && bordersOk;
        bordersOk=Export(First(PitLimiterKind).window,L"build\\qa\\border-pit-limiter.bmp") && bordersOk;
        relative.place.rounded=false; Fit(relative); SettingChanged(); Load(); bordersOk=!relative.place.rounded && bordersOk;
        bordersOk=Export(relative.window,L"build\\qa\\border-square.bmp") && bordersOk;
        relative.place.rounded=true; Fit(relative); SettingChanged();
        report<<"Overlay borders while editing and moving, Pit Limiter without border, square corners: "<<(bordersOk?"PASS":"FAIL")<<"\n"; ok=bordersOk && ok;
    }
    {
        const auto rosterSession=app.session; const auto rosterFrame=app.frame; const auto rosterOptions=First(StandingsKind).table.standings;
        app.session.type="Qualify"; app.session.results={}; for(auto& car:app.frame.cars) car=Car{};
        app.frame.camera=-1; app.frame.onTrack=true; First(StandingsKind).table.standings.ownClass=false; First(StandingsKind).table.standings.leaders=MaxCars;
        UpdateOverlays(); bool rosterOk=!Rows(First(1)).empty() && Rows(First(0)).size()==1 && Rows(First(0))[0].gap==0 && Rows(First(0))[0].car.position==0;
        Export(First(0).window,L"build\\qa\\relative-missing-progress.bmp"); Export(First(1).window,L"build\\qa\\qualifying-untimed.bmp");
        First(StandingsKind).table.standings.fallbackByRating=true; SettingChanged();
        Load(); rosterOk=First(StandingsKind).table.standings.fallbackByRating && rosterOk;
        app.session=rosterSession; app.frame=rosterFrame; First(StandingsKind).table.standings=rosterOptions; Save(); UpdateOverlays();
        report<<"Untimed qualifying roster, relative missing progress and sort preference persistence: "<<(rosterOk?"PASS":"FAIL")<<"\n"; ok=rosterOk && ok;
        const bool savedGained=First(StandingsKind).table.gained; const auto gainedGrid=app.session.gridPositions;
        First(StandingsKind).table.gained=false; Save(); const int gainedWidth=OverlayWidth(First(1));
        SetColumnVisible(First(StandingsKind),GainedColumn,true);
        Load(); bool gainedOk=First(StandingsKind).table.gained && ColumnVisible(First(1),14) && !ColumnVisible(First(0),14) && OverlayWidth(First(1))==gainedWidth+ColumnWidth(First(1),14);
        for(const auto& driver:app.session.drivers) app.session.gridPositions[driver.index]=driver.index+2;
        app.session.gridPositions[0]=3; app.session.gridPositions[1]=2;
        app.frame.cars[2].towing=true; UpdateOverlays();
        Export(First(1).window,L"build\\qa\\standings-gained-tow.bmp");
        app.frame.cars[2].towing=false;
        First(StandingsKind).table.gained=savedGained; app.session.gridPositions=gainedGrid; Save();
        report<<"Standings gained column settings: "<<(gainedOk?"PASS":"FAIL")<<"\n"; ok=gainedOk && ok;
        const auto savedSession=app.session; const auto savedFrame=app.frame; const auto savedOrder=First(1).table.columnOrder; const bool savedLaps=First(StandingsKind).table.laps;
        First(StandingsKind).table.laps=false; Save(); UpdateOverlays(); const int before=OverlayWidth(First(1));
        SetColumnVisible(First(StandingsKind),LapsColumn,true);
        Load(); bool lapsOk=First(StandingsKind).table.laps && OverlayWidth(First(1))==before+ColumnWidth(First(1),13) && ColumnVisible(First(1),13) && !ColumnVisible(First(0),13);
        Row row; row.driver.index=0; row.car.completed=5; row.car.lap=7; lapsOk=CompletedLaps(row)==L"5" && lapsOk;
        row.car.completed=-1; row.car.lap=1; lapsOk=CompletedLaps(row)==L"0" && lapsOk;
        row.car.lap=-1; app.session.number=app.frame.session; app.session.results[0].completed=9; lapsOk=CompletedLaps(row)==L"9" && lapsOk;
        app.session.number=app.frame.session+1; lapsOk=CompletedLaps(row)==L"—" && lapsOk;
        app.session=savedSession;
        app.frame.cars[0].completed=6; app.frame.cars[0].lap=7;
        UpdateOverlays(); lapsOk=Export(First(1).window,L"build\\qa\\standings-laps.bmp") && lapsOk;
        OpenOverlay(First(StandingsKind)); lapsOk=Export(app.main,L"build\\qa\\standings-laps-settings.bmp") && lapsOk;
        const int index=static_cast<int>(std::find(First(1).table.columnOrder.begin(),First(1).table.columnOrder.end(),LapsColumn)-First(1).table.columnOrder.begin());
        for(int step=0;step<index;++step) MoveInOrder(First(1).table.columnOrder,LapsColumn,-1);
        Load(); lapsOk=First(1).table.columnOrder.front()==13 && First(StandingsKind).table.laps && lapsOk;
        lapsOk=Export(First(1).window,L"build\\qa\\standings-laps-first.bmp") && lapsOk;
        SetColumnVisible(First(StandingsKind),LapsColumn,false);
        lapsOk=!First(StandingsKind).table.laps && OverlayWidth(First(1))==before && lapsOk;
        app.session=savedSession; app.frame=savedFrame; First(1).table.columnOrder=savedOrder; First(StandingsKind).table.laps=savedLaps; Save(); UpdateOverlays();
        report<<"Standings completed laps: toggle, width, persistence, column order, live count, zero laps, session fallback and missing data: "<<(lapsOk?"PASS":"FAIL")<<"\n"; ok=lapsOk && ok;
    }
    {
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        for(int kind:{1,4}) {
            auto& overlay=First(kind); InvalidateModel(); RefreshOverlay(overlay);
            const DWORD handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
            for(int pass=0;pass<300;++pass) { overlay.scene.clear(); InvalidateModel(); RefreshOverlay(overlay); }
            GdiFlush(); QueryPerformanceCounter(&end);
            report<<"Full scene + paint benchmark kind "<<kind<<": "<<(end.QuadPart-begin.QuadPart)*1000000.0/frequency.QuadPart/300<<" us; GDI handle delta "<<(static_cast<int>(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))-static_cast<int>(handles))<<"\n";
        }
    }
    {
        const auto savedFrame=app.frame; const auto savedSession=app.session; const int savedRows=First(RelativeKind).table.rows; const bool savedHide=First(RelativeKind).table.hidePit;
        bool centerOk=true;
        First(RelativeKind).table.rows=9; First(RelativeKind).table.hidePit=true; app.session.type="Practice";
        for(const auto& driver:app.session.drivers) if(driver.index!=app.frame.player) app.frame.cars[driver.index].surface=-1;
        app.frame.cars[app.frame.player].pit=true;
        UpdateOverlays(); centerOk=Export(First(0).window,L"build\\qa\\relative-centered-alone.bmp") && centerOk;
        auto& overlay=First(0); const int centerDpi=GetDpiForWindow(overlay.window),factor=centerDpi*overlay.place.scale;
        const int midY=MulDiv(TableRowsTop(First(0))+(First(RelativeKind).table.rows/2)*27+13-(ShowOverlayHeader(First(0))?0:32),factor,9600);
        centerOk=GetPixel(overlay.dc,MulDiv(8,factor,9600),midY)==Ink::OwnRow && centerOk;
        app.frame=savedFrame; app.session=savedSession; app.session.type="Practice";
        app.frame.incidents=18; app.frame.incidentsKnown=true; app.session.incidentLimit=25; First(0).bands.footer.metrics={1,0,2};
        UpdateOverlays(); centerOk=FooterValue(2)==L"18x" && centerOk;
        centerOk=Export(First(0).window,L"build\\qa\\relative-practice-colors.bmp") && centerOk;
        First(RelativeKind).table.rows=savedRows; First(RelativeKind).table.hidePit=savedHide; app.frame=savedFrame; app.session=savedSession; Save(); UpdateOverlays();
        report<<"Relative center with empty slots and reference in pit; practice footer has incidents without race limit: "<<(centerOk?"PASS":"FAIL")<<"\n"; ok=centerOk && ok;
    }
    const double savedCapacity=app.session.fuelCapacity,savedFuel=app.frame.fuel;
    app.session.fuelCapacity=100; UpdateOverlays();
    auto fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption,Plan().prediction.ownPace);
    ok=fuelSummary.openNow && fuelSummary.timeEmpty>0 && fuelSummary.fuelAtEnd<0 && ok;
    ok=Export(First(2).window,L"build\\qa\\fuel-open.bmp") && ok;
    {
        // The footer adds its band below the target rows.
        const int before=BaseHeight(First(FuelKind));
        First(FuelKind).bands.footer.enabled=true; SettingChanged();
        ok=BaseHeight(First(FuelKind))==before+FooterHeight(First(FuelKind)) && Export(First(2).window,L"build\\qa\\fuel-footer.bmp") && ok;
        First(FuelKind).bands.footer.enabled=false; SettingChanged();
        // Pit window below the timeline: refuel and pit window split into two rows; the order survives a reload.
        auto& fuel=First(FuelKind); const int paired=BaseHeight(fuel);
        ok=MoveInOrder(fuel.fuel.order,WindowCard,1) && BaseHeight(fuel)==paired+92 && ok;
        MoveInOrder(fuel.fuel.order,TankModule,2); Load();
        ok=First(FuelKind).fuel.order==std::vector<int>{LeaderDistanceModule,FiguresModule,TankModule,WhiteFlagTimeModule,HistoryModule,RefuelCard,TimelineModule,WindowCard,PitExitModule,OneShorterRow,TargetRow,OneLongerRow,TwoLongerRow} && BaseHeight(First(FuelKind))==paired+92 && ok;
        ok=Export(First(FuelKind).window,L"build\\qa\\fuel-reordered.bmp") && ok;
        auto& weather=First(WeatherKind); const int weatherHeight=BaseHeight(weather);
        ok=MoveInOrder(weather.weather.order,ConditionModule,3) && BaseHeight(weather)==weatherHeight && ok;
        Load(); ok=First(WeatherKind).weather.order[3]==ConditionModule && Export(First(WeatherKind).window,L"build\\qa\\weather-reordered.bmp") && ok;
        First(FuelKind).fuel.order=FuelOrder; First(WeatherKind).weather.order=WeatherOrder; SettingChanged();
        // Caution marks: off by default and switched inside the expanded "Last 10 laps" row, not as a row of their own.
        ok=!FuelModules[CautionMarks].on && !FuelModules[LeaderDistanceModule].on && !FuelModules[WhiteFlagTimeModule].on && ok; // Leader rows start off too.
        OpenOverlay(First(FuelKind));
        if(const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr)) {
            auto* rows=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA)); int history=-1;
            for(size_t i=0;i<rows->Rows().size();++i) {
                ok=rows->Rows()[i].title!=FuelModules[CautionMarks].title && ok;
                if(rows->Rows()[i].title==FuelModules[HistoryModule].title) history=static_cast<int>(i);
            }
            ok=history>=0 && ok;
            if(history>=0) {
                rows->PressIconButton(history,0); bool check=false;
                for(HWND child=GetWindow(list,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
                    wchar_t text[64]{}; GetWindowTextW(child,text,64);
                    check=check || (IsWindowVisible(child) && std::wstring(text)==FuelModules[CautionMarks].title);
                }
                ok=check && ok; rows->PressIconButton(history,0);
            }
        } else ok=false;
    }
    app.frame.fuel=50; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.stops==0 && fuelSummary.fuelAtEnd>0 && ok;
    ok=Export(First(2).window,L"build\\qa\\fuel-surplus.bmp") && ok;
    {
        // With no stop needed: the fuel left at the finish in the card, the tank bar's legend, the header pill and its own metric.
        auto& fuel=First(FuelKind); const auto window=FuelWindow();
        auto shows=[&](const std::wstring& text){ return std::any_of(fuel.scene.begin(),fuel.scene.end(),[&](const DrawCommand& c){ return c.type==DrawCommand::Text && c.text.find(text)!=std::wstring::npos; }); };
        const auto spare=SignedLitres(window.fuelAtEnd);
        bool finishOk=window.status==PitWindowStatus::NoStopNeeded && spare.rfind(L"+",0)==0 && shows(L"AT FINISH") && shows(L"laps spare") && shows(L"Spare ") && !shows(L"REFUEL TO FINISH");
        finishOk=FooterValue(StopsNeededMetric)==L"NO STOP \u00B7 "+spare && FooterValue(FuelAtFinishMetric)==L"At finish "+spare && finishOk;
        // 1.2 L short over 12.6 laps: saving 0.09 L a lap instead of a stop is shown under the refuel.
        app.frame.fuel=38; UpdateOverlays();
        finishOk=shows(L"REFUEL TO FINISH") && shows(L"or save 0.09 L/lap") && Export(fuel.window,L"build\\qa\\fuel-save.bmp") && finishOk;
        app.frame.fuel=50; UpdateOverlays();
        report<<"Fuel at the finish: card, tank bar, header pill and metric: "<<(finishOk?"PASS":"FAIL")<<"\n"; ok=finishOk && ok;
    }
    app.session.fuelCapacity=14; app.frame.fuel=12; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.stops==3 && ok;
    ok=Export(First(2).window,L"build\\qa\\fuel-three-stops.bmp") && ok;
    app.session.fuelCapacity=-1; UpdateOverlays();
    fuelSummary=CalculatePitWindow(app.session,app.frame,Plan().lapsToFinish,Plan().estimates[5].consumption);
    ok=fuelSummary.status==PitWindowStatus::Unavailable && !fuelSummary.openNow && ok;
    ok=Export(First(2).window,L"build\\qa\\fuel-window-unknown.bmp") && ok;
    app.session.fuelCapacity=savedCapacity; app.frame.fuel=savedFuel; UpdateOverlays();
    report<<"Fuel time empty, signed finish fuel, open/closed text and three-stop UI: "<<(ok?"PASS":"FAIL")<<"\n";
    for(int kind=0;kind<OverlayCount;++kind) {
        OpenOverlay(First(kind));
        ok=ui.page==Page::Overlay && ui.overlay==&First(kind) && ok;
        ok=Export(app.main,L"build\\qa\\settings-"+std::to_wstring(kind)+L".bmp")&&ok;
        if(kind==RelativeKind) {
            First(RelativeKind).table.rows=RelativeRowCount(6); SettingChanged();
            ok=First(RelativeKind).table.rows==7 && ok;
        } else if(kind==StandingsKind) {
            const int originalWidth=OverlayWidth(First(1)); First(StandingsKind).table.interval=false; Fit(First(1));
            SetColumnVisible(First(StandingsKind),IntervalColumn,true);
            ok=First(StandingsKind).table.interval && OverlayWidth(First(1))>=originalWidth && ok;
            First(StandingsKind).table.standings.liveGaps=true; SettingChanged();
            ok=First(StandingsKind).table.standings.liveGaps && ok; Save(); Load(); ok=First(StandingsKind).table.standings.liveGaps && ok;
            First(StandingsKind).table.standings.liveGaps=false; SettingChanged();
            ok=!First(StandingsKind).table.standings.liveGaps && ok;
            ok=Export(First(1).window,L"build\\qa\\standings-interval.bmp") && ok;
            First(StandingsKind).table.standings.leaders=2; First(StandingsKind).table.standings.ahead=1; First(StandingsKind).table.standings.behind=1; First(StandingsKind).table.standings.separateClasses=false; SettingChanged();
            const auto groups=SelectStandings(app.session,app.frame,First(StandingsKind).table.standings);
            ok=(!First(StandingsKind).table.standings.separateClasses && groups.size()==1 && groups[0].rows.size()==5 && First(RelativeKind).table.rows==7)&&ok;
            ok=Export(First(1).window,L"build\\qa\\standings-combined.bmp")&&ok;
            First(StandingsKind).table.standings.ownClass=true; SettingChanged();
            ok=First(StandingsKind).table.standings.ownClass && Rows(First(0)).size()<=7 && ok;
        } else if(kind==FuelKind) {
            First(FuelKind).fuel.figures[1]=false; SettingChanged(); ok=!First(FuelKind).fuel.figures[1] && ok;
            for(int i=0;i<FuelModuleCount;++i) { First(FuelKind).fuel.display[i]=false; SettingChanged(); }
            ok=HeightWithTitle(First(2))==32+40 && OverlayWidth(First(2))==FuelPanelWidth && ok;
            Save(); Load();
            ok=std::none_of(First(FuelKind).fuel.display.begin(),First(FuelKind).fuel.display.end(),[](bool value){return value;}) && ok;
            ok=Export(First(2).window,L"build\\qa\\fuel-compact.bmp") && ok;
            First(FuelKind).fuel.display.fill(true); SettingChanged();
            ok=HeightWithTitle(First(2))==32+112+52+104+92+46+16+4*24+3*32 && BaseHeight(First(2))==HeightWithTitle(First(2)) && ok;
            ok=Export(First(2).window,L"build\\qa\\fuel-all.bmp") && ok;
            First(FuelKind).fuel.strip=true; SettingChanged(); ok=Export(First(2).window,L"build\\qa\\fuel-strip.bmp") && ok;
            First(FuelKind).fuel.strip=false; SettingChanged();
            const auto savedFrame=app.frame; const double savedDistance=app.fuelPlan.lapsToFinish;
            app.frame.lapCompleted=7; app.frame.lapPct=.2; app.fuelPlan.lapsToFinish=12.02;
            ok=FuelFinishLap()==20 && ok;
            app.frame.lapPct=.99; ok=FuelFinishLap()==21 && ok;
            app.fuelPlan.lapsToFinish=0; ok=FuelFinishLap()==8 && ok;
            app.fuelPlan.lapsToFinish=-1; ok=FuelFinishLap()==-1 && ok;
            app.frame=savedFrame; app.fuelPlan.lapsToFinish=savedDistance;
        }
        if(IsTable(kind)) {
            SetColumnVisible(First(kind),PitStopColumn,true);
            ok=First(kind).table.driverColumns[5] && DriverValue(Rows(First(kind)).front(),5).find(L" s")!=std::wstring::npos && ok;
            ok=Export(First(kind).window,L"build\\qa\\pit-stop-"+std::to_wstring(kind)+L".bmp") && ok;
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
    {
        bool splitOk=raceControl.splitter!=nullptr; const int savedSplit=raceControl.splitPermille;
        RECT savedBounds{}; GetWindowRect(raceControl.window,&savedBounds);
        const int dpi=GetDpiForWindow(raceControl.window); auto d=[&](int n){return MulDiv(n,dpi,96);};
        auto bounds=[&](HWND window) { RECT area{}; GetWindowRect(window,&area); MapWindowPoints(nullptr,raceControl.window,reinterpret_cast<POINT*>(&area),2); return area; };
        auto drag=[&](int target) {
            RECT grip{}; GetClientRect(raceControl.splitter,&grip);
            const int grab=grip.right/2;
            SendMessageW(raceControl.splitter,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(grab,10));
            splitOk=GetCapture()==raceControl.splitter && splitOk;
            RECT client{}; GetClientRect(raceControl.window,&client);
            POINT point{d(12)+MulDiv(client.right-3*d(12),target,1000)+grab,d(130)};
            MapWindowPoints(raceControl.window,raceControl.splitter,&point,1);
            SendMessageW(raceControl.splitter,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y));
            SendMessageW(raceControl.splitter,WM_LBUTTONUP,0,0);
        };
        const int originalWidth=ListView_GetColumnWidth(raceControl.standings,0);
        ListView_SetColumnWidth(raceControl.standings,0,originalWidth+d(10));
        const auto originalRows=raceControl.rows; const auto originalEvents=raceControl.events;
        SetRaceSplit(560,false); drag(350);
        splitOk=std::abs(raceControl.splitPermille-350)<=1 && GetCapture()!=raceControl.splitter && !raceControl.splitDragging && splitOk;
        splitOk=ListView_GetColumnWidth(raceControl.standings,0)==originalWidth+d(10) && raceControl.rows.size()==originalRows.size() && raceControl.events.size()==originalEvents.size() && splitOk;
        const int dragged=raceControl.splitPermille; LoadRaceFields(); splitOk=raceControl.splitPermille==dragged && splitOk;
        CloseRaceControl(); OpenRaceControl(); splitOk=raceControl.splitPermille==dragged && splitOk;
        ok=Export(raceControl.window,L"build\\qa\\race-control-split-35.bmp") && ok;
        drag(700); splitOk=std::abs(raceControl.splitPermille-700)<=1 && splitOk;
        ok=Export(raceControl.window,L"build\\qa\\race-control-split-70.bmp") && ok;
        SendMessageW(raceControl.splitter,WM_LBUTTONDBLCLK,0,0); splitOk=raceControl.splitPermille==560 && splitOk;
        SendMessageW(raceControl.splitter,WM_KEYDOWN,VK_LEFT,0); splitOk=raceControl.splitPermille==550 && splitOk;
        SendMessageW(raceControl.splitter,WM_KEYDOWN,VK_HOME,0); splitOk=raceControl.splitPermille==560 && splitOk;
        SendMessageW(raceControl.splitter,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,10));
        SetRaceSplit(400,false); SendMessageW(raceControl.splitter,WM_KEYDOWN,VK_ESCAPE,0);
        splitOk=raceControl.splitPermille==560 && GetCapture()!=raceControl.splitter && splitOk;
        // Minimum window size, both drag limits and a visible car filter must keep the toolbar usable.
        SetWindowPos(raceControl.window,nullptr,0,0,d(900),d(520),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SetRaceCarFilter(0);
        for(int target:{100,900}) {
            drag(target); const auto events=bounds(raceControl.feed), standings=bounds(raceControl.standings), separator=bounds(raceControl.splitter);
            splitOk=events.right-events.left>=d(240)-1 && standings.right-standings.left>=d(240)-1 && events.right<=separator.left && separator.right<=standings.left && splitOk;
            const auto filter=bounds(raceControl.filter.Handle()), camera=bounds(RaceItem(RaceCameraId)), pill=bounds(RaceItem(RaceCarPillId));
            const auto count=bounds(RaceItem(RaceCountId)), only=bounds(RaceItem(RaceOnlyCarId));
            splitOk=filter.right<=camera.left && pill.right<=count.left && count.right<=only.left && pill.bottom<=events.top && splitOk;
            const auto replay=bounds(raceControl.replay.Handle()), selected=bounds(RaceItem(RaceSelectedId)), watch=bounds(RaceItem(RaceWatchId));
            splitOk=replay.right<=selected.left && selected.right<=watch.left && splitOk;
        }
        ok=Export(raceControl.window,L"build\\qa\\race-control-split-narrow.bmp") && ok;
        SetRaceCarFilter(-1); SetRaceSplit(savedSplit,true);
        SetWindowPos(raceControl.window,nullptr,savedBounds.left,savedBounds.top,savedBounds.right-savedBounds.left,savedBounds.bottom-savedBounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
        report<<"Race Control splitter: mouse, persisted ratio and reopen, minimum widths, stable columns, toolbar wrapping, keyboard, reset and cancel: "<<(splitOk?"PASS":"FAIL")<<"\n";
        ok=splitOk && ok;
    }
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
    {
        // Previous and Next walk the shown events in time order, whatever the sort, and replay each.
        bool stepOk=raceControl.events.size()>=3 && IsWindowEnabled(RaceItem(RacePreviousId)) && IsWindowEnabled(RaceItem(RaceNextId));
        std::vector<double> times; for(const auto& e:raceControl.events) times.push_back(e.time); std::sort(times.begin(),times.end());
        auto selectedTime=[]{ const int i=SelectedEventIndex(); return i<0?-1.0:raceControl.events[i].time; };
        ListView_SetItemState(raceControl.feed,-1,0,LVIS_SELECTED); raceControl.selectedEvent=0;
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RaceNextId,BN_CLICKED),0); stepOk=selectedTime()==times.front() && !raceControl.pendingNumber.empty() && stepOk;
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RaceNextId,BN_CLICKED),0); stepOk=selectedTime()==times[1] && stepOk;
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RacePreviousId,BN_CLICKED),0); stepOk=selectedTime()==times.front() && stepOk;
        SendMessageW(raceControl.window,WM_COMMAND,MAKEWPARAM(RacePreviousId,BN_CLICKED),0); stepOk=selectedTime()==times.front() && raceControl.message.find(L"earliest")!=std::wstring::npos && stepOk;
        // A jump waits for iRacing; in the preview it has arrived at once and plays.
        SendMessageW(raceControl.window,WM_TIMER,1,0); stepOk=raceControl.pendingNumber.empty() && stepOk;
        // The shortcuts, Ctrl+Alt+Shift+Left / Right by default, do the same.
        stepOk=DefaultHotkeys[3]==(HotkeyAllModifiers|(HOTKEYF_EXT<<8)|VK_LEFT) && HotkeyText(DefaultHotkeys[4]).find(L"Ctrl+Alt+Shift+")==0 && stepOk;
        SendMessageW(app.main,WM_HOTKEY,5,0); stepOk=selectedTime()==times[1] && stepOk;
        SendMessageW(app.main,WM_HOTKEY,4,0); stepOk=selectedTime()==times.front() && stepOk;
        SendMessageW(raceControl.window,WM_TIMER,1,0);
        report<<"Race Control Previous / Next and their shortcuts step through events in time order: "<<(stepOk?"PASS":"FAIL")<<"\n"; ok=stepOk && ok;
    }
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
        bool rcOk=true; const auto savedFields=raceControl.fields; const auto savedEvents=raceControl.eventFields; const auto savedOptions=First(StandingsKind).table.standings; const auto savedDrivers=app.session.drivers;
        First(StandingsKind).table.standings.ownClass=false; UpdateRaceControl(true);
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
        raceControl.fields=savedFields; raceControl.eventFields=savedEvents; First(StandingsKind).table.standings=savedOptions; app.session.drivers=savedDrivers;
        raceControl.sortColumn=-1; raceControl.eventSortColumn=2; raceControl.eventSortDescending=true; SaveRaceFields();
        SetRaceEventFilter(0);
        report<<"Race Control field visibility, numeric sorting, missing values, event sorting, car/type filters and replay for the filtered event: "<<(rcOk?"PASS":"FAIL")<<"\n"; ok=rcOk && ok;
    }
    {
        bool extrasOk=true; const auto savedFields=raceControl.fields; const auto savedEvents=raceControl.eventFields;
        const auto savedInfo=raceControl.info; const auto savedSession=app.session; const auto savedFrame=app.frame;
        const int savedSplit=raceControl.splitPermille;
        const int savedSort=raceControl.sortColumn, savedEventSort=raceControl.eventSortColumn;
        const bool savedDescending=raceControl.sortDescending, savedEventDescending=raceControl.eventSortDescending;
        // Old configurations enable none of the newly added options.
        for(int i=RaceFuelField;i<RaceFieldCount;++i) WritePrivateProfileStringW(L"RaceControl",(L"Field"+std::to_wstring(i)).c_str(),nullptr,app.config.c_str());
        WritePrivateProfileStringW(L"RaceControl",L"EventField7",nullptr,app.config.c_str());
        for(const auto key:RaceInfoKeys) WritePrivateProfileStringW(L"RaceControl",key,nullptr,app.config.c_str());
        LoadRaceFields();
        for(int i=RaceFuelField;i<RaceFieldCount;++i) extrasOk=!raceControl.fields[i] && extrasOk;
        extrasOk=!raceControl.eventFields[RaceEventLapField] && !raceControl.info[0] && !raceControl.info[1] && extrasOk;
        // Disabled extras do not request the shared fuel or rating calculations.
        raceControl.fields.fill(false); raceControl.fields[0]=raceControl.fields[1]=raceControl.fields[2]=true; raceControl.sortColumn=-1;
        InvalidateModel(); app.ratingVersion=app.opponentFuelVersion=~app.modelVersion;
        UpdateRaceControl(true);
        extrasOk=app.ratingVersion!=app.modelVersion && app.opponentFuelVersion!=app.modelVersion && extrasOk;
        for(int i=RaceFuelField;i<RaceFieldCount;++i) SetRaceField(false,i,true);
        SetRaceField(true,RaceEventLapField,true);
        SetRaceInfo(RaceLeaderDistanceInfo,true); SetRaceInfo(RaceWhiteFlagInfo,true);
        SaveRaceFields(); LoadRaceFields();
        for(int i=RaceFuelField;i<RaceFieldCount;++i) extrasOk=raceControl.fields[i] && extrasOk;
        extrasOk=raceControl.eventFields[RaceEventLapField] && raceControl.info[0] && raceControl.info[1] && extrasOk;
        ShowRaceControlPage();
        const HWND list=FindWindowExW(ui.host.Content(),nullptr,L"LaunchMateRowList",nullptr);
        auto* settings=reinterpret_cast<RowList*>(GetWindowLongPtrW(list,GWLP_USERDATA));
        int switches=0; if(settings) for(const auto& row:settings->Rows()) if(row.toggle>=0) ++switches;
        extrasOk=switches==RaceFieldCount+RaceEventFieldCount-FirstEventField+RaceInfoCount+1 && extrasOk;
        Export(app.main,L"build\\qa\\race-control-extra-settings.bmp"); ShowPage(Page::Overlays);
        Row leader{app.session.drivers[0],app.frame.cars[0]}, saver{app.session.drivers[4],app.frame.cars[4]}, unknown{app.session.drivers[6],app.frame.cars[6]};
        extrasOk=RaceStandingCell(leader,RaceFuelField)==L"END" && RaceStandingCell(saver,RaceFuelField)==L"SAVE" && RaceStandingCell(unknown,RaceFuelField).empty() && extrasOk;
        Cell overlay{{},Ink::White}; CellRenderers[FuelColumn](leader,First(RelativeKind),overlay);
        extrasOk=overlay.text==RaceStandingCell(leader,RaceFuelField) && extrasOk;
        leader.car.stintLaps=10; saver.car.stintLaps=2; unknown.car.stintLaps=-1;
        leader.car.stops=10; saver.car.stops=2; unknown.car.stops=-1;
        leader.driver.brand="BMW"; saver.driver.brand="Audi"; unknown.driver.brand.clear();
        app.ratingChanges.fill(NoRatingChange); app.ratingVersion=app.modelVersion;
        app.ratingChanges[leader.driver.index]=12; app.ratingChanges[saver.driver.index]=-10;
        extrasOk=RaceStandingCell(leader,RaceRatingChangeField)==L"+12" && RaceStandingCell(saver,RaceRatingChangeField)==L"-10" && RaceStandingCell(unknown,RaceRatingChangeField)==L"\u2014" && extrasOk;
        extrasOk=RaceStandingCell(leader,RaceStintTimeField)==L"23:14" && RaceStandingCell(unknown,RaceStintTimeField)==L"\u2014" && extrasOk;
        extrasOk=RaceStandingCell(leader,RaceStopsField)==L"10" && RaceStandingCell(unknown,RaceStopsField)==L"\u2014" && extrasOk;
        for(int column:{RaceFuelField,RaceStintLapsField,RaceStintTimeField,RaceStopsField,RaceRatingChangeField,RaceBrandField}) {
            raceControl.sortColumn=column;
            for(bool descending:{false,true}) {
                raceControl.sortDescending=descending; std::vector<Row> rows{unknown,saver,leader}; SortRaceRows(rows);
                const bool leaderFirst=column==RaceFuelField?!descending:descending;
                extrasOk=rows.back().driver.index==unknown.driver.index && rows.front().driver.index==(leaderFirst?leader.driver.index:saver.driver.index) && extrasOk;
            }
        }
        raceControl.sortColumn=-1;
        // A live stint timer uses an observed timestamp; unknown, pit and replay stay unavailable.
        app.preview=false; app.frame.time=500; leader.car.stintStartTime=400;
        extrasOk=RaceStandingCell(leader,RaceStintTimeField)==L"1:40" && extrasOk;
        leader.car.stintStartTime=-1; extrasOk=RaceStandingCell(leader,RaceStintTimeField)==L"\u2014" && extrasOk;
        leader.car.stintStartTime=400; leader.car.pit=true; extrasOk=RaceStandingCell(leader,RaceStintTimeField)==L"\u2014" && extrasOk; leader.car.pit=false;
        app.frame.replay=true; extrasOk=RaceStandingCell(leader,RaceStintTimeField)==L"\u2014" && extrasOk;
        app.preview=true; app.frame=savedFrame; InvalidateModel();
        for(bool descending:{false,true}) {
            std::vector<RaceEvent> events(3); events[0].lap=-1; events[1].lap=2; events[2].lap=10;
            raceControl.eventSortColumn=RaceEventLapField; raceControl.eventSortDescending=descending; SortRaceEvents(events);
            extrasOk=events.back().lap==-1 && events.front().lap==(descending?10:2) && extrasOk;
        }
        SetRaceSort(true,RaceEventLapField,0); SaveRaceFields(); LoadRaceFields();
        extrasOk=raceControl.eventSortColumn==RaceEventLapField && extrasOk;
        const int lapColumn=static_cast<int>(std::find(raceControl.feedColumns.begin(),raceControl.feedColumns.end(),RaceEventLapField)-raceControl.feedColumns.begin());
        wchar_t lapText[32]{}; NMLVDISPINFOW item{}; item.hdr.hwndFrom=raceControl.feed; item.hdr.code=LVN_GETDISPINFOW;
        item.item.mask=LVIF_TEXT; item.item.iItem=0; item.item.iSubItem=lapColumn; item.item.pszText=lapText; item.item.cchTextMax=32;
        SendMessageW(raceControl.window,WM_NOTIFY,RaceFeedId,reinterpret_cast<LPARAM>(&item));
        extrasOk=std::wstring(lapText)==L"4" && extrasOk;
        auto infoText=[] { wchar_t text[512]{}; GetWindowTextW(RaceItem(RaceInfoId),text,512); return std::wstring(text); };
        UpdateRaceControl(true); RECT withInfo{}; GetWindowRect(raceControl.feed,&withInfo);
        extrasOk=IsWindowVisible(RaceItem(RaceInfoId)) && infoText().find(LeaderDistanceValue())!=std::wstring::npos && infoText().find(WhiteFlagTimeValue())!=std::wstring::npos && extrasOk;
        SetRaceInfo(RaceLeaderDistanceInfo,false);
        extrasOk=infoText().find(L"Leader projection")==std::wstring::npos && infoText().find(L"Clock at leader")==0 && extrasOk;
        SetRaceInfo(RaceWhiteFlagInfo,false); RECT withoutInfo{}; GetWindowRect(raceControl.feed,&withoutInfo);
        extrasOk=!IsWindowVisible(RaceItem(RaceInfoId)) && withInfo.top-withoutInfo.top==MulDiv(30,GetDpiForWindow(raceControl.window),96) && extrasOk;
        SetRaceInfo(RaceLeaderDistanceInfo,true); SetRaceInfo(RaceWhiteFlagInfo,true); SetRaceSplit(270,true);
        CloseRaceControl(); OpenRaceControl();
        extrasOk=raceControl.info[0] && raceControl.info[1] && raceControl.fields[RaceFuelField] && raceControl.eventFields[RaceEventLapField] && extrasOk;
        Export(raceControl.window,L"build\\qa\\race-control-extra-fields.bmp");
        SetRaceField(false,RaceStopsField,false);
        extrasOk=std::find(raceControl.columns.begin(),raceControl.columns.end(),RaceStopsField)==raceControl.columns.end() && raceControl.fields[RaceFuelField] && extrasOk;
        SetRaceField(true,RaceEventLapField,false); extrasOk=raceControl.eventSortColumn==FirstEventField && extrasOk;
        app.session=savedSession; app.frame=savedFrame; InvalidateModel();
        raceControl.fields=savedFields; raceControl.eventFields=savedEvents; raceControl.info=savedInfo;
        raceControl.sortColumn=savedSort; raceControl.sortDescending=savedDescending; raceControl.eventSortColumn=savedEventSort; raceControl.eventSortDescending=savedEventDescending;
        SetRaceSplit(savedSplit,false); SaveRaceFields(); LayoutRaceControl(); UpdateRaceControl(true);
        report<<"Race Control extras: defaults off, independent settings, shared fuel badges and cache, stint time, signed rating, numeric/missing sorting, event lap, optional forecasts and persistence: "<<(extrasOk?"PASS":"FAIL")<<"\n";
        ok=extrasOk && ok;
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
    ok=First(0).table.driverColumns[5] && First(1).table.driverColumns[5] && First(StandingsKind).table.interval && First(RelativeKind).table.rows==7 && First(StandingsKind).table.standings.leaders==2 && First(StandingsKind).table.standings.ahead==1 && First(StandingsKind).table.standings.behind==1 && First(StandingsKind).table.standings.ownClass && !First(StandingsKind).table.standings.separateClasses && !First(FuelKind).fuel.figures[1] && app.startWithWindows && app.startMinimized && ok;
    ShowWindow(app.main,StartupShowCommand(true,SW_SHOW)); UpdateOverlays();
    ok=!IsWindowVisible(app.main) && IsWindowVisible(First(0).window) && ok;
    ShowWindow(app.main,StartupShowCommand(false,SW_SHOW)); ok=IsWindowVisible(app.main) && ok;
    const auto startup=StartupCommand(); ok=(!startup.empty() && startup.front()==L'"' && startup.back()==L'"') && ok;
    SendMessageW(app.main,WM_CLOSE,0,0); ok=!IsWindowVisible(app.main) && IsWindow(app.main) && ok;
    report<<"Overlay pages, independent options, class view switch, fuel rows, close-to-tray and persistence: "<<(ok?"PASS":"FAIL")<<"\n";
    First(RelativeKind).table.rows=9; First(StandingsKind).table.standings={}; First(FuelKind).fuel.figures.fill(true); First(FuelKind).fuel.display.fill(true); app.startWithWindows=app.startMinimized=false;
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
        ok=Export(First(2).window,L"build\\qa\\fuel-first-race-lap.bmp") && ok;
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
    const auto historicalRows=Rows(First(1));
    ok=historicalRows.size()==9 && ok;
    for(const auto& row:historicalRows) ok=row.car.best>0 && row.car.last>0 && ok;
    UpdateOverlays(); ok=Export(First(1).window,L"build\\qa\\standings-history.bmp")&&ok;
    report<<"Late-join standings populated from session results only: "<<(ok?"PASS":"FAIL")<<"\n";
    {
        app.session.results={}; for(auto& car:app.frame.cars) { car.best=car.last=car.f2Time=-1; car.position=car.classPosition=0; car.surface=-1; }
        UpdateOverlays(); const auto practiceRows=Rows(First(1));
        ok=!practiceRows.empty() && std::none_of(practiceRows.begin(),practiceRows.end(),[](const Row& row){return row.car.best>0;}) && ok;
        ok=Export(First(1).window,L"build\\qa\\practice-no-times.bmp") && ok;
        app.frame.cars[9].best=125; UpdateOverlays();
        const auto rankedPractice=Standings(app.session,app.frame,MaxCars,false);
        ok=!rankedPractice.empty() && rankedPractice.front().driver.index==9 && ok;
        ok=Export(First(1).window,L"build\\qa\\practice-first-time.bmp") && ok;
        report<<"Untimed practice roster, car-number order and first timed lap: "<<(ok?"PASS":"FAIL")<<"\n";
    }
    app.session=demoSession; app.frame=demoFrame; app.preview=true;
    {
        app.preview=false; app.session.type="Race"; app.session.number=app.frame.session; app.frame.state=3;
        app.frame.bestLap=app.frame.lastLap=-1; First(StandingsKind).table.standings.liveGaps=true;
        for(auto& driver:app.session.drivers) {
            driver.estimatedLapTime=140; app.session.results[driver.index]={};
            auto& car=app.frame.cars[driver.index]; car.position=car.classPosition=0; car.completed=-1; car.lap=0; car.best=car.last=car.f2Time=-1; car.estTime=car.pct*140;
        }
        UpdateOverlays();
        ok=!Rows(First(1)).empty() && ok;
        const auto earlyRows=Rows(First(0)); for(const auto& row:earlyRows) ok=std::isfinite(row.gap) && ok;
        ok=Export(First(0).window,L"build\\qa\\relative-formation.bmp") && ok;
        ok=Export(First(1).window,L"build\\qa\\standings-formation.bmp") && ok;
        app.frame.state=4; for(auto& car:app.frame.cars) if(car.surface>=0) { car.completed=0; car.lap=1; }
        UpdateOverlays(); ok=!Rows(First(1)).empty() && ok;
        ok=Export(First(1).window,L"build\\qa\\standings-first-lap.bmp") && ok;
        report<<"Formation / first-lap standings, early relative gaps and live-gap settings: "<<(ok?"PASS":"FAIL")<<"\n";
        app.session=demoSession; app.frame=demoFrame; app.preview=true; First(StandingsKind).table.standings.liveGaps=false;
    }
    app.frame.limiter=false; ok=Export(First(3).window,L"build\\qa\\pit-red.bmp")&&ok;
    {
        const auto savedFrame=app.frame; const auto savedSession=app.session; const auto savedStandings=First(StandingsKind).table.standings;
        app.preview=false; app.session.type="Race"; app.frame.state=4; First(StandingsKind).table.standings={}; First(StandingsKind).table.standings.liveGaps=true;
        app.frame.cars[app.frame.player].pct=.52; app.frame.cars[app.frame.player].estTime=.52*138.2;
        UpdateOverlays(); const auto liveRows=Rows(First(1)); const auto liveRelative=Rows(First(0));
        const auto player=std::find_if(liveRelative.begin(),liveRelative.end(),[](const Row& row){return row.player;});
        ok=player!=liveRelative.end() && player->car.position==1 && !liveRows.empty() && liveRows.front().player && liveRows.front().car.classPosition==1 && ok;
        ok=Export(First(0).window,L"build\\qa\\relative-live-overtake.bmp") && ok;
        ok=Export(First(1).window,L"build\\qa\\standings-live-overtake.bmp") && ok;
        app.frame=savedFrame; app.session=savedSession; First(StandingsKind).table.standings=savedStandings; app.preview=true;
        report<<"Live overtake updates Relative P and Standings row order / class P: "<<(ok?"PASS":"FAIL")<<"\n";
    }
    app.pitVisibility.Reset();
    app.preview=false; app.hidden=false; app.frame.pit=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(First(i).window))==(i!=3))&&ok;
    const auto originalSurface=app.frame.cars[app.frame.player].surface;
    app.frame.cars[app.frame.player].surface=2; UpdateOverlays(); ok=IsWindowVisible(First(3).window) && ok;
    ok=Export(First(3).window,L"build\\qa\\pit-approach.bmp") && ok;
    app.frame.cars[app.frame.player].surface=3; app.frame.pit=true; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(First(3).window) && ok;
    {
        // In the pit lane: an in-car adjustment changed after getting in is listed under the limiter, with the setup's value.
        auto text=[](const wchar_t* t){ return std::any_of(First(3).scene.begin(),First(3).scene.end(),[&](const DrawCommand& c){ return c.type==DrawCommand::Text && c.text==t; }); };
        auto client=[]{ RECT r{}; GetClientRect(First(3).window,&r); return r.bottom; };
        const auto savedSetup=app.session.setup; const int savedUpdate=app.session.setupUpdate;
        app.session.setup={{"BrakePressureBias","52.5%"},{"AbsSetting","5 (ABS)"}}; app.session.setupUpdate=7;
        app.frame.dialsKnown=1u<<BrakeBiasDial|1u<<AbsDial; app.frame.dials[BrakeBiasDial]=52.5; app.frame.dials[AbsDial]=5;
        app.setupDials.Reset(); UpdateOverlays(); const LONG plain=client();
        bool setupOk=!text(L"ABS") && First(3).setupChanges;
        app.frame.time+=3; app.frame.dials[AbsDial]=3; UpdateOverlays();
        setupOk=text(L"ABS") && text(L"3") && text(L"setup 5") && !text(L"Brake bias") && client()>plain && setupOk;
        setupOk=Export(First(3).window,L"build\\qa\\pit-setup-changes.bmp") && setupOk;
        First(3).setupChanges=false; UpdateOverlays(); setupOk=!text(L"ABS") && client()==plain && setupOk;
        First(3).setupChanges=true; app.session.setup=savedSetup; app.session.setupUpdate=savedUpdate; app.frame.dialsKnown=0; UpdateOverlays();
        report<<"Pit Limiter lists in-car adjustments that differ from the setup, in the pit lane: "<<(setupOk?"PASS":"FAIL")<<"\n"; ok=setupOk && ok;
    }
    app.frame.pit=false; app.frame.limiter=true; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(First(3).window) && ok;
    {
        // Past pit exit with the limiter on: the flashing warning instead of the green bar.
        const bool warning=app.pitVisibility.Warning() && std::any_of(First(3).scene.begin(),First(3).scene.end(),[](const DrawCommand& c){ return c.type==DrawCommand::Text && c.text==L"TURN OFF PIT LIMITER"; });
        const bool exported=Export(First(3).window,L"build\\qa\\pit-turn-off-limiter.bmp");
        report<<"Pit Limiter warns to turn off the limiter after pit exit: "<<(warning && exported?"PASS":"FAIL")<<"\n"; ok=warning && exported && ok;
    }
    app.frame.limiter=false; app.frame.time+=1; UpdateOverlays(); ok=!IsWindowVisible(First(3).window) && ok;
    app.frame.cars[app.frame.player].surface=2; app.frame.time+=1; UpdateOverlays(); ok=IsWindowVisible(First(3).window) && ok;
    app.frame.cars[app.frame.player].surface=originalSurface;
    app.frame.pit=true; UpdateOverlays(); ok=IsWindowVisible(First(3).window)&&ok;
    app.frame.onTrack=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(First(i).window))==(i!=3))&&ok;
    app.frame.camera=4; app.frame.replay=true; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(First(i).window))==(i!=3))&&ok;
    ok=ReferenceCar(app.frame)==4 && !Rows(First(0)).empty() && ok;
    ok=Export(First(0).window,L"build\\qa\\relative-replay.bmp")&&ok;
    app.frame.replay=false; UpdateOverlays();
    for(int i=0;i<OverlayCount;++i) ok=(static_cast<bool>(IsWindowVisible(First(i).window))==(i!=3))&&ok;
    app.frame.connected=false; UpdateOverlays();
    for(auto& o:app.overlays) ok=!IsWindowVisible(o.window)&&ok;
    app.preview=true; app.hidden=true; UpdateOverlays();
    for(auto& o:app.overlays) ok=!IsWindowVisible(o.window)&&ok;
    First(0).place.x=123; First(0).place.scale=130; Save();
    First(0).place.x=0; First(0).place.scale=50; Load();
    ok=(First(0).place.x==123 && First(0).place.scale==130 && !app.settingsError)&&ok;
    report<<"Visibility in car, spotting, replay, pause, disconnect, pit lane, hide and saved placement: "<<(ok?"PASS":"FAIL")<<"\n";
    app.preview=true; PreviewData(0);
    bool orderOk=true;
    for(int kind=0;kind<2;++kind) {
        const auto original=First(kind).table.columnOrder; const auto other=First(1-kind).table.columnOrder;
        orderOk=!MoveInOrder(First(kind).table.columnOrder,original.front(),-1) && First(kind).table.columnOrder==original && orderOk;
        orderOk=MoveInOrder(First(kind).table.columnOrder,original.front(),1) && orderOk;
        orderOk=First(kind).table.columnOrder[0]==original[1] && First(1-kind).table.columnOrder==other && orderOk;
        First(kind).table.columnOrder=original;
        for(size_t i=1;i<original.size();++i) MoveInOrder(First(kind).table.columnOrder,original.back(),-1);
        const auto changed=First(kind).table.columnOrder; Load(); orderOk=First(kind).table.columnOrder==changed && changed.front()==original.back() && orderOk;
        OpenOverlay(First(kind)); ok=Export(app.main,L"build\\qa\\column-order-"+std::to_wstring(kind)+L".bmp") && ok;
        UpdateOverlays(); ok=Export(First(kind).window,L"build\\qa\\reordered-"+std::to_wstring(kind)+L".bmp") && ok;
        First(kind).table.columnOrder=original; SettingChanged();
    }
    ShowPage(Page::Overlays);
    ok=orderOk && ok; report<<"Independent column ordering, move boundaries, hidden-column positions and persistence: "<<(orderOk?"PASS":"FAIL")<<"\n";
    bool cornersOk=true;
    for(int kind=0;kind<OverlayCount;++kind) {
        auto& overlay=First(kind); const auto original=overlay.place;
        overlay.place.growLeft=overlay.place.growUp=false; overlay.place.scale=100;
        SetOverlayOrigin(overlay,monitor.rcWork.left+300,monitor.rcWork.top+250); Fit(overlay);
        for(int corner=0;corner<4;++corner) {
            RECT before{},grown{},shrunk{}; GetWindowRect(overlay.window,&before);
            SetOverlayCorner(First(kind),corner);
            GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
            overlay.place.scale=130; Fit(overlay); GetWindowRect(overlay.window,&grown);
            cornersOk=((corner&1)?grown.right==before.right:grown.left==before.left) && ((corner&2)?grown.bottom==before.bottom:grown.top==before.top) && cornersOk;
            cornersOk=grown.right-grown.left>before.right-before.left && grown.bottom-grown.top>before.bottom-before.top && cornersOk;
            overlay.place.scale=100; Fit(overlay); GetWindowRect(overlay.window,&shrunk);
            cornersOk=EqualRect(&before,&shrunk) && cornersOk;
        }
        ok=Export(First(kind).window,L"build\\qa\\fixed-corner-"+std::to_wstring(kind)+L".bmp") && ok;
        const int anchorX=overlay.place.x,anchorY=overlay.place.y; Save();
        overlay.place.growLeft=overlay.place.growUp=false; overlay.place.x=overlay.place.y=0; Load();
        cornersOk=overlay.place.growLeft && overlay.place.growUp && overlay.place.x==anchorX && overlay.place.y==anchorY && cornersOk;
        overlay.place=original; Fit(overlay); Save();
    }
    ok=cornersOk && ok;
    report<<"Fixed corner: all four corners on every overlay, resize/shrink, no jump on selection and persisted anchors: "<<(cornersOk?"PASS":"FAIL")<<"\n";
    Frame liveFrame; Session liveSession;
    if(app.telemetry.Read(liveFrame,liveSession)) {
        app.preview=false; app.frame=liveFrame; app.session=liveSession; First(StandingsKind).table.standings={}; raceControl.message.clear();
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
    for(auto& o:app.overlays) { o.place.enabled=true; o.place.scale=100; o.place.rate=DefaultRates[o.kind]; }
    First(WeatherKind).weather.display.fill(true); First(FuelKind).fuel.display.fill(true); First(FuelKind).fuel.figures.fill(true);
    First(StandingsKind).table.interval=true; First(1).table.driverColumns[0]=First(1).table.driverColumns[1]=First(0).table.driverColumns[4]=true;
    Session session=DemoSession(); session.drivers.clear(); session.key="benchmark"; session.number=0; session.trackMeters=7004;
    const char* classes[]={"GTP","LMP2","GT3"}; const unsigned colors[]={0xffda59,0x33ceff,0xff5888};
    for(int i=0;i<60;++i) session.drivers.push_back({i,i%3+1,1500+i*37,colors[i%3],false,false,"Driver Number "+std::to_string(i),std::to_string(i+1),classes[i%3],"Car","car",0,"A 3.45"});
    session.player=7; app.session=session;
    // Include actual track-map rendering when the QA cache was prepared, without
    // fetching assets or timing initial SVG parsing as part of a connected refresh.
    bool mapPrepared=false; std::error_code mapError;
    if(std::filesystem::exists(TrackMapCache()/L"353"/L"source.txt",mapError)) {
        const std::atomic_bool cancelled=false; auto loaded=trackmap::Load(*trackmap::FindSource(353),TrackMapCache(),cancelled);
        if(loaded.map) {
            app.session.trackId=353; app.session.trackName="limerock 2019 gp";
            trackMap.task.Cancel(); trackMap.identity="353:limerock2019gp"; trackMap.id=353;
            trackMap.geometry=std::move(loaded.map.geometry); trackMap.ready=trackMap.cached=true; ++trackMap.revision; mapPrepared=true;
        }
    }
    Frame frame=DemoFrame(0); frame.player=7; frame.session=0; frame.state=4; frame.pit=false; frame.timeLeft=3600; frame.cars={};
    double progress[60];
    for(int i=0;i<60;++i) progress[i]=10+(60-i)*.02;
    LARGE_INTEGER frequency{},start{},end{}; QueryPerformanceFrequency(&frequency);
    FILETIME created{},exited{},kernelBefore{},userBefore{},kernelAfter{},userAfter{};
    constexpr int iterations=3000; double phases[3]{},paints[OverlayCount]{}; int changes[OverlayCount]{};
    bool rated=false; // false: every overlay refreshes on every step; true: each at its own rate, steps 100 ms apart.
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
        QueryPerformanceCounter(&b); app.now=static_cast<ULONGLONG>(t)*100; app.timerMs=100; UpdateOverlays(!rated); UpdateRaceControl();
        for(const auto& o:app.overlays) changes[o.kind]+=!(o.scene==previous[o.kind]);
        QueryPerformanceCounter(&c);
        for(auto& o:app.overlays) { LARGE_INTEGER before{},after{}; QueryPerformanceCounter(&before); UpdateWindow(o.window); QueryPerformanceCounter(&after); paints[o.kind]+=after.QuadPart-before.QuadPart; }
        QueryPerformanceCounter(&d); phases[0]+=b.QuadPart-a.QuadPart; phases[1]+=c.QuadPart-b.QuadPart; phases[2]+=d.QuadPart-c.QuadPart;
    };
    for(int t=0;t<100;++t) step(t);
    for(double& phase:phases) phase=0;
    for(double& paint:paints) paint=0;
    for(int& change:changes) change=0;
    PROCESS_MEMORY_COUNTERS memoryBefore{sizeof(memoryBefore)},memoryAfter{sizeof(memoryAfter)};
    GetProcessMemoryInfo(GetCurrentProcess(),&memoryBefore,sizeof(memoryBefore));
    refreshTicks.fill(0);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelBefore,&userBefore); QueryPerformanceCounter(&start);
    for(int t=100;t<100+iterations;++t) step(t);
    QueryPerformanceCounter(&end); GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelAfter,&userAfter);
    GetProcessMemoryInfo(GetCurrentProcess(),&memoryAfter,sizeof(memoryAfter));
    auto ticks=[](FILETIME a,FILETIME b){ return (ULARGE_INTEGER{{b.dwLowDateTime,b.dwHighDateTime}}.QuadPart-ULARGE_INTEGER{{a.dwLowDateTime,a.dwHighDateTime}}.QuadPart)/10.0; };
    const double cpu=(ticks(kernelBefore,kernelAfter)+ticks(userBefore,userAfter))/iterations;
    const double wall=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/iterations;
    std::ofstream report("build/qa/benchmark.txt");
    report<<"Official track-map geometry and 60 car markers included: "<<mapPrepared<<"\n";
    report<<"60-car race, all overlays visible, "<<iterations<<" refreshes: "<<wall<<" us wall, "<<cpu<<" us CPU per refresh\n";
    report<<"  model "<<phases[0]*1e6/frequency.QuadPart/iterations<<" us, overlay update "<<phases[1]*1e6/frequency.QuadPart/iterations<<" us, paint "<<phases[2]*1e6/frequency.QuadPart/iterations<<" us\n";
    // Update: scene, changed areas and drawing into the buffer. Screen: copying the buffer to the window.
    for(const auto& o:app.overlays) report<<"  overlay "<<o.kind<<": "<<changes[o.kind]<<" repaints, update "<<static_cast<double>(refreshTicks[o.kind])*1e6/frequency.QuadPart/iterations<<" us, screen "<<paints[o.kind]*1e6/frequency.QuadPart/iterations<<" us per refresh"<<std::endl;
    // Page faults (almost all soft: memory already in RAM) and the working set it ran in.
    report<<"  page faults "<<static_cast<double>(memoryAfter.PageFaultCount-memoryBefore.PageFaultCount)/iterations<<" per refresh, working set "
        <<memoryAfter.WorkingSetSize/1024<<" KB, peak "<<memoryAfter.PeakWorkingSetSize/1024<<" KB"<<std::endl;
    // The same race with each overlay at its default rate and telemetry at 10 Hz: CPU per second of racing.
    rated=true; for(auto& o:app.overlays) o.due=0;
    for(int t=0;t<100;++t) step(t);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelBefore,&userBefore);
    for(int t=100;t<100+iterations;++t) step(t);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelAfter,&userAfter);
    const double seconds=iterations/10.0;
    report<<"Default rates (";
    for(const auto& o:app.overlays) report<<(o.kind?", ":"")<<o.kind<<": "<<o.place.rate<<" Hz";
    report<<"), telemetry 10 Hz: "<<(ticks(kernelBefore,kernelAfter)+ticks(userBefore,userAfter))/1000/seconds<<" ms CPU per second"<<std::endl;
    // The same with the map transparent: per-pixel alpha, presented with UpdateLayeredWindow.
    First(TrackMapKind).map.background=false; Fit(First(TrackMapKind)); UpdateOverlays(); refreshTicks.fill(0); for(auto& o:app.overlays) o.due=0;
    for(int t=0;t<100;++t) step(t);
    refreshTicks.fill(0); GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelBefore,&userBefore);
    for(int t=100;t<100+iterations;++t) step(t);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelAfter,&userAfter);
    report<<"Track map without background: "<<(ticks(kernelBefore,kernelAfter)+ticks(userBefore,userAfter))/1000/seconds<<" ms CPU per second, its update "<<static_cast<double>(refreshTicks[TrackMapKind])*1e6/frequency.QuadPart/iterations<<" us per refresh, presentation included"<<std::endl;
    {
        // The same at 140 %, a common size on a 4K screen: the update with its scene and drawing apart.
        auto& m=First(TrackMapKind); m.place.scale=140; Fit(m); UpdateOverlays(); for(auto& o:app.overlays) o.due=0;
        for(int t=0;t<100;++t) step(t);
        profiler.on=true; profiler.entries.clear();
        for(int t=100;t<100+iterations;++t) step(t);
        profiler.on=false;
        auto perCall=[&](const char* part){ const auto& e=profiler.At(ProfileName(m,part)); return e.calls?e.ticks*1e6/frequency.QuadPart/e.calls:0.; };
        report<<"Track map without background at 140 % ("<<m.width<<"x"<<m.height<<"): update "<<perCall("update (scene, changes, drawing, present)")<<" us per refresh: scene "<<perCall("  scene")
            <<" us, drawing "<<perCall("  drawing")<<" us, present "<<perCall("present (UpdateLayeredWindow)")<<" us"<<std::endl;
        m.place.scale=100; Fit(m);
    }
    First(TrackMapKind).map.background=true; Fit(First(TrackMapKind));
    // The same with the track map switched off, as it ships: nothing of it may remain.
    First(TrackMapKind).place.enabled=false; UpdateOverlays(); for(auto& o:app.overlays) o.due=0;
    for(int t=0;t<100;++t) step(t);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelBefore,&userBefore);
    for(int t=100;t<100+iterations;++t) step(t);
    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernelAfter,&userAfter);
    report<<"Track map off, other rates as above: "<<(ticks(kernelBefore,kernelAfter)+ticks(userBefore,userAfter))/1000/seconds<<" ms CPU per second, SVG task "<<(trackMap.task.Running()?"running":"idle")<<std::endl;
    return 0;
}
