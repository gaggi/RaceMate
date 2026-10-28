// Race Control columns, their persistence and sorting. Standings fields are stored as
// Field0..Field25; Field25 (sector times) shows one column per sector, sorted by the IDs from RaceSectorColumn. Event fields 2..7 are stored as EventField2..7; 0 and 1 were the old
// per-row replay buttons and are no longer shown.
const wchar_t* RaceEventTitles[]={L"",L"",L"TIME",L"EVENT",L"#",L"DRIVER",L"DETAILS",L"LAP"};
const int RaceEventWidths[]={0,0,64,100,40,140,330,50};
constexpr int RaceEventOrder[]={2,RaceEventLapField,3,4,5,6};
static_assert(std::size(RaceEventTitles)==RaceEventFieldCount && std::size(RaceEventOrder)==RaceEventFieldCount-FirstEventField);
const wchar_t* RaceFieldTitle(int id) {
    const wchar_t* titles[]={L"P",L"#",L"DRIVER",L"CLASS",L"GAP",L"INT",L"BEST",L"LAST",L"STATUS",L"LICENSE",L"iRATING",L"TIRES",L"PIT LAP",L"PIT TIME",L"PIT STOP",L"FLAGS",L"LAPS",L"GAIN",L"INC",L"FUEL",L"STINT LAPS",L"STINT TIME",L"STOPS",L"iR +/-",L"CAR",L"SECTOR TIMES ~"};
    static_assert(std::size(titles)==RaceFieldCount);
    return titles[id];
}
int RaceFieldWidth(int id) { if(id>=RaceSectorColumn) return raceControl.sectorParts==3?58:66; const int widths[]={40,38,130,50,64,60,78,78,84,82,72,100,70,80,88,158,54,54,44,70,84,96,62,72,110,360}; static_assert(std::size(widths)==RaceFieldCount); return widths[id]; }
// Left to right; Race Control keeps this order.
constexpr int RaceFieldOrder[]={0,1,2,4,5,8,RaceFuelField,RaceIncidentsField,15,3,RaceBrandField,6,7,RaceSectorTimesField,16,17,9,10,RaceRatingChangeField,11,RaceStintLapsField,RaceStintTimeField,RaceStopsField,12,13,14};
static_assert(std::size(RaceFieldOrder)==RaceFieldCount);
void SaveRaceFields() {
    auto write=[&](const std::wstring& key,int value) { const auto text=std::to_wstring(value); WritePrivateProfileStringW(L"RaceControl",key.c_str(),text.c_str(),app.config.c_str()); };
    for(int i=0;i<RaceFieldCount;++i) write(L"Field"+std::to_wstring(i),raceControl.fields[i]);
    for(int i=FirstEventField;i<RaceEventFieldCount;++i) write(L"EventField"+std::to_wstring(i),raceControl.eventFields[i]);
    for(int i=0;i<RaceInfoCount;++i) write(RaceInfoKeys[i],raceControl.info[i]);
    write(L"SortColumn",raceControl.sortColumn); write(L"SortDescending",raceControl.sortDescending);
    write(L"EventSortColumn",raceControl.eventSortColumn); write(L"EventSortDescending",raceControl.eventSortDescending);
    write(L"AlwaysOnTop",raceControl.onTop); write(L"ReplayFrom",raceControl.replayMode);
    write(L"SplitPermille",raceControl.splitPermille);
    write(L"ShowSectors",raceControl.showSectors); write(L"SectorParts",raceControl.sectorParts);
}
void LoadRaceFields() {
    // New installs start narrow enough to fit beside the event feed without scrolling.
    for(int i=0;i<RaceFieldCount;++i) {
        const bool fallback=i<=2 || i==4 || i==5 || i==8 || i==RaceIncidentsField || (i>=9 && i<15 && First(StandingsKind).table.driverColumns[i-9]);
        raceControl.fields[i]=ReadInt(L"RaceControl",(L"Field"+std::to_wstring(i)).c_str(),fallback)!=0;
    }
    raceControl.eventFields={};
    raceControl.showSectors=ReadBool(L"RaceControl",L"ShowSectors",raceControl.fields[RaceSectorTimesField]);
    raceControl.fields[RaceSectorTimesField]=raceControl.showSectors;
    raceControl.sectorParts=ReadInt(L"RaceControl",L"SectorParts",1)==3?3:1;
    for(int i=FirstEventField;i<RaceEventFieldCount;++i) raceControl.eventFields[i]=ReadInt(L"RaceControl",(L"EventField"+std::to_wstring(i)).c_str(),i<RaceEventLapField)!=0;
    for(int i=0;i<RaceInfoCount;++i) raceControl.info[i]=ReadBool(L"RaceControl",RaceInfoKeys[i],false);
    if(std::none_of(raceControl.fields.begin(),raceControl.fields.end(),[](bool v){return v;})) raceControl.fields[2]=true;
    if(!raceControl.showSectors && std::none_of(raceControl.fields.begin(),raceControl.fields.begin()+RaceSectorTimesField,[](bool v){return v;})) raceControl.fields[2]=true;
    if(std::none_of(raceControl.eventFields.begin(),raceControl.eventFields.end(),[](bool v){return v;})) raceControl.eventFields[5]=true;
    const int sort=ReadInt(L"RaceControl",L"SortColumn",-1);
    raceControl.sortColumn=sort>=RaceSectorColumn && sort<RaceSectorColumn+SectorTiming::MaxSegments?sort:std::clamp(sort,-1,RaceFieldCount-1);
    raceControl.eventSortColumn=std::clamp(ReadInt(L"RaceControl",L"EventSortColumn",2),FirstEventField,RaceEventFieldCount-1);
    raceControl.sortDescending=ReadInt(L"RaceControl",L"SortDescending",0)!=0;
    raceControl.eventSortDescending=ReadInt(L"RaceControl",L"EventSortDescending",1)!=0;
    raceControl.onTop=ReadInt(L"RaceControl",L"AlwaysOnTop",0)!=0;
    raceControl.replayMode=std::clamp(ReadInt(L"RaceControl",L"ReplayFrom",1),0,2);
    raceControl.splitPermille=std::clamp(ReadInt(L"RaceControl",L"SplitPermille",560),100,900);
    if(raceControl.sortColumn>=0 && !raceControl.fields[RaceFieldOf(raceControl.sortColumn)]) raceControl.sortColumn=-1;
    if(!raceControl.showSectors && raceControl.sortColumn>=RaceSectorTimesField) raceControl.sortColumn=-1;
}
double RaceNumber(const std::string& value) {
    if(value.empty() || !std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0' && c<='9';})) return std::numeric_limits<double>::quiet_NaN();
    return std::strtod(value.c_str(),nullptr);
}
double RaceSortNumber(const Row& row,int col) {
    const double missing=std::numeric_limits<double>::quiet_NaN();
    auto positive=[&](double v){return v>0?v:missing;}; auto known=[&](double v){return v>=0?v:missing;};
    switch(col) {
    case 0:return positive(First(StandingsKind).table.standings.separateClasses?row.car.classPosition:row.car.position);
    case 1:return RaceNumber(row.driver.number);
    case 4:return row.lapDifference>0?row.lapDifference*86400.0:row.gap;
    case 5:return row.intervalLaps>0?row.intervalLaps*86400.0:row.interval;
    case 6:return positive(row.car.best); case 7:return positive(row.car.last);
    case 9: { const auto start=row.driver.license.find_first_of("0123456789"); return start==std::string::npos?missing:std::strtod(row.driver.license.c_str()+start,nullptr); }
    case 10:return positive(row.driver.rating); case 11:return known(row.car.tireCompound); case 12:return known(row.car.lastPitLap);
    case 13:return known(row.car.lastPitDuration); case 14:return known(row.car.pit?row.car.pitStationary:row.car.lastPitStationary);
    case 16: { int laps=row.car.completed; if(laps<0 && row.car.lap>=0) laps=std::max(0,row.car.lap-1); if(laps<0 && app.session.number==app.frame.session) laps=app.session.results[row.driver.index].completed; return known(laps); }
    case 17: { int gained=PositionsGained(app.session,row,First(StandingsKind).table.standings.separateClasses); return gained==-MaxCars?missing:gained; }
    case RaceIncidentsField: return known(RaceIncidents(row));
    case RaceFuelField: { const auto status=FuelFinishStatus(row.driver.index); return status==OpponentFuelStatus::None?missing:(status==OpponentFuelStatus::End?0:1); }
    case RaceStintLapsField: return known(row.car.stintLaps);
    case RaceStintTimeField: return RaceStintSeconds(row);
    case RaceStopsField: return known(row.car.stops);
    case RaceRatingChangeField: { const int change=RaceRatingChange(row); return change==NoRatingChange?missing:change; }
    }
    if(col>=RaceSectorColumn) return known(SectorColumnTime(row.driver.index,col-RaceSectorColumn,raceControl.sectorParts));
    return missing;
}
void SortRaceRows(std::vector<Row>& rows) {
    const int col=raceControl.sortColumn; if(col<0) return;
    std::stable_sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b) {
        if(col==2 || col==3 || col==8 || col==15 || col==RaceBrandField) {
            if(col==RaceBrandField && a.driver.brand.empty()!=b.driver.brand.empty()) return !a.driver.brand.empty();
            const auto av=RaceStandingCell(a,col),bv=RaceStandingCell(b,col); return raceControl.sortDescending?av>bv:av<bv;
        }
        const double av=RaceSortNumber(a,col),bv=RaceSortNumber(b,col); const bool ak=std::isfinite(av),bk=std::isfinite(bv);
        if(ak!=bk) return ak; if(!ak) return col==1? (raceControl.sortDescending?a.driver.number>b.driver.number:a.driver.number<b.driver.number):false;
        return raceControl.sortDescending?av>bv:av<bv;
    });
}
void SortRaceEvents(std::vector<RaceEvent>& events) {
    const int col=raceControl.eventSortColumn;
    std::stable_sort(events.begin(),events.end(),[&](const RaceEvent& a,const RaceEvent& b) {
        if(col==2 || col==4 || col==RaceEventLapField) {
            const double missing=std::numeric_limits<double>::quiet_NaN();
            auto number=[&](const RaceEvent& event) { return col==2?event.time:(col==4?RaceNumber(event.number):(event.lap>0?event.lap:missing)); };
            const double av=number(a),bv=number(b);
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
void ShowRaceControlPage();
// Shows or hides one column of the standings or event table; at least one column stays.
bool SetRaceField(bool feed,int index,bool on) {
    auto set=[&](auto& fields) {
        if(index<(feed?FirstEventField:0) || index>=static_cast<int>(fields.size())) return false;
        fields[index]=on;
        if(std::none_of(fields.begin(),fields.end(),[](bool v){return v;})) { fields[index]=true; return false; }
        return true;
    };
    const bool changed=feed?set(raceControl.eventFields):set(raceControl.fields);
    if(changed && !feed && index==RaceSectorTimesField) raceControl.showSectors=on;
    if(!feed && raceControl.sortColumn>=0 && !raceControl.fields[RaceFieldOf(raceControl.sortColumn)]) raceControl.sortColumn=-1;
    if(feed && !raceControl.eventFields[raceControl.eventSortColumn]) raceControl.eventSortColumn=FirstEventField;
    SaveRaceFields(); UpdateRaceControl(true);
    return changed;
}
void SetRaceInfo(int index,bool on) {
    if(index<0 || index>=RaceInfoCount) return;
    raceControl.info[index]=on; SaveRaceFields(); LayoutRaceControl(); UpdateRaceControl(true);
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
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Choose columns...");
        if(col>=0 && (!feed || col>=2)) { AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,2,L"Sort ascending"); AppendMenuW(menu,MF_STRING,3,L"Sort descending"); }
        if(!feed) AppendMenuW(menu,MF_STRING,4,L"Restore race order");
        const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,raceControl.window,nullptr); DestroyMenu(menu);
        if(command==1) ShowRaceControlPage(); if(command==2 || command==3) SetRaceSort(feed,col,command==3?1:0); if(command==4) SetRaceSort(false,-1,0);
        return 0;
    }
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,RaceHeaderProc,id);
    return DefSubclassProc(w,m,wp,lp);
}
