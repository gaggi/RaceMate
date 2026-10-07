#include "Telemetry.h"
#include "RaceControl.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef RACEMATE_DIAGNOSTICS
#include <fstream>
#endif

namespace {
using namespace rm;
constexpr COLORREF Background=RGB(246,247,249), Text=RGB(40,47,58);
constexpr UINT TrayMessage=WM_APP+1;
constexpr int PreviewId=110, EditId=111, HideId=112, ResetId=113, ExitId=114;
constexpr int OverlayCount=5;
constexpr int MainHeight=680;
const wchar_t* FuelNames[]={L"Last lap",L"Average 10",L"Average 5",L"Maximum",L"Minimum",L"Average"};
struct Placement { int x=40,y=120,scale=100,opacity=90; bool enabled=true, growLeft=false, growUp=false; };
struct Overlay {
    HWND window=nullptr; int kind=0; Placement place;
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ original=nullptr;
    HFONT font=nullptr, bold=nullptr, valueFont=nullptr; int width=0,height=0,dpi=0;
    std::wstring content;
    void Free() {
        if(dc) { SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
        if(font) DeleteObject(font); if(bold) DeleteObject(bold); if(valueFont) DeleteObject(valueFont);
        dc=nullptr; bitmap=nullptr; font=bold=valueFont=nullptr; width=height=dpi=0;
    }
};
const wchar_t* FooterTitles[]={L"None",L"Track",L"Incidents",L"Track Temperature",L"Air Temperature",L"Temperatures",L"Session Time Remaining",L"Session Time Elapsed",L"Current Lap",L"Current / Expected Final Lap",L"Estimated Laps Remaining",L"Local Time",L"Sim Time",L"Session Type",L"Track Wetness",L"Wind",L"Class SOF",L"Overlay Name"};
constexpr int FooterMetricCount=18;
struct FooterSettings { bool enabled=true; std::array<int,3> metrics{1,0,2}; };
struct App {
    HINSTANCE instance=nullptr; HWND main=nullptr,status=nullptr;
    HFONT font=nullptr,heading=nullptr; HBRUSH brush=nullptr;
    std::array<Overlay,OverlayCount> overlays;
    std::array<FooterSettings,2> footers;
    Frame frame; Session session; Telemetry telemetry; FuelCalculator fuel;
    EventRecorder events; PitHistory pitHistory; PitLimiterVisibility pitVisibility;
    std::array<std::array<bool,6>,2> driverColumns{};
    std::array<std::array<bool,2>,2> driverShortView{};
    std::array<bool,2> showDriver{true,true}, showStatus{true,true}, tableHeaders{}, columnHeaders{false,false};
    std::array<int,2> nameFormat{};
    std::array<std::array<int,3>,2> headerMetrics{{{17,0,13},{17,0,13}}};
    std::array<std::array<bool,3>,2> driverFlags{{{true,true,true},{true,true,true}}};
    RacePredictor predictor; PredictionOptions predictionOptions; FuelPlan fuelPlan;
    std::wstring config;
    std::array<std::vector<int>,2> columnOrder;
    bool preview=false, edit=false, hidden=false, diagnostics=false;
    bool startWithWindows=false, startMinimized=false, showTitleBar=false, relativeHidePit=false;
    StandingsOptions standings; bool showInterval=false, standingsLaps=false, standingsGained=false;
    std::vector<int> weatherOrder{0,1,2,3,4};
    std::array<bool,6> weatherDisplay{true,true,true,true,true,true};
    std::vector<int> fuelOrder{0,9,1,2,3,4,10,5,6,7,8};
    std::array<bool,11> fuelDisplay{true,true,true,true,true,true,true,true,true,true,true};
    std::array<bool,FuelMetricCount> fuelRows{true,true,true,true,true,true};
    bool excludeYellowFuelLaps=false;
    HWND overlaySettings=nullptr; int settingsKind=-1;
    bool previousConnection=false, settingsError=false, trayAvailable=false;
    int rows=7,gapDecimals=1,rate=10,timerMs=0,scroll=0;
    NOTIFYICONDATAW tray{}; std::vector<HWND> controls;
    int D(int v) const { return MulDiv(v,GetDpiForWindow(main),96); }
} app;
void OpenRaceControl();
void UpdateRaceControl(bool force=false);
void CloseRaceControl();
const wchar_t* Titles[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pit Limiter",L"Weather"};
// Keep existing settings section names when translating display labels.
const wchar_t* Sections[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pitlimiter",L"Weather"};
std::wstring Wide(const std::string& s) {
    if(s.empty()) return {};
    UINT cp=CP_UTF8; DWORD flags=MB_ERR_INVALID_CHARS;
    int n=MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) { cp=CP_ACP; flags=0; n=MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),nullptr,0); }
    std::wstring w(n,0); MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),w.data(),n); return w;
}
std::wstring Format(double x,int decimals=1) {
    if(!std::isfinite(x) || x<0) return L"—";
    wchar_t b[64]; swprintf_s(b,L"%.*f",decimals,x); return b;
}
std::wstring Lap(double x) {
    if(x<=0 || !std::isfinite(x)) return L"—";
    wchar_t b[64]; swprintf_s(b,L"%d:%06.3f",static_cast<int>(x)/60,std::fmod(x,60)); return b;
}
void Fill(HDC dc,RECT r,COLORREF color) { SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); }
void Label(HDC dc,const std::wstring& s,RECT r,COLORREF c,HFONT font,UINT align=DT_LEFT) {
    auto old=SelectObject(dc,font); SetTextColor(dc,c); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);
}
int NormalizeScale(int scale) { return ((std::clamp(scale,50,150)+5)/10)*10; }
int ReadInt(const wchar_t* section,const wchar_t* key,int fallback) { return GetPrivateProfileIntW(section,key,fallback,app.config.c_str()); }
const wchar_t* DriverColumnKeys[]={L"ShowLicense",L"ShowIRating",L"ShowTires",L"ShowLastPitLap",L"ShowPitTime",L"ShowPitStopTime"};
const wchar_t* DriverColumnTitles[]={L"LICENSE",L"iRATING",L"TIRES",L"PIT LAP",L"PIT TIME",L"PIT STOP"};
constexpr int DriverColumnWidths[]={82,72,64,70,80,88};
int DriverColumnWidth(int column) { return column==2 && std::any_of(app.session.drivers.begin(),app.session.drivers.end(),HasMultipleDryCompounds)?100:DriverColumnWidths[column]; }
std::vector<int> DefaultColumns(int kind) { return kind==0?std::vector<int>{0,1,2,15,3,7,8,9,10,11,12}:std::vector<int>{0,14,1,2,15,3,4,5,6,13,7,8,9,10,11,12}; }
const wchar_t* ColumnTitle(int id) { if(id==15) return L"STATUS"; if(id==14) return L"GAIN"; if(id==13) return L"LAPS"; const wchar_t* base[]={L"P",L"#",L"DRIVER",L"GAP",L"INT",L"BEST",L"LAST"}; return id<7?base[id]:DriverColumnTitles[id-7]; }
bool ColumnVisible(int kind,int id) { if(id==2) return app.showDriver[kind]; if(id==15) return app.showStatus[kind]; if(id==14) return kind==1 && app.standingsGained; if(id==13) return kind==1 && app.standingsLaps; return id>=7?app.driverColumns[kind][id-7]:(id==4?app.showInterval:true); }
int ColumnWidth(int id,int kind=-1) {
    if(kind>=0 && kind<2 && !app.columnHeaders[kind]) {
        const int compact[]={36,42,191,76,68,88,88,68,60,56,52,76,76,52,50,64};
        if(id==2 && app.nameFormat[kind]!=0) return 146;
        if(id==7 && app.driverShortView[kind][0]) return 56;
        if(id==8 && app.driverShortView[kind][1]) return 56;
        if(id==9 && std::any_of(app.session.drivers.begin(),app.session.drivers.end(),HasMultipleDryCompounds)) return 90;
        return compact[id];
    }
    if(id==15) return 70; if(id==14) return 64; if(id==13) return 64;
    const int widths[]={33,42,191,87,80,94,93}; return id<7?widths[id]:DriverColumnWidth(id-7);
}
std::vector<int> VisibleColumns(int kind) { std::vector<int> result; for(int id:app.columnOrder[kind]) if(ColumnVisible(kind,id)) result.push_back(id); return result; }
const wchar_t* FuelDisplayKeys[]={L"ShowLeaderDistance",L"ShowFinalLapTime",L"ShowTimeEmpty",L"ShowFuelAtEnd",L"ShowStopsRemaining",L"ShowTankGauge",L"ShowStintTimeline",L"ShowPitWindowPanel",L"ShowPitWindowColumn",L"ShowRemainingLeaderDistance",L"ShowRefuelToEnd"};
constexpr int FuelDisplayHeights[]={29,29,27,27,27,47,30,39,0,29,27};
int FuelFinishLap() {
    const auto& f=app.frame; const double distance=app.fuelPlan.lapsToFinish;
    return f.lapCompleted>=0 && std::isfinite(distance) && distance>=0?std::max(f.lapCompleted+1,f.lapCompleted+static_cast<int>(std::ceil(f.lapPct+distance-1e-6))):-1;
}
int StintCellCount() {
    const double average=app.fuelPlan.estimates[5].consumption,capacity=app.session.fuelCapacity;
    return std::isfinite(average) && average>0 && std::isfinite(capacity) && capacity>0?static_cast<int>(std::clamp(std::ceil(capacity/average),1.0,10000.0)):0;
}
const wchar_t* WeatherKeys[]={L"ShowTrackTemperature",L"ShowAirTemperature",L"ShowWind",L"ShowTrackWetness",L"ShowTrackUsage",L"ShowWindDirection"};
constexpr int WeatherHeights[]={36,36,190,36,36};
int WeatherHeight() { int height=40; for(int i=0;i<5;++i) if(app.weatherDisplay[i]) height+=WeatherHeights[i]+8; return height; }
int OverlayWidth(int kind) {
    if(kind<2) { int width=12; for(int id:VisibleColumns(kind)) width+=ColumnWidth(id,kind); return std::max(120,width); }
    if(kind==4) return 240;
    if(kind==2) return std::max(app.fuelDisplay[8] || std::any_of(app.fuelDisplay.begin(),app.fuelDisplay.end(),[](bool value){return value;})?465:365,app.fuelDisplay[6]?64+StintCellCount()*4:0);
    return 365;
}
COLORREF TireTextColor(const Row& row) {
    const auto label=TireLabel(row.driver,row.car.tireCompound);
    if(label=="Wet") return RGB(80,165,255);
    if(label=="Soft") return RGB(245,90,90);
    if(label=="Medium") return RGB(246,204,66);
    if(label=="Alternate") return row.driver.carPath.find("dw12")!=std::string::npos || row.driver.carName.find("DW12")!=std::string::npos?RGB(245,90,90):RGB(87,210,121);
    return RGB(232,234,237);
}
std::wstring CompletedLaps(const Row& row) {
    int completed=row.car.completed;
    if(completed<0 && row.car.lap>=0) completed=std::max(0,row.car.lap-1);
    const int index=row.driver.index;
    if(completed<0 && app.session.number==app.frame.session && index>=0 && index<MaxCars) completed=app.session.results[index].completed;
    return completed>=0?std::to_wstring(completed):L"—";
}
bool ShowOverlayHeader(int kind) { return kind<2?app.tableHeaders[kind]:app.showTitleBar; }
int TableRowsTop(int kind) { return 32+(app.columnHeaders[kind]?27:0); }
std::wstring DriverName(const Row& row,int kind) {
    auto name=Wide(row.driver.name); const auto first=name.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return L"—"; name=name.substr(first,name.find_last_not_of(L" \t\r\n")-first+1);
    if(app.nameFormat[kind]==0) return name;
    const auto split=name.find_first_of(L" \t"); if(split==std::wstring::npos) return name;
    const auto last=name.find_first_not_of(L" \t",split); if(last==std::wstring::npos) return name;
    const size_t initial=(name[0]>=0xd800 && name[0]<=0xdbff && name.size()>1 && name[1]>=0xdc00 && name[1]<=0xdfff)?2:1;
    return name.substr(0,initial)+(app.nameFormat[kind]==1?L". ":L" ")+name.substr(last);
}
std::wstring OverlayStatus(const Row& row,int kind) {
    if(row.car.pit) return L"PIT";
    if(row.car.towing) return L"TOW";
    if(app.driverFlags[kind][0] && (row.car.sessionFlags&SessionFlag::Disqualify)) return L"DQ";
    if(app.driverFlags[kind][0] && (row.car.sessionFlags&SessionFlag::Black)) return L"BLACK";
    if(app.driverFlags[kind][2] && (row.car.sessionFlags&SessionFlag::Repair)) return L"REPAIR";
    if(app.driverFlags[kind][1] && (row.car.sessionFlags&SessionFlag::Furled)) return L"SLOW";
    return row.car.outLap?L"OUT":L"";
}
std::wstring DriverValue(const Row& row,int column,int kind=-1) {
    const bool shortView=kind>=0 && kind<2 && column<2 && app.driverShortView[kind][column];
    if(column==0) {
        if(!shortView) return row.driver.license.empty()?L"—":Wide(row.driver.license);
        const auto start=row.driver.license.find_first_of("0123456789");
        if(start==std::string::npos) return L"—";
        const auto end=row.driver.license.find_first_not_of("0123456789.,",start);
        auto value=Wide(row.driver.license.substr(start,end==std::string::npos?end:end-start));
        std::replace(value.begin(),value.end(),L'.',L','); return value;
    }
    if(column==1) {
        if(row.driver.rating<=0) return L"—";
        if(!shortView) return std::to_wstring(row.driver.rating);
        auto value=Format(row.driver.rating/1000.0,1); std::replace(value.begin(),value.end(),L'.',L','); return value+L"K";
    }
    if(column==2) { const auto label=TireLabel(row.driver,row.car.tireCompound); return label.empty()?L"—":Wide(label); }
    if(column==3) return row.car.lastPitLap<0?L"—":std::to_wstring(row.car.lastPitLap);
    if(column==5) {
        const double duration=row.car.pit?row.car.pitStationary:row.car.lastPitStationary;
        return duration<0?L"—":L"≈"+Format(duration)+L" s";
    }
    return row.car.lastPitDuration<0?L"—":Format(row.car.lastPitDuration)+L" s";
}
void Load() {
    app.weatherOrder.clear(); wchar_t weatherSaved[128]{}; GetPrivateProfileStringW(L"Weather",L"ElementOrder",L"",weatherSaved,128,app.config.c_str());
    wchar_t* weatherToken=weatherSaved;
    while(*weatherToken) { wchar_t* end=nullptr; long id=wcstol(weatherToken,&end,10); if(end==weatherToken) break;
        if(id>=0 && id<5 && std::find(app.weatherOrder.begin(),app.weatherOrder.end(),id)==app.weatherOrder.end()) app.weatherOrder.push_back(static_cast<int>(id));
        weatherToken=end; if(*weatherToken==L',') ++weatherToken; else if(*weatherToken) break;
    }
    for(int id=0;id<5;++id) if(std::find(app.weatherOrder.begin(),app.weatherOrder.end(),id)==app.weatherOrder.end()) app.weatherOrder.push_back(id);
    for(int i=0;i<6;++i) app.weatherDisplay[i]=ReadInt(L"Weather",WeatherKeys[i],1)!=0;
    app.fuelOrder.clear(); wchar_t fuelSaved[256]{};
    GetPrivateProfileStringW(L"Fuel Calculator",L"ElementOrder",L"",fuelSaved,256,app.config.c_str());
    wchar_t* fuelToken=fuelSaved;
    while(*fuelToken) { wchar_t* end=nullptr; long id=wcstol(fuelToken,&end,10); if(end==fuelToken) break;
        if(id>=0 && id<11 && std::find(app.fuelOrder.begin(),app.fuelOrder.end(),id)==app.fuelOrder.end()) app.fuelOrder.push_back(static_cast<int>(id));
        fuelToken=end; if(*fuelToken==L',') ++fuelToken; else if(*fuelToken) break;
    }
    for(int id:{0,9,1,2,3,4,10,5,6,7,8}) if(std::find(app.fuelOrder.begin(),app.fuelOrder.end(),id)==app.fuelOrder.end()) {
        // Add new fields beside their related existing elements without resetting a custom order.
        auto anchor=std::find(app.fuelOrder.begin(),app.fuelOrder.end(),id==9?0:4);
        if((id==9 || id==10) && anchor!=app.fuelOrder.end()) app.fuelOrder.insert(anchor+1,id); else app.fuelOrder.push_back(id);
    }

    for(int i=0;i<11;++i) app.fuelDisplay[i]=ReadInt(L"Fuel Calculator",FuelDisplayKeys[i],1)!=0;
    for(int kind=0;kind<2;++kind) {
        app.showDriver[kind]=ReadInt(Sections[kind],L"ShowDriver",1)!=0;
        app.showStatus[kind]=ReadInt(Sections[kind],L"ShowStatus",1)!=0;
        app.nameFormat[kind]=std::clamp(ReadInt(Sections[kind],L"DriverNameFormat",0),0,2);
        app.tableHeaders[kind]=ReadInt(Sections[kind],L"ShowHeader",ReadInt(L"General",L"ShowTitleBar",0))!=0;
        const wchar_t* headerKeys[]={L"HeaderLeft",L"HeaderCenter",L"HeaderRight"}; const int headerDefaults[]={17,0,13};
        for(int slot=0;slot<3;++slot) app.headerMetrics[kind][slot]=std::clamp(ReadInt(Sections[kind],headerKeys[slot],headerDefaults[slot]),0,FooterMetricCount-1);
        app.columnHeaders[kind]=ReadInt(Sections[kind],L"DefaultColumnHeadersVersion",0)>=1 && ReadInt(Sections[kind],L"ShowColumnHeaders",0)!=0;
        const wchar_t* flagKeys[]={L"ShowBlackFlag",L"ShowSlowdown",L"ShowRequiredRepairs"};
        for(int i=0;i<3;++i) app.driverFlags[kind][i]=ReadInt(Sections[kind],flagKeys[i],1)!=0;
        app.driverShortView[kind][0]=ReadInt(Sections[kind],L"ShortLicenseView",0)!=0;
        app.driverShortView[kind][1]=ReadInt(Sections[kind],L"ShortIRatingView",0)!=0;
        app.footers[kind].enabled=ReadInt(Sections[kind],L"ShowFooter",1)!=0;
        const wchar_t* footerKeys[]={L"FooterLeft",L"FooterCenter",L"FooterRight"}; const int footerDefaults[]={1,0,2};
        for(int slot=0;slot<3;++slot) app.footers[kind].metrics[slot]=std::clamp(ReadInt(Sections[kind],footerKeys[slot],footerDefaults[slot]),0,FooterMetricCount-1);
        auto defaults=DefaultColumns(kind); auto& order=app.columnOrder[kind]; order.clear();
        wchar_t saved[256]{}; GetPrivateProfileStringW(Sections[kind],L"ColumnOrder",L"",saved,256,app.config.c_str());
        wchar_t* token=saved;
        while(*token) { wchar_t* end=nullptr; const long id=wcstol(token,&end,10); if(end==token) break;
            if(std::find(defaults.begin(),defaults.end(),id)!=defaults.end() && std::find(order.begin(),order.end(),id)==order.end()) order.push_back(static_cast<int>(id));
            token=end; if(*token==L',') ++token; else if(*token) break;
        }
        for(int id:defaults) if(std::find(order.begin(),order.end(),id)==order.end()) order.push_back(id);
        if(ReadInt(Sections[kind],L"StatusColumnVersion",0)<1) { std::erase(order,15); const auto driver=std::find(order.begin(),order.end(),2); order.insert(driver==order.end()?order.end():driver+1,15); }
    }
    for(int k=0;k<2;++k) for(int i=0;i<6;++i) app.driverColumns[k][i]=ReadInt(Sections[k],DriverColumnKeys[i],0)!=0;
    app.gapDecimals=std::clamp(ReadInt(L"Relative",L"GapDecimals",1),0,3);
    app.relativeHidePit=ReadInt(L"Relative",L"HideDriversInPit",0)!=0;
    app.showTitleBar=ReadInt(L"General",L"ShowTitleBar",0)!=0;
    app.rows=RelativeRowCount(ReadInt(L"Relative",L"Rows",ReadInt(L"General",L"Rows",7)));
    if(ReadInt(L"Relative",L"DefaultRowsVersion",0)<1 && app.rows==9) app.rows=7;
    app.rate=std::clamp(ReadInt(L"General",L"RefreshHz",10),2,20);
    app.standings.liveGaps=ReadInt(L"Standings",L"LiveGaps",0)!=0;
    app.standingsGained=ReadInt(L"Standings",L"ShowPositionsGained",0)!=0;
    app.standingsLaps=ReadInt(L"Standings",L"ShowLaps",0)!=0;
    app.showInterval=ReadInt(L"Standings",L"ShowInterval",0)!=0;
    app.standings.fallbackByRating=ReadInt(L"Standings",L"FallbackByIRating",0)!=0;
    app.standings.ownClass=ReadInt(L"Standings",L"OwnClass",ReadInt(L"General",L"OwnClass",0))!=0;
    app.standings.separateClasses=ReadInt(L"Standings",L"SeparateClasses",1)!=0;
    app.standings.leaders=std::clamp(ReadInt(L"Standings",L"Leaders",3),0,MaxCars);
    app.standings.ahead=std::clamp(ReadInt(L"Standings",L"Ahead",2),0,MaxCars);
    app.standings.behind=std::clamp(ReadInt(L"Standings",L"Behind",2),0,MaxCars);
    app.startWithWindows=ReadInt(L"General",L"StartWithWindows",0)!=0;
    app.startMinimized=ReadInt(L"General",L"StartMinimized",0)!=0;
    const wchar_t* keys[]={L"ShowLast",L"ShowAverage10",L"ShowAverage5",L"ShowMaximum",L"ShowMinimum",L"ShowAverage"};
    for(int i=0;i<FuelMetricCount;++i) app.fuelRows[i]=ReadInt(L"Fuel Calculator",keys[i],1)!=0;
    app.excludeYellowFuelLaps=ReadInt(L"Fuel Calculator",L"ExcludeYellowLaps",0)!=0;
    app.predictionOptions={}; app.predictionOptions.stops=StopMode::None;
    for(int i=0;i<OverlayCount;++i) {
        auto& p=app.overlays[i].place;
        if(i==4) {
            p.x=ReadInt(L"Weather",L"X",ReadInt(L"Wind",L"X",850)); p.y=ReadInt(L"Weather",L"Y",ReadInt(L"Wind",L"Y",120));
            p.scale=NormalizeScale(ReadInt(L"Weather",L"Scale",ReadInt(L"Wind",L"Scale",100))); p.opacity=std::clamp(ReadInt(L"Weather",L"Opacity",ReadInt(L"Wind",L"Opacity",90)),40,100);
            p.enabled=ReadInt(L"Weather",L"Enabled",ReadInt(L"Wind",L"Enabled",1))!=0; p.growLeft=ReadInt(L"Weather",L"GrowLeft",ReadInt(L"Wind",L"GrowLeft",0))!=0; p.growUp=ReadInt(L"Weather",L"GrowUp",ReadInt(L"Wind",L"GrowUp",0))!=0; continue;
        }
        p.x=ReadInt(Sections[i],L"X",i==1?480:40);
        p.y=ReadInt(Sections[i],L"Y",i==1?120:(i==2?470:(i==3?720:820)));
        p.scale=NormalizeScale(ReadInt(Sections[i],L"Scale",100));
        p.opacity=std::clamp(ReadInt(Sections[i],L"Opacity",90),40,100);
        p.enabled=ReadInt(Sections[i],L"Enabled",1)!=0;
        p.growLeft=ReadInt(Sections[i],L"GrowLeft",0)!=0; p.growUp=ReadInt(Sections[i],L"GrowUp",0)!=0;
    }
}
void Save() {
    bool success=true;
    std::wstring weatherOrderText; for(int id:app.weatherOrder) { if(!weatherOrderText.empty()) weatherOrderText+=L","; weatherOrderText+=std::to_wstring(id); }
    success=WritePrivateProfileStringW(L"Weather",L"ElementOrder",weatherOrderText.c_str(),app.config.c_str()) && success;
    for(int i=0;i<6;++i) success=WritePrivateProfileStringW(L"Weather",WeatherKeys[i],app.weatherDisplay[i]?L"1":L"0",app.config.c_str()) && success;
    std::wstring fuelOrderText; for(int id:app.fuelOrder) { if(!fuelOrderText.empty()) fuelOrderText+=L","; fuelOrderText+=std::to_wstring(id); }
    success=WritePrivateProfileStringW(L"Fuel Calculator",L"ElementOrder",fuelOrderText.c_str(),app.config.c_str()) && success;
    for(int i=0;i<11;++i) success=WritePrivateProfileStringW(L"Fuel Calculator",FuelDisplayKeys[i],app.fuelDisplay[i]?L"1":L"0",app.config.c_str()) && success;
    for(int kind=0;kind<2;++kind) { std::wstring order; for(int id:app.columnOrder[kind]) { if(!order.empty()) order+=L","; order+=std::to_wstring(id); } success=WritePrivateProfileStringW(Sections[kind],L"ColumnOrder",order.c_str(),app.config.c_str()) && success; }
    auto write=[&](const wchar_t* s,const wchar_t* k,int v) { const auto text=std::to_wstring(v); success=WritePrivateProfileStringW(s,k,text.c_str(),app.config.c_str()) && success; };
    for(int k=0;k<2;++k) { write(Sections[k],L"ShowFooter",app.footers[k].enabled); const wchar_t* keys[]={L"FooterLeft",L"FooterCenter",L"FooterRight"}; for(int slot=0;slot<3;++slot) write(Sections[k],keys[slot],app.footers[k].metrics[slot]); }
    for(int k=0;k<2;++k) { write(Sections[k],L"StatusColumnVersion",1); write(Sections[k],L"ShowDriver",app.showDriver[k]); write(Sections[k],L"ShowStatus",app.showStatus[k]); write(Sections[k],L"DriverNameFormat",app.nameFormat[k]); write(Sections[k],L"ShowHeader",app.tableHeaders[k]); write(Sections[k],L"DefaultColumnHeadersVersion",1); write(Sections[k],L"ShowColumnHeaders",app.columnHeaders[k]); }
    const wchar_t* headerKeys[]={L"HeaderLeft",L"HeaderCenter",L"HeaderRight"};
    for(int k=0;k<2;++k) for(int slot=0;slot<3;++slot) write(Sections[k],headerKeys[slot],app.headerMetrics[k][slot]);
    const wchar_t* flagKeys[]={L"ShowBlackFlag",L"ShowSlowdown",L"ShowRequiredRepairs"};
    for(int k=0;k<2;++k) for(int i=0;i<3;++i) write(Sections[k],flagKeys[i],app.driverFlags[k][i]);
    for(int k=0;k<2;++k) { write(Sections[k],L"ShortLicenseView",app.driverShortView[k][0]); write(Sections[k],L"ShortIRatingView",app.driverShortView[k][1]); }
    for(int k=0;k<2;++k) for(int i=0;i<6;++i) write(Sections[k],DriverColumnKeys[i],app.driverColumns[k][i]);
    write(L"Relative",L"GapDecimals",app.gapDecimals); write(L"Relative",L"HideDriversInPit",app.relativeHidePit); write(L"General",L"ShowTitleBar",app.showTitleBar);
    write(L"Relative",L"DefaultRowsVersion",1); write(L"Relative",L"Rows",app.rows); write(L"General",L"RefreshHz",app.rate);
    write(L"General",L"StartWithWindows",app.startWithWindows); write(L"General",L"StartMinimized",app.startMinimized);
    write(L"Standings",L"ShowPositionsGained",app.standingsGained);
    write(L"Standings",L"ShowLaps",app.standingsLaps);
    write(L"Standings",L"ShowInterval",app.showInterval); write(L"Standings",L"LiveGaps",app.standings.liveGaps);
    write(L"Standings",L"FallbackByIRating",app.standings.fallbackByRating);
    write(L"Standings",L"OwnClass",app.standings.ownClass); write(L"Standings",L"SeparateClasses",app.standings.separateClasses);
    write(L"Standings",L"Leaders",app.standings.leaders); write(L"Standings",L"Ahead",app.standings.ahead); write(L"Standings",L"Behind",app.standings.behind);
    const wchar_t* keys[]={L"ShowLast",L"ShowAverage10",L"ShowAverage5",L"ShowMaximum",L"ShowMinimum",L"ShowAverage"};
    for(int i=0;i<FuelMetricCount;++i) write(L"Fuel Calculator",keys[i],app.fuelRows[i]);
    write(L"Fuel Calculator",L"ExcludeYellowLaps",app.excludeYellowFuelLaps);
    for(int i=0;i<OverlayCount;++i) { const auto& p=app.overlays[i].place;
        write(Sections[i],L"X",p.x); write(Sections[i],L"Y",p.y); write(Sections[i],L"Scale",p.scale); write(Sections[i],L"Opacity",p.opacity); write(Sections[i],L"Enabled",p.enabled); write(Sections[i],L"GrowLeft",p.growLeft); write(Sections[i],L"GrowUp",p.growUp);
    }
    if(!success && !app.settingsError) { app.settingsError=true; MessageBoxW(app.main,L"Could not save settings. Please check write access to the RaceMate configuration folder.",L"RaceMate",MB_OK|MB_ICONWARNING); }
}
void RecalculatePlan() {
    if(app.preview) { FuelPlan p; p.samples=12; p.stintStartLap=4; p.lapsToFinish=12.55; p.confidence=Confidence::High;
        auto& race=p.prediction; race.status=PredictionStatus::Racing; race.leader=0; race.leaderPace=138.2; race.ownPace=139.4;
        auto& a=race.noStop; a.valid=true; a.winner=0; a.leaderTotalDistance=19+17/race.leaderPace; a.leaderDistance=12.02; a.finalLapTimeLeft=17;
        a.ownDistance=p.lapsToFinish; race.withStops=a;
        const double burns[]={3.09,3.12,3.10,3.21,2.98,3.11}; const int counts[]={1,10,5,12,12,12};
        for(int i=0;i<FuelMetricCount;++i) p.estimates[i]={burns[i],std::max(0.0,a.ownDistance*burns[i]-app.frame.fuel),counts[i],-1,-1};
        app.fuelPlan=p; return; }
    const auto prediction=app.predictor.Predict(app.session,app.frame,app.predictionOptions);
    app.fuelPlan=app.fuel.Plan(app.frame,0,false,0,&prediction);
}
const FuelPlan& Plan() { return app.fuelPlan; }
std::vector<Row> Rows(int kind) {
    if(kind==0) return Relative(app.session,app.frame,app.rows,false,app.relativeHidePit);
    std::vector<Row> rows;
    for(const auto& group:SelectStandings(app.session,app.frame,app.standings)) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
    return rows;
}
#include "Footer.inc"
int HeightWithTitle(int kind) {
    if(kind==0) return TableRowsTop(kind)+app.rows*27+(app.footers[0].enabled?28:0);
    if(kind==1) {
        int height=TableRowsTop(kind)+(app.footers[1].enabled?28:0),count=0;
        for(const auto& group:SelectStandings(app.session,app.frame,app.standings)) {
            height+=static_cast<int>(group.rows.size())*27+(app.standings.separateClasses?25:0); count+=static_cast<int>(group.rows.size());
        }
        return height+(count==0?54:0);
    }
    if(kind==2) {
        int h=44; for(int i=0;i<11;++i) if(app.fuelDisplay[i]) h+=FuelDisplayHeights[i];
        const int rows=static_cast<int>(std::count(app.fuelRows.begin(),app.fuelRows.end(),true));
        return h+(rows?24+29*rows:0);
    }
    return kind==4?WeatherHeight():90;
}
int BaseHeight(int kind) { return HeightWithTitle(kind)-(ShowOverlayHeader(kind)?0:32); }
std::wstring Gap(const Row& r,int kind) {
    if(kind==0) {
        if(r.player) return Format(0,app.gapDecimals);
        if(!std::isfinite(r.gap)) return L"—";
        return (r.gap>0?L"+":L"−")+Format(std::abs(r.gap),app.gapDecimals);
    }
    if((app.standings.separateClasses?r.car.classPosition:r.car.position)==1) return app.session.type!="Race" && !(r.car.best>0)?L"—":L"LEADER";
    if(r.lapDifference>0) return L"+"+std::to_wstring(r.lapDifference)+L" L";
    return std::isfinite(r.gap)?L"+"+Format(r.gap):L"—";
}
std::wstring Interval(const Row& row) {
    if((app.standings.separateClasses?row.car.classPosition:row.car.position)==1) return L"—";
    if(row.intervalLaps>0) return L"+"+std::to_wstring(row.intervalLaps)+L" L";
    return std::isfinite(row.interval)?L"+"+Format(row.interval):L"—";
}
std::wstring Content(int kind) {
    std::wstring result=app.preview?L"PREVIEW ":L"";
    result+=Wide(app.session.track)+Wide(app.session.type);
    result+=std::to_wstring(app.frame.replay)+std::to_wstring(app.frame.onTrack)+std::to_wstring(ReferenceCar(app.frame));
    if(kind<2) {
        result+=std::to_wstring(app.showDriver[kind])+std::to_wstring(app.showStatus[kind])+std::to_wstring(app.nameFormat[kind])+std::to_wstring(app.tableHeaders[kind])+std::to_wstring(app.columnHeaders[kind]);
        result+=app.footers[kind].enabled?L"F1":L"F0";
        if(app.tableHeaders[kind]) for(int id:app.headerMetrics[kind]) result+=L"H"+std::to_wstring(id)+L":"+FooterValue(id,kind)+L"|";
        if(app.footers[kind].enabled) for(int id:app.footers[kind].metrics) result+=std::to_wstring(id)+L":"+FooterValue(id,kind)+L"|";
        for(int id:app.columnOrder[kind]) result+=std::to_wstring(id)+L",";
        const auto groups=kind==1?SelectStandings(app.session,app.frame,app.standings):std::vector<StandingsGroup>{};
        if(kind==1) for(const auto& group:groups) result+=Wide(group.name)+std::to_wstring(group.classId);
        if(kind==1) result+=app.standingsLaps?L"L1":L"L0";
        std::vector<Row> rows;
        if(kind==0) rows=Rows(0); else for(const auto& group:groups) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
        for(const auto& r:rows) {
            if(ColumnVisible(kind,15)) result+=OverlayStatus(r,kind);
            if(kind==1 && app.standingsGained) result+=std::to_wstring(PositionsGained(app.session,r,app.standings.separateClasses));
            if(kind==1 && app.standingsLaps) result+=CompletedLaps(r);
            for(int i=0;i<6;++i) if(app.driverColumns[kind][i]) result+=DriverValue(r,i,kind);
            if(kind==1 && app.showInterval) result+=Interval(r);
            result+=Wide(r.driver.name)+Wide(r.driver.number)+std::to_wstring(r.car.position)+std::to_wstring(r.car.classPosition)+Gap(r,kind)+Lap(r.car.best)+Lap(r.car.last);
            result+=std::to_wstring(r.car.towing)+std::to_wstring(r.car.outLap)+std::to_wstring(r.car.pit)+std::to_wstring(r.lapDifference)+std::to_wstring(r.driver.classColor)+std::to_wstring(r.player);
        }
        result+=std::to_wstring(app.frame.incidents)+std::to_wstring(app.standings.separateClasses)+std::to_wstring(app.standings.liveGaps);
    } else if(kind==2) {
        for(int id:app.fuelOrder) result+=std::to_wstring(id)+L",";
        for(bool enabled:app.fuelDisplay) result+=enabled?L"1":L"0";
        const auto& p=Plan(); result+=std::to_wstring(p.stintStartLap)+L"|"+Format(p.lapsToFinish,4)+L"|"+Format(p.prediction.ownPace,3)+L"|"+std::to_wstring(app.frame.inStall)+L"|"; result+=std::to_wstring(app.frame.lapCompleted)+Format(app.frame.lapPct,3)+Format(app.frame.fuel,2)+Format(app.session.fuelCapacity)+std::to_wstring(app.frame.pit); result+=Format(p.prediction.noStop.leaderTotalDistance,2)+Format(p.prediction.noStop.leaderDistance,2)+Format(p.prediction.noStop.finalLapTimeLeft);
        for(int i=0;i<FuelMetricCount;++i) result+=std::to_wstring(app.fuelRows[i])+Format(p.estimates[i].consumption,2)+Format(p.estimates[i].add)+std::to_wstring(p.estimates[i].samples);
    } else if(kind==4) {
        for(int id:app.weatherOrder) result+=std::to_wstring(id)+L",";
        for(bool value:app.weatherDisplay) result+=value?L"1":L"0";
        if(app.weatherDisplay[0]) result+=FooterTemperature(app.frame.trackTemperature);
        if(app.weatherDisplay[1]) result+=FooterTemperature(app.frame.airTemperature);
        if(app.weatherDisplay[2]) { result+=Format(app.frame.windSpeed*3.6,0); if(app.weatherDisplay[5]) result+=Format(RelativeWindAngle(app.frame),3)+std::to_wstring(app.frame.windSpeed==0); }
        if(app.weatherDisplay[3]) result+=std::to_wstring(app.frame.trackWetness);
        if(app.weatherDisplay[4]) result+=Wide(app.session.trackUsage);
    }
    else result+=std::to_wstring(app.frame.limiter)+std::to_wstring(app.frame.limiterKnown);
    result+=std::to_wstring(app.edit); return result;
}
void Render(Overlay& o,HDC target) {
    RECT client{}; GetClientRect(o.window,&client);
    const int width=client.right,height=client.bottom,dpi=GetDpiForWindow(o.window);
    if(width<=0 || height<=0) return;
    const int fontSize=std::max(8,MulDiv(14,dpi*o.place.scale,9600));
    if(!o.dc || o.width!=width || o.height!=height || o.dpi!=dpi) {
        o.Free(); o.dc=CreateCompatibleDC(target); o.bitmap=CreateCompatibleBitmap(target,width,height); o.original=SelectObject(o.dc,o.bitmap);
        o.font=CreateFontW(-fontSize,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        o.bold=CreateFontW(-fontSize,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        if(o.kind==4) o.valueFont=CreateFontW(-MulDiv(32,dpi*o.place.scale,9600),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        o.width=width; o.height=height; o.dpi=dpi;
    }
    HDC dc=o.dc;
    auto px=[&](int v){return MulDiv(v,dpi*o.place.scale,9600);};
    auto rect=[&](int x,int y,int w,int h){return RECT{px(x),px(y),px(x+w),px(y+h)};};
    const COLORREF white=RGB(232,234,237), muted=RGB(152,159,169), yellow=RGB(246,204,66);
    Fill(dc,client,RGB(18,21,26));
    const int titleOffset=ShowOverlayHeader(o.kind)?0:px(32); SetViewportOrgEx(dc,0,-titleOffset,nullptr);
    if(ShowOverlayHeader(o.kind)) {
    Fill(dc,{0,0,width,px(32)},RGB(37,41,48));
    Fill(dc,{0,0,px(3),px(32)},yellow);
    if(o.kind<2) {
        const auto& metrics=app.headerMetrics[o.kind]; const int margin=px(12),third=width/3;
        for(int slot=0;slot<3;++slot) {
            RECT bounds{slot==0?margin:(slot==1?third:2*third),0,slot==0?third:(slot==1?2*third:width-margin),px(32)};
            if(slot==0 && metrics[1]==0) bounds.right=metrics[2]==0?width-margin:2*third;
            if(slot==2 && metrics[1]==0) bounds.left=metrics[0]==0?margin:2*third;
            Label(dc,FooterValue(metrics[slot],o.kind),bounds,metrics[slot]==2?yellow:white,metrics[slot]==17 || metrics[slot]==2?o.bold:o.font,slot==0?DT_LEFT:(slot==1?DT_CENTER:DT_RIGHT));
        }
    } else {
    std::wstring title=Titles[o.kind]; if(o.kind==3) title=L"PIT LANE";
    Label(dc,title,rect(12,0,230,32),white,o.bold);
    const auto mode=app.frame.replay?L"REPLAY":(!app.frame.onTrack?L"SPOTTING":Wide(app.session.type));
    if(o.kind!=4) Label(dc,app.edit?L"MOVE":(app.preview?L"PREVIEW":mode),{width-px(145),0,width-px(12),px(32)},muted,o.font,DT_RIGHT);
    }
    }
    if(o.kind<2) {
        const auto columns=VisibleColumns(o.kind); int headerX=12;
        if(app.columnHeaders[o.kind]) for(int id:columns) { Label(dc,id==3 && o.kind==0?L"GAP ≈":ColumnTitle(id),rect(headerX,34,ColumnWidth(id,o.kind)-12,24),muted,o.font,id==2 || id==15?DT_LEFT:DT_RIGHT); headerX+=ColumnWidth(id,o.kind); }
        int y=TableRowsTop(o.kind); const auto rows=o.kind==0?Rows(0):std::vector<Row>{};
        const auto groups=o.kind==1?SelectStandings(app.session,app.frame,app.standings):std::vector<StandingsGroup>{};
        const bool rowsEmpty=o.kind==0?rows.empty():std::none_of(groups.begin(),groups.end(),[](const StandingsGroup& group){return !group.rows.empty();});
        auto drawRow=[&](const Row& r) {
            Fill(dc,{0,px(y),width,px(y+27)},r.player?RGB(54,49,30):((y/27)%2?RGB(25,28,34):RGB(20,23,28)));
            const unsigned col=r.driver.classColor;
            Fill(dc,rect(0,y,3,27),RGB((col>>16)&255,(col>>8)&255,col&255));
            COLORREF color=r.player?yellow:white;
            if(o.kind==0 && app.session.type=="Race" && !r.player && r.lapDifference>0) color=RGB(236,120,114);
            if(o.kind==0 && app.session.type=="Race" && !r.player && r.lapDifference<0) color=RGB(108,174,238);
            int x=12;
            for(int id:columns) {
                const int w=ColumnWidth(id,o.kind); std::wstring text; COLORREF ink=color;
                if(id==0) { const int position=o.kind==1 && app.standings.separateClasses?r.car.classPosition:r.car.position; text=position>0?std::to_wstring(position):L"—"; }
                if(id==1) { const COLORREF badge=RGB((col>>16)&255,(col>>8)&255,col&255); Fill(dc,rect(x,y+4,w-6,19),badge); ink=(299*((col>>16)&255)+587*((col>>8)&255)+114*(col&255)>140000)?RGB(12,14,18):RGB(255,255,255); text=Wide(r.driver.number); }
                if(id==2) text=DriverName(r,o.kind);
                if(id==3) text=Gap(r,o.kind); if(id==4) text=Interval(r); if(id==5) text=Lap(r.car.best); if(id==6) text=Lap(r.car.last);
                if(id==15) { text=OverlayStatus(r,o.kind); ink=text==L"BLACK" || text==L"DQ"?RGB(236,120,114):(text==L"TOW" || text==L"REPAIR" || text==L"SLOW"?RGB(245,163,65):yellow); }
                else if(id==14) {
                    const int gained=PositionsGained(app.session,r,app.standings.separateClasses);
                    text=gained==-MaxCars?L"—":(gained==0?L"0":(gained>0?L"↑ ":L"↓ ")+std::to_wstring(std::abs(gained)));
                    ink=gained==-MaxCars || gained==0?muted:(gained>0?RGB(96,208,136):RGB(236,120,114));
                }
                else if(id==13) text=CompletedLaps(r);
                else if(id>=7) { const int i=id-7; text=DriverValue(r,i,o.kind); const unsigned lc=r.driver.licenseColor; ink=i==0?RGB((lc>>16)&255,(lc>>8)&255,lc&255):(i==2?TireTextColor(r):white); if(i==2 && TireLabel(r.driver,r.car.tireCompound)=="Primary") Fill(dc,rect(x,y+4,w-12,19),RGB(0,0,0)); }
                Label(dc,text,id==1?rect(x,y+4,w-6,19):rect(x,y,w-12,27),ink,id<2 || (id==2 && r.player)?o.bold:o.font,id==2 || id==15?DT_LEFT:(id==1?DT_CENTER:DT_RIGHT)); x+=w;
            }
            y+=27;
        };
        if(o.kind==0) {
            const auto reference=std::find_if(rows.begin(),rows.end(),[](const Row& row){return row.player;});
            if(reference!=rows.end()) y+=(app.rows/2-static_cast<int>(reference-rows.begin()))*27;
            for(const auto& row:rows) drawRow(row);
        }
        else for(const auto& group:groups) {
            if(app.standings.separateClasses) {
                Fill(dc,{0,px(y),width,px(y+25)},RGB(35,39,46));
                Label(dc,Wide(group.name),rect(12,y,520,25),RGB((group.color>>16)&255,(group.color>>8)&255,group.color&255),o.bold);
                y+=25;
            }
            int previous=0;
            for(const auto& row:group.rows) {
                const int position=app.standings.separateClasses?row.car.classPosition:row.car.position;
                const bool skipped=previous && position>previous+1;
                drawRow(row);
                if(skipped) {
                    HPEN pen=CreatePen(PS_DOT,1,RGB(108,115,128)); const auto old=SelectObject(dc,pen);
                    MoveToEx(dc,px(12),px(y-27),nullptr); LineTo(dc,width-px(12),px(y-27)); SelectObject(dc,old); DeleteObject(pen);
                }
                previous=position;
            }
        }
        if(rowsEmpty) Label(dc,L"Waiting for driver and position data …",rect(12,TableRowsTop(o.kind)+16,335,28),muted,o.font);
        if(app.footers[o.kind].enabled) {
            const int footer=o.kind==0?TableRowsTop(o.kind)+app.rows*27:y+(rowsEmpty?54:0);
            Fill(dc,{0,px(footer),width,height+titleOffset},RGB(30,34,41));
            const auto& metrics=app.footers[o.kind].metrics;
            const int margin=px(12),third=width/3;
            for(int slot=0;slot<3;++slot) {
                RECT bounds{slot==0?margin:(slot==1?third:2*third),px(footer),slot==0?third:(slot==1?2*third:width-margin),height+titleOffset};
                if(slot==0 && metrics[1]==0) bounds.right=metrics[2]==0?width-margin:2*third;
                if(slot==2 && metrics[1]==0) bounds.left=metrics[0]==0?margin:2*third;
                Label(dc,FooterValue(metrics[slot],o.kind),bounds,metrics[slot]==2?yellow:muted,metrics[slot]==2?o.bold:o.font,slot==0?DT_LEFT:(slot==1?DT_CENTER:DT_RIGHT));
            }
        }
    } else if(o.kind==2) {
        const auto& p=Plan(); const auto& scene=p.prediction.noStop;
        double pace=p.prediction.ownPace; if(pace<=0) pace=app.frame.lastLap>0?app.frame.lastLap:app.frame.bestLap;
        const auto baseWindow=CalculatePitWindow(app.session,app.frame,p.lapsToFinish,p.estimates[5].consumption,pace);
        int y=36; const int layoutWidth=OverlayWidth(2);
        const bool endKnown=std::isfinite(baseWindow.fuelAtEnd);
        const bool tankKnown=app.frame.fuelKnown && std::isfinite(app.frame.fuel) && app.frame.fuel>=0;
        const double capacity=app.session.fuelCapacity; const bool capacityKnown=std::isfinite(capacity) && capacity>0;
        const int current=app.frame.lapCompleted>=0?app.frame.lapCompleted+1:-1;
        const int finishLap=FuelFinishLap();
        for(int element:app.fuelOrder) switch(element) {
        case 0: {
        if(app.fuelDisplay[0]) {
        Label(dc,L"Leader Distance",rect(12,y,212,29),muted,o.font);
        Label(dc,Format(scene.leaderTotalDistance,2)+L" laps",rect(325,y,128,29),white,o.bold,DT_RIGHT);
        }
        if(app.fuelDisplay[0]) y+=FuelDisplayHeights[0];
        break; }
        case 9: {
        if(app.fuelDisplay[9]) {
            Label(dc,L"Remaining Leader Distance",rect(12,y,300,29),muted,o.font);
            Label(dc,Format(scene.leaderDistance,2)+L" laps",rect(325,y,128,29),white,o.bold,DT_RIGHT);
            y+=FuelDisplayHeights[9];
        }
        break; }
        case 10: {
        if(app.fuelDisplay[10]) {
            Label(dc,L"Refuel to End (Average)",rect(12,y,280,27),muted,o.font);
            const double add=p.estimates[5].add;
            Label(dc,add>=0?Format(add,1)+L" L":L"—",rect(300,y,153,27),yellow,o.bold,DT_RIGHT);
            y+=FuelDisplayHeights[10];
        }
        break; }
        case 1: {
        if(app.fuelDisplay[1]) {
        Label(dc,L"Time left at final lap start",rect(12,y,230,29),muted,o.font);
        Label(dc,Format(scene.finalLapTimeLeft)+L" s",rect(345,y,108,29),yellow,o.bold,DT_RIGHT);
        }
        if(app.fuelDisplay[1]) y+=FuelDisplayHeights[1];
        break; }
        case 2: {
        if(app.fuelDisplay[2]) {
        Label(dc,L"Time Empty (Average)",rect(12,y,260,27),muted,o.font);
        wchar_t clock[64]{}; if(baseWindow.timeEmpty>=0 && baseWindow.timeEmpty<36000000) { const int seconds=static_cast<int>(baseWindow.timeEmpty); swprintf_s(clock,L"%d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60); }
        const std::wstring empty=clock[0]?clock:L"—";
        Label(dc,empty,rect(280,y,173,27),white,o.bold,DT_RIGHT);
        }
        if(app.fuelDisplay[2]) y+=FuelDisplayHeights[2];
        break; }
        case 3: {
        if(app.fuelDisplay[3]) {
        Label(dc,L"Fuel at End (Average)",rect(12,y,260,27),muted,o.font);
        const auto end=endKnown?(baseWindow.fuelAtEnd<0?L"−":L"")+Format(std::abs(baseWindow.fuelAtEnd),1)+L" L":L"—";
        Label(dc,end,rect(280,y,173,27),!endKnown?muted:(baseWindow.fuelAtEnd<0?RGB(238,91,91):RGB(90,197,104)),o.bold,DT_RIGHT);
        } y+=app.fuelDisplay[3]?27:0;
        break; }
        case 4: {
        if(app.fuelDisplay[4]) {
        Label(dc,L"Stops Remaining (Average)",rect(12,y,300,27),muted,o.font);
        Label(dc,baseWindow.stops>=0?std::to_wstring(baseWindow.stops):L"—",rect(330,y,123,27),white,o.bold,DT_RIGHT);
        }
        if(app.fuelDisplay[4]) y+=27;
        break; }
        case 5: {
        if(app.fuelDisplay[5]) {
        Fill(dc,rect(12,y,layoutWidth-24,19),RGB(65,70,78)); Fill(dc,rect(14,y+2,layoutWidth-28,15),RGB(32,36,42));
        if(tankKnown && capacityKnown) Fill(dc,rect(14,y+2,static_cast<int>((layoutWidth-28)*std::clamp(app.frame.fuel/capacity,0.0,1.0)),15),white);
        Label(dc,L"E",rect(12,y+19,24,24),muted,o.font);
        Label(dc,tankKnown?Format(app.frame.fuel,1)+L" L":L"—",rect(12,y+19,layoutWidth-24,24),white,o.bold,DT_CENTER);
        Label(dc,capacityKnown?Format(capacity,1)+L" L max":L"—",rect(layoutWidth-150,y+19,138,24),muted,o.font,DT_RIGHT);
        }
        if(app.fuelDisplay[5]) y+=FuelDisplayHeights[5];
        break; }
        case 6: {
        if(app.fuelDisplay[6]) {
        Label(dc,current>0?L"L"+std::to_wstring(current):L"PIT",rect(12,y+2,34,24),white,o.bold);
        const int first=p.stintStartLap>0?p.stintStartLap:std::max(1,current);
        const int count=finishLap>0?std::max(0,std::min(StintCellCount(),finishLap-first+1)):StintCellCount();
        // Narrow blocks stay taller than they are wide, even for short stints.
        const int pitch=count>0?std::max(1,std::min(11,(OverlayWidth(2)-64)/count)):11;
        for(int i=0;i<count;++i) {
            const int lap=first+i,x=52+i*pitch,w=std::max(1,pitch-3);
            const bool open=baseWindow.status==PitWindowStatus::Open && lap>=baseWindow.firstLap && lap<=baseWindow.lastLap;
            const COLORREF ink=open?RGB(49,157,67):(lap==current?yellow:RGB(53,57,64));
            const RECT block=rect(x,y,w,26); Fill(dc,block,ink);
            if(lap==current && open) Fill(dc,{block.left,block.top,block.right,block.top+std::max(1,px(3))},yellow);
        }
        }
        if(app.fuelDisplay[6]) y+=FuelDisplayHeights[6];
        break; }
        case 7: {
        if(app.fuelDisplay[7]) {
        Label(dc,current>0?L"Current Lap "+std::to_wstring(current)+L" of "+(finishLap>0?std::to_wstring(finishLap):L"—"):L"Current Lap —",rect(12,y,190,39),muted,o.font);
        const COLORREF windowColor=baseWindow.openNow?RGB(39,153,62):RGB(77,82,91);
        Fill(dc,rect(205,y,248,39),windowColor); Fill(dc,rect(301,y+2,150,35),RGB(18,21,26));
        const bool rangeKnown=baseWindow.status==PitWindowStatus::Open;
        Label(dc,baseWindow.openNow?L"OPEN":L"CLOSED",rect(207,rangeKnown?y:y+4,92,rangeKnown?20:30),white,o.bold,DT_CENTER);
        if(rangeKnown) Label(dc,L"L"+std::to_wstring(baseWindow.firstLap)+L"–"+std::to_wstring(baseWindow.lastLap),rect(207,y+19,92,20),white,o.font,DT_CENTER);
        double fill=-1;
        if(rangeKnown && tankKnown && capacityKnown && std::isfinite(baseWindow.fuelAtEnd)) {
            if(baseWindow.stops<=1) fill=std::min(capacity,std::max(0.0,-baseWindow.fuelAtEnd));
            else {
                const double untilStop=baseWindow.openNow?0:std::max(0.0,baseWindow.firstLap-(app.frame.lapCompleted+app.frame.lapPct));
                const double fuelAtStop=std::max(0.0,app.frame.fuel-untilStop*p.estimates[5].consumption);
                fill=std::max(0.0,capacity-fuelAtStop);
            }
        }
        Label(dc,fill>=0?L"+"+Format(fill,1)+L" L":L"—",rect(305,y+2,143,35),white,o.bold,DT_CENTER);
        }
        if(app.fuelDisplay[7]) y+=FuelDisplayHeights[7];
        break; }
        case 8: {
        if(std::any_of(app.fuelRows.begin(),app.fuelRows.end(),[](bool shown){return shown;})) {
        Label(dc,L"CONSUMPTION",rect(12,y,151,24),muted,o.font);
        Label(dc,L"L / LAP",rect(170,y,70,24),muted,o.font,DT_RIGHT);
        Label(dc,L"ADD TO FINISH",rect(250,y,103,24),yellow,o.font,DT_RIGHT);
        if(app.fuelDisplay[8]) Label(dc,L"PIT ≈",rect(363,y,90,24),muted,o.font,DT_RIGHT);
        y+=24; }
        for(int i:{0,5,1,2,3,4}) if(app.fuelRows[i]) {
            const auto& estimate=p.estimates[i];
            Fill(dc,{0,px(y),width,px(y+29)},(y/29)%2?RGB(25,28,34):RGB(20,23,28));
            std::wstring name=FuelNames[i]; if((i==1 && estimate.samples<10) || (i==2 && estimate.samples<5)) name+=L" ("+std::to_wstring(estimate.samples)+L")";
            Label(dc,name,rect(12,y,151,29),white,o.font); Label(dc,estimate.samples?Format(estimate.consumption,2):L"—",rect(170,y,70,29),white,o.font,DT_RIGHT);
            Label(dc,Format(estimate.add)+L" L",rect(250,y,103,29),yellow,o.bold,DT_RIGHT);
            const auto window=CalculatePitWindow(app.session,app.frame,p.lapsToFinish,estimate.consumption);
            const auto text=window.status==PitWindowStatus::Open?L"L"+std::to_wstring(window.firstLap)+L"–"+std::to_wstring(window.lastLap):(window.status==PitWindowStatus::NoStopNeeded?L"No stop":(window.status==PitWindowStatus::ExtraStopNeeded?L"No window":L"—"));
            if(app.fuelDisplay[8]) Label(dc,text,rect(363,y,90,29),window.status==PitWindowStatus::Open?RGB(90,197,104):muted,o.font,DT_RIGHT); y+=29;
        }
        break; }
        }

    } else if(o.kind==4) {
#include "WeatherRender.inc"
    } else {
        const bool known=app.frame.limiterKnown;
        const COLORREF color=!known?RGB(95,100,110):(app.frame.limiter?RGB(27,135,68):RGB(182,41,44));
        Fill(dc,{0,px(32),width,height+titleOffset},color);
        Label(dc,!known?L"PIT LIMITER STATUS UNKNOWN":(app.frame.limiter?L"PIT LIMITER ACTIVE":L"PIT LIMITER NOT ACTIVE"),{px(12),px(38),width-px(12),height+titleOffset-px(6)},RGB(255,255,255),o.bold,DT_CENTER);
    }
    SetViewportOrgEx(dc,0,0,nullptr);
    BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY);
}
// X/Y store the fixed corner, rather than the changing top-left origin.
void CaptureOverlayCorner(Overlay& o) {
    RECT r{}; GetWindowRect(o.window,&r);
    o.place.x=o.place.growLeft?r.right:r.left; o.place.y=o.place.growUp?r.bottom:r.top;
}
void SetOverlayOrigin(Overlay& o,int x,int y) {
    const int dpi=GetDpiForWindow(o.window);
    o.place.x=x+(o.place.growLeft?MulDiv(OverlayWidth(o.kind),dpi*o.place.scale,9600):0);
    o.place.y=y+(o.place.growUp?MulDiv(BaseHeight(o.kind),dpi*o.place.scale,9600):0);
}
void Fit(Overlay& o,bool recover=false) {
    const int dpi=GetDpiForWindow(o.window);
    const int w=MulDiv(OverlayWidth(o.kind),dpi*o.place.scale,9600),h=MulDiv(BaseHeight(o.kind),dpi*o.place.scale,9600);
    int x=o.place.x-(o.place.growLeft?w:0),y=o.place.y-(o.place.growUp?h:0);
    RECT r{x,y,x+w,y+h};
    HMONITOR monitor=MonitorFromRect(&r,MONITOR_DEFAULTTONULL);
    if(recover || !monitor) {
        if(!monitor) { monitor=MonitorFromWindow(app.main,MONITOR_DEFAULTTONEAREST); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info); x=info.rcWork.left+30; y=info.rcWork.top+50; }
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
        x=static_cast<int>(std::clamp<LONG>(x,info.rcWork.left-w+60,info.rcWork.right-60));
        const LONG maxY=o.kind==2?std::max(info.rcWork.top,info.rcWork.bottom-h):info.rcWork.bottom-32;
        y=static_cast<int>(std::clamp<LONG>(y,info.rcWork.top,maxY));
        o.place.x=x+(o.place.growLeft?w:0); o.place.y=y+(o.place.growUp?h:0);
    }
    SetWindowPos(o.window,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE);
    o.Free(); o.content.clear(); InvalidateRect(o.window,nullptr,FALSE);
    SetLayeredWindowAttributes(o.window,0,static_cast<BYTE>(o.place.opacity*255/100),LWA_ALPHA);
}
void UpdateOverlays() {
    const bool pitVisible=app.pitVisibility.Update(app.session,app.frame);
    RecalculatePlan();
    for(auto& o:app.overlays) {
        bool visible=o.place.enabled && !app.hidden && (o.kind!=4 || std::any_of(app.weatherDisplay.begin(),app.weatherDisplay.begin()+5,[](bool value){return value;})) && (app.preview || (app.frame.connected && (o.kind!=3 || pitVisible)));
        auto style=GetWindowLongPtrW(o.window,GWL_EXSTYLE);
        const auto desired=app.edit?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT);
        if(style!=desired) SetWindowLongPtrW(o.window,GWL_EXSTYLE,desired);
        if(visible && !IsWindowVisible(o.window)) ShowWindow(o.window,SW_SHOWNOACTIVATE);
        if(!visible && IsWindowVisible(o.window)) ShowWindow(o.window,SW_HIDE);
        if(visible) {
            const int desiredHeight=MulDiv(BaseHeight(o.kind),GetDpiForWindow(o.window)*o.place.scale,9600);
            RECT client{}; GetClientRect(o.window,&client);
            const int desiredWidth=MulDiv(OverlayWidth(o.kind),GetDpiForWindow(o.window)*o.place.scale,9600);
            if(client.bottom!=desiredHeight || client.right!=desiredWidth) Fit(o);
            auto content=Content(o.kind);
            if(content!=o.content) { o.content=std::move(content); InvalidateRect(o.window,nullptr,FALSE); }
        }
    }
}
void Timer() {
    const int ms=app.preview || app.frame.connected?1000/app.rate:1000;
    if(app.timerMs!=ms) { KillTimer(app.main,1); SetTimer(app.main,1,ms,nullptr); app.timerMs=ms; }
}
void PreviewData(double time) {
    app.session=DemoSession(); app.frame=DemoFrame(time); app.frame.pit=false;
    for(auto& d:app.session.drivers) { d.license="A 3.45"; d.licenseColor=0x55aaff; auto& c=app.frame.cars[d.index]; c.tireCompound=d.index%2; c.lastPitLap=8; c.lastPitDuration=42.3; c.lastPitStationary=21.6; }
    app.frame.cars[1].outLap=true; app.frame.cars[2].pit=true; app.frame.cars[2].pitStationary=8.4; app.frame.cars[5].surface=-1;
    for(auto& driver:app.session.drivers) if(driver.index>=6) {
        driver.classId=2; driver.className="GT4"; driver.classColor=0x67b7ff;
        app.frame.cars[driver.index].classPosition=driver.index-5;
    }
}
void Tick() {
    if(app.preview) PreviewData(GetTickCount64()/1000.0);
    else {
        const bool connected=app.telemetry.Read(app.frame,app.session);
        if(!connected) { app.frame={}; app.events.Update(app.session,app.frame); app.session={}; app.fuel.Reset(); app.predictor.Reset(); app.pitHistory.Reset(); }
        else { app.pitHistory.Update(app.session,app.frame); app.events.Update(app.session,app.frame); app.fuel.Update(app.session,app.frame,app.excludeYellowFuelLaps); app.predictor.Update(app.session,app.frame); }
    }
    std::wstring status=app.preview?L"Preview · sample data, no live telemetry":(app.frame.connected?L"Connected · "+Wide(app.session.track):L"Waiting for iRacing …");
    if(app.edit) status+=L" · Move overlays";
    if(!app.trayAvailable && !app.diagnostics) status+=L" · No tray icon; closing Settings exits RaceMate";
    wchar_t previous[512]{}; GetWindowTextW(app.status,previous,512);
    if(status!=previous) SetWindowTextW(app.status,status.c_str());
    UpdateOverlays(); UpdateRaceControl(); Timer();
}
void ShowSettings() { ShowWindow(app.main,SW_RESTORE); SetForegroundWindow(app.main); }
void TogglePreview() { app.preview=!app.preview; app.frame={}; app.session={}; app.fuel.Reset(); app.predictor.Reset(); app.pitHistory.Reset(); app.telemetry.Close(); app.events.Reset();
    SendMessageW(GetDlgItem(app.main,PreviewId),BM_SETCHECK,app.preview?BST_CHECKED:BST_UNCHECKED,0); Tick(); }
void ToggleEdit() { app.edit=!app.edit; SendMessageW(GetDlgItem(app.main,EditId),BM_SETCHECK,app.edit?BST_CHECKED:BST_UNCHECKED,0); Tick(); }
void ToggleHidden() { app.hidden=!app.hidden; SetWindowTextW(GetDlgItem(app.main,HideId),app.hidden?L"Show overlays":L"Hide overlays"); UpdateOverlays(); }
LRESULT CALLBACK OverlayProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    auto* o=reinterpret_cast<Overlay*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(m==WM_NCCREATE) { o=static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); o->window=w; SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(o)); }
    if(!o) return DefWindowProcW(w,m,wp,lp);
    switch(m) {
    case WM_PAINT: { PAINTSTRUCT p{}; HDC dc=BeginPaint(w,&p); Render(*o,dc); EndPaint(w,&p); return 0; }
    case WM_PRINTCLIENT: Render(*o,reinterpret_cast<HDC>(wp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: return app.edit?HTCAPTION:HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_EXITSIZEMOVE: CaptureOverlayCorner(*o); Save(); return 0;
    case WM_DPICHANGED: Fit(*o); return 0;
    case WM_DISPLAYCHANGE: Fit(*o,true); return 0;
    case WM_DESTROY: o->Free(); return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
HWND Control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,app.D(x),app.D(y),app.D(w),app.D(h),app.main,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE); app.controls.push_back(c); return c;
}
LRESULT CALLBACK GroupProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_PAINT || m==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{}; HDC dc=m==WM_PAINT?BeginPaint(w,&paint):reinterpret_cast<HDC>(wp);
        const int saved=SaveDC(dc);
        for(HWND child:app.controls) {
            if(child==w) continue;
            RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&r),2);
            ExcludeClipRect(dc,r.left,r.top,r.right,r.bottom);
        }
        RECT r{}; GetClientRect(w,&r); FillRect(dc,&r,app.brush);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(220,222,225));
        auto oldPen=SelectObject(dc,pen); auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,0,app.D(9),r.right,r.bottom);
        wchar_t text[128]{}; GetWindowTextW(w,text,128);
        auto oldFont=SelectObject(dc,app.font); SIZE size{}; GetTextExtentPoint32W(dc,text,static_cast<int>(wcslen(text)),&size);
        RECT title{app.D(10),0,app.D(16)+size.cx,app.D(23)}; FillRect(dc,&title,app.brush);
        Label(dc,text,title,Text,app.font);
        SelectObject(dc,oldFont); SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(pen);
        RestoreDC(dc,saved); if(m==WM_PAINT) EndPaint(w,&paint); return 0;
    }
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,GroupProc,id);
    return DefSubclassProc(w,m,wp,lp);
}
void Check(const wchar_t* text,int id,int x,int y,int w,bool checked) {
    auto c=Control(L"BUTTON",text,id,x,y,w,24,BS_AUTOCHECKBOX|WS_TABSTOP); SendMessageW(c,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
}
void Combo(int id,int x,int y,int width,const std::vector<std::wstring>& items,int selected) {
    auto c=Control(L"COMBOBOX",L"",id,x,y,width,260,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    for(const auto& item:items) SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));
    SendMessageW(c,CB_SETCURSEL,selected,0);
}
#include "SettingsScroll.inc"
#include "Settings.inc"
#include "RaceControl.inc"
void CreateControls() {
    app.font=CreateFontW(-app.D(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    app.heading=CreateFontW(-app.D(22),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    auto title=Control(L"STATIC",L"RaceMate",0,24,15,360,32); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(app.heading),TRUE);
    Control(L"STATIC",L"Native iRacing Overlays · Version 0.4.36",0,25,49,660,22);
    app.status=Control(L"STATIC",L"Waiting for iRacing …",0,25,80,690,25);
    HWND group=Control(L"BUTTON",L"Overlays",0,20,116,720,256,BS_GROUPBOX|WS_CLIPSIBLINGS);
    SetWindowSubclass(group,GroupProc,1,0);
    Control(L"STATIC",L"Display",0,38,141,260,24); Control(L"STATIC",L"Scale",0,312,141,130,24); Control(L"STATIC",L"Opacity",0,456,141,130,24);
    for(int i=0;i<OverlayCount;++i) {
        Check(Titles[i],200+i,38,174+i*36,256,app.overlays[i].place.enabled);
        std::vector<std::wstring> scales,opacity; for(int n=50;n<=150;n+=10) scales.push_back(std::to_wstring(n)+L" %");
        for(int n=40;n<=100;n+=10) opacity.push_back(std::to_wstring(n)+L" %");
        Combo(210+i,312,174+i*36,126,scales,(app.overlays[i].place.scale-50)/10);
        Combo(220+i,456,174+i*36,126,opacity,(app.overlays[i].place.opacity-40)/10);
        Control(L"BUTTON",L"Settings",240+i,610,171+i*36,106,28,WS_TABSTOP);
    }
    group=Control(L"BUTTON",L"General",0,20,386,720,136,BS_GROUPBOX|WS_CLIPSIBLINGS);
    SetWindowSubclass(group,GroupProc,1,0);
    Control(L"STATIC",L"Refresh rate",0,38,415,116,24);
    Combo(231,167,415,112,{L"2 Hz",L"5 Hz",L"10 Hz",L"20 Hz"},app.rate==2?0:(app.rate==5?1:(app.rate==20?3:2)));
    Check(L"Start with Windows",250,38,459,300,app.startWithWindows);
    Check(L"Start Minimized",251,380,459,330,app.startMinimized);
    Check(L"Show Title Bar",252,38,490,300,app.showTitleBar);
    Check(L"Preview with sample data",PreviewId,25,539,300,app.preview);
    Check(L"Move overlays",EditId,380,539,330,app.edit);
    Control(L"STATIC",L"Ctrl+Shift+E: Move  ·  H: Hide  ·  S: Settings",0,25,574,705,24);
    Control(L"STATIC",L"Closing Settings keeps RaceMate running in the tray.",0,25,599,705,24);
    Control(L"BUTTON",L"Reset positions",ResetId,25,626,194,30,WS_TABSTOP);
    Control(L"BUTTON",app.hidden?L"Show overlays":L"Hide overlays",HideId,233,626,187,30,WS_TABSTOP);
    Control(L"BUTTON",L"Race Control",115,434,626,134,30,WS_TABSTOP);
    Control(L"BUTTON",L"Exit",ExitId,582,626,134,30,WS_TABSTOP);
}
void ConfigureScroll() {
    if(!app.main) return;
    RECT r{}; GetClientRect(app.main,&r);
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
    info.nMin=0; info.nMax=app.D(MainHeight)-1; info.nPage=r.bottom; info.nPos=app.scroll;
    SetScrollInfo(app.main,SB_VERT,&info,TRUE);
}
void ScrollTo(int position) {
    RECT client{}; GetClientRect(app.main,&client);
    position=std::clamp(position,0,std::max(0,app.D(MainHeight)-static_cast<int>(client.bottom)));
    const int change=app.scroll-position;
    if(!change) return;
    app.scroll=position;
    for(HWND child:app.controls) {
        RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,app.main,reinterpret_cast<POINT*>(&r),2);
        SetWindowPos(child,nullptr,r.left,r.top+change,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    ConfigureScroll(); InvalidateRect(app.main,nullptr,TRUE);
}
void TrayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"Settings");
    AppendMenuW(menu,MF_STRING|(app.edit?MF_CHECKED:0),2,L"Move overlays");
    AppendMenuW(menu,MF_STRING,3,app.hidden?L"Show overlays":L"Hide overlays");
    AppendMenuW(menu,MF_STRING|(app.preview?MF_CHECKED:0),4,L"Preview");
    AppendMenuW(menu,MF_STRING,6,L"Race Control");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,5,L"Exit");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(app.main);
    int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,app.main,nullptr);
    DestroyMenu(menu); PostMessageW(app.main,WM_NULL,0,0);
    if(choice==6) OpenRaceControl(); if(choice==1) ShowSettings(); if(choice==2) ToggleEdit(); if(choice==3) ToggleHidden(); if(choice==4) TogglePreview(); if(choice==5) DestroyWindow(app.main);
}
LRESULT CALLBACK MainProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    static const UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    if(m==taskbarCreated) {
        app.trayAvailable=Shell_NotifyIconW(NIM_ADD,&app.tray)!=FALSE;
        if(!app.trayAvailable) ShowSettings();
        return 0;
    }
    switch(m) {
    case WM_TIMER: if(wp==2) DestroyWindow(w); else Tick(); return 0;
    case WM_CLOSE:
        if(!app.trayAvailable && !app.diagnostics) { DestroyWindow(w); return 0; }
        if(app.overlaySettings) SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        ShowWindow(w,SW_HIDE); return 0;
    case WM_SIZE:
        if(wp==SIZE_MINIMIZED) { if(app.trayAvailable || app.diagnostics) ShowWindow(w,SW_HIDE); }
        else if(app.main) { ScrollTo(app.scroll); ConfigureScroll(); }
        return 0;
    case WM_MOUSEWHEEL: ScrollTo(app.scroll-GET_WHEEL_DELTA_WPARAM(wp)*app.D(48)/WHEEL_DELTA); return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(w,SB_VERT,&info);
        int target=app.scroll;
        switch(LOWORD(wp)) {
        case SB_LINEUP: target-=app.D(24); break;
        case SB_LINEDOWN: target+=app.D(24); break;
        case SB_PAGEUP: target-=info.nPage; break;
        case SB_PAGEDOWN: target+=info.nPage; break;
        case SB_THUMBTRACK: target=info.nTrackPos; break;
        case SB_TOP: target=0; break;
        case SB_BOTTOM: target=info.nMax; break;
        }
        ScrollTo(target); return 0;
    }
    case TrayMessage: if(lp==WM_LBUTTONDBLCLK || lp==WM_LBUTTONUP) ShowSettings(); else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) TrayMenu(); return 0;
    case WM_HOTKEY: if(wp==1) ToggleEdit(); if(wp==2) ToggleHidden(); if(wp==3) ShowSettings(); return 0;
    case WM_COMMAND: {
        const int id=LOWORD(wp),notification=HIWORD(wp);
        if(id==PreviewId) TogglePreview(); else if(id==EditId) ToggleEdit(); else if(id==HideId) ToggleHidden();
        else if(id==115) OpenRaceControl();
        else if(id==ExitId) DestroyWindow(w);
        else if(id>=240 && id<240+OverlayCount) OpenOverlaySettings(id-240);
        else if((id==250 || id==251) && notification==BN_CLICKED) {
            const bool checked=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
            if(id==250) {
                if(RegisterStartup(checked)) app.startWithWindows=checked;
                else { SendDlgItemMessageW(w,id,BM_SETCHECK,app.startWithWindows?BST_CHECKED:BST_UNCHECKED,0); MessageBoxW(w,L"Could not update Windows startup. The previous setting was kept.",L"RaceMate",MB_OK|MB_ICONERROR); }
            } else app.startMinimized=checked;
            Save();
        }
        else if(id==252 && notification==BN_CLICKED) { app.showTitleBar=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED; app.tableHeaders.fill(app.showTitleBar); for(auto& overlay:app.overlays) Fit(overlay); Save(); UpdateOverlays(); }
        else if(id==ResetId) { for(auto& o:app.overlays) { SetOverlayOrigin(o,o.kind==1?480:40,o.kind==1?120:(o.kind==2?470:(o.kind==3?720:820))); Fit(o,true); } Save(); }
        else if(id>=200 && id<200+OverlayCount && notification==BN_CLICKED) { app.overlays[id-200].place.enabled=SendMessageW(GetDlgItem(w,id),BM_GETCHECK,0,0)==BST_CHECKED; Save(); }
        else if(id>=210 && id<210+OverlayCount && notification==CBN_SELCHANGE) { auto& o=app.overlays[id-210]; o.place.scale=50+10*static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); Fit(o); Save(); }
        else if(id>=220 && id<220+OverlayCount && notification==CBN_SELCHANGE) { auto& o=app.overlays[id-220]; o.place.opacity=40+10*static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); Fit(o); Save(); }
        else if(id==231 && notification==CBN_SELCHANGE) { const int rates[]={2,5,10,20}; int index=static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); if(index>=0 && index<4) app.rate=rates[index]; Timer(); Save(); }
        UpdateOverlays(); return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wp),Background); SetTextColor(reinterpret_cast<HDC>(wp),Text); return reinterpret_cast<LRESULT>(app.brush);
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(w,&r); FillRect(reinterpret_cast<HDC>(wp),&r,app.brush); return 1; }
    case WM_DPICHANGED: {
        const auto* r=reinterpret_cast<RECT*>(lp); SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        for(HWND c:app.controls) DestroyWindow(c); app.controls.clear(); DeleteObject(app.font); DeleteObject(app.heading); app.scroll=0; CreateControls(); ConfigureScroll();
        SendMessageW(GetDlgItem(w,PreviewId),BM_SETCHECK,app.preview?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(GetDlgItem(w,EditId),BM_SETCHECK,app.edit?BST_CHECKED:BST_UNCHECKED,0); Tick(); return 0;
    }
    case WM_DESTROY:
        CloseRaceControl();
        if(app.overlaySettings) SendMessageW(app.overlaySettings,WM_CLOSE,0,0);
        KillTimer(w,1); Shell_NotifyIconW(NIM_DELETE,&app.tray);
        for(int i=1;i<=3;++i) UnregisterHotKey(w,i);
        for(auto& o:app.overlays) if(o.window) DestroyWindow(o.window);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
#ifdef RACEMATE_DIAGNOSTICS
#include "Diagnostics.inc"
#endif
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int show) {
    struct DrawingRuntime {
        ULONG_PTR token=0;
        DrawingRuntime() { Gdiplus::GdiplusStartupInput input; input.SuppressExternalCodecs=TRUE; if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) token=0; }
        ~DrawingRuntime() { if(token) Gdiplus::GdiplusShutdown(token); }
    } drawing;
    if(!drawing.token) return 1;
#ifdef RACEMATE_DIAGNOSTICS
    const bool diagnostics=wcsstr(args,L"--diagnostics")!=nullptr;
#else
    constexpr bool diagnostics=false;
#endif
    const bool benchmark=wcsstr(args,L"--benchmark")!=nullptr;
    app.instance=instance; app.diagnostics=diagnostics || benchmark;
    HANDLE mutex=nullptr;
    if(!app.diagnostics) {
        mutex=CreateMutexW(nullptr,FALSE,L"Local\\RaceMate.SingleInstance");
        if(mutex && GetLastError()==ERROR_ALREADY_EXISTS) { HWND existing=FindWindowW(L"RaceMate.Settings",nullptr); if(existing) { ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); } CloseHandle(mutex); return 0; }
    }
    PWSTR data=nullptr;
    if(app.diagnostics) {
        wchar_t current[32768]{}; GetCurrentDirectoryW(32768,current);
        app.config=std::wstring(current)+L"\\build\\qa\\settings.ini"; CreateDirectoryW(L"build\\qa",nullptr);
    }
    else if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&data))) { app.config=std::wstring(data)+L"\\RaceMate"; CoTaskMemFree(data); CreateDirectoryW(app.config.c_str(),nullptr); app.config+=L"\\settings.ini"; }
    else { MessageBoxW(nullptr,L"Could not open the configuration folder.",L"RaceMate",MB_OK|MB_ICONERROR); if(mutex) CloseHandle(mutex); return 1; }
    Load();
    if(diagnostics) {
        app.weatherOrder={0,1,2,3,4}; app.weatherDisplay.fill(true); app.footers={}; app.columnHeaders.fill(false); app.rows=9; app.rate=10; app.standings={}; app.fuelRows.fill(true); app.fuelDisplay.fill(true); app.predictionOptions={}; app.startWithWindows=app.startMinimized=false;
        for(int i=0;i<OverlayCount;++i) {
            auto& p=app.overlays[i].place; p={};
            p.x=i==1?480:40; p.y=i==1?120:(i==2?470:(i==3?720:820));
        }
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    app.brush=CreateSolidBrush(Background);
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=MainProc; cls.hInstance=instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.Settings"; RegisterClassExW(&cls);
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN|WS_VSCROLL;
    const UINT dpi=GetDpiForSystem(); RECT size{0,0,MulDiv(760,dpi,96),MulDiv(MainHeight,dpi,96)}; AdjustWindowRectExForDpi(&size,style,FALSE,0,dpi);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
    const int windowHeight=std::min(static_cast<int>(size.bottom-size.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-20);
    app.main=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"RaceMate – Settings",style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,windowHeight,nullptr,nullptr,instance,nullptr);
    if(!app.main) { DeleteObject(app.brush); if(mutex) CloseHandle(mutex); return 1; }
    CreateControls(); ConfigureScroll();
    cls.lpfnWndProc=OverlayProc; cls.hbrBackground=nullptr; cls.lpszClassName=L"RaceMate.Overlay"; RegisterClassExW(&cls);
    for(int i=0;i<OverlayCount;++i) {
        auto& o=app.overlays[i]; o.kind=i;
        CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,cls.lpszClassName,Titles[i],WS_POPUP,o.place.x,o.place.y,365,200,nullptr,nullptr,instance,&o); Fit(o,true);
    }
#ifdef RACEMATE_DIAGNOSTICS
    if(diagnostics) return RunDiagnostics();
#endif
    if(benchmark) {
        app.hidden=true; Tick(); SetTimer(app.main,2,10000,nullptr);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
        DeleteObject(app.font); DeleteObject(app.heading); DeleteObject(app.brush); return 0;
    }
    app.tray.cbSize=sizeof(app.tray); app.tray.hWnd=app.main; app.tray.uID=1; app.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; app.tray.uCallbackMessage=TrayMessage; app.tray.hIcon=cls.hIcon; wcscpy_s(app.tray.szTip,L"RaceMate · iRacing Overlays");
    app.trayAvailable=Shell_NotifyIconW(NIM_ADD,&app.tray)!=FALSE;
    if(!app.trayAvailable) SetWindowTextW(app.status,L"Tray unavailable; closing Settings will exit RaceMate.");
    if(app.startWithWindows && !RegisterStartup(true)) MessageBoxW(app.main,L"Could not refresh the Windows startup path. Select Start with Windows again to retry.",L"RaceMate",MB_OK|MB_ICONWARNING);
    bool hotkeys=true; hotkeys=RegisterHotKey(app.main,1,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'E')&&hotkeys;
    hotkeys=RegisterHotKey(app.main,2,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'H')&&hotkeys;
    hotkeys=RegisterHotKey(app.main,3,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'S')&&hotkeys;
    Tick(); ShowWindow(app.main,StartupShowCommand(app.trayAvailable,show)); UpdateWindow(app.main);
    if(!hotkeys) MessageBoxW(app.main,L"An overlay shortcut is already in use. You can still access these functions through Settings and the tray menu.",L"RaceMate",MB_OK|MB_ICONINFORMATION);
    MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) if(!(raceFieldsWindow && IsDialogMessageW(raceFieldsWindow,&message)) && !(footerSettingsWindow && IsDialogMessageW(footerSettingsWindow,&message)) && !(columnOrderWindow && IsDialogMessageW(columnOrderWindow,&message)) && !(app.overlaySettings && IsDialogMessageW(app.overlaySettings,&message)) && !IsDialogMessageW(app.main,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(app.font); DeleteObject(app.heading); DeleteObject(app.brush); if(mutex) CloseHandle(mutex); return 0;
}
