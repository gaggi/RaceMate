bool SectorDiagnostics(std::ostream& report) {
    auto& map=First(TrackMapKind); const auto savedMap=map.map; const auto place=map.place;

    const bool preview=app.preview; app.preview=true;
    bool ok=true;
    auto refresh=[&] { InvalidateModel(); RefreshOverlay(map); UpdateWindow(map.window); };
    map.place.enabled=true; map.map.sectors=true;
    auto colors=[&] { return std::find(map.sectorColors.begin(),map.sectorColors.end(),RGB(205,100,255))!=map.sectorColors.end() && std::find(map.sectorColors.begin(),map.sectorColors.end(),Ink::Green)!=map.sectorColors.end() && std::find(map.sectorColors.begin(),map.sectorColors.end(),Ink::Yellow)!=map.sectorColors.end(); };
    for(int layout:{CircleLayout,LineLayout,MapLayout}) {
        if(layout==MapLayout && !trackMap.ready) continue;
        map.map.layout=layout;
        for(int divisions:{1,3}) {
            map.map.sectorParts=divisions; map.map.background=true; map.place.scale=100; Fit(map);
            LARGE_INTEGER frequency{},begin{},end{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
            refresh(); QueryPerformanceCounter(&end);
            report<<"Sector initial layout (includes first scene, road mask and fill), layout "<<layout<<", parts "<<divisions<<": "<<(end.QuadPart-begin.QuadPart)*1000./frequency.QuadPart<<" ms\n";
            ok=map.sectorBitmap && map.sectorPaths.size()==static_cast<size_t>(3*divisions) && colors() && ok;
            const auto bitmap=map.sectorBitmap; const auto* points=map.sectorPaths.empty()?nullptr:map.sectorPaths.front().data();
            refresh(); ok=bitmap==map.sectorBitmap && !map.sectorPaths.empty() && points==map.sectorPaths.front().data() && ok;
            ok=Export(map.window,L"build\\qa\\sectors-"+std::to_wstring(layout)+L"-"+std::to_wstring(divisions)+L".bmp") && ok;
            map.map.background=false; map.place.scale=140; map.place.opacity=50; Fit(map); refresh();
            ok=map.layeredAlpha && map.sectorBitmap && colors() && map.sectorPixels[0]==0 && ok;
            ok=std::all_of(map.sectorPixels,map.sectorPixels+map.mapWidth*map.mapHeight,[](uint32_t p){const auto a=p>>24; return (p&255)<=a && ((p>>8)&255)<=a && ((p>>16)&255)<=a;}) && ok;
            ok=Export(map.window,L"build\\qa\\sectors-transparent-"+std::to_wstring(layout)+L"-"+std::to_wstring(divisions)+L".bmp",true) && ok;
        }
    }
    {
        const auto frame=app.frame;
        map.map.background=true; map.map.details=true; map.place.scale=100; map.map.sectorParts=3;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        for(bool enabled:{false,true}) {
            map.map.sectors=enabled; Fit(map);
            auto step=[&](int n) { for(const auto& d:app.session.drivers) if(d.index>=0 && d.index<MaxCars) app.frame.cars[d.index].pct=std::fmod(.05+d.index*.091+n*.0002,1.); InvalidateModel(); RefreshOverlay(map); };
            for(int n=0;n<40;++n) step(n);
            LARGE_INTEGER begin{},end{}; GdiFlush(); QueryPerformanceCounter(&begin);
            for(int n=40;n<640;++n) step(n);
            GdiFlush(); QueryPerformanceCounter(&end);
            report<<"Sector render benchmark (same map area, 10 preview cars, scene + buffer drawing, no screen copy): "<<(enabled?"3 minis/sector":"off")<<" "<<(end.QuadPart-begin.QuadPart)*1e6/frequency.QuadPart/600<<" us/refresh\n";
        }
        app.frame=frame;
    }
    map.map=savedMap; map.place=place;
    const bool wasOpen=raceControl.window!=nullptr;
    if(!wasOpen) LoadRaceFields();
    const auto fields=raceControl.fields; const bool showSectors=raceControl.showSectors; const int raceParts=raceControl.sectorParts;
    OpenRaceControl(); const int split=raceControl.splitPermille;
    raceControl.fields.fill(false); raceControl.fields[2]=true;
    raceControl.fields[RaceSectorTimesField]=true;
    raceControl.showSectors=true;
    raceControl.splitPermille=200; LayoutRaceControl();
    Row row{app.session.drivers[2],app.frame.cars[2]};
    // One standings column per sector, then per mini-sector, each with a time.
    for(int parts:{1,3}) {
        raceControl.sectorParts=parts; UpdateRaceControl(true);
        const auto sectorColumns=std::count_if(raceControl.columns.begin(),raceControl.columns.end(),[](int c){return c>=RaceSectorColumn;});
        ok=sectorColumns==SectorCount()*parts && RaceStandingCell(row,RaceSectorColumn+parts).find(L'.')!=std::wstring::npos && ok;
        ok=Export(raceControl.window,L"build\\qa\\race-control-sectors-"+std::to_wstring(parts)+L".bmp") && ok;
    }
    raceControl.fields=fields; raceControl.showSectors=showSectors; raceControl.sectorParts=raceParts; raceControl.splitPermille=split; LayoutRaceControl(); SaveRaceFields(); UpdateRaceControl(true);
    if(!wasOpen) CloseRaceControl();
    Fit(map); refresh();
    // Both overlays use the same latest-split model; no duplicate timekeeping per table.
    for(int kind:{RelativeKind,StandingsKind}) {
        auto& table=First(kind); const auto settings=table.table; const auto placement=table.place;
        table.table.extraColumns[SectorsColumn-BrandColumn]=true;
        if(std::find(table.table.columnOrder.begin(),table.table.columnOrder.end(),SectorsColumn)==table.table.columnOrder.end()) table.table.columnOrder.push_back(SectorsColumn);
        table.place.enabled=true; Fit(table); InvalidateModel(); RefreshOverlay(table); UpdateWindow(table.window);
        table.table.columnHeaders=true;
        for(int parts:{1,3}) {
            table.table.sectorParts=parts; Fit(table); InvalidateModel(); RefreshOverlay(table); UpdateWindow(table.window);
            ok=ColumnVisible(table,SectorsColumn) && ColumnWidth(table,SectorsColumn)==3*parts*(parts==3?54:62)
                && Export(table.window,L"build\\qa\\sector-table-"+std::to_wstring(kind)+L"-"+std::to_wstring(parts)+L".bmp") && ok;
        }
        table.table=settings; table.place=placement; Fit(table);
    }
    app.preview=preview; Fit(map); refresh();
    report<<"Sector timing: full-width fill, F1 colours, cached mask, full/mini sectors, panel/transparent at 100/140%, one Relative/Standings/Race Control column per sector and mini-sector:"<<(ok?"PASS":"FAIL")<<"\n";
    return ok;
}
