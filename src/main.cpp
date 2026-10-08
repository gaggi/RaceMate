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
constexpr int MainHeight=714;
enum OverlayKind { RelativeKind, StandingsKind, FuelKind, PitLimiterKind, WeatherKind };
constexpr int OverlayCount=WeatherKind+1;
constexpr bool IsTable(int kind) { return kind==RelativeKind || kind==StandingsKind; }
const wchar_t* Titles[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pit Limiter",L"Weather"};
const wchar_t* Sections[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pitlimiter",L"Weather"};
constexpr POINT DefaultPositions[]={{40,120},{480,120},{40,470},{40,720},{40,820}};
// Control identifiers. The diagnostics address several of them by value.
enum ControlId {
    PreviewId=110, EditId, HideId, ResetId, ExitId, RaceControlId,
    OverlayEnableId=200, OverlayScaleId=210, OverlayOpacityId=220, RefreshRateId=231, OverlaySettingsId=240,
    StartWithWindowsId=250, StartMinimizedId, ShowTitleBarId, HotkeyFieldId, HotkeyClearId=HotkeyFieldId+3,
    RowsId=400, ClassViewId, LeadersId, AheadId, BehindId, OwnClassId, GapDecimalsId, HidePitId, IntervalId, LiveGapsId,
    FuelRowId=410, DriverColumnId=430, LapsColumnId=436, GainedColumnId, UntimedOrderId, CornerId=440,
    OrderButtonId=445, FooterButtonId, ShortViewId, HeaderButtonId=ShortViewId+2, OrderListId, MoveUpId, MoveDownId,
    FuelDisplayId=460, ExcludeYellowId=475, SlotEnabledId=480, SlotMetricId,
    WeatherDisplayId=490, StatusFlagId=497, ShowDriverId=510, NameFormatId, ShowStatusId, ColumnHeadingsId=514
};
// Relative and Standings columns. The values are stored in ColumnOrder.
enum Column {
    PositionColumn, NumberColumn, DriverColumn, GapColumn, IntervalColumn, BestColumn, LastColumn,
    LicenseColumn, IRatingColumn, TiresColumn, PitLapColumn, PitTimeColumn, PitStopColumn,
    LapsColumn, GainedColumn, StatusColumn
};
constexpr int ColumnCount=StatusColumn+1;
// License..PitStop are the optional driver columns, also used by Race Control.
constexpr int DriverColumnCount=PitStopColumn-LicenseColumn+1;
struct ColumnInfo { const wchar_t* title; int width, compactWidth; };
constexpr ColumnInfo Columns[]={
    {L"P",33,36},{L"#",42,42},{L"DRIVER",191,191},{L"GAP",87,76},{L"INT",80,68},{L"BEST",94,88},{L"LAST",93,88},
    {L"LICENSE",82,68},{L"iRATING",72,60},{L"TIRES",64,56},{L"PIT LAP",70,52},{L"PIT TIME",80,76},{L"PIT STOP",88,76},
    {L"LAPS",64,52},{L"GAIN",64,50},{L"STATUS",70,64}};
static_assert(std::size(Columns)==ColumnCount);
const wchar_t* DriverColumnKeys[]={L"ShowLicense",L"ShowIRating",L"ShowTires",L"ShowLastPitLap",L"ShowPitTime",L"ShowPitStopTime"};
const std::vector<int> DefaultColumns[]={
    {PositionColumn,NumberColumn,DriverColumn,StatusColumn,GapColumn,LicenseColumn,IRatingColumn,TiresColumn,PitLapColumn,PitTimeColumn,PitStopColumn},
    {PositionColumn,GainedColumn,NumberColumn,DriverColumn,StatusColumn,GapColumn,IntervalColumn,BestColumn,LastColumn,LapsColumn,
     LicenseColumn,IRatingColumn,TiresColumn,PitLapColumn,PitTimeColumn,PitStopColumn}};
// Header and footer metrics. The values are stored in the Header*/Footer* keys.
enum Metric {
    NoMetric, TrackMetric, IncidentsMetric, TrackTempMetric, AirTempMetric, TemperaturesMetric, TimeRemainingMetric, TimeElapsedMetric,
    CurrentLapMetric, FinalLapMetric, LapsRemainingMetric, LocalTimeMetric, SimTimeMetric, SessionTypeMetric, WetnessMetric, WindMetric,
    SofMetric, OverlayNameMetric
};
constexpr int MetricCount=OverlayNameMetric+1;
const wchar_t* MetricTitles[]={L"None",L"Track",L"Incidents",L"Track Temperature",L"Air Temperature",L"Temperatures",L"Session Time Remaining",L"Session Time Elapsed",L"Current Lap",L"Current / Expected Final Lap",L"Estimated Laps Remaining",L"Local Time",L"Sim Time",L"Session Type",L"Track Wetness",L"Wind",L"Class SOF",L"Overlay Name"};
static_assert(std::size(MetricTitles)==MetricCount);
using Slots=std::array<int,3>;
const wchar_t* HeaderKeys[]={L"HeaderLeft",L"HeaderCenter",L"HeaderRight"};
const wchar_t* FooterKeys[]={L"FooterLeft",L"FooterCenter",L"FooterRight"};
constexpr Slots DefaultHeader{OverlayNameMetric,NoMetric,SessionTypeMetric}, DefaultFooter{TrackMetric,NoMetric,IncidentsMetric};
// Fuel Calculator elements. The values are stored in ElementOrder; the consumption table's display flag toggles its PIT column.
enum FuelElement {
    LeaderDistanceElement, FinalLapTimeElement, TimeEmptyElement, FuelAtEndElement, StopsRemainingElement, TankGaugeElement,
    StintTimelineElement, PitPanelElement, ConsumptionTableElement, RemainingLeaderDistanceElement, RefuelToEndElement,
    KeyNumbersElement, PitTimelineElement
};
constexpr int FuelElementCount=PitTimelineElement+1;
struct ElementInfo { const wchar_t* key; const wchar_t* title; const wchar_t* option; int height; };
constexpr ElementInfo FuelElements[]={
    {L"ShowLeaderDistance",L"Leader Distance",L"Leader distance",29},
    {L"ShowFinalLapTime",L"Final Lap Time",L"Time left at final lap start",29},
    {L"ShowTimeEmpty",L"Time Empty",L"Time until empty",27},
    {L"ShowFuelAtEnd",L"Fuel at End",L"Fuel at finish",27},
    {L"ShowStopsRemaining",L"Stops Remaining",L"Stops remaining",27},
    {L"ShowTankGauge",L"Tank Gauge",L"Tank gauge",47},
    {L"ShowStintTimeline",L"Stint Timeline",L"Stint timeline",30},
    {L"ShowPitWindowPanel",L"Pit Window Panel",L"Pit window panel",39},
    {L"ShowPitWindowColumn",L"Consumption Table",L"Pit window column",0},
    {L"ShowRemainingLeaderDistance",L"Remaining Leader Distance",L"Remaining leader distance",29},
    {L"ShowRefuelToEnd",L"Refuel to End",L"Refuel to finish",27},
    {L"ShowKeyNumbers",L"Tank / Range / At Finish",L"Tank, range and fuel at finish",50},
    {L"ShowPitTimeline",L"Pit Window Timeline",L"Pit window timeline",36}};
static_assert(std::size(FuelElements)==FuelElementCount);
const std::vector<int> DefaultFuelOrder{KeyNumbersElement,PitTimelineElement,LeaderDistanceElement,RemainingLeaderDistanceElement,FinalLapTimeElement,TimeEmptyElement,FuelAtEndElement,
    StopsRemainingElement,RefuelToEndElement,TankGaugeElement,StintTimelineElement,PitPanelElement,ConsumptionTableElement};
const wchar_t* FuelMetricNames[]={L"Last lap",L"Average 10",L"Average 5",L"Maximum",L"Minimum",L"Average"};
// Pit window, Time Empty and the stint timeline all use the whole-race average.
constexpr int RaceAverage=static_cast<int>(FuelMetric::Average);
// Weather cards. The values are stored in ElementOrder; WindDirectionOption is a switch nested below the wind card.
enum WeatherElement { TrackTempElement, AirTempElement, WindElement, WetnessElement, UsageElement };
constexpr int WeatherCardCount=UsageElement+1, WindDirectionOption=WeatherCardCount;
constexpr ElementInfo WeatherElements[]={
    {L"ShowTrackTemperature",L"Track Temp",L"Show Track Temperature",36},
    {L"ShowAirTemperature",L"Air Temp",L"Show Air Temperature",36},
    {L"ShowWind",L"Wind",L"Show Wind",190},
    {L"ShowTrackWetness",L"Track Wetness",L"Show Track Wetness",36},
    {L"ShowTrackUsage",L"Track Usage",L"Show Track Usage",36},
    {L"ShowWindDirection",L"Wind Direction",L"Show Wind Direction",0}};
const std::vector<int> DefaultWeatherOrder{TrackTempElement,AirTempElement,WindElement,WetnessElement,UsageElement};
struct Placement { int x=40,y=120,scale=100,opacity=90; bool enabled=true, growLeft=false, growUp=false; };
enum FontId : unsigned char { Regular, Bold, Large, Small, Hero, FontCount };
struct DrawCommand {
    enum Type : unsigned char { Fill, Text, DottedLine, WindRing };
    Type type=Fill; FontId font=Regular; UINT align=DT_LEFT; RECT r{}; COLORREF color=0;
    std::array<float,5> shape{}; // Wind ring: center x/y, radius, scale, bearing (negative without direction).
    std::wstring text;
    bool operator==(const DrawCommand& o) const {
        return type==o.type && font==o.font && align==o.align && r.left==o.r.left && r.top==o.r.top && r.right==o.r.right && r.bottom==o.r.bottom
            && color==o.color && shape==o.shape && text==o.text;
    }
};
struct Overlay {
    HWND window=nullptr; int kind=0; Placement place;
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ original=nullptr, originalFont=nullptr;
    std::array<HFONT,FontCount> fonts{}; int width=0,height=0,dpi=0;
    std::vector<DrawCommand> scene;
    void Free() {
        if(dc) { SelectObject(dc,originalFont); SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
        for(HFONT& font:fonts) if(font) { DeleteObject(font); font=nullptr; }
        dc=nullptr; bitmap=nullptr; width=height=dpi=0; scene.clear();
    }
};
struct FooterSettings { bool enabled=true; Slots metrics=DefaultFooter; };
// Global shortcuts, packed like the hotkey control: low byte virtual key, high byte HOTKEYF_* modifiers; 0 disables.
constexpr int HotkeyCount=3;
const wchar_t* HotkeyKeys[]={L"Move",L"Hide",L"Settings"};
constexpr WORD HotkeyAllModifiers=(HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT)<<8;
constexpr WORD DefaultHotkeys[]={HotkeyAllModifiers|'E',HotkeyAllModifiers|'H',HotkeyAllModifiers|'S'};
struct App {
    HINSTANCE instance=nullptr; HWND main=nullptr,status=nullptr;
    HFONT font=nullptr,heading=nullptr; HBRUSH brush=nullptr;
    std::array<Overlay,OverlayCount> overlays;
    Frame frame; Session session; Telemetry telemetry; FuelCalculator fuel;
    EventRecorder events; PitHistory pitHistory; PitLimiterVisibility pitVisibility;
    RacePredictor predictor; FuelPlan fuelPlan;
    std::wstring config;
    // Relative and Standings settings, indexed by kind.
    std::array<std::vector<int>,2> columnOrder;
    std::array<std::array<bool,DriverColumnCount>,2> driverColumns{};
    std::array<std::array<bool,2>,2> driverShortView{};
    std::array<std::array<bool,3>,2> driverFlags{{{true,true,true},{true,true,true}}};
    std::array<bool,2> showDriver{true,true}, showStatus{true,true}, tableHeaders{}, columnHeaders{};
    std::array<int,2> nameFormat{};
    std::array<Slots,2> headerMetrics{DefaultHeader,DefaultHeader};
    std::array<FooterSettings,2> footers;
    int rows=7, gapDecimals=1; bool relativeHidePit=false;
    StandingsOptions standings; bool showInterval=false, standingsLaps=false, standingsGained=false;
    std::vector<int> fuelOrder=DefaultFuelOrder;
    std::array<bool,FuelElementCount> fuelDisplay{};
    std::array<bool,FuelMetricCount> fuelRows{};
    bool excludeYellowFuelLaps=false;
    std::vector<int> weatherOrder=DefaultWeatherOrder;
    std::array<bool,WeatherCardCount+1> weatherDisplay{};
    bool preview=false, edit=false, hidden=false, diagnostics=false;
    bool startWithWindows=false, startMinimized=false, showTitleBar=false;
    HWND overlaySettings=nullptr; int settingsKind=-1;
    bool settingsError=false, trayAvailable=false;
    std::array<WORD,HotkeyCount> hotkeys{}; bool hotkeysEnabled=false; std::wstring hotkeyError;
    int rate=10,timerMs=0,scroll=0;
    unsigned modelVersion=0,relativeVersion=~0u,standingsVersion=~0u,planVersion=~0u;
    std::vector<Row> relativeRows; std::vector<StandingsGroup> standingsGroups; std::vector<DrawCommand> sceneBuffer;
    NOTIFYICONDATAW tray{}; std::vector<HWND> controls;
    int D(int v) const { return MulDiv(v,GetDpiForWindow(main),96); }
} app;
void OpenRaceControl();
void UpdateRaceControl(bool force=false);
void CloseRaceControl();
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
void Label(HDC dc,const std::wstring& s,RECT r,COLORREF c,HFONT font,UINT align=DT_LEFT) {
    auto old=SelectObject(dc,font); SetTextColor(dc,c); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);
}
bool MultiCompoundSession() { return std::any_of(app.session.drivers.begin(),app.session.drivers.end(),HasMultipleDryCompounds); }
const wchar_t* ColumnTitle(int id) { return Columns[id].title; }
bool ColumnVisible(int kind,int id) {
    switch(id) {
    case DriverColumn: return app.showDriver[kind];
    case StatusColumn: return app.showStatus[kind];
    case GainedColumn: return kind==StandingsKind && app.standingsGained;
    case LapsColumn: return kind==StandingsKind && app.standingsLaps;
    case IntervalColumn: return app.showInterval;
    default: return id>=LicenseColumn?app.driverColumns[kind][id-LicenseColumn]:true;
    }
}
// Without column headings, columns shrink to fit their values.
int ColumnWidth(int id,int kind) {
    const bool compact=!app.columnHeaders[kind];
    if(id==TiresColumn && MultiCompoundSession()) return compact?90:100;
    if(compact && id==DriverColumn && app.nameFormat[kind]!=0) return 146;
    if(compact && (id==LicenseColumn || id==IRatingColumn) && app.driverShortView[kind][id-LicenseColumn]) return 56;
    return compact?Columns[id].compactWidth:Columns[id].width;
}
std::vector<int> VisibleColumns(int kind) { std::vector<int> result; for(int id:app.columnOrder[kind]) if(ColumnVisible(kind,id)) result.push_back(id); return result; }
int FuelFinishLap() {
    const auto& f=app.frame; const double distance=app.fuelPlan.lapsToFinish;
    return f.lapCompleted>=0 && std::isfinite(distance) && distance>=0?std::max(f.lapCompleted+1,f.lapCompleted+static_cast<int>(std::ceil(f.lapPct+distance-1e-6))):-1;
}
int StintCellCount() {
    const double average=app.fuelPlan.estimates[RaceAverage].consumption,capacity=app.session.fuelCapacity;
    return std::isfinite(average) && average>0 && std::isfinite(capacity) && capacity>0?static_cast<int>(std::clamp(std::ceil(capacity/average),1.0,10000.0)):0;
}
int WeatherHeight() { int height=40; for(int i=0;i<WeatherCardCount;++i) if(app.weatherDisplay[i]) height+=WeatherElements[i].height+8; return height; }
bool FuelElementVisible(int id) { return id!=ConsumptionTableElement?app.fuelDisplay[id]:std::any_of(app.fuelRows.begin(),app.fuelRows.end(),[](bool value){return value;}); }
int OverlayWidth(int kind) {
    if(IsTable(kind)) { int width=12; for(int id:VisibleColumns(kind)) width+=ColumnWidth(id,kind); return std::max(120,width); }
    if(kind==WeatherKind) return 240;
    if(kind==FuelKind) return std::max(std::any_of(app.fuelDisplay.begin(),app.fuelDisplay.end(),[](bool value){return value;})?465:365,app.fuelDisplay[StintTimelineElement]?64+StintCellCount()*4:0);
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
bool ShowOverlayHeader(int kind) { return IsTable(kind)?app.tableHeaders[kind]:app.showTitleBar; }
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
// driverColumn counts from LicenseColumn; kind selects the overlay's short views, -1 for full values.
std::wstring DriverValue(const Row& row,int driverColumn,int kind=-1) {
    const int id=LicenseColumn+driverColumn;
    const bool shortView=kind>=0 && IsTable(kind) && (id==LicenseColumn || id==IRatingColumn) && app.driverShortView[kind][driverColumn];
    switch(id) {
    case LicenseColumn: {
        if(!shortView) return row.driver.license.empty()?L"—":Wide(row.driver.license);
        const auto start=row.driver.license.find_first_of("0123456789");
        if(start==std::string::npos) return L"—";
        const auto end=row.driver.license.find_first_not_of("0123456789.,",start);
        auto value=Wide(row.driver.license.substr(start,end==std::string::npos?end:end-start));
        std::replace(value.begin(),value.end(),L'.',L','); return value;
    }
    case IRatingColumn: {
        if(row.driver.rating<=0) return L"—";
        if(!shortView) return std::to_wstring(row.driver.rating);
        auto value=Format(row.driver.rating/1000.0,1); std::replace(value.begin(),value.end(),L'.',L','); return value+L"K";
    }
    case TiresColumn: { const auto label=TireLabel(row.driver,row.car.tireCompound); return label.empty()?L"—":Wide(label); }
    case PitLapColumn: return row.car.lastPitLap<0?L"—":std::to_wstring(row.car.lastPitLap);
    case PitStopColumn: {
        const double duration=row.car.pit?row.car.pitStationary:row.car.lastPitStationary;
        return duration<0?L"—":L"≈"+Format(duration)+L" s";
    }
    default: return row.car.lastPitDuration<0?L"—":Format(row.car.lastPitDuration)+L" s";
    }
}
int ReadInt(const wchar_t* section,const wchar_t* key,int fallback) { return GetPrivateProfileIntW(section,key,fallback,app.config.c_str()); }
bool ReadBool(const wchar_t* section,const wchar_t* key,bool fallback) { return ReadInt(section,key,fallback)!=0; }
// Keeps valid, unique saved IDs; IDs the file does not mention yet go before their default successor.
std::vector<int> ReadOrder(const wchar_t* section,const wchar_t* key,const std::vector<int>& defaults) {
    wchar_t saved[256]{}; GetPrivateProfileStringW(section,key,L"",saved,256,app.config.c_str());
    std::vector<int> order;
    for(wchar_t* token=saved;*token;) {
        wchar_t* end=nullptr; const long id=wcstol(token,&end,10); if(end==token) break;
        if(std::find(defaults.begin(),defaults.end(),id)!=defaults.end() && std::find(order.begin(),order.end(),id)==order.end()) order.push_back(static_cast<int>(id));
        token=end; if(*token==L',') ++token; else if(*token) break;
    }
    for(size_t i=0;i<defaults.size();++i) if(std::find(order.begin(),order.end(),defaults[i])==order.end()) {
        auto next=order.end();
        for(size_t j=i+1;j<defaults.size() && next==order.end();++j) next=std::find(order.begin(),order.end(),defaults[j]);
        order.insert(next,defaults[i]);
    }
    return order;
}
Slots ReadSlots(const wchar_t* section,const wchar_t* const keys[],const Slots& defaults) {
    Slots slots{}; for(int i=0;i<3;++i) slots[i]=std::clamp(ReadInt(section,keys[i],defaults[i]),0,MetricCount-1); return slots;
}
int NormalizeScale(int scale) { return ((std::clamp(scale,50,150)+5)/10)*10; }
void Load() {
    for(int kind:{RelativeKind,StandingsKind}) {
        const auto* section=Sections[kind];
        app.columnOrder[kind]=ReadOrder(section,L"ColumnOrder",DefaultColumns[kind]);
        for(int i=0;i<DriverColumnCount;++i) app.driverColumns[kind][i]=ReadBool(section,DriverColumnKeys[i],false);
        app.driverShortView[kind]={ReadBool(section,L"ShortLicenseView",false),ReadBool(section,L"ShortIRatingView",false)};
        app.driverFlags[kind]={ReadBool(section,L"ShowBlackFlag",true),ReadBool(section,L"ShowSlowdown",true),ReadBool(section,L"ShowRequiredRepairs",true)};
        app.showDriver[kind]=ReadBool(section,L"ShowDriver",true);
        app.showStatus[kind]=ReadBool(section,L"ShowStatus",true);
        app.nameFormat[kind]=std::clamp(ReadInt(section,L"DriverNameFormat",0),0,2);
        app.columnHeaders[kind]=ReadBool(section,L"ShowColumnHeaders",false);
        app.tableHeaders[kind]=ReadBool(section,L"ShowHeader",false);
        app.headerMetrics[kind]=ReadSlots(section,HeaderKeys,DefaultHeader);
        app.footers[kind].enabled=ReadBool(section,L"ShowFooter",true);
        app.footers[kind].metrics=ReadSlots(section,FooterKeys,DefaultFooter);
    }
    app.rows=RelativeRowCount(ReadInt(L"Relative",L"Rows",7));
    app.gapDecimals=std::clamp(ReadInt(L"Relative",L"GapDecimals",1),0,3);
    app.relativeHidePit=ReadBool(L"Relative",L"HideDriversInPit",false);
    auto& standings=app.standings;
    standings.separateClasses=ReadBool(L"Standings",L"SeparateClasses",true);
    standings.ownClass=ReadBool(L"Standings",L"OwnClass",false);
    standings.liveGaps=ReadBool(L"Standings",L"LiveGaps",false);
    standings.fallbackByRating=ReadBool(L"Standings",L"FallbackByIRating",false);
    standings.leaders=std::clamp(ReadInt(L"Standings",L"Leaders",3),0,MaxCars);
    standings.ahead=std::clamp(ReadInt(L"Standings",L"Ahead",2),0,MaxCars);
    standings.behind=std::clamp(ReadInt(L"Standings",L"Behind",2),0,MaxCars);
    app.showInterval=ReadBool(L"Standings",L"ShowInterval",false);
    app.standingsLaps=ReadBool(L"Standings",L"ShowLaps",false);
    app.standingsGained=ReadBool(L"Standings",L"ShowPositionsGained",false);
    app.fuelOrder=ReadOrder(L"Fuel Calculator",L"ElementOrder",DefaultFuelOrder);
    for(int i=0;i<FuelElementCount;++i) app.fuelDisplay[i]=ReadBool(L"Fuel Calculator",FuelElements[i].key,true);
    const wchar_t* fuelRowKeys[]={L"ShowLast",L"ShowAverage10",L"ShowAverage5",L"ShowMaximum",L"ShowMinimum",L"ShowAverage"};
    for(int i=0;i<FuelMetricCount;++i) app.fuelRows[i]=ReadBool(L"Fuel Calculator",fuelRowKeys[i],true);
    app.excludeYellowFuelLaps=ReadBool(L"Fuel Calculator",L"ExcludeYellowLaps",false);
    app.weatherOrder=ReadOrder(L"Weather",L"ElementOrder",DefaultWeatherOrder);
    for(int i=0;i<=WindDirectionOption;++i) app.weatherDisplay[i]=ReadBool(L"Weather",WeatherElements[i].key,true);
    app.showTitleBar=ReadBool(L"General",L"ShowTitleBar",false);
    app.rate=std::clamp(ReadInt(L"General",L"RefreshHz",10),2,20);
    app.startWithWindows=ReadBool(L"General",L"StartWithWindows",false);
    app.startMinimized=ReadBool(L"General",L"StartMinimized",false);
    for(int i=0;i<HotkeyCount;++i) { const WORD hotkey=static_cast<WORD>(ReadInt(L"Hotkeys",HotkeyKeys[i],DefaultHotkeys[i])); app.hotkeys[i]=LOBYTE(hotkey)?hotkey:0; }
    for(int kind=0;kind<OverlayCount;++kind) {
        auto& p=app.overlays[kind].place; const auto* section=Sections[kind];
        p.x=ReadInt(section,L"X",DefaultPositions[kind].x); p.y=ReadInt(section,L"Y",DefaultPositions[kind].y);
        p.scale=NormalizeScale(ReadInt(section,L"Scale",100)); p.opacity=std::clamp(ReadInt(section,L"Opacity",90),40,100);
        p.enabled=ReadBool(section,L"Enabled",true); p.growLeft=ReadBool(section,L"GrowLeft",false); p.growUp=ReadBool(section,L"GrowUp",false);
    }
}
void Save() {
    bool success=true;
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value) { success=WritePrivateProfileStringW(section,key,value.c_str(),app.config.c_str()) && success; };
    auto number=[&](const wchar_t* section,const wchar_t* key,int value) { write(section,key,std::to_wstring(value)); };
    auto order=[&](const wchar_t* section,const wchar_t* key,const std::vector<int>& ids) {
        std::wstring text; for(int id:ids) { if(!text.empty()) text+=L","; text+=std::to_wstring(id); } write(section,key,text);
    };
    for(int kind:{RelativeKind,StandingsKind}) {
        const auto* section=Sections[kind];
        order(section,L"ColumnOrder",app.columnOrder[kind]);
        for(int i=0;i<DriverColumnCount;++i) number(section,DriverColumnKeys[i],app.driverColumns[kind][i]);
        number(section,L"ShortLicenseView",app.driverShortView[kind][0]); number(section,L"ShortIRatingView",app.driverShortView[kind][1]);
        number(section,L"ShowBlackFlag",app.driverFlags[kind][0]); number(section,L"ShowSlowdown",app.driverFlags[kind][1]); number(section,L"ShowRequiredRepairs",app.driverFlags[kind][2]);
        number(section,L"ShowDriver",app.showDriver[kind]); number(section,L"ShowStatus",app.showStatus[kind]); number(section,L"DriverNameFormat",app.nameFormat[kind]);
        number(section,L"ShowColumnHeaders",app.columnHeaders[kind]); number(section,L"ShowHeader",app.tableHeaders[kind]); number(section,L"ShowFooter",app.footers[kind].enabled);
        for(int i=0;i<3;++i) { number(section,HeaderKeys[i],app.headerMetrics[kind][i]); number(section,FooterKeys[i],app.footers[kind].metrics[i]); }
    }
    number(L"Relative",L"Rows",app.rows); number(L"Relative",L"GapDecimals",app.gapDecimals); number(L"Relative",L"HideDriversInPit",app.relativeHidePit);
    const auto& standings=app.standings;
    number(L"Standings",L"SeparateClasses",standings.separateClasses); number(L"Standings",L"OwnClass",standings.ownClass);
    number(L"Standings",L"LiveGaps",standings.liveGaps); number(L"Standings",L"FallbackByIRating",standings.fallbackByRating);
    number(L"Standings",L"Leaders",standings.leaders); number(L"Standings",L"Ahead",standings.ahead); number(L"Standings",L"Behind",standings.behind);
    number(L"Standings",L"ShowInterval",app.showInterval); number(L"Standings",L"ShowLaps",app.standingsLaps); number(L"Standings",L"ShowPositionsGained",app.standingsGained);
    order(L"Fuel Calculator",L"ElementOrder",app.fuelOrder);
    for(int i=0;i<FuelElementCount;++i) number(L"Fuel Calculator",FuelElements[i].key,app.fuelDisplay[i]);
    const wchar_t* fuelRowKeys[]={L"ShowLast",L"ShowAverage10",L"ShowAverage5",L"ShowMaximum",L"ShowMinimum",L"ShowAverage"};
    for(int i=0;i<FuelMetricCount;++i) number(L"Fuel Calculator",fuelRowKeys[i],app.fuelRows[i]);
    number(L"Fuel Calculator",L"ExcludeYellowLaps",app.excludeYellowFuelLaps);
    order(L"Weather",L"ElementOrder",app.weatherOrder);
    for(int i=0;i<=WindDirectionOption;++i) number(L"Weather",WeatherElements[i].key,app.weatherDisplay[i]);
    number(L"General",L"ShowTitleBar",app.showTitleBar); number(L"General",L"RefreshHz",app.rate);
    number(L"General",L"StartWithWindows",app.startWithWindows); number(L"General",L"StartMinimized",app.startMinimized);
    for(int i=0;i<HotkeyCount;++i) number(L"Hotkeys",HotkeyKeys[i],app.hotkeys[i]);
    for(int kind=0;kind<OverlayCount;++kind) {
        const auto& p=app.overlays[kind].place; const auto* section=Sections[kind];
        number(section,L"X",p.x); number(section,L"Y",p.y); number(section,L"Scale",p.scale); number(section,L"Opacity",p.opacity);
        number(section,L"Enabled",p.enabled); number(section,L"GrowLeft",p.growLeft); number(section,L"GrowUp",p.growUp);
    }
    if(!success && !app.settingsError) { app.settingsError=true; MessageBoxW(app.main,L"Could not save settings. Please check write access to the RaceMate configuration folder.",L"RaceMate",MB_OK|MB_ICONWARNING); }
}
UINT HotkeyModifiers(WORD hotkey) {
    const BYTE flags=HIBYTE(hotkey); UINT modifiers=MOD_NOREPEAT;
    if(flags&HOTKEYF_CONTROL) modifiers|=MOD_CONTROL;
    if(flags&HOTKEYF_ALT) modifiers|=MOD_ALT;
    if(flags&HOTKEYF_SHIFT) modifiers|=MOD_SHIFT;
    return modifiers;
}
void UnregisterHotkeys() { for(int i=0;i<HotkeyCount;++i) UnregisterHotKey(app.main,i+1); }
// Records shortcuts that another application (or a duplicate assignment) already owns.
void RegisterHotkeys() {
    if(!app.hotkeysEnabled) return;
    UnregisterHotkeys(); app.hotkeyError.clear();
    for(int i=0;i<HotkeyCount;++i) {
        const WORD hotkey=app.hotkeys[i];
        if(LOBYTE(hotkey) && !RegisterHotKey(app.main,i+1,HotkeyModifiers(hotkey),LOBYTE(hotkey)))
            app.hotkeyError+=(app.hotkeyError.empty()?L"":L", ")+std::wstring(HotkeyKeys[i]);
    }
}
// Derived data is cached per model version, so nothing is computed for overlays or footers that are not shown.
void InvalidateModel() { ++app.modelVersion; }
FuelPlan PreviewPlan() {
    FuelPlan p; p.samples=12; p.stintStartLap=4; p.lapsToFinish=12.55;
    auto& race=p.prediction; race.status=PredictionStatus::Racing; race.leader=0; race.leaderPace=138.2; race.ownPace=139.4;
    auto& finish=race.finish; finish.valid=true; finish.winner=0; finish.leaderTotalDistance=19+17/race.leaderPace; finish.leaderDistance=12.02; finish.finalLapTimeLeft=17;
    finish.ownDistance=p.lapsToFinish;
    const double burns[]={3.09,3.12,3.10,3.21,2.98,3.11}; const int counts[]={1,10,5,12,12,12};
    for(int i=0;i<FuelMetricCount;++i) p.estimates[i]={burns[i],std::max(0.0,finish.ownDistance*burns[i]-app.frame.fuel),counts[i]};
    return p;
}
const FuelPlan& Plan() {
    if(app.planVersion!=app.modelVersion) {
        if(app.preview) app.fuelPlan=PreviewPlan();
        else { const auto prediction=app.predictor.Predict(app.session,app.frame); app.fuelPlan=app.fuel.Plan(app.frame,&prediction); }
        app.planVersion=app.modelVersion;
    }
    return app.fuelPlan;
}
const std::vector<Row>& RelativeRows() {
    if(app.relativeVersion!=app.modelVersion) { app.relativeRows=Relative(app.session,app.frame,app.rows,false,app.relativeHidePit); app.relativeVersion=app.modelVersion; }
    return app.relativeRows;
}
const std::vector<StandingsGroup>& StandingsGroups() {
    if(app.standingsVersion!=app.modelVersion) { app.standingsGroups=SelectStandings(app.session,app.frame,app.standings); app.standingsVersion=app.modelVersion; }
    return app.standingsGroups;
}
std::vector<Row> Rows(int kind) {
    if(kind==RelativeKind) return RelativeRows();
    std::vector<Row> rows;
    for(const auto& group:StandingsGroups()) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
    return rows;
}
#include "Footer.inc"
int HeightWithTitle(int kind) {
    if(kind==RelativeKind) return TableRowsTop(kind)+app.rows*27+(app.footers[kind].enabled?28:0);
    if(kind==StandingsKind) {
        int height=TableRowsTop(kind)+(app.footers[kind].enabled?28:0),count=0;
        for(const auto& group:StandingsGroups()) {
            height+=static_cast<int>(group.rows.size())*27+(app.standings.separateClasses?25:0); count+=static_cast<int>(group.rows.size());
        }
        return height+(count==0?54:0);
    }
    if(kind==FuelKind) {
        int h=44; for(int i=0;i<FuelElementCount;++i) if(app.fuelDisplay[i]) h+=FuelElements[i].height;
        const int rows=static_cast<int>(std::count(app.fuelRows.begin(),app.fuelRows.end(),true));
        return h+(rows?24+29*rows:0);
    }
    return kind==WeatherKind?WeatherHeight():90;
}
int BaseHeight(int kind) { return HeightWithTitle(kind)-(ShowOverlayHeader(kind)?0:32); }
std::wstring Gap(const Row& r,int kind) {
    if(kind==RelativeKind) {
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
// Fuel to add at the next stop: a final stop covers the shortfall, an intermediate stop fills the tank.
double PlannedStopFuel(const PitWindow& window,const FuelPlan& p) {
    const auto& f=app.frame; const double capacity=app.session.fuelCapacity;
    if(window.status!=PitWindowStatus::Open || !f.fuelKnown || !std::isfinite(f.fuel) || f.fuel<0 || !std::isfinite(capacity) || capacity<=0 || !std::isfinite(window.fuelAtEnd)) return -1;
    if(window.stops<=1) return std::min(capacity,std::max(0.0,-window.fuelAtEnd));
    const double untilStop=window.openNow?0:std::max(0.0,window.firstLap-(f.lapCompleted+f.lapPct));
    return std::max(0.0,capacity-std::max(0.0,f.fuel-untilStop*p.estimates[RaceAverage].consumption));
}
COLORREF ColorOf(unsigned rgb) { return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); }
// Describes an overlay in physical pixels. Comparing it with the previous scene yields the exact changed areas.
void BuildScene(const Overlay& o,int width,int height,int dpi,std::vector<DrawCommand>& out) {
    out.clear();
    auto px=[&](int v){return MulDiv(v,dpi*o.place.scale,9600);};
    auto rect=[&](int x,int y,int w,int h){return RECT{px(x),px(y),px(x+w),px(y+h)};};
    const int titleOffset=ShowOverlayHeader(o.kind)?0:px(32);
    auto fill=[&](RECT r,COLORREF color) { r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({DrawCommand::Fill,Regular,0,r,color}); };
    auto label=[&](std::wstring text,RECT r,COLORREF color,FontId font,UINT align=DT_LEFT) {
        r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({DrawCommand::Text,font,align,r,color,{},std::move(text)});
    };
    const COLORREF white=RGB(232,234,237), muted=RGB(152,159,169), yellow=RGB(246,204,66);
    out.push_back({DrawCommand::Fill,Regular,0,{0,0,width,height},RGB(18,21,26)});
    auto slots=[&](const Slots& metrics,RECT band,COLORREF ink,bool header) {
        const int margin=px(12),third=width/3;
        for(int slot=0;slot<3;++slot) {
            RECT bounds{slot==0?margin:(slot==1?third:2*third),band.top,slot==0?third:(slot==1?2*third:width-margin),band.bottom};
            // An empty center lets the outer slots use its space.
            if(slot==0 && metrics[1]==NoMetric) bounds.right=metrics[2]==NoMetric?width-margin:2*third;
            if(slot==2 && metrics[1]==NoMetric) bounds.left=metrics[0]==NoMetric?margin:2*third;
            const bool emphasis=metrics[slot]==IncidentsMetric || (header && metrics[slot]==OverlayNameMetric);
            label(FooterValue(metrics[slot],o.kind),bounds,metrics[slot]==IncidentsMetric?yellow:ink,emphasis?Bold:Regular,slot==0?DT_LEFT:(slot==1?DT_CENTER:DT_RIGHT));
        }
    };
    if(ShowOverlayHeader(o.kind)) {
        fill({0,0,width,px(32)},RGB(37,41,48));
        fill({0,0,px(3),px(32)},yellow);
        if(IsTable(o.kind)) slots(app.headerMetrics[o.kind],{0,0,width,px(32)},white,true);
        else {
            label(o.kind==PitLimiterKind?L"PIT LANE":Titles[o.kind],rect(12,0,230,32),white,Bold);
            const auto mode=app.frame.replay?L"REPLAY":(!app.frame.onTrack?L"SPOTTING":Wide(app.session.type));
            if(o.kind!=WeatherKind) label(app.edit?L"MOVE":(app.preview?L"PREVIEW":mode),{width-px(145),0,width-px(12),px(32)},muted,Regular,DT_RIGHT);
        }
    }
    if(IsTable(o.kind)) {
        const auto columns=VisibleColumns(o.kind); int headerX=12;
        if(app.columnHeaders[o.kind]) for(int id:columns) { label(id==GapColumn && o.kind==RelativeKind?L"GAP ≈":ColumnTitle(id),rect(headerX,34,ColumnWidth(id,o.kind)-12,24),muted,Regular,id==DriverColumn || id==StatusColumn?DT_LEFT:DT_RIGHT); headerX+=ColumnWidth(id,o.kind); }
        int y=TableRowsTop(o.kind);
        static const std::vector<Row> noRows; static const std::vector<StandingsGroup> noGroups;
        const auto& rows=o.kind==RelativeKind?RelativeRows():noRows;
        const auto& groups=o.kind==StandingsKind?StandingsGroups():noGroups;
        const bool rowsEmpty=o.kind==RelativeKind?rows.empty():std::none_of(groups.begin(),groups.end(),[](const StandingsGroup& group){return !group.rows.empty();});
        auto drawRow=[&](const Row& r) {
            fill({0,px(y),width,px(y+27)},r.player?RGB(54,49,30):((y/27)%2?RGB(25,28,34):RGB(20,23,28)));
            const COLORREF classColor=ColorOf(r.driver.classColor);
            fill(rect(0,y,3,27),classColor);
            COLORREF color=r.player?yellow:white;
            if(o.kind==RelativeKind && app.session.type=="Race" && !r.player && r.lapDifference>0) color=RGB(236,120,114);
            if(o.kind==RelativeKind && app.session.type=="Race" && !r.player && r.lapDifference<0) color=RGB(108,174,238);
            int x=12;
            for(int id:columns) {
                const int w=ColumnWidth(id,o.kind); std::wstring text; COLORREF ink=color;
                switch(id) {
                case PositionColumn: { const int position=o.kind==StandingsKind && app.standings.separateClasses?r.car.classPosition:r.car.position; text=position>0?std::to_wstring(position):L"—"; break; }
                case NumberColumn: {
                    const unsigned col=r.driver.classColor; fill(rect(x,y+4,w-6,19),classColor);
                    ink=(299*((col>>16)&255)+587*((col>>8)&255)+114*(col&255)>140000)?RGB(12,14,18):RGB(255,255,255); text=Wide(r.driver.number); break;
                }
                case DriverColumn: text=DriverName(r,o.kind); break;
                case GapColumn: text=Gap(r,o.kind); break;
                case IntervalColumn: text=Interval(r); break;
                case BestColumn: text=Lap(r.car.best); break;
                case LastColumn: text=Lap(r.car.last); break;
                case StatusColumn: text=OverlayStatus(r,o.kind); ink=text==L"BLACK" || text==L"DQ"?RGB(236,120,114):(text==L"TOW" || text==L"REPAIR" || text==L"SLOW"?RGB(245,163,65):yellow); break;
                case GainedColumn: {
                    const int gained=PositionsGained(app.session,r,app.standings.separateClasses);
                    text=gained==-MaxCars?L"—":(gained==0?L"0":(gained>0?L"↑ ":L"↓ ")+std::to_wstring(std::abs(gained)));
                    ink=gained==-MaxCars || gained==0?muted:(gained>0?RGB(96,208,136):RGB(236,120,114)); break;
                }
                case LapsColumn: text=CompletedLaps(r); break;
                default:
                    text=DriverValue(r,id-LicenseColumn,o.kind);
                    ink=id==LicenseColumn?ColorOf(r.driver.licenseColor):(id==TiresColumn?TireTextColor(r):white);
                    if(id==TiresColumn && TireLabel(r.driver,r.car.tireCompound)=="Primary") fill(rect(x,y+4,w-12,19),RGB(0,0,0));
                }
                const bool badge=id==NumberColumn;
                label(std::move(text),badge?rect(x,y+4,w-6,19):rect(x,y,w-12,27),ink,id==PositionColumn || badge || (id==DriverColumn && r.player)?Bold:Regular,id==DriverColumn || id==StatusColumn?DT_LEFT:(badge?DT_CENTER:DT_RIGHT)); x+=w;
            }
            y+=27;
        };
        if(o.kind==RelativeKind) {
            const auto reference=std::find_if(rows.begin(),rows.end(),[](const Row& row){return row.player;});
            if(reference!=rows.end()) y+=(app.rows/2-static_cast<int>(reference-rows.begin()))*27;
            for(const auto& row:rows) drawRow(row);
        }
        else for(const auto& group:groups) {
            if(app.standings.separateClasses) {
                fill({0,px(y),width,px(y+25)},RGB(35,39,46));
                int cars=0; const int sof=ClassSof(group.classId,cars);
                label(Wide(group.name)+L" · "+std::to_wstring(cars)+(cars==1?L" car":L" cars"),{px(12),px(y),width-px(110),px(y+25)},ColorOf(group.color),Bold);
                label(sof>0?L"SOF "+std::to_wstring(sof):L"",{width-px(110),px(y),width-px(12),px(y+25)},muted,Regular,DT_RIGHT);
                y+=25;
            }
            int previous=0;
            for(const auto& row:group.rows) {
                const int position=app.standings.separateClasses?row.car.classPosition:row.car.position;
                const bool skipped=previous && position>previous+1;
                drawRow(row);
                if(skipped) out.push_back({DrawCommand::DottedLine,Regular,0,{px(12),px(y-27)-titleOffset,width-px(12),px(y-27)-titleOffset+1},RGB(108,115,128)});
                previous=position;
            }
        }
        if(rowsEmpty) label(L"Waiting for driver and position data …",rect(12,TableRowsTop(o.kind)+16,335,28),muted,Regular);
        if(app.footers[o.kind].enabled) {
            const int footer=o.kind==RelativeKind?TableRowsTop(o.kind)+app.rows*27:y+(rowsEmpty?54:0);
            fill({0,px(footer),width,height+titleOffset},RGB(30,34,41));
            slots(app.footers[o.kind].metrics,{0,px(footer),width,height+titleOffset},muted,false);
        }
    } else if(o.kind==FuelKind) {
        const auto& p=Plan(); const auto& scene=p.prediction.finish;
        double pace=p.prediction.ownPace; if(pace<=0) pace=app.frame.lastLap>0?app.frame.lastLap:app.frame.bestLap;
        const auto baseWindow=CalculatePitWindow(app.session,app.frame,p.lapsToFinish,p.estimates[RaceAverage].consumption,pace);
        int y=36; const int layoutWidth=OverlayWidth(2);
        const bool endKnown=std::isfinite(baseWindow.fuelAtEnd);
        const bool tankKnown=app.frame.fuelKnown && std::isfinite(app.frame.fuel) && app.frame.fuel>=0;
        const double capacity=app.session.fuelCapacity; const bool capacityKnown=std::isfinite(capacity) && capacity>0;
        const int current=app.frame.lapCompleted>=0?app.frame.lapCompleted+1:-1;
        const int finishLap=FuelFinishLap();
        // The timeline covers one full-tank stint from its first lap, capped at the predicted finish.
        const int first=p.stintStartLap>0?p.stintStartLap:std::max(1,current);
        const int count=finishLap>0?std::max(0,std::min(StintCellCount(),finishLap-first+1)):StintCellCount();
        const COLORREF red=RGB(238,91,91), green=RGB(90,197,104);
        for(int element:app.fuelOrder) {
            if(element!=ConsumptionTableElement && !app.fuelDisplay[element]) continue;
            switch(element) {
            case KeyNumbersElement: {
                const int third=(layoutWidth-24)/3; const double average=p.estimates[RaceAverage].consumption;
                const wchar_t* captions[]={L"TANK",L"RANGE (LAPS)",L"AT FINISH"};
                const std::wstring values[]={tankKnown?Format(app.frame.fuel,1)+L" L":L"—",Format(tankKnown && average>0?app.frame.fuel/average:-1,1),
                    endKnown?(baseWindow.fuelAtEnd<0?L"−":L"+")+Format(std::abs(baseWindow.fuelAtEnd),1)+L" L":L"—"};
                for(int i=0;i<3;++i) {
                    label(captions[i],rect(12+i*third,y,third,16),muted,Small);
                    label(values[i],rect(12+i*third,y+16,third,30),i==2 && endKnown?(baseWindow.fuelAtEnd<0?red:green):white,Hero);
                }
                break; }
            case PitTimelineElement: {
                const int span=layoutWidth-24;
                for(int i=0;i<count;++i) {
                    const int lap=first+i, left=12+i*span/count, right=12+(i+1)*span/count-2;
                    const bool open=baseWindow.status==PitWindowStatus::Open && lap>=baseWindow.firstLap && lap<=baseWindow.lastLap;
                    const RECT block=rect(left,y,std::max(1,right-left),12); fill(block,open?RGB(49,157,67):(lap==current?yellow:RGB(53,57,64)));
                    if(lap==current && open) fill({block.left,block.top,block.right,block.top+std::max(1,px(3))},yellow);
                }
                label(current>0?L"L"+std::to_wstring(current):L"PIT",rect(12,y+12,40,18),muted,Small);
                label(finishLap>0?L"L"+std::to_wstring(finishLap):L"",rect(layoutWidth-52,y+12,40,18),muted,Small,DT_RIGHT);
                std::wstring box=L"—"; COLORREF boxInk=muted;
                if(baseWindow.status==PitWindowStatus::Open) {
                    const double add=PlannedStopFuel(baseWindow,p);
                    box=(baseWindow.openNow?L"BOX NOW · L":L"BOX L")+std::to_wstring(baseWindow.firstLap)+L"–"+std::to_wstring(baseWindow.lastLap);
                    if(add>=0) box+=L" · +"+Format(add,1)+L" L";
                    if(baseWindow.stops>1) box+=L" · "+std::to_wstring(baseWindow.stops)+L" STOPS";
                    boxInk=baseWindow.openNow?green:white;
                }
                else if(baseWindow.status==PitWindowStatus::NoStopNeeded) { box=L"NO STOP NEEDED"; boxInk=green; }
                else if(baseWindow.status==PitWindowStatus::ExtraStopNeeded) box=L"NO PIT WINDOW";
                label(std::move(box),rect(52,y+12,layoutWidth-104,18),boxInk,Small,DT_CENTER);
                break; }
            case LeaderDistanceElement:
                label(L"Leader Distance",rect(12,y,212,29),muted,Regular);
                label(Format(scene.leaderTotalDistance,2)+L" laps",rect(325,y,128,29),white,Bold,DT_RIGHT);
                break;
            case RemainingLeaderDistanceElement:
                label(L"Remaining Leader Distance",rect(12,y,300,29),muted,Regular);
                label(Format(scene.leaderDistance,2)+L" laps",rect(325,y,128,29),white,Bold,DT_RIGHT);
                break;
            case RefuelToEndElement: {
                label(L"Refuel to End (Average)",rect(12,y,280,27),muted,Regular);
                const double add=p.estimates[RaceAverage].add;
                label(add>=0?Format(add,1)+L" L":L"—",rect(300,y,153,27),yellow,Bold,DT_RIGHT);
                break; }
            case FinalLapTimeElement:
                label(L"Time left at final lap start",rect(12,y,230,29),muted,Regular);
                label(Format(scene.finalLapTimeLeft)+L" s",rect(345,y,108,29),yellow,Bold,DT_RIGHT);
                break;
            case TimeEmptyElement: {
                label(L"Time Empty (Average)",rect(12,y,260,27),muted,Regular);
                wchar_t clock[64]{}; if(baseWindow.timeEmpty>=0 && baseWindow.timeEmpty<36000000) { const int seconds=static_cast<int>(baseWindow.timeEmpty); swprintf_s(clock,L"%d:%02d:%02d",seconds/3600,(seconds/60)%60,seconds%60); }
                label(clock[0]?clock:L"—",rect(280,y,173,27),white,Bold,DT_RIGHT);
                break; }
            case FuelAtEndElement: {
                label(L"Fuel at End (Average)",rect(12,y,260,27),muted,Regular);
                const auto end=endKnown?(baseWindow.fuelAtEnd<0?L"−":L"")+Format(std::abs(baseWindow.fuelAtEnd),1)+L" L":L"—";
                label(end,rect(280,y,173,27),!endKnown?muted:(baseWindow.fuelAtEnd<0?red:green),Bold,DT_RIGHT);
                break; }
            case StopsRemainingElement:
                label(L"Stops Remaining (Average)",rect(12,y,300,27),muted,Regular);
                label(baseWindow.stops>=0?std::to_wstring(baseWindow.stops):L"—",rect(330,y,123,27),white,Bold,DT_RIGHT);
                break;
            case TankGaugeElement:
                fill(rect(12,y,layoutWidth-24,19),RGB(65,70,78)); fill(rect(14,y+2,layoutWidth-28,15),RGB(32,36,42));
                if(tankKnown && capacityKnown) fill(rect(14,y+2,static_cast<int>((layoutWidth-28)*std::clamp(app.frame.fuel/capacity,0.0,1.0)),15),white);
                label(L"E",rect(12,y+19,24,24),muted,Regular);
                label(tankKnown?Format(app.frame.fuel,1)+L" L":L"—",rect(12,y+19,layoutWidth-24,24),white,Bold,DT_CENTER);
                label(capacityKnown?Format(capacity,1)+L" L max":L"—",rect(layoutWidth-150,y+19,138,24),muted,Regular,DT_RIGHT);
                break;
            case StintTimelineElement: {
                label(current>0?L"L"+std::to_wstring(current):L"PIT",rect(12,y+2,34,24),white,Bold);
                // Narrow blocks stay taller than they are wide, even for short stints.
                const int pitch=count>0?std::max(1,std::min(11,(layoutWidth-64)/count)):11;
                for(int i=0;i<count;++i) {
                    const int lap=first+i,x=52+i*pitch,w=std::max(1,pitch-3);
                    const bool open=baseWindow.status==PitWindowStatus::Open && lap>=baseWindow.firstLap && lap<=baseWindow.lastLap;
                    const RECT block=rect(x,y,w,26); fill(block,open?RGB(49,157,67):(lap==current?yellow:RGB(53,57,64)));
                    if(lap==current && open) fill({block.left,block.top,block.right,block.top+std::max(1,px(3))},yellow);
                }
                break; }
            case PitPanelElement: {
                label(current>0?L"Current Lap "+std::to_wstring(current)+L" of "+(finishLap>0?std::to_wstring(finishLap):L"—"):L"Current Lap —",rect(12,y,190,39),muted,Regular);
                fill(rect(205,y,248,39),baseWindow.openNow?RGB(39,153,62):RGB(77,82,91)); fill(rect(301,y+2,150,35),RGB(18,21,26));
                const bool rangeKnown=baseWindow.status==PitWindowStatus::Open;
                label(baseWindow.openNow?L"OPEN":L"CLOSED",rect(207,rangeKnown?y:y+4,92,rangeKnown?20:30),white,Bold,DT_CENTER);
                if(rangeKnown) label(L"L"+std::to_wstring(baseWindow.firstLap)+L"–"+std::to_wstring(baseWindow.lastLap),rect(207,y+19,92,20),white,Regular,DT_CENTER);
                const double add=PlannedStopFuel(baseWindow,p);
                label(add>=0?L"+"+Format(add,1)+L" L":L"—",rect(305,y+2,143,35),white,Bold,DT_CENTER);
                break; }
            case ConsumptionTableElement: {
                if(std::any_of(app.fuelRows.begin(),app.fuelRows.end(),[](bool shown){return shown;})) {
                    label(L"CONSUMPTION",rect(12,y,151,24),muted,Regular);
                    label(L"L / LAP",rect(170,y,70,24),muted,Regular,DT_RIGHT);
                    label(L"ADD TO FINISH",rect(250,y,103,24),yellow,Regular,DT_RIGHT);
                    if(app.fuelDisplay[ConsumptionTableElement]) label(L"PIT ≈",rect(363,y,90,24),muted,Regular,DT_RIGHT);
                    y+=24;
                }
                for(int i:{0,5,1,2,3,4}) if(app.fuelRows[i]) {
                    const auto& estimate=p.estimates[i];
                    fill({0,px(y),width,px(y+29)},(y/29)%2?RGB(25,28,34):RGB(20,23,28));
                    std::wstring name=FuelMetricNames[i]; if((i==1 && estimate.samples<10) || (i==2 && estimate.samples<5)) name+=L" ("+std::to_wstring(estimate.samples)+L")";
                    label(std::move(name),rect(12,y,151,29),white,Regular); label(estimate.samples?Format(estimate.consumption,2):L"—",rect(170,y,70,29),white,Regular,DT_RIGHT);
                    label(Format(estimate.add)+L" L",rect(250,y,103,29),yellow,Bold,DT_RIGHT);
                    if(app.fuelDisplay[ConsumptionTableElement]) {
                        const auto window=CalculatePitWindow(app.session,app.frame,p.lapsToFinish,estimate.consumption);
                        const auto text=window.status==PitWindowStatus::Open?L"L"+std::to_wstring(window.firstLap)+L"–"+std::to_wstring(window.lastLap):(window.status==PitWindowStatus::NoStopNeeded?L"No stop":(window.status==PitWindowStatus::ExtraStopNeeded?L"No window":L"—"));
                        label(text,rect(363,y,90,29),window.status==PitWindowStatus::Open?RGB(90,197,104):muted,Regular,DT_RIGHT);
                    }
                    y+=29;
                }
                continue; }
            }
            y+=FuelElements[element].height;
        }
    } else if(o.kind==WeatherKind) {
        int y=40;
        for(int element:app.weatherOrder) if(app.weatherDisplay[element]) {
            const int h=WeatherElements[element].height;
            fill(rect(8,y,224,h),RGB(30,34,41));
            if(element==TrackTempElement || element==AirTempElement) {
                label(WeatherElements[element].title,rect(20,y,112,h),white,Regular);
                label(FooterTemperature(element==TrackTempElement?app.frame.trackTemperature:app.frame.airTemperature),rect(132,y,88,h),white,Bold,DT_RIGHT);
            } else if(element==WindElement) {
                label(L"Wind",rect(20,y,110,32),white,Regular);
                label(L"km/h",rect(166,y,54,32),muted,Regular,DT_RIGHT);
                const double angle=app.weatherDisplay[WindDirectionOption]?RelativeWindAngle(app.frame):std::numeric_limits<double>::quiet_NaN();
                const int cy=y+106;
                // Float geometry keeps the anti-aliased ring free of intermediate pixel rounding.
                const float scale=static_cast<float>(dpi*o.place.scale)/9600, centerX=120*scale, centerY=cy*scale-titleOffset, radius=65*scale;
                const RECT bounds{static_cast<LONG>(centerX-radius-8*scale)-1,static_cast<LONG>(centerY-radius-8*scale)-1,static_cast<LONG>(centerX+radius+8*scale)+2,static_cast<LONG>(centerY+radius+8*scale)+2};
                out.push_back({DrawCommand::WindRing,Regular,0,bounds,0,{centerX,centerY,radius,scale,std::isfinite(angle)?static_cast<float>(angle):-1.f}});
                label(Format(app.frame.windSpeed*3.6,0),rect(65,cy-24,110,48),white,Large,DT_CENTER);
            } else if(element==WetnessElement) {
                label(FooterValue(WetnessMetric),rect(20,y,200,h),white,Regular,DT_CENTER);
            } else {
                std::wstring usage=Wide(app.session.trackUsage); if(usage.empty()) usage=L"Track Usage —"; else usage[0]=static_cast<wchar_t>(towupper(usage[0]));
                label(std::move(usage),rect(20,y,200,h),white,Regular,DT_CENTER);
            }
            y+=h+8;
        }
    } else {
        const bool known=app.frame.limiterKnown;
        fill({0,px(32),width,height+titleOffset},!known?RGB(95,100,110):(app.frame.limiter?RGB(27,135,68):RGB(182,41,44)));
        label(!known?L"PIT LIMITER STATUS UNKNOWN":(app.frame.limiter?L"PIT LIMITER ACTIVE":L"PIT LIMITER NOT ACTIVE"),{px(12),px(38),width-px(12),height+titleOffset-px(6)},RGB(255,255,255),Bold,DT_CENTER);
    }
}
// DrawTextW costs about three times as much as measuring and ExtTextOutW; it remains only for ellipsis.
void DrawLabel(HDC dc,const DrawCommand& c) {
    if(c.text.empty()) return;
    SetTextColor(dc,c.color);
    const int length=static_cast<int>(c.text.size()); RECT r=c.r; SIZE size{};
    GetTextExtentPoint32W(dc,c.text.c_str(),length,&size);
    if(size.cx>r.right-r.left) { DrawTextW(dc,c.text.c_str(),length,&r,c.align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX); return; }
    const int x=c.align==DT_CENTER?(r.left+r.right-size.cx)/2:(c.align==DT_RIGHT?r.right-size.cx:r.left);
    ExtTextOutW(dc,x,(r.top+r.bottom-size.cy)/2,ETO_CLIPPED,&r,c.text.c_str(),length,nullptr);
}
void DrawWind(HDC dc,const DrawCommand& c) {
    const auto [centerX,centerY,radius,scale,angle]=c.shape;
    Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen ring(Gdiplus::Color(255,232,234,237),4*scale); ring.SetStartCap(Gdiplus::LineCapRound); ring.SetEndCap(Gdiplus::LineCapRound);
    if(angle<0) { graphics.DrawEllipse(&ring,centerX-radius,centerY-radius,2*radius,2*radius); return; }
    graphics.DrawArc(&ring,centerX-radius,centerY-radius,2*radius,2*radius,static_cast<float>((angle+.3)*180/3.141592653589793-90),static_cast<float>(360-.6*180/3.141592653589793));
    auto point=[&](double bearing,double r) {return Gdiplus::PointF(centerX+static_cast<float>(std::sin(bearing)*r)*scale,centerY-static_cast<float>(std::cos(bearing)*r)*scale);};
    Gdiplus::PointF arrow[]={point(angle-.15,71),point(angle,56),point(angle+.15,71)};
    Gdiplus::Pen pen(Gdiplus::Color(255,246,204,66),4*scale); pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLines(&pen,arrow,3);
}
// Replays the scene into the back buffer, limited to the dirty region when one is given.
void PaintScene(Overlay& o,HRGN dirty) {
    HDC dc=o.dc;
    SelectClipRgn(dc,dirty);
    int font=-1;
    for(const auto& c:o.scene) {
        if(dirty && !RectInRegion(dirty,&c.r)) continue;
        switch(c.type) {
        case DrawCommand::Fill: SetDCBrushColor(dc,c.color); FillRect(dc,&c.r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); break;
        case DrawCommand::Text: if(font!=c.font) { SelectObject(dc,o.fonts[c.font]); font=c.font; } DrawLabel(dc,c); break;
        case DrawCommand::DottedLine: {
            HPEN pen=CreatePen(PS_DOT,1,c.color); const auto old=SelectObject(dc,pen);
            MoveToEx(dc,c.r.left,c.r.top,nullptr); LineTo(dc,c.r.right,c.r.top); SelectObject(dc,old); DeleteObject(pen); break;
        }
        case DrawCommand::WindRing: DrawWind(dc,c); break;
        }
    }
    SelectClipRgn(dc,nullptr);
}
bool EnsureBuffer(Overlay& o,int width,int height,int dpi) {
    if(o.dc && o.width==width && o.height==height && o.dpi==dpi) return false;
    o.Free();
    HDC screen=GetDC(o.window);
    o.dc=CreateCompatibleDC(screen); o.bitmap=CreateCompatibleBitmap(screen,width,height); o.original=SelectObject(o.dc,o.bitmap);
    ReleaseDC(o.window,screen);
    const int size=std::max(8,MulDiv(14,dpi*o.place.scale,9600));
    auto font=[&](int pixels,int weight){ return CreateFontW(-pixels,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI"); };
    auto scaled=[&](int pixels){ return MulDiv(pixels,dpi*o.place.scale,9600); };
    o.fonts={font(size,FW_NORMAL),font(size,FW_SEMIBOLD),font(scaled(32),FW_SEMIBOLD),font(std::max(7,scaled(11)),FW_NORMAL),font(scaled(22),FW_SEMIBOLD)};
    o.originalFont=SelectObject(o.dc,o.fonts[Regular]);
    SetBkMode(o.dc,TRANSPARENT);
    o.width=width; o.height=height; o.dpi=dpi;
    return true;
}
// Rebuilds the scene and redraws only the commands that changed since the last refresh.
void RefreshOverlay(Overlay& o) {
    RECT client{}; GetClientRect(o.window,&client);
    if(client.right<=0 || client.bottom<=0) return;
    const int dpi=GetDpiForWindow(o.window);
    BuildScene(o,client.right,client.bottom,dpi,app.sceneBuffer);
    const bool full=EnsureBuffer(o,client.right,client.bottom,dpi) || app.sceneBuffer.size()!=o.scene.size();
    HRGN dirty=nullptr;
    if(!full) {
        for(size_t i=0;i<app.sceneBuffer.size();++i) {
            const auto& a=app.sceneBuffer[i]; const auto& b=o.scene[i];
            if(a==b) continue;
            for(const RECT* r:{&a.r,&b.r}) {
                HRGN part=CreateRectRgnIndirect(r);
                if(!dirty) dirty=part; else { CombineRgn(dirty,dirty,part,RGN_OR); DeleteObject(part); }
            }
        }
        if(!dirty) return;
    }
    std::swap(o.scene,app.sceneBuffer);
    PaintScene(o,dirty);
    if(dirty) { InvalidateRgn(o.window,dirty,FALSE); DeleteObject(dirty); }
    else InvalidateRect(o.window,nullptr,FALSE);
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
    InvalidateModel();
    const int dpi=GetDpiForWindow(o.window);
    const int w=MulDiv(OverlayWidth(o.kind),dpi*o.place.scale,9600),h=MulDiv(BaseHeight(o.kind),dpi*o.place.scale,9600);
    int x=o.place.x-(o.place.growLeft?w:0),y=o.place.y-(o.place.growUp?h:0);
    RECT r{x,y,x+w,y+h};
    HMONITOR monitor=MonitorFromRect(&r,MONITOR_DEFAULTTONULL);
    if(recover || !monitor) {
        if(!monitor) { monitor=MonitorFromWindow(app.main,MONITOR_DEFAULTTONEAREST); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info); x=info.rcWork.left+30; y=info.rcWork.top+50; }
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
        x=static_cast<int>(std::clamp<LONG>(x,info.rcWork.left-w+60,info.rcWork.right-60));
        const LONG maxY=o.kind==FuelKind?std::max(info.rcWork.top,info.rcWork.bottom-h):info.rcWork.bottom-32;
        y=static_cast<int>(std::clamp<LONG>(y,info.rcWork.top,maxY));
        o.place.x=x+(o.place.growLeft?w:0); o.place.y=y+(o.place.growUp?h:0);
    }
    SetWindowPos(o.window,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE);
    o.Free(); InvalidateRect(o.window,nullptr,FALSE);
    SetLayeredWindowAttributes(o.window,0,static_cast<BYTE>(o.place.opacity*255/100),LWA_ALPHA);
}
void UpdateOverlays() {
    InvalidateModel();
    const bool pitVisible=app.pitVisibility.Update(app.session,app.frame);
    for(auto& o:app.overlays) {
        bool visible=o.place.enabled && !app.hidden && (o.kind!=WeatherKind || std::any_of(app.weatherDisplay.begin(),app.weatherDisplay.begin()+WeatherCardCount,[](bool value){return value;})) && (app.preview || (app.frame.connected && (o.kind!=PitLimiterKind || pitVisible)));
        auto style=GetWindowLongPtrW(o.window,GWL_EXSTYLE);
        const auto desired=app.edit?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT);
        if(style!=desired) SetWindowLongPtrW(o.window,GWL_EXSTYLE,desired);
        if(visible && !IsWindowVisible(o.window)) ShowWindow(o.window,SW_SHOWNOACTIVATE);
        if(!visible && IsWindowVisible(o.window)) ShowWindow(o.window,SW_HIDE);
        if(visible) {
            const int dpi=GetDpiForWindow(o.window);
            RECT client{}; GetClientRect(o.window,&client);
            if(client.bottom!=MulDiv(BaseHeight(o.kind),dpi*o.place.scale,9600) || client.right!=MulDiv(OverlayWidth(o.kind),dpi*o.place.scale,9600)) { Fit(o); InvalidateModel(); }
            RefreshOverlay(o);
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
void ProcessFrame(bool connected) {
    if(!connected) { app.frame={}; app.events.Update(app.session,app.frame); app.session={}; app.fuel.Reset(); app.predictor.Reset(); app.pitHistory.Reset(); }
    else { app.pitHistory.Update(app.session,app.frame); app.events.Update(app.session,app.frame); app.fuel.Update(app.session,app.frame,app.excludeYellowFuelLaps); app.predictor.Update(app.session,app.frame); }
}
void Tick() {
    if(app.preview) PreviewData(GetTickCount64()/1000.0);
    else ProcessFrame(app.telemetry.Read(app.frame,app.session));
    std::wstring status=app.preview?L"Preview · sample data, no live telemetry":(app.frame.connected?L"Connected · "+Wide(app.session.track):L"Waiting for iRacing …");
    if(app.edit) status+=L" · Move overlays";
    if(!app.trayAvailable && !app.diagnostics) status+=L" · No tray icon; closing Settings exits RaceMate";
    if(!app.hotkeyError.empty()) status+=L" · Shortcut in use: "+app.hotkeyError;
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
    case WM_PAINT: {
        if(!o->dc) RefreshOverlay(*o);
        PAINTSTRUCT p{}; HDC dc=BeginPaint(w,&p); const RECT& r=p.rcPaint;
        if(o->dc) BitBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,o->dc,r.left,r.top,SRCCOPY);
        EndPaint(w,&p); return 0;
    }
    case WM_PRINTCLIENT: InvalidateModel(); RefreshOverlay(*o); if(o->dc) BitBlt(reinterpret_cast<HDC>(wp),0,0,o->width,o->height,o->dc,0,0,SRCCOPY); return 0;
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
// Global shortcuts would swallow the keys being assigned, so release them while a shortcut field has focus.
LRESULT CALLBACK HotkeyFieldProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(m==WM_SETFOCUS && app.hotkeysEnabled) UnregisterHotkeys();
    if(m==WM_KILLFOCUS) RegisterHotkeys();
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,HotkeyFieldProc,id);
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
    Control(L"STATIC",L"Native iRacing Overlays · Version 0.4.37",0,25,49,660,22);
    app.status=Control(L"STATIC",L"Waiting for iRacing …",0,25,80,690,25);
    HWND group=Control(L"BUTTON",L"Overlays",0,20,116,720,256,BS_GROUPBOX|WS_CLIPSIBLINGS);
    SetWindowSubclass(group,GroupProc,1,0);
    Control(L"STATIC",L"Display",0,38,141,260,24); Control(L"STATIC",L"Scale",0,312,141,130,24); Control(L"STATIC",L"Opacity",0,456,141,130,24);
    for(int i=0;i<OverlayCount;++i) {
        Check(Titles[i],OverlayEnableId+i,38,174+i*36,256,app.overlays[i].place.enabled);
        std::vector<std::wstring> scales,opacity; for(int n=50;n<=150;n+=10) scales.push_back(std::to_wstring(n)+L" %");
        for(int n=40;n<=100;n+=10) opacity.push_back(std::to_wstring(n)+L" %");
        Combo(OverlayScaleId+i,312,174+i*36,126,scales,(app.overlays[i].place.scale-50)/10);
        Combo(OverlayOpacityId+i,456,174+i*36,126,opacity,(app.overlays[i].place.opacity-40)/10);
        Control(L"BUTTON",L"Settings",OverlaySettingsId+i,610,171+i*36,106,28,WS_TABSTOP);
    }
    group=Control(L"BUTTON",L"General",0,20,386,720,170,BS_GROUPBOX|WS_CLIPSIBLINGS);
    SetWindowSubclass(group,GroupProc,1,0);
    Control(L"STATIC",L"Refresh rate",0,38,415,116,24);
    Combo(RefreshRateId,167,415,112,{L"2 Hz",L"5 Hz",L"10 Hz",L"20 Hz"},app.rate==2?0:(app.rate==5?1:(app.rate==20?3:2)));
    Check(L"Start with Windows",StartWithWindowsId,38,459,300,app.startWithWindows);
    Check(L"Start Minimized",StartMinimizedId,380,459,330,app.startMinimized);
    Check(L"Show Title Bar",ShowTitleBarId,38,490,300,app.showTitleBar);
    const wchar_t* hotkeyLabels[]={L"Move",L"Hide",L"Settings"};
    for(int i=0;i<HotkeyCount;++i) {
        const int x=38+i*232;
        Control(L"STATIC",hotkeyLabels[i],0,x,525,52,24);
        HWND field=Control(HOTKEY_CLASSW,L"",HotkeyFieldId+i,x+52,522,142,26,WS_BORDER|WS_TABSTOP);
        // A shortcut always needs Ctrl or Alt so plain typing is never captured.
        SendMessageW(field,HKM_SETRULES,HKCOMB_NONE|HKCOMB_S,MAKELPARAM(HOTKEYF_CONTROL|HOTKEYF_ALT,0));
        SendMessageW(field,HKM_SETHOTKEY,app.hotkeys[i],0); SetWindowSubclass(field,HotkeyFieldProc,1,0);
        Control(L"BUTTON",L"\u00d7",HotkeyClearId+i,x+196,522,28,26,WS_TABSTOP);
    }
    Check(L"Preview with sample data",PreviewId,25,573,300,app.preview);
    Check(L"Move overlays",EditId,380,573,330,app.edit);
    Control(L"STATIC",L"Shortcuts work in every application. Click a field and press a new combination; \u00d7 turns it off.",0,25,608,705,24);
    Control(L"STATIC",L"Closing Settings keeps RaceMate running in the tray.",0,25,633,705,24);
    Control(L"BUTTON",L"Reset positions",ResetId,25,660,194,30,WS_TABSTOP);
    Control(L"BUTTON",app.hidden?L"Show overlays":L"Hide overlays",HideId,233,660,187,30,WS_TABSTOP);
    Control(L"BUTTON",L"Race Control",RaceControlId,434,660,134,30,WS_TABSTOP);
    Control(L"BUTTON",L"Exit",ExitId,582,660,134,30,WS_TABSTOP);
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
        else if(id==RaceControlId) OpenRaceControl();
        else if(id==ExitId) DestroyWindow(w);
        else if(id>=OverlaySettingsId && id<OverlaySettingsId+OverlayCount) OpenOverlaySettings(id-OverlaySettingsId);
        else if((id==StartWithWindowsId || id==StartMinimizedId) && notification==BN_CLICKED) {
            const bool checked=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED;
            if(id==StartWithWindowsId) {
                if(RegisterStartup(checked)) app.startWithWindows=checked;
                else { SendDlgItemMessageW(w,id,BM_SETCHECK,app.startWithWindows?BST_CHECKED:BST_UNCHECKED,0); MessageBoxW(w,L"Could not update Windows startup. The previous setting was kept.",L"RaceMate",MB_OK|MB_ICONERROR); }
            } else app.startMinimized=checked;
            Save();
        }
        else if(id>=HotkeyFieldId && id<HotkeyFieldId+HotkeyCount && notification==EN_CHANGE) {
            // Modifier-only intermediate states have no key yet; registration follows when the field loses focus.
            const WORD hotkey=static_cast<WORD>(SendDlgItemMessageW(w,id,HKM_GETHOTKEY,0,0));
            if(LOBYTE(hotkey)) { app.hotkeys[id-HotkeyFieldId]=hotkey; Save(); }
        }
        else if(id>=HotkeyClearId && id<HotkeyClearId+HotkeyCount && notification==BN_CLICKED) {
            app.hotkeys[id-HotkeyClearId]=0; SendDlgItemMessageW(w,HotkeyFieldId+id-HotkeyClearId,HKM_SETHOTKEY,0,0); RegisterHotkeys(); Save();
        }
        else if(id==ShowTitleBarId && notification==BN_CLICKED) { app.showTitleBar=SendDlgItemMessageW(w,id,BM_GETCHECK,0,0)==BST_CHECKED; app.tableHeaders.fill(app.showTitleBar); for(auto& overlay:app.overlays) Fit(overlay); Save(); UpdateOverlays(); }
        else if(id==ResetId) { for(auto& o:app.overlays) { SetOverlayOrigin(o,DefaultPositions[o.kind].x,DefaultPositions[o.kind].y); Fit(o,true); } Save(); }
        else if(id>=OverlayEnableId && id<OverlayEnableId+OverlayCount && notification==BN_CLICKED) { app.overlays[id-OverlayEnableId].place.enabled=SendMessageW(GetDlgItem(w,id),BM_GETCHECK,0,0)==BST_CHECKED; Save(); }
        else if(id>=OverlayScaleId && id<OverlayScaleId+OverlayCount && notification==CBN_SELCHANGE) { auto& o=app.overlays[id-OverlayScaleId]; o.place.scale=50+10*static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); Fit(o); Save(); }
        else if(id>=OverlayOpacityId && id<OverlayOpacityId+OverlayCount && notification==CBN_SELCHANGE) { auto& o=app.overlays[id-OverlayOpacityId]; o.place.opacity=40+10*static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); Fit(o); Save(); }
        else if(id==RefreshRateId && notification==CBN_SELCHANGE) { const int rates[]={2,5,10,20}; int index=static_cast<int>(SendMessageW(GetDlgItem(w,id),CB_GETCURSEL,0,0)); if(index>=0 && index<4) app.rate=rates[index]; Timer(); Save(); }
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
        UnregisterHotkeys(); app.hotkeysEnabled=false;
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
    if(!app.diagnostics) {
        // Same as Task Manager's efficiency mode: the simulator always wins CPU time, and Windows may
        // run RaceMate on efficient cores at low clock speeds. A refresh takes well under a millisecond.
        SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
        PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,PROCESS_POWER_THROTTLING_EXECUTION_SPEED,PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
        SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttling,sizeof(throttling));
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
        app.weatherOrder=DefaultWeatherOrder; app.weatherDisplay.fill(true); app.footers={}; app.columnHeaders.fill(false); app.rows=9; app.rate=10; app.standings={}; app.fuelRows.fill(true); app.fuelDisplay.fill(true); app.startWithWindows=app.startMinimized=false;
        for(int kind=0;kind<OverlayCount;++kind) { auto& p=app.overlays[kind].place; p={}; p.x=DefaultPositions[kind].x; p.y=DefaultPositions[kind].y; }
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_HOTKEY_CLASS}; InitCommonControlsEx(&controls);
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
    if(wcsstr(args,L"--benchmark-tick")) return RunTickBenchmark();
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
    app.hotkeysEnabled=true; RegisterHotkeys();
    Tick(); ShowWindow(app.main,StartupShowCommand(app.trayAvailable,show)); UpdateWindow(app.main);
    if(!app.hotkeyError.empty()) MessageBoxW(app.main,(L"Another application already uses the shortcut for: "+app.hotkeyError+L". Choose a different combination in Settings.").c_str(),L"RaceMate",MB_OK|MB_ICONINFORMATION);
    MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) if(!(raceFieldsWindow && IsDialogMessageW(raceFieldsWindow,&message)) && !(footerSettingsWindow && IsDialogMessageW(footerSettingsWindow,&message)) && !(columnOrderWindow && IsDialogMessageW(columnOrderWindow,&message)) && !(app.overlaySettings && IsDialogMessageW(app.overlaySettings,&message)) && !IsDialogMessageW(app.main,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(app.font); DeleteObject(app.heading); DeleteObject(app.brush); if(mutex) CloseHandle(mutex); return 0;
}
