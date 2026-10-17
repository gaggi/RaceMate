#include "Telemetry.h"
#include "RaceControl.h"
#include "TrackMapLoader.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include "ui/BackgroundTask.h"
#include "ui/NavBar.h"
#include "ui/PageWindow.h"
#include "ui/RowEditors.h"
#include "ui/RowList.h"
#include "ui/ScrollHost.h"
#include "ui/SegmentedControl.h"
#include "ui/StatusPanel.h"
#include "update/UpdateChecker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <list>
#include <map>
#ifdef RACEMATE_DIAGNOSTICS
#include <fstream>
#include <psapi.h>
#endif

namespace {
using namespace rm;
constexpr COLORREF Background=RGB(246,247,249), Text=RGB(40,47,58);
constexpr UINT TrayMessage=WM_APP+1;
enum OverlayKind { RelativeKind, StandingsKind, FuelKind, PitLimiterKind, WeatherKind, TrackMapKind };
constexpr int OverlayCount=TrackMapKind+1;
constexpr bool IsTable(int kind) { return kind==RelativeKind || kind==StandingsKind; }
const wchar_t* Titles[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pit Limiter",L"Weather",L"Track Map"};
const wchar_t* Sections[]={L"Relative",L"Standings",L"Fuel Calculator",L"Pitlimiter",L"Weather",L"Track Map"};
constexpr POINT DefaultPositions[]={{40,120},{480,120},{40,470},{40,720},{40,820},{960,120}};
// Refresh rates an overlay can choose, and each overlay's default; telemetry is read at least as often as the fastest.
constexpr int OverlayRates[]={1,2,5,10,15,20};
constexpr int DefaultRates[]={10,1,1,10,1,10};
// Relative and Standings columns. The values are stored in ColumnOrder.
enum Column {
    PositionColumn, NumberColumn, DriverColumn, GapColumn, IntervalColumn, BestColumn, LastColumn,
    LicenseColumn, IRatingColumn, TiresColumn, PitLapColumn, PitTimeColumn, PitStopColumn,
    LapsColumn, GainedColumn, StatusColumn, BrandColumn, RatingChangeColumn, StintColumn, StopsColumn, FuelColumn
};
constexpr int ColumnCount=FuelColumn+1;
// License..PitStop are the optional driver columns, also used by Race Control; Brand..Stint came later with their own keys.
constexpr int DriverColumnCount=PitStopColumn-LicenseColumn+1, ExtraColumnCount=FuelColumn-BrandColumn+1;
// Widths include the 8 px gap to the next column; Status is the badge after the driver's name, not a column.
struct ColumnInfo { const wchar_t* title; int width, compactWidth; UINT align=DT_RIGHT; };
constexpr ColumnInfo Columns[]={
    {L"P",36,32},{L"#",50,50,DT_CENTER},{L"DRIVER",180,170,DT_LEFT},{L"GAP",70,60},{L"INT",64,56},{L"BEST",80,72},{L"LAST",80,72},
    {L"LICENSE",70,58,DT_CENTER},{L"iRATING",66,48},{L"TIRES",54,30,DT_CENTER},{L"PIT LAP",64,48},{L"PIT TIME",74,66},{L"PIT STOP",80,72},
    {L"LAPS",50,42},{L"GAIN",50,42},{L"STATUS",0,0,DT_LEFT},{L"CAR",124,116,DT_LEFT},{L"iR ±",54,46},{L"STINT",58,46,DT_CENTER},{L"STOPS",56,32,DT_CENTER},{L"FUEL",62,58,DT_CENTER}};
static_assert(std::size(Columns)==ColumnCount);
const wchar_t* DriverColumnKeys[]={L"ShowLicense",L"ShowIRating",L"ShowTires",L"ShowLastPitLap",L"ShowPitTime",L"ShowPitStopTime"};
const wchar_t* ExtraColumnKeys[]={L"ShowCar",L"ShowIRatingChange",L"ShowStint",L"ShowStops",L"ShowFuelFinish"};
// Saved orders keep their place; columns a file does not list yet go before their default successor.
const std::vector<int> DefaultColumns[]={
    {PositionColumn,NumberColumn,DriverColumn,StatusColumn,BrandColumn,LicenseColumn,IRatingColumn,RatingChangeColumn,StintColumn,StopsColumn,BestColumn,LastColumn,GapColumn,
     TiresColumn,PitLapColumn,PitTimeColumn,PitStopColumn,FuelColumn},
    {PositionColumn,GainedColumn,NumberColumn,DriverColumn,StatusColumn,BrandColumn,GapColumn,IntervalColumn,BestColumn,LastColumn,StintColumn,StopsColumn,LapsColumn,
     LicenseColumn,IRatingColumn,RatingChangeColumn,TiresColumn,PitLapColumn,PitTimeColumn,PitStopColumn,FuelColumn}};
// Header and footer metrics. The values are stored in the Header*/Footer* keys.
enum Metric {
    NoMetric, TrackMetric, IncidentsMetric, TrackTempMetric, AirTempMetric, TemperaturesMetric, TimeRemainingMetric, TimeElapsedMetric,
    CurrentLapMetric, FinalLapMetric, LapsRemainingMetric, LocalTimeMetric, SimTimeMetric, SessionTypeMetric, WetnessMetric, WindMetric,
    SofMetric, OverlayNameMetric, PositionMetric, BrakeBiasMetric, StopsNeededMetric, DeclaredWetMetric,
    LeaderDistanceMetric, WhiteFlagTimeMetric, LapLeaderDistanceMetric, TrackLayoutMetric, PitExitMetric
};
constexpr int MetricCount=PitExitMetric+1;
const wchar_t* MetricTitles[]={L"None",L"Track",L"Incidents",L"Track Temperature",L"Air Temperature",L"Temperatures",L"Session Time Remaining",L"Session Time Elapsed",L"Current Lap",L"Current / Expected Final Lap",L"Estimated Laps Remaining",L"Local Time",L"Sim Time",L"Session Type",L"Track Wetness",L"Wind",L"Class SOF",L"Overlay Name",L"Position in Class",L"Brake Bias (your car)",L"Stops Needed (your car)",L"Declared Wet",L"Leader projected race distance",L"Time left at leader's final lap",L"Current lap / Leader projected distance",L"Track and layout",L"Pit exit (your car)"};
static_assert(std::size(MetricTitles)==MetricCount);
// The first slot sits on the left, the others right-aligned in order. Keys keep their earlier names.
constexpr int SlotCount=4;
using Slots=std::array<int,SlotCount>;
const wchar_t* HeaderKeys[]={L"HeaderLeft",L"HeaderCenter",L"HeaderRight",L"HeaderFourth"};
const wchar_t* FooterKeys[]={L"FooterLeft",L"FooterCenter",L"FooterRight",L"FooterFourth"};
constexpr Slots DefaultHeader{OverlayNameMetric,SofMetric,IncidentsMetric,TimeRemainingMetric}, DefaultFooter{TemperaturesMetric,NoMetric,WetnessMetric,BrakeBiasMetric};
// Fuel Calculator modules, top to bottom; each switch is stored under its key.
enum FuelModule {
    TankModule, FiguresModule, HistoryModule, CautionMarks, RefuelCard, WindowCard, TimelineModule,
    OneShorterRow, TargetRow, OneLongerRow, TwoLongerRow, LeaderDistanceModule, WhiteFlagTimeModule, PitExitModule
};
constexpr int FuelModuleCount=PitExitModule+1;
struct ModuleInfo { const wchar_t* key; const wchar_t* title; const wchar_t* detail; bool on; };
constexpr ModuleInfo FuelModules[]={
    {L"ShowTank",L"Tank",L"Fuel, range in laps and a bar of the fuel to the finish",true},
    {L"ShowFigures",L"Consumption",L"",true},
    {L"ShowHistory",L"Last 10 laps",L"Fuel of each lap against the 10-lap average",true},
    // Switched in the expanded "Last 10 laps" row. A new key, so it starts off for earlier installs too.
    {L"MarkCautionLaps",L"Mark caution laps",L"In yellow; they never count toward consumption",false},
    {L"ShowRefuel",L"Refuel to finish",L"Without a safety margin; green when it fits in the tank",true},
    {L"ShowPitWindow",L"Pit window",L"Earliest and latest lap to stop",true},
    {L"ShowTimeline",L"Pit window timeline",L"One block per lap of a full tank: window green, current lap yellow",true},
    {L"ShowOneShorter",L"One lap shorter",L"Most fuel per lap for one lap less",true},
    {L"ShowTarget",L"Target",L"Most fuel per lap that still reaches the latest stop, or the finish",true},
    {L"ShowOneLonger",L"One lap longer",L"Most fuel per lap for one lap more",true},
    {L"ShowTwoLonger",L"Two laps longer",L"Most fuel per lap for two laps more",false},
    {L"ShowLeaderDistance",L"Leader projected race distance",L"Total leader laps at time expiry, with two decimals; at the finish for lap-limited races. No future stops assumed.",false},
    {L"ShowFinalLapTime",L"Time left at leader's final lap",L"Race clock when the current overall leader is projected to start the final lap. No future stops assumed.",false},
    {L"ShowPitExit",L"Pit exit",L"Class position and the car ahead if you box now: pit lane loss plus the service set in the pit menu",false}};
static_assert(std::size(FuelModules)==FuelModuleCount);
// Consumption figures in display order; app.fuelRows follows this order.
struct FigureInfo { int metric; const wchar_t* heading; const wchar_t* title; const wchar_t* key; bool on; };
constexpr FigureInfo FuelFigures[]={
    {0,L"LAST",L"Last lap",L"ShowLast",true},{6,L"AVG 3",L"Average of 3 laps",L"ShowAverage3",true},
    {1,L"AVG 10",L"Average of 10 laps",L"ShowAverage10",true},{2,L"AVG 5",L"Average of 5 laps",L"ShowAverage5",false},
    {5,L"RACE",L"Race average",L"ShowAverage",false},{4,L"MIN",L"Minimum",L"ShowMinimum",true},{3,L"MAX",L"Maximum",L"ShowMaximum",true}};
static_assert(std::size(FuelFigures)==FuelMetricCount);
// The average of the last 10 clean laps drives range, pit window, refuel and targets.
constexpr int FuelBasis=static_cast<int>(FuelMetric::Average10);
// Weather modules, top to bottom; the last two switch trend arrows and the wind relative to the car.
enum WeatherModule {
    ConditionModule, RainModule, TemperatureModule, TemperatureHistoryModule, WindModule, DetailsModule, UsageModule,
    TrendArrows, WindRelative, WindRose, WindText
};
constexpr int WeatherModuleCount=WindText+1, WeatherPanelCount=UsageModule+1;
constexpr ModuleInfo WeatherModules[]={
    {L"ShowTrackWetness",L"Track condition",L"iRacing's seven levels from dry to extremely wet",true},
    {L"ShowPrecipitation",L"Precipitation",L"Rain at the start/finish line",true},
    {L"ShowTemperatures",L"Temperatures",L"Track and air",true},
    {L"ShowTemperatureHistory",L"Track temperature history",L"The last 20 minutes",true},
    {L"ShowWind",L"Wind",L"Speed and compass direction around the wind ring",true},
    {L"ShowDetails",L"Details",L"Humidity, sky and fog",false},
    {L"ShowTrackUsage",L"Track usage",L"How much rubber is on the track",false},
    {L"ShowTrends",L"Trend arrows",L"Rising, falling or steady over the last 10 minutes",true},
    {L"ShowWindDirection",L"Wind relative to the car",L"Arrow on the ring and crosswind text; needs your car's heading, so only while you drive",true},
    // Parts of the wind module, switched in its expanded row.
    {L"ShowWindRose",L"Wind rose",L"The ring with the speed inside",true},
    {L"ShowWindText",L"Wind text",L"Speed, compass direction and crosswind",true}};
static_assert(std::size(WeatherModules)==WeatherModuleCount);
// Modules in their default order; the page moves them, the rest are display options.
const std::vector<int> FuelOrder{TankModule,FiguresModule,LeaderDistanceModule,WhiteFlagTimeModule,HistoryModule,RefuelCard,WindowCard,TimelineModule,PitExitModule,OneShorterRow,TargetRow,OneLongerRow,TwoLongerRow};
const std::vector<int> WeatherOrder{ConditionModule,RainModule,TemperatureModule,TemperatureHistoryModule,WindModule,DetailsModule,UsageModule};
const wchar_t* WetnessNames[]={L"—",L"Dry",L"Mostly Dry",L"Very Lightly Wet",L"Lightly Wet",L"Moderately Wet",L"Very Wet",L"Extremely Wet"};
struct Placement { int x=40,y=120,scale=100,opacity=90,rate=10; bool enabled=true, growLeft=false, growUp=false, rounded=false; };
enum FontId : unsigned char { Regular, Bold, Large, Small, Hero, SmallBold, FontCount };
struct DrawCommand {
    enum Type : unsigned char { Fill, Text, DottedLine, WindRing, Outline, RoundFill, Ring, Hatch, Sparkline, Logo, Map, MapCar };
    Type type=Fill; FontId font=Regular; UINT align=DT_LEFT; RECT r{}; COLORREF color=0;
    std::array<float,5> shape{}; // Wind ring: center x/y, radius, scale, bearing (negative without direction). Outline: corner radius, line width (1 when 0), dashed when 1. RoundFill: corner radius. Ring: stroke width. Hatch: background color. Sparkline: stroke width; its text holds the points, 1 + 0..1000 from bottom to top. Logo: atlas cell.
    std::wstring text;
    bool operator==(const DrawCommand& o) const {
        return type==o.type && font==o.font && align==o.align && r.left==o.r.left && r.top==o.r.top && r.right==o.r.right && r.bottom==o.r.bottom
            && color==o.color && shape==o.shape && text==o.text;
    }
};
struct FooterSettings { bool enabled=true; Slots metrics=DefaultFooter; };
// What one Relative or Standings overlay shows; each kind ignores the other kind's part.
struct TableSettings {
    std::vector<int> columnOrder;
    std::array<bool,DriverColumnCount> driverColumns{};
    std::array<bool,ExtraColumnCount> extraColumns{};
    std::array<bool,2> driverShortView{};
    std::array<bool,3> driverFlags{true,true,true};
    bool showDriver=true, showStatus=true, columnHeaders=false, classColors=true, brandLogo=true, brandName=true;
    bool gapSigns=false; // + / - before gaps (and intervals in the Standings); off by default: the row order already tells.
    int nameFormat=0;
    int rows=7, gapDecimals=1; bool hidePit=false, best=false, last=false, lapColors=true; // Relative.
    StandingsOptions standings; bool interval=false, laps=false, gained=false, progress=true, pitExit=false; // Standings.
};
// Every overlay uses the same selectable header/footer metrics.
struct Bands { bool header=false; Slots headerMetrics=DefaultHeader; FooterSettings footer; };
Bands DefaultBands(int kind) {
    if(kind==FuelKind) return {true,{OverlayNameMetric,FinalLapMetric,StopsNeededMetric,NoMetric},{false,DefaultFooter}};
    if(kind==WeatherKind) return {true,{OverlayNameMetric,DeclaredWetMetric,NoMetric,NoMetric},{false,DefaultFooter}};
    if(kind==PitLimiterKind) return {false,{OverlayNameMetric,NoMetric,NoMetric,NoMetric},{false,DefaultFooter}};
    if(kind==TrackMapKind) return {true,{TrackLayoutMetric,NoMetric,NoMetric,NoMetric},{false,DefaultFooter}};
    return {};
}
struct FuelSettings { std::array<bool,FuelModuleCount> display{}; std::array<bool,FuelMetricCount> figures{}; std::vector<int> order=FuelOrder; bool strip=false; };
struct WeatherSettings { std::array<bool,WeatherModuleCount> display{}; std::vector<int> order=WeatherOrder; bool strip=false; };
struct TrackMapSettings {
    bool numbers=true, ownClass=false, pit=true, details=false, background=true, pitLane=true, startFinish=true;
    bool cornerNumbers=false, cornerNames=false, labelContrast=true, pitExit=true;
    int labelScale=100;
    int layout=0; // The official map, a circle, or a line for wide monitors; circle and line need no SVG.
};
enum TrackMapLayout { MapLayout, CircleLayout, LineLayout };
constexpr int TrackMapWidth=440, TrackMapBodyHeight=264;
int TrackMapWidthOf(const TrackMapSettings& m) { return m.layout==CircleLayout?280:(m.layout==LineLayout?880:TrackMapWidth); }
// Body under the header, or the whole height without background; the line leaves a row for a note.
int TrackMapBody(const TrackMapSettings& m,bool mapOnly,bool note) {
    if(m.layout==LineLayout) return mapOnly?64:(note?68:52);
    return mapOnly?240:TrackMapBodyHeight;
}
constexpr const wchar_t* TrackMapPreviewName=L"Lime Rock Park · Grand Prix"; // The preview's map and sample cars.
constexpr COLORREF TrackMapTransparentKey=RGB(1,2,3); // Reserved, distinct from marker text and fills.
// One overlay window. A kind can have several instances; the first keeps the kind's plain name and settings section.
struct Overlay {
    HWND window=nullptr; int kind=0, number=1; Placement place;
    Bands bands; TableSettings table; FuelSettings fuel; WeatherSettings weather; TrackMapSettings map;
    HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ original=nullptr, originalFont=nullptr;
    HWND frame=nullptr; int frameState=-1; RECT frameRect{}; // The ring around it while edited or movable.
    std::array<HFONT,FontCount> fonts{}; int width=0,height=0,dpi=0;
    std::vector<DrawCommand> scene;
    ULONGLONG due=0; // Next scheduled refresh, in app.now time.
    struct Width { FontId font; std::wstring text; int width; };
    mutable std::vector<Width> widths; // Measured text widths for the current fonts.
    struct Logo { int index, size; HBITMAP bitmap; };
    std::vector<Logo> logos; // Car brand logos at this overlay's size, premultiplied for AlphaBlend.
    HBITMAP mapBitmap=nullptr; int mapWidth=0,mapHeight=0,mapStyle=-1; unsigned mapRevision=~0u;
    struct MapMarker { int width,height,stroke; COLORREF color; bool hollow; HBITMAP bitmap; const std::uint32_t* bits; };
    std::vector<MapMarker> mapMarkers;
    // The map area plus a margin, where car markers are blended in software before one copy back.
    HDC canvasDc=nullptr; HBITMAP canvas=nullptr; HGDIOBJ canvasOriginal=nullptr; std::uint32_t* canvasBits=nullptr;
    RECT canvasRect{}; bool canvasPending=false;
    // Rows for the current model version.
    mutable unsigned rowsVersion=~0u; mutable std::vector<Row> relativeRows; mutable std::vector<StandingsGroup> standingsGroups;
    void Free() {
        if(dc) { SelectObject(dc,originalFont); SelectObject(dc,original); DeleteObject(bitmap); DeleteDC(dc); }
        for(const auto& logo:logos) DeleteObject(logo.bitmap);
        logos.clear();
        if(mapBitmap) DeleteObject(mapBitmap);
        mapBitmap=nullptr; mapWidth=mapHeight=0; mapStyle=-1; mapRevision=~0u;
        for(const auto& marker:mapMarkers) DeleteObject(marker.bitmap);
        mapMarkers.clear();
        if(canvasDc) { SelectObject(canvasDc,canvasOriginal); DeleteDC(canvasDc); DeleteObject(canvas); }
        canvasDc=nullptr; canvas=nullptr; canvasBits=nullptr; canvasRect={}; canvasPending=false;
        for(HFONT& font:fonts) if(font) { DeleteObject(font); font=nullptr; }
        dc=nullptr; bitmap=nullptr; width=height=dpi=0; scene.clear(); widths.clear();
    }
};
// Global shortcuts, packed like the hotkey control: low byte virtual key, high byte HOTKEYF_* modifiers; 0 disables.
constexpr int HotkeyCount=3;
const wchar_t* HotkeyKeys[]={L"Move",L"Hide",L"Settings"};
constexpr WORD HotkeyAllModifiers=(HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT)<<8;
constexpr WORD DefaultHotkeys[]={HotkeyAllModifiers|'E',HotkeyAllModifiers|'H',HotkeyAllModifiers|'S'};
struct App {
    HINSTANCE instance=nullptr; HWND main=nullptr; HBRUSH brush=nullptr;
    RECT windowRect{}; bool windowMaximized=false;
    std::list<Overlay> overlays; // Grouped by kind, in instance order; each stays at its address for its window.
    Frame frame; Session session; Telemetry telemetry; FuelCalculator fuel;
    EventRecorder events; PitHistory pitHistory; PitLimiterVisibility pitVisibility;
    RacePredictor predictor; FuelPlan fuelPlan;
    std::wstring config;
    unsigned classBestVersion=~0u; std::vector<std::pair<int,double>> classBest;
    WeatherHistory weatherHistory;
    bool preview=false, edit=false, hidden=false, diagnostics=false;
    bool startWithWindows=false, startMinimized=false, showTitleBar=false, checkUpdates=true;
    bool settingsError=false, trayAvailable=false;
    // Values Save() last wrote, keyed "section\nkey"; Save() only writes what differs.
    std::map<std::wstring,std::wstring> savedSettings; unsigned settingsWrites=0;
    std::array<WORD,HotkeyCount> hotkeys{}; bool hotkeysEnabled=false; std::wstring hotkeyError;
    int rate=10,timerMs=0; ULONGLONG now=0;
    int opponentFuelExtraLaps=3;
    // Pit stop estimate: lane loss learned per track or fixed by the user, service times per car (CarPath).
    PitLaneLoss pitLane; RefuelRate refuel;
    struct { int track=-1; std::wstring car; double fixedLane=-1, tires=-1, rate=-1; int samplesSaved=0; } pitTimes;
    unsigned pitExitVersion=~0u; PitRejoin pitExit; bool pitExitPartial=false;
    unsigned modelVersion=0,planVersion=~0u,ratingVersion=~0u;
    std::array<int,MaxCars> ratingChanges{};
    unsigned opponentFuelVersion=~0u; int opponentFuelExtra=-1; bool opponentFuelPreview=false;
    std::array<OpponentFuelStatus,MaxCars> opponentFuelStatuses{};
    std::vector<DrawCommand> sceneBuffer; bool overlayClass=false;
    NOTIFYICONDATAW tray{};
} app;
std::wstring Numbered(const wchar_t* name,int number) { return number>1?std::wstring(name)+L" "+std::to_wstring(number):std::wstring(name); }
std::wstring Name(const Overlay& o) { return Numbered(Titles[o.kind],o.number); }
std::wstring Section(const Overlay& o) { return Numbered(Sections[o.kind],o.number); }
// The first instance of every kind always exists; Race Control follows the first Standings.
Overlay& First(int kind) { return *std::find_if(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return o.kind==kind && o.number==1;}); }
int InstanceCount(int kind) { return static_cast<int>(std::count_if(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return o.kind==kind;})); }
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
double ReadNumber(const wchar_t* section,const std::wstring& key) {
    wchar_t text[32]{}; GetPrivateProfileStringW(section,key.c_str(),L"",text,32,app.config.c_str());
    wchar_t* end=nullptr; const double value=wcstod(text,&end);
    return end!=text && std::isfinite(value) && value>0?value:-1;
}
std::wstring Format(double x,int decimals=1) {
    if(!std::isfinite(x) || x<0) return L"—";
    wchar_t b[64]; swprintf_s(b,L"%.*f",decimals,x); return b;
}
std::wstring Lap(double x) {
    if(x<=0 || !std::isfinite(x)) return L"—";
    wchar_t b[64]; swprintf_s(b,L"%d:%06.3f",static_cast<int>(x)/60,std::fmod(x,60)); return b;
}
const wchar_t* ColumnTitle(int id) { return Columns[id].title; }
bool ColumnVisible(const Overlay& o,int id) {
    const auto& t=o.table; const bool standings=o.kind==StandingsKind;
    switch(id) {
    case DriverColumn: return t.showDriver;
    case StatusColumn: return false; // Drawn as the badge after the name; showStatus switches it.
    case GainedColumn: return standings && t.gained;
    case LapsColumn: return standings && t.laps;
    case IntervalColumn: return t.interval;
    // Always shown in the Standings, optional in the Relative.
    case BestColumn: return standings || t.best;
    case LastColumn: return standings || t.last;
    default:
        if(id>=BrandColumn) return t.extraColumns[id-BrandColumn];
        return id>=LicenseColumn?t.driverColumns[id-LicenseColumn]:true;
    }
}
// Without column headings, columns shrink to fit their values.
int ColumnWidth(const Overlay& o,int id) {
    const auto& t=o.table; const bool compact=!t.columnHeaders;
    if(id==BrandColumn && !t.brandName) return compact?30:44;
    if(id==BrandColumn && !t.brandLogo) return compact?92:100;
    if(compact && id==DriverColumn && t.nameFormat!=0) return 150;
    if(compact && (id==LicenseColumn || id==IRatingColumn) && t.driverShortView[id-LicenseColumn]) return 50;
    return compact?Columns[id].compactWidth:Columns[id].width;
}
std::vector<int> VisibleColumns(const Overlay& o) { std::vector<int> result; for(int id:o.table.columnOrder) if(ColumnVisible(o,id)) result.push_back(id); return result; }
int FuelFinishLap() {
    const auto& f=app.frame; const double distance=app.fuelPlan.lapsToFinish;
    return f.lapCompleted>=0 && std::isfinite(distance) && distance>=0?std::max(f.lapCompleted+1,f.lapCompleted+static_cast<int>(std::ceil(f.lapPct+distance-1e-6))):-1;
}
int StintCellCount() {
    const double average=app.fuelPlan.estimates[FuelBasis].consumption,capacity=app.session.fuelCapacity;
    return std::isfinite(average) && average>0 && std::isfinite(capacity) && capacity>0?static_cast<int>(std::clamp(std::ceil(capacity/average),1.0,10000.0)):0;
}
// Cells in the weather strip: condition, rain, both temperatures and wind (two cells wide).
int WeatherStripCount(const Overlay& o) { const auto& d=o.weather.display; return d[ConditionModule]+d[RainModule]+2*d[TemperatureModule]+2*d[WindModule]; }
constexpr int WeatherPanelWidth=360, WeatherStripCell=112, WindRoseWidth=124;
// Only the wind rose: the overlay shrinks to the ring, small enough to sit in the middle of the screen.
bool WindRoseOnly(const Overlay& o) {
    const auto& d=o.weather.display;
    return d[WindModule] && d[WindRose] && !d[WindText] && std::count(d.begin(),d.begin()+WeatherPanelCount,true)==1 && !o.bands.header && !o.bands.footer.enabled;
}
int WeatherHeight(const Overlay& o) {
    if(o.weather.strip) return 50;
    const auto& d=o.weather.display; constexpr int heights[]={84,36,62,76,0,32,32};
    int h=0; for(int i=0;i<WeatherPanelCount;++i) if(d[i]) h+=heights[i];
    if(d[WindModule]) h+=d[WindRose]?108:(d[WindText]?76:0);
    return std::max(h,40);
}
// Values in the strip layout, including the two independent overall-leader forecasts.
int FuelStripCount(const Overlay& o) { const auto& d=o.fuel.display; return 2*d[TankModule]+d[FiguresModule]+d[RefuelCard]+d[WindowCard]+d[TargetRow]+d[OneLongerRow]+d[LeaderDistanceModule]+d[WhiteFlagTimeModule]+d[PitExitModule]; }
constexpr int FuelPanelWidth=440, FuelStripCell=86;
// Whether the strip layout shows a module or uses an option; the page marks the others "Panel only".
bool InStrip(int kind,int id) {
    if(kind==FuelKind) return id==TankModule || id==FiguresModule || id==RefuelCard || id==WindowCard || id==TargetRow || id==OneLongerRow || id==LeaderDistanceModule || id==WhiteFlagTimeModule || id==PitExitModule;
    return id==ConditionModule || id==RainModule || id==TemperatureModule || id==WindModule || id==TrendArrows || id==WindRelative;
}
// The panel's modules that show, in their order. Refuel and pit window share a row when
// adjacent here; adjacent target rows share one band.
std::vector<int> FuelShown(const Overlay& o) {
    std::vector<int> shown;
    for(int id:o.fuel.order) if(o.fuel.display[id] && (id!=FiguresModule || std::count(o.fuel.figures.begin(),o.fuel.figures.end(),true))) shown.push_back(id);
    return shown;
}
bool IsCard(int id) { return id==RefuelCard || id==WindowCard; }
bool IsTarget(int id) { return id>=OneShorterRow && id<=TwoLongerRow; }
int FuelHeight(const Overlay& o) {
    if(o.fuel.strip) return 50;
    constexpr int heights[]={112,52,104,0,92,92,46,24,24,24,24,32,32,32};
    static_assert(std::size(heights)==FuelModuleCount);
    const auto shown=FuelShown(o); int h=0;
    for(size_t k=0;k<shown.size();++k) {
        const int id=shown[k], next=k+1<shown.size()?shown[k+1]:-1;
        h+=heights[id];
        if(IsTarget(id)) h+=8*(!k || !IsTarget(shown[k-1]))+8*!IsTarget(next);
        if(IsCard(id) && IsCard(next)) ++k;
    }
    return std::max(h,40);
}
int OverlayWidth(const Overlay& o) {
    if(o.kind==TrackMapKind) return TrackMapWidthOf(o.map);
    if(IsTable(o.kind)) { int width=16; for(int id:VisibleColumns(o)) width+=ColumnWidth(o,id); return std::max(120,width); }
    if(o.kind==WeatherKind) return o.weather.strip?std::max(1,WeatherStripCount(o))*WeatherStripCell:(WindRoseOnly(o)?WindRoseWidth:WeatherPanelWidth);
    if(o.kind==FuelKind) return o.fuel.strip?std::max(1,FuelStripCount(o))*FuelStripCell:FuelPanelWidth;
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
// All overlay kinds draw the same configurable metric bands.
bool MapOnly(const Overlay& o) { return o.kind==TrackMapKind && !o.map.background; }
bool ShowOverlayHeader(const Overlay& o) { return o.bands.header && !MapOnly(o); }
int FooterHeight(const Overlay& o) { return o.bands.footer.enabled && !MapOnly(o)?28:0; }
double SessionProgress();
// The Standings' session progress bar, a row of its own under the header while the session has a known length.
int ProgressHeight(const Overlay& o) { return o.kind==StandingsKind && o.table.progress && SessionProgress()>=0?24:0; }
int TableRowsTop(const Overlay& o) { return 32+ProgressHeight(o)+(o.table.columnHeaders?27:0); }
std::wstring DriverName(const Row& row,const TableSettings& t) {
    auto name=Wide(row.driver.name); const auto first=name.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return L"—"; name=name.substr(first,name.find_last_not_of(L" \t\r\n")-first+1);
    if(t.nameFormat==0) return name;
    const auto split=name.find_first_of(L" \t"); if(split==std::wstring::npos) return name;
    const auto last=name.find_first_not_of(L" \t",split); if(last==std::wstring::npos) return name;
    const size_t initial=(name[0]>=0xd800 && name[0]<=0xdbff && name.size()>1 && name[1]>=0xdc00 && name[1]<=0xdfff)?2:1;
    return name.substr(0,initial)+(t.nameFormat==1?L". ":L" ")+name.substr(last);
}
std::wstring OverlayStatus(const Row& row,const TableSettings& t) {
    if(row.car.pit) return L"PIT";
    if(row.car.towing) return L"TOW";
    if(t.driverFlags[0] && (row.car.sessionFlags&SessionFlag::Disqualify)) return L"DQ";
    if(t.driverFlags[0] && (row.car.sessionFlags&SessionFlag::Black)) return L"BLACK";
    if(t.driverFlags[2] && (row.car.sessionFlags&SessionFlag::Repair)) return L"REPAIR";
    if(t.driverFlags[1] && (row.car.sessionFlags&SessionFlag::Furled)) return L"SLOW";
    return row.car.outLap?L"OUT":L"";
}
// driverColumn counts from LicenseColumn; a table's settings select its short views, none gives full values.
std::wstring DriverValue(const Row& row,int driverColumn,const TableSettings* t=nullptr) {
    const int id=LicenseColumn+driverColumn;
    const bool shortView=t && (id==LicenseColumn || id==IRatingColumn) && t->driverShortView[driverColumn];
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
    Slots slots{}; for(int i=0;i<SlotCount;++i) slots[i]=std::clamp(ReadInt(section,keys[i],defaults[i]),0,MetricCount-1); return slots;
}
int NormalizeScale(int scale) { return ((std::clamp(scale,50,150)+5)/10)*10; }
void ReadTable(const wchar_t* section,int kind,TableSettings& t) {
    t.columnOrder=ReadOrder(section,L"ColumnOrder",DefaultColumns[kind]);
    for(int i=0;i<DriverColumnCount;++i) t.driverColumns[i]=ReadBool(section,DriverColumnKeys[i],false);
    t.driverShortView={ReadBool(section,L"ShortLicenseView",false),ReadBool(section,L"ShortIRatingView",false)};
    for(int i=0;i<ExtraColumnCount;++i) t.extraColumns[i]=ReadBool(section,ExtraColumnKeys[i],false);
    t.classColors=ReadBool(section,L"ShowClassColor",true);
    t.brandLogo=ReadBool(section,L"ShowCarLogo",true); t.brandName=ReadBool(section,L"ShowCarName",true);
    t.gapSigns=ReadBool(section,L"ShowGapSigns",false);
    t.driverFlags={ReadBool(section,L"ShowBlackFlag",true),ReadBool(section,L"ShowSlowdown",true),ReadBool(section,L"ShowRequiredRepairs",true)};
    t.showDriver=ReadBool(section,L"ShowDriver",true);
    t.showStatus=ReadBool(section,L"ShowStatus",true);
    t.nameFormat=std::clamp(ReadInt(section,L"DriverNameFormat",0),0,2);
    t.columnHeaders=ReadBool(section,L"ShowColumnHeaders",false);
    t.interval=ReadBool(section,L"ShowInterval",false);
    if(kind==RelativeKind) {
        t.rows=RelativeRowCount(ReadInt(section,L"Rows",7));
        t.gapDecimals=std::clamp(ReadInt(section,L"GapDecimals",1),0,3);
        t.hidePit=ReadBool(section,L"HideDriversInPit",false);
        t.lapColors=ReadBool(section,L"ShowLapColors",true);
        t.best=ReadBool(section,L"ShowBestLap",false); t.last=ReadBool(section,L"ShowLastLap",false);
    } else {
        auto& standings=t.standings;
        standings.separateClasses=ReadBool(section,L"SeparateClasses",true);
        standings.ownClass=ReadBool(section,L"OwnClass",false);
        standings.liveGaps=ReadBool(section,L"LiveGaps",false);
        standings.fallbackByRating=ReadBool(section,L"FallbackByIRating",false);
        standings.leaders=std::clamp(ReadInt(section,L"Leaders",3),0,MaxCars);
        standings.ahead=std::clamp(ReadInt(section,L"Ahead",2),0,MaxCars);
        standings.behind=std::clamp(ReadInt(section,L"Behind",2),0,MaxCars);
        t.progress=ReadBool(section,L"ShowProgress",true); t.pitExit=ReadBool(section,L"ShowPitExitRow",false);
        t.laps=ReadBool(section,L"ShowLaps",false);
        t.gained=ReadBool(section,L"ShowPositionsGained",false);
    }
}
void CreateOverlayWindow(Overlay& o);
void DestroyOverlay(Overlay& o) { if(o.window) DestroyWindow(o.window); o.window=nullptr; }
// Adds instance number of a kind after the kind's other instances, without settings or window yet.
Overlay& AddInstance(int kind,int number) {
    auto next=std::find_if(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return o.kind>kind || (o.kind==kind && o.number>number);});
    auto& o=*app.overlays.emplace(next); o.kind=kind; o.number=number;
    return o;
}
void ReadInstance(Overlay& o) {
    const auto section=Section(o); const int kind=o.kind;
    auto& p=o.place; const int offset=(o.number-1)*30; // Later instances start a little below and to the right.
    p.x=ReadInt(section.c_str(),L"X",DefaultPositions[kind].x+offset); p.y=ReadInt(section.c_str(),L"Y",DefaultPositions[kind].y+offset);
    p.scale=NormalizeScale(ReadInt(section.c_str(),L"Scale",100)); p.opacity=std::clamp(ReadInt(section.c_str(),L"Opacity",90),40,100);
    const int rate=ReadInt(section.c_str(),L"RefreshHz",DefaultRates[kind]);
    p.rate=*std::find_if(std::begin(OverlayRates),std::end(OverlayRates)-1,[&](int choice){return choice>=rate;});
    p.enabled=ReadBool(section.c_str(),L"Enabled",kind!=TrackMapKind); p.growLeft=ReadBool(section.c_str(),L"GrowLeft",false); p.growUp=ReadBool(section.c_str(),L"GrowUp",false);
    p.rounded=ReadBool(section.c_str(),L"RoundedCorners",false);
    const Bands defaults=DefaultBands(kind); auto& b=o.bands;
    b.header=ReadBool(section.c_str(),L"ShowHeader",kind==PitLimiterKind?ReadBool(L"General",L"ShowTitleBar",false):defaults.header); b.headerMetrics=ReadSlots(section.c_str(),HeaderKeys,defaults.headerMetrics);
    b.footer.enabled=ReadBool(section.c_str(),L"ShowFooter",defaults.footer.enabled); b.footer.metrics=ReadSlots(section.c_str(),FooterKeys,defaults.footer.metrics);
    if(IsTable(kind)) ReadTable(section.c_str(),kind,o.table);
    for(int i=0;i<FuelModuleCount;++i) o.fuel.display[i]=ReadBool(section.c_str(),FuelModules[i].key,FuelModules[i].on);
    for(int i=0;i<FuelMetricCount;++i) o.fuel.figures[i]=ReadBool(section.c_str(),FuelFigures[i].key,FuelFigures[i].on);
    o.fuel.strip=ReadInt(section.c_str(),L"Layout",0)==1;
    if(kind==FuelKind) o.fuel.order=ReadOrder(section.c_str(),L"ModuleOrder",FuelOrder);
    if(kind==WeatherKind) o.weather.order=ReadOrder(section.c_str(),L"ModuleOrder",WeatherOrder);
    for(int i=0;i<WeatherModuleCount;++i) o.weather.display[i]=ReadBool(section.c_str(),WeatherModules[i].key,WeatherModules[i].on);
    o.weather.strip=o.fuel.strip;
    if(kind==TrackMapKind) {
        o.map.numbers=ReadBool(section.c_str(),L"ShowCarNumbers",true); o.map.ownClass=ReadBool(section.c_str(),L"OwnClass",false);
        o.map.pit=ReadBool(section.c_str(),L"ShowPitCars",true); o.map.details=ReadBool(section.c_str(),L"ShowMapDetails",false);
        o.map.background=ReadBool(section.c_str(),L"ShowBackground",true); o.map.pitLane=ReadBool(section.c_str(),L"ShowPitLane",true);
        o.map.startFinish=ReadBool(section.c_str(),L"ShowStartFinish",true);
        o.map.cornerNumbers=ReadBool(section.c_str(),L"ShowCornerNumbers",false); o.map.cornerNames=ReadBool(section.c_str(),L"ShowCornerNames",false);
        o.map.labelContrast=ReadBool(section.c_str(),L"CornerLabelContrast",true); o.map.pitExit=ReadBool(section.c_str(),L"ShowPitExit",true);
        o.map.layout=std::clamp(ReadInt(section.c_str(),L"MapLayout",MapLayout),0,static_cast<int>(LineLayout));
        o.map.labelScale=((std::clamp(ReadInt(section.c_str(),L"CornerLabelScale",100),75,200)+12)/25)*25;
    }
}
void Load() {
    app.savedSettings.clear(); // The next Save() writes everything once.
    // Each kind's section counts its instances; the Pit Limiter has only one.
    for(int kind=0;kind<OverlayCount;++kind) {
        const int count=kind==PitLimiterKind?1:std::clamp(ReadInt(Sections[kind],L"Instances",1),1,99);
        for(int number=1;number<=count;++number)
            if(std::none_of(app.overlays.begin(),app.overlays.end(),[&](const Overlay& o){return o.kind==kind && o.number==number;})) AddInstance(kind,number);
        for(auto it=app.overlays.begin();it!=app.overlays.end();) {
            if(it->kind==kind && it->number>count) { DestroyOverlay(*it); it=app.overlays.erase(it); } else ++it;
        }
    }
    for(auto& o:app.overlays) { ReadInstance(o); if(!o.window && app.overlayClass) CreateOverlayWindow(o); }
    app.showTitleBar=ReadBool(L"General",L"ShowTitleBar",false);
    app.checkUpdates=ReadBool(L"General",L"CheckForUpdates",true);
    app.rate=std::clamp(ReadInt(L"General",L"RefreshHz",10),5,20);
    app.opponentFuelExtraLaps=std::clamp(ReadInt(L"General",L"OpponentFuelExtraLaps",3),0,10);
    app.startWithWindows=ReadBool(L"General",L"StartWithWindows",false);
    app.startMinimized=ReadBool(L"General",L"StartMinimized",false);
    app.windowRect={ReadInt(L"General",L"WindowLeft",0),ReadInt(L"General",L"WindowTop",0),ReadInt(L"General",L"WindowRight",0),ReadInt(L"General",L"WindowBottom",0)};
    app.windowMaximized=ReadBool(L"General",L"WindowMaximized",false);
    for(int i=0;i<HotkeyCount;++i) { const WORD hotkey=static_cast<WORD>(ReadInt(L"Hotkeys",HotkeyKeys[i],DefaultHotkeys[i])); app.hotkeys[i]=LOBYTE(hotkey)?hotkey:0; }
}
// Each WritePrivateProfileString call rewrites the file; all keys take about 100 ms, so
// only keys whose value changed since the last write go out. A missing file gets all.
void Save() {
    bool success=true;
    if(GetFileAttributesW(app.config.c_str())==INVALID_FILE_ATTRIBUTES) app.savedSettings.clear();
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value) {
        const auto id=std::wstring(section)+L"\n"+key;
        if(const auto saved=app.savedSettings.find(id); saved!=app.savedSettings.end() && saved->second==value) return;
        ++app.settingsWrites;
        if(WritePrivateProfileStringW(section,key,value.c_str(),app.config.c_str())) app.savedSettings[id]=value;
        else success=false;
    };
    auto number=[&](const wchar_t* section,const wchar_t* key,int value) { write(section,key,std::to_wstring(value)); };
    auto order=[&](const wchar_t* section,const wchar_t* key,const std::vector<int>& ids) {
        std::wstring text; for(int id:ids) { if(!text.empty()) text+=L","; text+=std::to_wstring(id); } write(section,key,text);
    };
    for(int kind=0;kind<OverlayCount;++kind) if(kind!=PitLimiterKind) number(Sections[kind],L"Instances",InstanceCount(kind));
    for(const auto& o:app.overlays) {
        const auto name=Section(o); const auto* section=name.c_str(); const auto& p=o.place;
        number(section,L"X",p.x); number(section,L"Y",p.y); number(section,L"Scale",p.scale); number(section,L"Opacity",p.opacity);
        number(section,L"RefreshHz",p.rate); number(section,L"Enabled",p.enabled); number(section,L"GrowLeft",p.growLeft); number(section,L"GrowUp",p.growUp); number(section,L"RoundedCorners",p.rounded);
        {
            const auto& b=o.bands; number(section,L"ShowHeader",b.header); number(section,L"ShowFooter",b.footer.enabled);
            for(int i=0;i<SlotCount;++i) { number(section,HeaderKeys[i],b.headerMetrics[i]); number(section,FooterKeys[i],b.footer.metrics[i]); }
        }
        if(IsTable(o.kind)) {
            const auto& t=o.table;
            order(section,L"ColumnOrder",t.columnOrder);
            for(int i=0;i<DriverColumnCount;++i) number(section,DriverColumnKeys[i],t.driverColumns[i]);
            number(section,L"ShortLicenseView",t.driverShortView[0]); number(section,L"ShortIRatingView",t.driverShortView[1]);
            for(int i=0;i<ExtraColumnCount;++i) number(section,ExtraColumnKeys[i],t.extraColumns[i]);
            number(section,L"ShowClassColor",t.classColors); number(section,L"ShowCarLogo",t.brandLogo); number(section,L"ShowCarName",t.brandName);
            number(section,L"ShowBlackFlag",t.driverFlags[0]); number(section,L"ShowSlowdown",t.driverFlags[1]); number(section,L"ShowRequiredRepairs",t.driverFlags[2]);
            number(section,L"ShowDriver",t.showDriver); number(section,L"ShowStatus",t.showStatus); number(section,L"DriverNameFormat",t.nameFormat);
            number(section,L"ShowColumnHeaders",t.columnHeaders); number(section,L"ShowGapSigns",t.gapSigns);
            number(section,L"ShowInterval",t.interval);
            if(o.kind==RelativeKind) {
                number(section,L"Rows",t.rows); number(section,L"GapDecimals",t.gapDecimals); number(section,L"HideDriversInPit",t.hidePit);
                number(section,L"ShowLapColors",t.lapColors); number(section,L"ShowBestLap",t.best); number(section,L"ShowLastLap",t.last);
            } else {
                const auto& standings=t.standings;
                number(section,L"SeparateClasses",standings.separateClasses); number(section,L"OwnClass",standings.ownClass);
                number(section,L"LiveGaps",standings.liveGaps); number(section,L"FallbackByIRating",standings.fallbackByRating);
                number(section,L"Leaders",standings.leaders); number(section,L"Ahead",standings.ahead); number(section,L"Behind",standings.behind);
                number(section,L"ShowProgress",t.progress); number(section,L"ShowPitExitRow",t.pitExit); number(section,L"ShowLaps",t.laps); number(section,L"ShowPositionsGained",t.gained);
            }
        }
        if(o.kind==FuelKind) {
            for(int i=0;i<FuelModuleCount;++i) number(section,FuelModules[i].key,o.fuel.display[i]);
            for(int i=0;i<FuelMetricCount;++i) number(section,FuelFigures[i].key,o.fuel.figures[i]);
            number(section,L"Layout",o.fuel.strip); order(section,L"ModuleOrder",o.fuel.order);
        }
        if(o.kind==WeatherKind) {
            for(int i=0;i<WeatherModuleCount;++i) number(section,WeatherModules[i].key,o.weather.display[i]);
            number(section,L"Layout",o.weather.strip); order(section,L"ModuleOrder",o.weather.order);
        }
        if(o.kind==TrackMapKind) {
            number(section,L"ShowCarNumbers",o.map.numbers); number(section,L"OwnClass",o.map.ownClass);
            number(section,L"ShowPitCars",o.map.pit); number(section,L"ShowMapDetails",o.map.details);
            number(section,L"ShowBackground",o.map.background); number(section,L"ShowPitLane",o.map.pitLane); number(section,L"ShowStartFinish",o.map.startFinish);
            number(section,L"ShowCornerNumbers",o.map.cornerNumbers); number(section,L"ShowCornerNames",o.map.cornerNames);
            number(section,L"CornerLabelContrast",o.map.labelContrast); number(section,L"CornerLabelScale",o.map.labelScale); number(section,L"ShowPitExit",o.map.pitExit); number(section,L"MapLayout",o.map.layout);
        }
    }
    number(L"General",L"ShowTitleBar",app.showTitleBar); number(L"General",L"RefreshHz",app.rate);
    number(L"General",L"OpponentFuelExtraLaps",app.opponentFuelExtraLaps);
    number(L"General",L"StartWithWindows",app.startWithWindows); number(L"General",L"StartMinimized",app.startMinimized); number(L"General",L"CheckForUpdates",app.checkUpdates);
    if(app.windowRect.right>app.windowRect.left) {
        number(L"General",L"WindowLeft",app.windowRect.left); number(L"General",L"WindowTop",app.windowRect.top);
        number(L"General",L"WindowRight",app.windowRect.right); number(L"General",L"WindowBottom",app.windowRect.bottom);
        number(L"General",L"WindowMaximized",app.windowMaximized);
    }
    for(int i=0;i<HotkeyCount;++i) number(L"Hotkeys",HotkeyKeys[i],app.hotkeys[i]);
    if(!success && !app.settingsError) { app.settingsError=true; MessageBoxW(app.main,L"Could not save settings. Please check write access to the RaceMate configuration folder.",L"RaceMate",MB_OK|MB_ICONWARNING); }
}
// Removes an added instance with its settings section; the first instance of a kind stays.
void RemoveInstance(Overlay& o) {
    if(o.number==1) return;
    const auto section=Section(o);
    WritePrivateProfileStringW(section.c_str(),nullptr,nullptr,app.config.c_str());
    for(auto it=app.savedSettings.begin();it!=app.savedSettings.end();) it=it->first.rfind(section+L"\n",0)==0?app.savedSettings.erase(it):std::next(it);
    // Later instances move up a number, so their sections are written again under the new names.
    const int kind=o.kind, number=o.number;
    DestroyOverlay(o); app.overlays.remove_if([&](const Overlay& other){return &other==&o;});
    for(auto& other:app.overlays) if(other.kind==kind && other.number>number) {
        const auto old=Section(other);
        WritePrivateProfileStringW(old.c_str(),nullptr,nullptr,app.config.c_str());
        for(auto it=app.savedSettings.begin();it!=app.savedSettings.end();) it=it->first.rfind(old+L"\n",0)==0?app.savedSettings.erase(it):std::next(it);
        --other.number;
    }
    Save();
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
    const double burns[]={3.09,3.12,3.10,3.21,2.98,3.11,3.13}; const int counts[]={1,10,5,12,12,12,3};
    for(int i=0;i<FuelMetricCount;++i) p.estimates[i]={burns[i],std::max(0.0,finish.ownDistance*burns[i]-app.frame.fuel),counts[i]};
    // Whole laps so far, one of them under caution.
    const double laps[]={3.06,3.11,3.02,3.09,3.10,3.12,3.04,3.12,3.07,3.09};
    p.historyCount=std::clamp(app.frame.lapCompleted,0,10);
    for(int i=0;i<p.historyCount;++i) { const bool caution=i==p.historyCount-3; p.history[i]={app.frame.lapCompleted-p.historyCount+1+i,laps[10-p.historyCount+i]-(caution?.7:0),!caution,caution}; }
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
OpponentFuelStatus FuelFinishStatus(int index) {
    if(index<0 || index>=MaxCars) return OpponentFuelStatus::None;
    if(app.opponentFuelVersion!=app.modelVersion || app.opponentFuelExtra!=app.opponentFuelExtraLaps || app.opponentFuelPreview!=app.preview) {
        app.opponentFuelStatuses.fill(OpponentFuelStatus::None);
        if(app.preview) { app.opponentFuelStatuses[0]=OpponentFuelStatus::End; app.opponentFuelStatuses[4]=OpponentFuelStatus::Save; }
        else {
            const auto& plan=Plan();
            for(const auto& driver:app.session.drivers) if(driver.index>=0 && driver.index<MaxCars)
                app.opponentFuelStatuses[driver.index]=OpponentFuel(app.session,app.frame,plan,driver.index,app.opponentFuelExtraLaps);
        }
        app.opponentFuelVersion=app.modelVersion; app.opponentFuelExtra=app.opponentFuelExtraLaps; app.opponentFuelPreview=app.preview;
    }
    return app.opponentFuelStatuses[index];
}
const std::vector<Row>& RelativeRows(const Overlay& o) {
    if(o.rowsVersion!=app.modelVersion) { o.relativeRows=Relative(app.session,app.frame,o.table.rows,false,o.table.hidePit); o.rowsVersion=app.modelVersion; }
    return o.relativeRows;
}
const PitRejoin& PitExit();
const std::vector<StandingsGroup>& StandingsGroups(const Overlay& o) {
    if(o.rowsVersion!=app.modelVersion) {
        o.standingsGroups=SelectStandings(app.session,app.frame,o.table.standings); o.rowsVersion=app.modelVersion;
        // The pit exit as a row of its own, before the first car that would then be behind.
        const auto& rejoin=PitExit();
        if(o.table.pitExit && rejoin.valid) for(auto& group:o.standingsGroups) {
            const auto own=std::find_if(group.rows.begin(),group.rows.end(),[](const Row& r){ return r.player; });
            if(own==group.rows.end()) continue;
            Row ghost=*own; ghost.player=false; ghost.pitExit=true; ghost.car.position=rejoin.position; ghost.car.classPosition=rejoin.classPosition;
            const bool byClass=o.table.standings.separateClasses; const int position=byClass?rejoin.classPosition:rejoin.position;
            const auto at=std::find_if(group.rows.begin(),group.rows.end(),[&](const Row& r){ return !r.player && (byClass?r.car.classPosition:r.car.position)>=position; });
            group.rows.insert(at,ghost); break;
        }
    }
    return o.standingsGroups;
}
std::vector<Row> Rows(const Overlay& o) {
    if(o.kind==RelativeKind) return RelativeRows(o);
    std::vector<Row> rows;
    for(const auto& group:StandingsGroups(o)) rows.insert(rows.end(),group.rows.begin(),group.rows.end());
    return rows;
}
// Overlay palette. Translucent fills of the design are pre-blended over the panel, since the window has one alpha.
namespace Ink {
constexpr COLORREF Panel=RGB(12,15,19), Band=RGB(22,25,28), Line=RGB(27,29,33), Border=RGB(32,34,38), ClassBand=RGB(24,27,31), OwnRow=RGB(22,39,57), PitExitRow=RGB(16,27,38), Divider=RGB(64,67,71);
constexpr COLORREF White=RGB(232,237,242), Muted=RGB(154,166,178), Label=RGB(135,148,161), Accent=RGB(77,163,255);
constexpr COLORREF Yellow=RGB(232,184,58), Orange=RGB(255,176,103), Red=RGB(255,122,107), Green=RGB(61,220,132), Purple=RGB(192,132,252), Wet=RGB(111,182,255);
constexpr COLORREF LapsAhead=RGB(255,138,125), LapsBehind=RGB(127,191,255);
constexpr COLORREF Value=RGB(195,204,213), Frame=RGB(46,49,53), IconBack=RGB(36,38,42), NeutralBadge=RGB(37,40,44), Brand=RGB(170,181,192);
constexpr COLORREF BarBack=RGB(29,31,35), HatchFore=RGB(131,96,66), HatchBack=RGB(52,45,42), HatchGrey=RGB(78,82,88), Missing=RGB(158,111,69);
constexpr COLORREF ChartBar=RGB(80,82,85), CautionBar=RGB(122,99,38), PitBar=RGB(40,43,47), WindowOpen=RGB(46,160,92), LapAhead=RGB(34,37,40), LapDone=RGB(52,55,60);
}
// A translucent ink pre-blended over a background.
COLORREF Mix(COLORREF base,COLORREF ink,double alpha) {
    auto channel=[&](int b,int i){ return static_cast<int>(std::lround(b+(i-b)*alpha)); };
    return RGB(channel(GetRValue(base),GetRValue(ink)),channel(GetGValue(base),GetGValue(ink)),channel(GetBValue(base),GetBValue(ink)));
}
// The pit window at the 10-lap average and the own pace, as the Fuel Calculator and the Stops Needed metric show it.
// Writes a positive value, or removes the key.
void WriteNumber(const wchar_t* section,const std::wstring& key,double value) {
    const auto text=Format(value,2); WritePrivateProfileStringW(section,key.c_str(),value>0?text.c_str():nullptr,app.config.c_str());
}
const Driver* PlayerDriver() {
    const auto it=std::find_if(app.session.drivers.begin(),app.session.drivers.end(),[](const Driver& d){ return d.index==app.frame.player; });
    return it==app.session.drivers.end()?nullptr:&*it;
}
void LoadPitTimes() {
    auto& t=app.pitTimes; const auto track=std::to_wstring(app.session.trackId);
    if(app.session.trackId>=0 && app.pitLane.Track()!=app.session.trackId) { app.pitLane.Seed(app.session.trackId,ReadNumber(L"PitStop",L"Lane "+track)); t.samplesSaved=0; }
    if(t.track!=app.session.trackId) { t.track=app.session.trackId; t.fixedLane=t.track>=0?ReadNumber(L"PitStop",L"LaneFixed "+track):-1; }
    const auto* own=PlayerDriver(); const std::wstring car=own?Wide(own->carPath):L"";
    if(car!=t.car) { t.car=car; t.tires=car.empty()?-1:ReadNumber(L"PitStop",L"Tires "+car); t.rate=car.empty()?-1:ReadNumber(L"PitStop",L"FuelRate "+car); app.refuel.Reset(); }
}
// Learns from every frame; a new lane loss or a measured refuel rate is kept for the next session.
void UpdatePitStop() {
    LoadPitTimes();
    app.pitLane.Update(app.session,app.frame); app.refuel.Update(app.frame);
    auto& t=app.pitTimes;
    if(app.pitLane.Samples()!=t.samplesSaved && app.pitLane.Samples()>0 && t.track>=0) { t.samplesSaved=app.pitLane.Samples(); WriteNumber(L"PitStop",L"Lane "+std::to_wstring(t.track),app.pitLane.Loss()); }
    if(app.refuel.Rate()>0 && std::abs(app.refuel.Rate()-t.rate)>.01 && !t.car.empty()) { t.rate=app.refuel.Rate(); WriteNumber(L"PitStop",L"FuelRate "+t.car,t.rate); }
}
double PitLaneSeconds() { return app.pitTimes.fixedLane>0?app.pitTimes.fixedLane:app.pitLane.Loss(); }
// Where the own car rejoins if it boxes now. Service parts without a known time count as zero and mark it partial.
const PitRejoin& PitExit() {
    if(app.pitExitVersion==app.modelVersion) return app.pitExit;
    app.pitExitVersion=app.modelVersion; app.pitExit={}; app.pitExitPartial=false;
    const auto& f=app.frame;
    if(app.session.type!="Race" || f.pit || View(f)!=ViewMode::InCar) return app.pitExit;
    const double lane=app.preview?22:PitLaneSeconds();
    if(lane<=0) return app.pitExit;
    auto service=PlannedService(app.session,f); const auto& t=app.pitTimes;
    if(service.fuel>0 && t.rate<=0) { service.fuel=0; app.pitExitPartial=true; }
    if(service.tires>0 && t.tires<=0) { service.tires=0; app.pitExitPartial=true; }
    const double stop=app.preview?20:std::max(0.0,ServiceTime(service,t.rate,t.tires));
    double pace=Plan().prediction.ownPace; if(!(pace>0)) pace=f.lastLap>0?f.lastLap:f.bestLap;
    app.pitExit=PredictRejoin(app.session,f,lane+stop,pace);
    return app.pitExit;
}
std::wstring PitExitAhead() {
    const auto& r=PitExit(); if(!r.valid || r.ahead<0) return {};
    for(const auto& d:app.session.drivers) if(d.index==r.ahead) return Format(r.aheadGap,1)+L" s behind #"+Wide(d.number);
    return {};
}
PitWindow FuelWindow() {
    const auto& p=Plan(); const auto& f=app.frame; const auto& basis=p.estimates[FuelBasis];
    double pace=p.prediction.ownPace; if(pace<=0) pace=f.lastLap>0?f.lastLap:f.bestLap;
    return CalculatePitWindow(app.session,f,p.lapsToFinish,basis.samples?basis.consumption:-1,pace);
}
#include "Footer.inc"
#include "BrandLogos.inc"
int BrandLogo(const std::string& brand) {
    for(int i=0;i<static_cast<int>(std::size(BrandLogoNames));++i) if(brand==BrandLogoNames[i]) return i;
    return -1;
}
int HeightWithTitle(const Overlay& o) {
    if(o.kind==TrackMapKind) return 32+TrackMapBody(o.map,MapOnly(o),o.map.details)+FooterHeight(o);
    const auto& t=o.table;
    if(o.kind==RelativeKind) return TableRowsTop(o)+t.rows*27+FooterHeight(o);
    if(o.kind==StandingsKind) {
        int height=TableRowsTop(o)+FooterHeight(o),count=0;
        for(const auto& group:StandingsGroups(o)) {
            height+=static_cast<int>(group.rows.size())*27+(t.standings.separateClasses?25:0); count+=static_cast<int>(group.rows.size());
        }
        return height+(count==0?54:0);
    }
    if(o.kind==FuelKind) return 32+FuelHeight(o)+FooterHeight(o);
    return o.kind==WeatherKind?32+WeatherHeight(o)+FooterHeight(o):90+FooterHeight(o);
}
int BaseHeight(const Overlay& o) { return HeightWithTitle(o)-(ShowOverlayHeader(o)?0:32); }
std::wstring Gap(const Row& r,const Overlay& o) {
    const auto& t=o.table;
    if(o.kind==RelativeKind) {
        if(r.player) return Format(0,t.gapDecimals);
        if(!std::isfinite(r.gap)) return L"—";
        const std::wstring sign=t.gapSigns?(r.gap>0?L"+":L"−"):L"";
        return sign+Format(std::abs(r.gap),t.gapDecimals);
    }
    const std::wstring plus=t.gapSigns?L"+":L"";
    if((t.standings.separateClasses?r.car.classPosition:r.car.position)==1) return app.session.type!="Race" && !(r.car.best>0)?L"—":L"LEADER";
    if(r.lapDifference>0) return plus+std::to_wstring(r.lapDifference)+L" L";
    return std::isfinite(r.gap)?plus+Format(r.gap):L"—";
}
std::wstring Interval(const Row& row,const TableSettings& t) {
    const std::wstring plus=t.gapSigns?L"+":L"";
    if((t.standings.separateClasses?row.car.classPosition:row.car.position)==1) return L"—";
    if(row.intervalLaps>0) return plus+std::to_wstring(row.intervalLaps)+L" L";
    return std::isfinite(row.interval)?plus+Format(row.interval):L"—";
}
// Fuel to add at the next stop: a final stop covers the shortfall, an intermediate stop fills the tank.
double PlannedStopFuel(const PitWindow& window,const FuelPlan& p) {
    const auto& f=app.frame; const double capacity=app.session.fuelCapacity;
    if(window.status!=PitWindowStatus::Open || !f.fuelKnown || !std::isfinite(f.fuel) || f.fuel<0 || !std::isfinite(capacity) || capacity<=0 || !std::isfinite(window.fuelAtEnd)) return -1;
    if(window.stops<=1) return std::min(capacity,std::max(0.0,-window.fuelAtEnd));
    const double untilStop=window.openNow?0:std::max(0.0,window.firstLap-(f.lapCompleted+f.lapPct));
    return std::max(0.0,capacity-std::max(0.0,f.fuel-untilStop*p.estimates[FuelBasis].consumption));
}
COLORREF ColorOf(unsigned rgb) { return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); }
constexpr int CornerRadius=10;
// License pill colors by class letter, as on iRacing's license cards; other strings keep the SDK color.
COLORREF LicenseFill(const Driver& d) {
    switch(d.license.empty()?0:d.license[0]) {
    case 'A': return RGB(31,111,209); case 'B': return RGB(47,154,74); case 'C': return RGB(184,135,15); case 'D': return RGB(208,102,29);
    case 'R': return RGB(194,58,50); case 'P': return RGB(40,44,50);
    default: return ColorOf(d.licenseColor);
    }
}
// Fastest lap of each class, live or from the session results.
double ClassBest(int classId) {
    if(app.classBestVersion!=app.modelVersion) {
        app.classBest.clear();
        for(const auto& d:app.session.drivers) {
            if(d.index<0 || d.index>=MaxCars) continue;
            double best=app.frame.cars[d.index].best;
            if(!(best>0) && app.session.number==app.frame.session) best=app.session.results[d.index].best;
            if(!(best>0)) continue;
            const auto known=std::find_if(app.classBest.begin(),app.classBest.end(),[&](const auto& entry){return entry.first==d.classId;});
            if(known==app.classBest.end()) app.classBest.push_back({d.classId,best}); else known->second=std::min(known->second,best);
        }
        app.classBestVersion=app.modelVersion;
    }
    const auto known=std::find_if(app.classBest.begin(),app.classBest.end(),[&](const auto& entry){return entry.first==classId;});
    return known==app.classBest.end()?-1:known->second;
}
// Laps or time done of the session, 0 to 1; -1 for unlimited sessions.
double SessionProgress() {
    const auto& f=app.frame;
    if(f.lapsTotal>0 && f.lapsTotal<32767 && f.lapCompleted>=0) return std::clamp((f.lapCompleted+f.lapPct)/f.lapsTotal,0.0,1.0);
    if(f.timeTotal>0 && f.timeTotal<604800 && f.timeLeftKnown) return std::clamp(1-f.timeLeft/f.timeTotal,0.0,1.0);
    return -1;
}
const std::array<int,MaxCars>& RatingChanges() {
    if(app.ratingVersion!=app.modelVersion) { app.ratingChanges=IRatingChanges(app.session,app.frame); app.ratingVersion=app.modelVersion; }
    return app.ratingChanges;
}
// A table cell. Text, ink and font start from the row's defaults; a box (filled pill or frame) fills the column,
// an icon square goes before the text and a badge (the status) after it.
struct Cell {
    enum Box : unsigned char { Plain, Filled, Framed, Ringed };
    std::wstring text; COLORREF ink; FontId font=Regular; Box box=Plain; COLORREF boxColor=0;
    std::wstring icon, badge; COLORREF badgeFill=0, badgeInk=0; int logo=-1;
};
void SetBadge(Cell& c,std::wstring status) {
    c.badgeFill=Ink::Yellow; c.badgeInk=Ink::Panel;
    if(status==L"OUT") { c.badgeFill=RGB(70,78,88); c.badgeInk=Ink::White; }
    else if(status==L"TOW" || status==L"REPAIR" || status==L"SLOW") c.badgeFill=RGB(245,140,60);
    else if(status==L"BLACK" || status==L"DQ") { c.badgeFill=RGB(214,64,56); c.badgeInk=RGB(255,255,255); }
    c.badge=std::move(status);
}
using CellRenderer=void(*)(const Row&,const Overlay&,Cell&);
// Indexed by Column, shared by Relative and Standings.
const CellRenderer CellRenderers[]={
    +[](const Row& r,const Overlay& o,Cell& c) {
        const int position=o.kind==StandingsKind && o.table.standings.separateClasses?r.car.classPosition:r.car.position;
        c.text=position>0?std::to_wstring(position):L"—"; c.ink=Ink::Muted; },
    +[](const Row& r,const Overlay& o,Cell& c) {
        c.box=Cell::Filled; c.font=Bold; c.text=L"#"+Wide(r.driver.number);
        if(!o.table.classColors) { c.boxColor=Ink::NeutralBadge; c.ink=Ink::White; return; }
        const unsigned col=r.driver.classColor; c.boxColor=ColorOf(col);
        c.ink=(299*((col>>16)&255)+587*((col>>8)&255)+114*(col&255)>100000)?Ink::Panel:RGB(255,255,255); },
    +[](const Row& r,const Overlay& o,Cell& c) {
        c.text=DriverName(r,o.table); if(r.player) c.font=Bold;
        if(o.table.showStatus) if(auto status=OverlayStatus(r,o.table); !status.empty()) SetBadge(c,std::move(status)); },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=Gap(r,o); if(o.kind==RelativeKind) { c.font=Bold; if(r.player) c.ink=Ink::Accent; } },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=Interval(r,o.table); c.ink=Ink::Muted; },
    +[](const Row& r,const Overlay&,Cell& c) { c.text=Lap(r.car.best); c.ink=r.car.best>0 && r.car.best<=ClassBest(r.driver.classId)+1e-4?Ink::Purple:Ink::Value; },
    // A last lap equal to the car's best is a personal best.
    +[](const Row& r,const Overlay&,Cell& c) { c.text=Lap(r.car.last); c.ink=r.car.last>0 && r.car.best>0 && r.car.last<=r.car.best+1e-4?Ink::Green:Ink::Value; },
    +[](const Row& r,const Overlay& o,Cell& c) {
        c.text=DriverValue(r,0,&o.table); if(c.text==L"—") { c.ink=Ink::Muted; return; }
        c.box=Cell::Filled; c.boxColor=LicenseFill(r.driver); c.ink=RGB(255,255,255); c.font=SmallBold; },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=DriverValue(r,IRatingColumn-LicenseColumn,&o.table); c.ink=Ink::Value; },
    +[](const Row& r,const Overlay&,Cell& c) {
        // The compound's initial in a ring: D dry, W wet, or S/M/H, P/A where a car has several dry compounds.
        const auto label=TireLabel(r.driver,r.car.tireCompound);
        if(label.empty()) { c.text=L"—"; c.ink=Ink::Muted; return; }
        c.box=Cell::Ringed; c.font=SmallBold; c.text=std::wstring(1,static_cast<wchar_t>(label[0]));
        c.ink=label=="Wet"?Ink::Wet:(label=="Soft"?Ink::Red:(label=="Medium"?Ink::Yellow:Ink::Value));
        if(label=="Alternate") c.ink=r.driver.carPath.find("dw12")!=std::string::npos || r.driver.carName.find("DW12")!=std::string::npos?Ink::Red:Ink::Green; },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=DriverValue(r,PitLapColumn-LicenseColumn,&o.table); c.ink=Ink::Value; },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=DriverValue(r,PitTimeColumn-LicenseColumn,&o.table); c.ink=Ink::Value; },
    +[](const Row& r,const Overlay& o,Cell& c) { c.text=DriverValue(r,PitStopColumn-LicenseColumn,&o.table); c.ink=Ink::Value; },
    +[](const Row& r,const Overlay&,Cell& c) { c.text=CompletedLaps(r); c.ink=Ink::Value; },
    +[](const Row& r,const Overlay& o,Cell& c) {
        const int gained=PositionsGained(app.session,r,o.table.standings.separateClasses);
        c.text=gained==-MaxCars?L"—":(gained==0?L"–":(gained>0?L"\u25B2":L"\u25BC")+std::to_wstring(std::abs(gained)));
        c.ink=gained==-MaxCars || gained==0?Ink::Muted:(gained>0?Ink::Green:Ink::Red); },
    +[](const Row& r,const Overlay& o,Cell& c) {
        c.text=OverlayStatus(r,o.table);
        c.ink=c.text==L"BLACK" || c.text==L"DQ"?Ink::Red:(c.text==L"TOW" || c.text==L"REPAIR" || c.text==L"SLOW"?RGB(245,163,65):Ink::Yellow); },
    +[](const Row& r,const Overlay& o,Cell& c) {
        // The brand's logo (its initial where there is none) and name, each with its own switch.
        auto brand=Wide(r.driver.brand); if(brand.empty()) { c.text=L"—"; c.ink=Ink::Muted; return; }
        CharUpperBuffW(brand.data(),static_cast<DWORD>(brand.size()));
        if(o.table.brandLogo) { c.logo=BrandLogo(r.driver.brand); if(c.logo<0) c.icon=brand.substr(0,1); }
        if(o.table.brandName) c.text=std::move(brand);
        c.font=SmallBold; c.ink=Ink::Brand; },
    +[](const Row& r,const Overlay&,Cell& c) {
        const int index=r.driver.index, change=index>=0 && index<MaxCars?RatingChanges()[index]:NoRatingChange;
        if(change==NoRatingChange) { c.text=L"—"; c.ink=Ink::Muted; return; }
        c.text=change>0?L"+"+std::to_wstring(change):(change<0?L"\u2212"+std::to_wstring(-change):L"0");
        c.ink=change>0?Ink::Green:(change<0?Ink::Red:Ink::Muted); },
    +[](const Row& r,const Overlay&,Cell& c) {
        c.box=Cell::Framed; c.boxColor=Ink::Frame; c.font=Small; c.ink=Ink::Value;
        c.text=r.car.stintLaps>=0?L"L"+std::to_wstring(r.car.stintLaps):L"L-"; },
    +[](const Row& r,const Overlay&,Cell& c) { c.text=r.car.stops>=0?std::to_wstring(r.car.stops):L"?"; c.ink=r.car.stops>=0?Ink::Value:Ink::Muted; },
    +[](const Row& r,const Overlay&,Cell& c) {
        const auto status=FuelFinishStatus(r.driver.index);
        if(status==OpponentFuelStatus::None) return;
        c.text=status==OpponentFuelStatus::End?L"END":L"SAVE";
        c.box=Cell::Filled; c.boxColor=status==OpponentFuelStatus::End?Ink::Green:Ink::Yellow; c.ink=Ink::Panel; c.font=SmallBold; }};
static_assert(std::size(CellRenderers)==ColumnCount);
bool Editing(const Overlay& o); // Its own page is open in the visible main window.
// Describes an overlay in physical pixels. Comparing it with the previous scene yields the exact changed areas.
#include "TrackMap.inc"
void BuildScene(const Overlay& o,int width,int height,int dpi,std::vector<DrawCommand>& out) {
    out.clear();
    auto px=[&](int v){return MulDiv(v,dpi*o.place.scale,9600);};
    auto rect=[&](int x,int y,int w,int h){return RECT{px(x),px(y),px(x+w),px(y+h)};};
    const int titleOffset=ShowOverlayHeader(o)?0:px(32);
    auto fill=[&](RECT r,COLORREF color) { r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({DrawCommand::Fill,Regular,0,r,color}); };
    auto label=[&](std::wstring text,RECT r,COLORREF color,FontId font,UINT align=DT_LEFT) {
        r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({DrawCommand::Text,font,align,r,color,{},std::move(text)});
    };
    auto shape=[&](DrawCommand::Type type,RECT r,COLORREF color,int radius=3) {
        r.top-=titleOffset; r.bottom-=titleOffset; out.push_back({type,Regular,0,r,color,{static_cast<float>(px(radius))}});
    };
    const COLORREF white=Ink::White, muted=Ink::Muted;
    out.push_back({DrawCommand::Fill,Regular,0,{0,0,width,height},MapOnly(o)?TrackMapTransparentKey:Ink::Panel});
    // Width in physical pixels, measured with the overlay's fonts exactly as DrawLabel will.
    // Most metric texts repeat between refreshes, so widths are kept until the fonts change.
    auto measure=[&](const std::wstring& text,FontId font) {
        if(!o.dc || text.empty()) return 0;
        for(const auto& [cachedFont,cachedText,width]:o.widths) if(cachedFont==font && cachedText==text) return width;
        SIZE size{}; SelectObject(o.dc,o.fonts[font]); GetTextExtentPoint32W(o.dc,text.c_str(),static_cast<int>(text.size()),&size);
        if(o.widths.size()>=32) o.widths.erase(o.widths.begin());
        o.widths.push_back({font,text,static_cast<int>(size.cx)}); return static_cast<int>(size.cx);
    };
    // Header and footer metrics: the first on the left, the others right-aligned in order; labels muted, values bright.
    auto slots=[&](const Slots& metrics,RECT band) {
        const int margin=px(14),gap=px(14);
        std::array<MetricText,SlotCount> texts; std::array<std::array<int,4>,SlotCount> parts{}; std::array<int,SlotCount> widths{}; int right=0;
        for(int slot=0;slot<SlotCount;++slot) {
            texts[slot]=Metric(metrics[slot],&o);
            if(texts[slot].pill!=CLR_INVALID && !texts[slot].parts[1].empty()) widths[slot]=measure(texts[slot].parts[1],SmallBold)+px(18);
            else for(int part=0;part<4;++part) widths[slot]+=parts[slot][part]=measure(texts[slot].parts[part],part%2?Bold:Regular);
            if(slot && widths[slot]) right+=widths[slot]+(right?gap:0);
        }
        auto draw=[&](int slot,int x) {
            if(const COLORREF pill=texts[slot].pill; pill!=CLR_INVALID) {
                // A status as a pill in its color: stops needed, declared wet.
                const int middle=(band.top+band.bottom)/2; const RECT area{x,middle-px(9),x+widths[slot],middle+px(9)};
                shape(DrawCommand::RoundFill,{area.left,area.top+titleOffset,area.right,area.bottom+titleOffset},Mix(Ink::Band,pill,.16),9);
                label(std::move(texts[slot].parts[1]),{area.left,area.top+titleOffset,area.right,area.bottom+titleOffset},pill,SmallBold,DT_CENTER);
                return;
            }
            for(int part=0;part<4;++part) {
                auto& text=texts[slot].parts[part]; if(text.empty()) continue;
                const int w=parts[slot][part];
                const COLORREF ink=part%2?(metrics[slot]==IncidentsMetric?Ink::Orange:white):Ink::Label;
                label(std::move(text),{x,band.top,std::min(x+w+1,width-margin),band.bottom},ink,part%2?Bold:Regular); x+=w;
            }
        };
        draw(0,margin);
        int x=std::max(width-margin-right,margin+widths[0]+gap);
        for(int slot=1;slot<SlotCount;++slot) if(widths[slot]) { draw(slot,x); x+=widths[slot]+gap; }
    };
    if(ShowOverlayHeader(o)) {
        fill({0,0,width,px(32)},Ink::Band);
        fill({0,px(31),width,px(32)},Ink::Line);
        slots(o.bands.headerMetrics,{0,0,width,px(31)});
    }
    // The footer from logical y down to the bottom.
    auto footer=[&](int y) {
        if(!FooterHeight(o)) return;
        fill({0,px(y),width,height+titleOffset},Ink::Band); fill({0,px(y),width,px(y)+std::max(1,px(1))},Ink::Line);
        slots(o.bands.footer.metrics,{0,px(y),width,height+titleOffset});
    };
    if(o.kind==TrackMapKind) {
#include "TrackMapScene.inc"
    } else if(IsTable(o.kind)) {
        const auto columns=VisibleColumns(o); int headerX=12;
        if(ProgressHeight(o)) {
            // Like the fuel tank: the session done in the accent color on a dark track, the percentage after it.
            const double progress=SessionProgress(); const int left=px(14), right=width-px(58), top=px(39);
            // The part still to drive is hatched, in neutral grey since it isn't a warning like missing fuel.
            const int done=left+static_cast<int>((right-left)*progress);
            fill({left,top,done,top+px(10)},Ink::Accent);
            if(done<right) out.push_back({DrawCommand::Hatch,Regular,0,{done,top-titleOffset,right,top+px(10)-titleOffset},Ink::HatchGrey,{static_cast<float>(Ink::BarBack)}});
            label(Format(progress*100,0)+L" %",{right,px(32),width-px(14),px(56)},Ink::Muted,Small,DT_RIGHT);
        }
        if(o.table.columnHeaders) for(int id:columns) { label(id==GapColumn && o.kind==RelativeKind?L"GAP ≈":ColumnTitle(id),rect(headerX,34+ProgressHeight(o),ColumnWidth(o,id)-8,24),Ink::Label,Regular,Columns[id].align); headerX+=ColumnWidth(o,id); }
        int y=TableRowsTop(o);
        static const std::vector<Row> noRows; static const std::vector<StandingsGroup> noGroups;
        const auto& rows=o.kind==RelativeKind?RelativeRows(o):noRows;
        const auto& groups=o.kind==StandingsKind?StandingsGroups(o):noGroups;
        const bool rowsEmpty=o.kind==RelativeKind?rows.empty():std::none_of(groups.begin(),groups.end(),[](const StandingsGroup& group){return !group.rows.empty();});
        auto drawRow=[&](const Row& r) {
            if(r.pitExit) {
                // The own row's highlight, faded: position, then what the stop costs and who is ahead.
                const auto& rejoin=PitExit(); int x=12;
                fill({0,px(y),width,px(y+27)},Ink::PitExitRow);
                if(o.table.classColors) fill(rect(0,y,3,27),ColorOf(r.driver.classColor));
                if(std::find(columns.begin(),columns.end(),PositionColumn)!=columns.end()) {
                    Cell cell{{},Ink::Accent}; CellRenderers[PositionColumn](r,o,cell);
                    label(std::move(cell.text),rect(x,y,ColumnWidth(o,PositionColumn)-8,27),Ink::Accent,Regular,Columns[PositionColumn].align);
                    x+=ColumnWidth(o,PositionColumn);
                }
                const auto ahead=PitExitAhead();
                label(L"PIT EXIT  ·  "+Format(rejoin.loss,0)+L" s"+(app.pitExitPartial?L"?":L"")+(ahead.empty()?L"":L"  ·  "+ahead),rect(x,y,OverlayWidth(o)-x-12,27),Ink::Accent,Regular);
                y+=27; return;
            }
            fill({0,px(y),width,px(y+27)},r.player?Ink::OwnRow:Ink::Panel);
            if(o.table.classColors) fill(rect(0,y,3,27),ColorOf(r.driver.classColor));
            COLORREF color=white;
            const bool lapColors=o.kind==RelativeKind && o.table.lapColors && app.session.type=="Race" && !r.player;
            if(lapColors && r.lapDifference>0) color=Ink::LapsAhead;
            if(lapColors && r.lapDifference<0) color=Ink::LapsBehind;
            int x=12;
            for(int id:columns) {
                const int w=ColumnWidth(o,id), inner=w-8; Cell cell{{},color};
                CellRenderers[id](r,o,cell);
                if(cell.box==Cell::Ringed) {
                    const int left=x+(inner-19)/2;
                    out.push_back({DrawCommand::Ring,cell.font,0,{px(left),px(y+4)-titleOffset,px(left+19),px(y+23)-titleOffset},cell.ink,{std::max(1.f,1.6f*px(100)/100.f)},std::move(cell.text)});
                } else if(cell.box!=Cell::Plain) {
                    const RECT area=rect(x,y+5,inner,17);
                    shape(cell.box==Cell::Filled?DrawCommand::RoundFill:DrawCommand::Outline,area,cell.boxColor);
                    label(std::move(cell.text),area,cell.ink,cell.font,DT_CENTER);
                } else {
                    int left=x;
                    if(cell.logo>=0) { const RECT icon=rect(x,y+4,19,19); out.push_back({DrawCommand::Logo,Regular,0,{icon.left,icon.top-titleOffset,icon.left+icon.bottom-icon.top,icon.bottom-titleOffset},Ink::Brand,{static_cast<float>(cell.logo)}}); left+=25; }
                    else if(!cell.icon.empty()) { const RECT icon=rect(x,y+5,17,17); shape(DrawCommand::RoundFill,icon,Ink::IconBack); label(std::move(cell.icon),icon,Ink::Label,SmallBold,DT_CENTER); left+=23; }
                    RECT text=rect(left,y,x+inner-left,27);
                    if(!cell.badge.empty()) {
                        // The name shrinks (with an ellipsis) so the badge always fits behind it.
                        const int badgeWidth=measure(cell.badge,SmallBold)+px(10), gap=px(6);
                        text.right=std::min(text.left+measure(cell.text,cell.font)+1,text.right-badgeWidth-gap);
                        const RECT badge{text.right+gap,px(y+6),text.right+gap+badgeWidth,px(y+21)};
                        shape(DrawCommand::RoundFill,badge,cell.badgeFill); label(std::move(cell.badge),badge,cell.badgeInk,SmallBold,DT_CENTER);
                    }
                    label(std::move(cell.text),text,cell.ink,cell.font,Columns[id].align);
                }
                x+=w;
            }
            y+=27;
        };
        if(o.kind==RelativeKind) {
            const auto reference=std::find_if(rows.begin(),rows.end(),[](const Row& row){return row.player;});
            if(reference!=rows.end()) y+=(o.table.rows/2-static_cast<int>(reference-rows.begin()))*27;
            for(const auto& row:rows) drawRow(row);
        }
        else for(const auto& group:groups) {
            if(o.table.standings.separateClasses) {
                fill({0,px(y),width,px(y+25)},Ink::ClassBand); fill(rect(0,y,3,25),ColorOf(group.color));
                int cars=0; const int sof=ClassSof(group.classId,cars);
                auto name=Wide(group.name); const int nameWidth=measure(name,Bold);
                label(std::move(name),{px(12),px(y),std::min(px(12)+nameWidth+1,width-px(12)),px(y+25)},ColorOf(group.color),Bold);
                label(std::to_wstring(cars)+(cars==1?L" car":L" cars")+(sof>0?L"  \u00B7  SOF "+std::to_wstring(sof):L""),{px(12)+nameWidth+px(10),px(y),width-px(12),px(y+25)},Ink::Label,Small);
                y+=25;
            }
            int previous=0;
            for(const auto& row:group.rows) {
                const int position=o.table.standings.separateClasses?row.car.classPosition:row.car.position;
                const bool skipped=previous && position>previous+1;
                drawRow(row);
                if(skipped) out.push_back({DrawCommand::DottedLine,Regular,0,{px(12),px(y-27)-titleOffset,width-px(12),px(y-27)-titleOffset+1},Ink::Divider});
                previous=position;
            }
        }
        if(rowsEmpty) label(L"Waiting for driver and position data …",rect(12,TableRowsTop(o)+16,335,28),muted,Regular);
        footer(o.kind==RelativeKind?TableRowsTop(o)+o.table.rows*27:y+(rowsEmpty?54:0));
    } else if(o.kind==FuelKind) {
        const auto& p=Plan(); const auto& f=app.frame; const auto& on=o.fuel.display;
        const auto& basis=p.estimates[FuelBasis]; const double burn=basis.samples?basis.consumption:-1;
        const auto window=FuelWindow();
        const bool open=window.status==PitWindowStatus::Open, noStop=window.status==PitWindowStatus::NoStopNeeded, extraStop=window.status==PitWindowStatus::ExtraStopNeeded;
        const bool tankKnown=f.fuelKnown && std::isfinite(f.fuel) && f.fuel>=0;
        const double capacity=app.session.fuelCapacity; const bool capacityKnown=std::isfinite(capacity) && capacity>0;
        const double range=tankKnown && burn>0?f.fuel/burn:-1, refuel=burn>0?basis.add:-1, needed=burn>0 && p.lapsToFinish>=0?burn*p.lapsToFinish:-1;
        const bool refuelFits=refuel>=0 && (!capacityKnown || refuel<=capacity);
        const int current=f.lapCompleted>=0?f.lapCompleted+1:-1, finishLap=FuelFinishLap();
        auto lap=[](int number){ return number>0?L"L"+std::to_wstring(number):std::wstring(L"—"); };
        // Targets: the most fuel per lap that still reaches the latest stop, or the finish when no stop is needed.
        const int targetLap=open?window.lastLap:(noStop?finishLap:-1);
        const double targetDistance=open?window.lastLap-(f.lapCompleted+f.lapPct):(noStop?p.lapsToFinish:-1);
        auto target=[&](int extra){ const double distance=targetDistance+extra; return tankKnown && targetDistance>0 && distance>.05?f.fuel/distance:-1.0; };
        auto targetInk=[&](double value){ return value<0 || burn<=0?Ink::Muted:(value>=burn?Ink::Green:(value>=.9*burn?Ink::Yellow:Ink::Red)); };
        auto litres=[](double value,bool plus=false){ return value<0?std::wstring(L"—"):(plus?L"+":L"")+Format(value,1)+L" L"; };
        const int W=OverlayWidth(o); int y=32;
        if(o.fuel.strip) {
            int n=0;
            auto item=[&](const wchar_t* head,std::wstring value,COLORREF ink) {
                const int x=n++*FuelStripCell; if(x) fill(rect(x,y+1,1,48),Ink::Line);
                label(head,rect(x+12,y+6,FuelStripCell-16,16),Ink::Label,Small); label(std::move(value),rect(x+12,y+21,FuelStripCell-16,24),ink,Bold);
            };
            for(int id:o.fuel.order) if(on[id]) switch(id) {
            case TankModule: item(L"TANK",litres(tankKnown?f.fuel:-1),white); item(L"RANGE",range>=0?Format(range,1)+L" laps":L"—",Ink::Accent); break;
            case FiguresModule: item(L"AVG 10",burn>0?Format(burn,2):L"—",white); break;
            case RefuelCard: item(L"REFUEL",litres(refuel,true),refuel<0?Ink::Muted:(refuelFits?Ink::Green:Ink::Red)); break;
            case WindowCard: item(L"BOX BY",open?lap(window.lastLap):L"—",white); break;
            case TargetRow: item(L"TARGET",target(0)>=0?Format(target(0),2):L"—",targetInk(target(0))); break;
            case OneLongerRow: item(L"+1 LAP",target(1)>=0?Format(target(1),2):L"—",targetInk(target(1))); break;
            case LeaderDistanceModule: item(L"LEADER LAPS",LeaderDistanceValue(),Ink::Accent); break;
            case WhiteFlagTimeModule: item(L"FINAL LAP",WhiteFlagTimeValue(),white); break;
            case PitExitModule: item(L"PIT EXIT",PitExit().valid?L"P"+std::to_wstring(PitExit().classPosition):L"—",Ink::Accent); break;
            default: break;
            }
            y+=50;
        } else {
            const auto shown=FuelShown(o); const int figures=static_cast<int>(std::count(o.fuel.figures.begin(),o.fuel.figures.end(),true));
            for(size_t k=0;k<shown.size();++k) {
                const int id=shown[k], next=k+1<shown.size()?shown[k+1]:-1;
                // A line above each module that has none of its own; the consumption figures close with one.
                auto separator=[&]{ if(y>32 && (!k || shown[k-1]!=FiguresModule)) fill(rect(0,y,W,1),Ink::Line); };
                if(id==PitExitModule) {
                    // Box now: class position and the car ahead at the rejoin point, with the time lost.
                    separator();
                    const auto& r=PitExit();
                    label(r.valid?L"PIT EXIT  ·  "+Format(r.loss,0)+L" s"+(app.pitExitPartial?L"?":L""):L"PIT EXIT",rect(16,y,W/2,32),Ink::Label,Small);
                    const std::wstring position=r.valid?L"P"+std::to_wstring(r.classPosition):L"—", ahead=PitExitAhead();
                    const int w=measure(position,Bold);
                    label(position,{px(W-16)-w,px(y),px(W-16),px(y+32)},Ink::Accent,Bold);
                    if(!ahead.empty()) label(ahead,{px(W/2),px(y),px(W-16)-w-px(8),px(y+32)},Ink::Muted,Small,DT_RIGHT);
                    y+=32;
                }
                if(id==LeaderDistanceModule || id==WhiteFlagTimeModule) {
                    // One value row each, like Weather's precipitation.
                    separator();
                    label(id==LeaderDistanceModule?L"LEADER LAPS AT FINISH":L"TIME LEFT AT LEADER'S FINAL LAP",rect(16,y,W-150,32),Ink::Label,Small);
                    label(id==LeaderDistanceModule?LeaderDistanceValue()+L" laps":WhiteFlagTimeValue(),rect(W-142,y,126,32),id==LeaderDistanceModule?Ink::Accent:white,Bold,DT_RIGHT);
                    y+=32;
                }
                if(id==TankModule) {
                    separator();
                    label(L"IN TANK",rect(16,y+10,150,16),Ink::Label,Small); label(L"RANGE",rect(W-166,y+10,150,16),Ink::Label,Small,DT_RIGHT);
                    const std::wstring fuel=tankKnown?Format(f.fuel,1):L"—", laps=range>=0?Format(range,1):L"—";
                    const int fuelWidth=measure(fuel,Large), unitWidth=measure(L"laps",Regular), lapsWidth=measure(laps,Large), end=px(W-16);
                    label(fuel,{px(16),px(y+24),px(16)+fuelWidth+1,px(y+68)},white,Large);
                    label(L"L",{px(21)+fuelWidth,px(y+38),px(60)+fuelWidth,px(y+64)},Ink::Label,Regular);
                    label(L"laps",{end-unitWidth,px(y+38),end,px(y+64)},Ink::Label,Regular);
                    label(laps,{end-unitWidth-px(5)-lapsWidth,px(y+24),end-unitWidth-px(5)+1,px(y+68)},Ink::Accent,Large);
                    // The bar spans the tank (or more fuel when the finish needs more): fuel in it, fuel missing to the finish, and the finish mark.
                    const int left=16,span=W-32,top=y+74; const double scale=std::max({capacityKnown?capacity:0.0,needed,tankKnown?f.fuel:0.0});
                    fill(rect(left,top,span,12),Ink::BarBack);
                    auto at=[&](double value){ return left+static_cast<int>(span*std::clamp(value/scale,0.0,1.0)); };
                    if(scale>0 && tankKnown) fill(rect(left,top,at(f.fuel)-left,12),Ink::Accent);
                    if(scale>0 && tankKnown && needed>f.fuel) out.push_back({DrawCommand::Hatch,Regular,0,{px(at(f.fuel)),px(top)-titleOffset,px(at(needed)),px(top+12)-titleOffset},Ink::HatchFore,{static_cast<float>(Ink::HatchBack)}});
                    if(scale>0 && needed>=0) fill(rect(at(needed)-1,top-4,2,20),RGB(255,255,255));
                    fill(rect(16,y+97,8,8),Ink::Accent); label(L"In tank",rect(29,y+92,70,18),Ink::Muted,Small);
                    if(tankKnown && needed>f.fuel) { fill(rect(100,y+97,8,8),Ink::Missing); label(L"Missing "+litres(needed-f.fuel),rect(113,y+92,130,18),Ink::Muted,Small); }
                    if(needed>=0) { fill(rect(246,y+96,2,10),RGB(255,255,255)); label(L"Needed "+litres(needed)+L" · "+Format(p.lapsToFinish,1)+L" laps",rect(253,y+92,W-269,18),Ink::Muted,Small); }
                    y+=112;
                }
                if(id==FiguresModule) {
                    fill(rect(0,y,W,1),Ink::Line);
                    for(int i=0,index=0;i<FuelMetricCount;++i) if(o.fuel.figures[i]) {
                        const auto& figure=FuelFigures[i]; const auto& estimate=p.estimates[figure.metric];
                        const int left=W*index/figures,cell=W*(index+1)/figures-left; ++index;
                        if(left) fill(rect(left,y+1,1,50),Ink::Line);
                        const COLORREF ink=figure.metric==FuelBasis?Ink::Accent:(figure.metric==static_cast<int>(FuelMetric::Minimum)?Ink::Green:(figure.metric==static_cast<int>(FuelMetric::Maximum)?Ink::Red:white));
                        static constexpr int laps[]={0,10,5,0,0,0,3};
                        const bool partial=estimate.samples>0 && estimate.samples<laps[figure.metric];
                        label(partial?std::wstring(figure.heading)+L" ("+std::to_wstring(estimate.samples)+L")":figure.heading,rect(left,y+7,cell,16),Ink::Label,Small,DT_CENTER);
                        label(estimate.samples?Format(estimate.consumption,2):L"—",rect(left,y+23,cell,22),partial?Ink::Muted:ink,Bold,DT_CENTER);
                    }
                    fill(rect(0,y+51,W,1),Ink::Line); y+=52;
                }
                if(id==HistoryModule) {
                    separator();
                    label(L"CONSUMPTION  ·  LAST 10 LAPS",rect(16,y+8,W/2,16),Ink::Label,Small);
                    const int n=p.historyCount,top=y+30,chart=56,gap=6,bar=(W-32-9*gap)/10;
                    if(!n) label(L"Waiting for complete laps",rect(16,top,W-32,chart),Ink::Muted,Regular,DT_CENTER);
                    else {
                        // The scale starts a little below the lowest lap, so differences of a few percent stay visible.
                        double low=burn>0?burn:p.history[0].burn,high=low; int lastCounted=-1; const FuelLap* caution=nullptr;
                        for(int i=0;i<n;++i) { low=std::min(low,p.history[i].burn); high=std::max(high,p.history[i].burn); if(p.history[i].counted) lastCounted=i; }
                        low=std::max(0.0,low-std::max(.2*(high-low),.04*high)); high*=1.02;
                        auto barTop=[&](double value){ return top+chart-std::max(2,static_cast<int>(chart*(value-low)/(high-low))); };
                        for(int i=0;i<n;++i) {
                            const auto& l=p.history[i]; const int x=16+(10-n+i)*(bar+gap), barY=barTop(l.burn);
                            fill(rect(x,barY,bar,top+chart-barY),l.counted?(i==lastCounted?Ink::Accent:Ink::ChartBar):(l.caution && on[CautionMarks]?Ink::CautionBar:Ink::PitBar));
                            if(l.caution && !caution) caution=&l;
                        }
                        if(burn>0) {
                            const int average=barTop(burn);
                            out.push_back({DrawCommand::DottedLine,Regular,0,{px(16),px(average)-titleOffset,px(W-16),px(average)+1-titleOffset},Ink::Muted});
                            label(L"Ø "+Format(burn,2),rect(W-80,average-16,64,14),Ink::Value,Small,DT_RIGHT);
                        }
                        label(lap(p.history[0].lap),rect(16+(10-n)*(bar+gap),top+chart+2,60,16),Ink::Label,Small);
                        label(lap(p.history[n-1].lap),rect(W-76,top+chart+2,60,16),Ink::Label,Small,DT_RIGHT);
                        if(caution && on[CautionMarks]) label(lap(caution->lap)+L" caution · not averaged",rect(W/2,y+8,W/2-16,16),Ink::Yellow,Small,DT_RIGHT);
                    }
                    y+=104;
                }
                if(IsCard(id)) {
                    const bool pair=IsCard(next); const int cardWidth=pair?(W-42)/2:W-32; int x=16;
                    auto card=[&](const wchar_t* title,std::wstring value,COLORREF ink,std::wstring note) {
                        shape(DrawCommand::RoundFill,rect(x,y,cardWidth,80),Ink::Band,6);
                        label(title,rect(x+12,y+8,cardWidth-24,16),Ink::Label,Small);
                        label(std::move(value),rect(x+12,y+24,cardWidth-24,32),ink,Hero);
                        label(std::move(note),rect(x+12,y+56,cardWidth-24,16),Ink::Muted,Small); x+=cardWidth+10;
                    };
                    for(int which:{id,pair?next:-1}) if(which==RefuelCard) card(L"REFUEL TO FINISH",litres(refuel,true),refuel<0?Ink::Muted:(refuelFits?Ink::Green:Ink::Red),
                        refuel<0?L"waiting for laps":(refuel==0?L"enough fuel to finish":(refuelFits?L"fits in the tank":L"more than one tank")));
                    else if(which==WindowCard) card(L"PIT WINDOW",open?lap(window.firstLap)+L"–"+std::to_wstring(window.lastLap):L"—",white,
                        open?(window.openNow?std::wstring(L"open now"):L"opens "+lap(window.firstLap))+L" · latest "+lap(window.lastLap)
                            :(noStop?L"no stop needed":(extraStop?L"an extra stop is needed":L"waiting for laps")));
                    if(pair) ++k;
                    y+=92;
                }
                if(id==TimelineModule) {
                    // One block per lap of a full tank from the stint's first lap, capped at the finish.
                    fill(rect(0,y,W,1),Ink::Line);
                    const int first=p.stintStartLap>0?p.stintStartLap:std::max(1,current);
                    const int count=finishLap>0?std::max(0,std::min(StintCellCount(),finishLap-first+1)):StintCellCount();
                    for(int i=0;i<count;++i) {
                        const int number=first+i,left=16+i*(W-32)/count,blockWidth=std::max(1,16+(i+1)*(W-32)/count-2-left);
                        const bool inWindow=open && number>=window.firstLap && number<=window.lastLap;
                        if(number==current) fill(rect(left,y+9,blockWidth,18),Ink::Yellow);
                        else fill(rect(left,y+12,blockWidth,12),inWindow?Ink::WindowOpen:(number<current?Ink::LapDone:Ink::LapAhead));
                    }
                    if(count) { label(lap(first),rect(16,y+27,60,16),Ink::Label,Small); label(lap(first+count-1),rect(W-76,y+27,60,16),Ink::Label,Small,DT_RIGHT); }
                    std::wstring box=L"—"; COLORREF boxInk=Ink::Muted;
                    if(open) {
                        const double add=PlannedStopFuel(window,p);
                        box=(window.openNow?L"BOX NOW · ":L"BOX ")+lap(window.firstLap)+L"–"+std::to_wstring(window.lastLap);
                        if(add>=0) box+=L" · "+litres(add,true);
                        if(window.stops>1) box+=L" · "+std::to_wstring(window.stops)+L" STOPS";
                        boxInk=window.openNow?Ink::Green:white;
                    }
                    else if(noStop) { box=L"NO STOP NEEDED"; boxInk=Ink::Green; }
                    else if(extraStop) box=L"NO PIT WINDOW";
                    label(std::move(box),rect(76,y+27,W-152,16),boxInk,SmallBold,DT_CENTER);
                    y+=46;
                }
                if(IsTarget(id)) {
                    // The first of adjacent target rows draws their shared band.
                    if(!k || !IsTarget(shown[k-1])) {
                        int rows=0; for(size_t j=k;j<shown.size() && IsTarget(shown[j]);++j) ++rows;
                        fill(rect(0,y,W,16+24*rows),Ink::Band); fill(rect(0,y,W,1),Ink::Line); y+=8;
                    }
                    static constexpr const wchar_t* names[]={L"One lap shorter",L"Target",L"One lap longer",L"Two laps longer"};
                    const int extra=id-TargetRow; const double value=target(extra); const bool main=extra==0;
                    label(names[id-OneShorterRow],rect(16,y,150,24),main?white:Ink::Muted,main?Bold:Regular);
                    label(targetLap>0?lap(targetLap+extra):L"",rect(166,y,50,24),Ink::Label,Small);
                    label(value>=0?L"max "+Format(value,2)+L" L/lap":L"—",rect(216,y,150,24),targetInk(value),main?Bold:Regular,DT_RIGHT);
                    label(value>=0 && burn>0?(value>=burn?L"+":L"−")+Format(std::abs(value/burn-1)*100,0)+L" %":L"",rect(366,y,W-382,24),Ink::Label,Small,DT_RIGHT);
                    y+=24;
                    if(!IsTarget(next)) y+=8;
                }
            }
        }
        footer(y);
    } else if(o.kind==WeatherKind) {
        const auto& f=app.frame; const auto& on=o.weather.display; const auto& history=app.weatherHistory;
        const int W=OverlayWidth(o), wet=f.trackWetness>=0 && f.trackWetness<8?f.trackWetness:0; int y=32;
        const COLORREF wetInk=RGB(140,198,255);
        auto degrees=[](double value){ if(!std::isfinite(value)) return std::wstring(L"—"); wchar_t text[24]; swprintf_s(text,L"%.1f°",value); return std::wstring(text); };
        auto percent=[](double fraction){ return std::isfinite(fraction) && fraction>=0?Format(fraction*100,0)+L" %":std::wstring(L"—"); };
        // Trend arrows over the last ten minutes; nothing while unknown, switched off or while old
        // footage plays, as the history is the live session's.
        const bool trends=on[TrendArrows] && View(f)!=ViewMode::Replay;
        using Trend=std::pair<std::wstring,COLORREF>;
        auto trend=[&](double change,double steady,int decimals,const wchar_t* unit,COLORREF rising,COLORREF falling) {
            if(!trends || !std::isfinite(change)) return Trend{};
            if(std::abs(change)<steady) return Trend{L"→",Ink::Muted};
            return Trend{(change>0?L"↑ +":L"↓ −")+Format(std::abs(change),decimals)+unit,change>0?rising:falling};
        };
        const double wetChange=history.Change(&WeatherHistory::Sample::wetness);
        const Trend wetTrend=!trends || !std::isfinite(wetChange)?Trend{}:(wetChange>=1?Trend{L"↗ getting wetter",wetInk}:(wetChange<=-1?Trend{L"↘ drying",Ink::Value}:Trend{L"→ steady",Ink::Muted}));
        const Trend rainTrend=trend(history.Change(&WeatherHistory::Sample::rain)*100,2,0,L" %",wetInk,Ink::Muted);
        const Trend trackTrend=trend(history.Change(&WeatherHistory::Sample::track),.3,1,L"",Ink::Orange,Ink::Wet), airTrend=trend(history.Change(&WeatherHistory::Sample::air),.3,1,L"",Ink::Orange,Ink::Wet);
        // Wind: compass direction it comes from, and where it hits the car.
        static constexpr const wchar_t* Compass[]={L"N",L"NE",L"E",L"SE",L"S",L"SW",L"W",L"NW"};
        static constexpr const wchar_t* Sides[]={L"Headwind",L"From front right",L"Crosswind from the right",L"From rear right",L"Tailwind",L"From rear left",L"Crosswind from the left",L"From front left"};
        constexpr double pi=3.141592653589793;
        const double angle=on[WindRelative]?RelativeWindAngle(f):std::numeric_limits<double>::quiet_NaN();
        auto octant=[&](double radians){ return static_cast<int>(std::lround(std::fmod(std::fmod(radians,2*pi)+2*pi,2*pi)/(pi/4)))%8; };
        const std::wstring speed=f.windSpeed>=0?Format(f.windSpeed*3.6,0):L"—", from=std::isfinite(f.windDirection)?Compass[octant(f.windDirection)]:L"";
        const std::wstring side=std::isfinite(angle)?Sides[octant(angle)]:L"";
        if(o.weather.strip) {
            int n=0;
            auto item=[&](const wchar_t* head,std::wstring value,COLORREF ink,Trend extra,int cells=1) {
                const int x=n*WeatherStripCell, cell=cells*WeatherStripCell; n+=cells; if(x) fill(rect(x,y+1,1,48),Ink::Line);
                label(head,rect(x+12,y+6,cell-16,16),Ink::Label,Small);
                const int w=measure(value,Bold); label(std::move(value),{px(x+12),px(y+21),std::min(px(x+12)+w+1,px(x+cell-4)),px(y+45)},ink,Bold);
                if(!extra.first.empty()) label(std::move(extra.first),{px(x+12)+w+px(4),px(y+21),px(x+cell-4),px(y+45)},extra.second,Small);
            };
            for(int id:o.weather.order) if(on[id]) switch(id) {
            case ConditionModule: item(L"TRACK",WetnessNames[wet],wet>=3?wetInk:white,wetTrend.first.empty()?Trend{}:Trend{wetTrend.first.substr(0,1),wetTrend.second}); break;
            case RainModule: item(L"RAIN",percent(f.precipitation),white,rainTrend.first.empty()?Trend{}:Trend{rainTrend.first.substr(0,1),rainTrend.second}); break;
            case TemperatureModule:
                item(L"TRACK °C",degrees(f.trackTemperature),white,trackTrend.first.empty()?Trend{}:Trend{trackTrend.first.substr(0,1),trackTrend.second});
                item(L"AIR °C",degrees(f.airTemperature),white,airTrend.first.empty()?Trend{}:Trend{airTrend.first.substr(0,1),airTrend.second});
                break;
            case WindModule: item(L"WIND",speed+L" km/h"+(side.empty()?(from.empty()?L"":L" from "+from):L" · "+side),Ink::Yellow,{},2); break;
            default: break;
            }
            y+=50;
        } else {
            // A value with its trend right after it.
            auto valueWithTrend=[&](int x,int top,int h,std::wstring value,FontId font,COLORREF ink,Trend extra,int right) {
                const int w=measure(value,font); label(std::move(value),{px(x),px(top),std::min(px(x)+w+1,px(right)),px(top+h)},ink,font);
                if(!extra.first.empty()) label(std::move(extra.first),{px(x)+w+px(6),px(top),px(right),px(top+h)},extra.second,Small);
            };
            for(int id:o.weather.order) if(on[id]) {
                if(id==ConditionModule) {
                    if(y>32) fill(rect(0,y,W,1),Ink::Line);
                    label(L"TRACK CONDITION",rect(16,y+10,W/2,16),Ink::Label,Small);
                    if(!wetTrend.first.empty()) label(wetTrend.first,rect(W/2,y+10,W/2-16,16),wetTrend.second,Small,DT_RIGHT);
                    label(WetnessNames[wet],rect(16,y+26,W-32,30),wet>=3?wetInk:white,Hero);
                    // iRacing's seven levels; the track is wet from "very lightly wet" on.
                    const int segment=(W-32-6*3)/7;
                    for(int i=0;i<7;++i) fill(rect(16+i*(segment+3),y+58,segment,7),i<wet?(wet>=3?RGB(79,155,232):Ink::Value):Ink::LapAhead);
                    label(L"Dry",rect(16,y+66,100,16),Ink::Label,Small); label(L"Extremely wet",rect(W-116,y+66,100,16),Ink::Label,Small,DT_RIGHT);
                    y+=84;
                }
                if(id==RainModule) {
                    fill(rect(0,y,W,1),Ink::Line);
                    label(L"PRECIPITATION NOW",rect(16,y,W/2,36),Ink::Label,Small);
                    // Right-aligned: the value, then its trend.
                    const std::wstring value=percent(f.precipitation); const int valueWidth=measure(value,Bold), trendWidth=rainTrend.first.empty()?0:measure(rainTrend.first,Small)+px(6);
                    const int left=px(W-16)-trendWidth-valueWidth;
                    label(value,{left,px(y),left+valueWidth+1,px(y+36)},white,Bold);
                    if(trendWidth) label(rainTrend.first,{left+valueWidth+px(6),px(y),px(W-16),px(y+36)},rainTrend.second,Small);
                    y+=36;
                }
                if(id==TemperatureModule) {
                    fill(rect(0,y,W,1),Ink::Line); fill(rect(W/2,y,1,62),Ink::Line);
                    label(L"TRACK",rect(16,y+9,W/2-32,16),Ink::Label,Small); valueWithTrend(16,y+26,28,degrees(f.trackTemperature),Hero,white,trackTrend,W/2-8);
                    label(L"AIR",rect(W/2+16,y+9,W/2-32,16),Ink::Label,Small); valueWithTrend(W/2+16,y+26,28,degrees(f.airTemperature),Hero,white,airTrend,W-8);
                    y+=62;
                }
                if(id==TemperatureHistoryModule) {
                    fill(rect(0,y,W,1),Ink::Line);
                    const int minutes=history.Count()>1?static_cast<int>(std::lround((history.At(history.Count()-1).time-history.At(0).time)/60)):0;
                    label(L"TRACK TEMP  ·  LAST "+std::to_wstring(std::clamp(minutes,1,20))+L" MIN",rect(16,y+8,W-32,16),Ink::Label,Small);
                    double low=1e9,high=-1e9; int count=0;
                    for(int i=0;i<history.Count();++i) if(const double t=history.At(i).track; std::isfinite(t)) { low=std::min(low,t); high=std::max(high,t); ++count; }
                    if(count<2) label(L"Collecting the first minutes …",rect(16,y+28,W-32,36),Ink::Muted,Regular,DT_CENTER);
                    else {
                        // At least 1 °C of height, so sensor noise stays flat.
                        const double span=std::max(1.0,high-low), base=(low+high)/2-span/2; std::wstring points;
                        for(int i=0;i<history.Count();++i) if(const double t=history.At(i).track; std::isfinite(t)) points+=static_cast<wchar_t>(1+std::lround(1000*(t-base)/span));
                        out.push_back({DrawCommand::Sparkline,Regular,0,{px(18),px(y+30)-titleOffset,px(W-64),px(y+64)-titleOffset},Ink::Wet,{std::max(1.f,2.f*px(100)/100.f)},std::move(points)});
                        label(degrees(high),rect(W-60,y+24,44,16),Ink::Label,Small,DT_RIGHT); label(degrees(low),rect(W-60,y+54,44,16),Ink::Label,Small,DT_RIGHT);
                    }
                    y+=76;
                }
                if(id==WindModule && (on[WindRose] || on[WindText])) {
                    if(y>32) fill(rect(0,y,W,1),Ink::Line);
                    int text=16, top=y+10;
                    if(on[WindRose]) {
                        // Today's wind ring at 40 px radius: the arrow shows where the wind comes from relative to the car.
                        const float scale=static_cast<float>(dpi*o.place.scale)/9600, ring=scale*40/65, centerX=62*scale, centerY=(y+54)*scale-titleOffset, radius=40*scale;
                        const RECT bounds{static_cast<LONG>(centerX-radius-8*ring)-1,static_cast<LONG>(centerY-radius-8*ring)-1,static_cast<LONG>(centerX+radius+8*ring)+2,static_cast<LONG>(centerY+radius+8*ring)+2};
                        out.push_back({DrawCommand::WindRing,Regular,0,bounds,0,{centerX,centerY,radius,ring,std::isfinite(angle)?static_cast<float>(angle):-1.f}});
                        label(speed,rect(22,y+32,80,30),white,Hero,DT_CENTER); label(L"km/h",rect(22,y+60,80,14),Ink::Label,Small,DT_CENTER);
                        text=124; top=y+20;
                    }
                    if(on[WindText]) {
                        label(L"WIND",rect(text,top,W-text-16,16),Ink::Label,Small);
                        valueWithTrend(text,top+18,26,speed+L" km/h",Bold,white,from.empty()?Trend{}:Trend{L"from "+from,Ink::Muted},W-16);
                        if(!side.empty()) label(side,rect(text,top+44,W-text-16,22),Ink::Yellow,Regular);
                    }
                    y+=on[WindRose]?108:76;
                }
                if(id==DetailsModule) {
                    fill(rect(0,y,W,32),Ink::Band); fill(rect(0,y,W,1),Ink::Line);
                    static constexpr const wchar_t* Skies[]={L"clear",L"partly cloudy",L"mostly cloudy",L"overcast"};
                    const std::wstring parts[]={L"Humidity ",percent(f.humidity),L"Sky ",f.skies>=0 && f.skies<4?Skies[f.skies]:L"—",L"Fog ",percent(f.fog)};
                    int x=px(16);
                    for(int i=0;i<6;++i) { const FontId font=i%2?Bold:Regular; const int w=measure(parts[i],font); label(parts[i],{x,px(y),std::min(x+w+1,px(W-8)),px(y+32)},i%2?white:Ink::Label,font); x+=w+(i%2?px(18):0); }
                    y+=32;
                }
                if(id==UsageModule) {
                    fill(rect(0,y,W,1),Ink::Line);
                    std::wstring usage=Wide(app.session.trackUsage); if(usage.empty()) usage=L"—"; else usage[0]=static_cast<wchar_t>(towupper(usage[0]));
                    label(L"TRACK USAGE",rect(16,y,W/2,32),Ink::Label,Small); label(std::move(usage),rect(W/2,y,W/2-16,32),white,Regular,DT_RIGHT);
                    y+=32;
                }
            }
        }
        footer(y);
    } else {
        const bool known=app.frame.limiterKnown;
        fill({0,px(32),width,px(90)},!known?RGB(95,100,110):(app.frame.limiter?RGB(27,135,68):RGB(182,41,44)));
        label(!known?L"PIT LIMITER STATUS UNKNOWN":(app.frame.limiter?L"PIT LIMITER ACTIVE":L"PIT LIMITER NOT ACTIVE"),{px(12),px(38),width-px(12),px(84)},RGB(255,255,255),Bold,DT_CENTER);
        footer(90);
    }
    // The 1 px border, last so it stays on top; the window region rounds the same corners. The Pit Limiter's color needs none.
    if(o.kind!=PitLimiterKind && !MapOnly(o)) out.push_back({DrawCommand::Outline,Regular,0,{0,0,width,height},Ink::Border,{static_cast<float>(o.place.rounded?px(CornerRadius):0)}});
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
// The logo atlas (resource 102) is decoded only while logos are missing at some overlay's size, then released.
struct LogoAtlas { IStream* stream=nullptr; Gdiplus::Bitmap* bitmap=nullptr; } logoAtlas;
void ReleaseLogoAtlas() { delete logoAtlas.bitmap; if(logoAtlas.stream) logoAtlas.stream->Release(); logoAtlas={}; }
HBITMAP LogoBitmap(Overlay& o,int index,int size,COLORREF tint) {
    for(const auto& logo:o.logos) if(logo.index==index && logo.size==size) return logo.bitmap;
    if(!logoAtlas.bitmap) {
        const HRSRC resource=FindResourceW(app.instance,MAKEINTRESOURCEW(102),RT_RCDATA);
        const HGLOBAL data=resource?LoadResource(app.instance,resource):nullptr;
        if(!data) return nullptr;
        logoAtlas.stream=SHCreateMemStream(static_cast<const BYTE*>(LockResource(data)),SizeofResource(app.instance,resource));
        if(logoAtlas.stream) logoAtlas.bitmap=Gdiplus::Bitmap::FromStream(logoAtlas.stream);
        if(!logoAtlas.bitmap || logoAtlas.bitmap->GetLastStatus()!=Gdiplus::Ok) { ReleaseLogoAtlas(); return nullptr; }
    }
    BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB};
    void* bits=nullptr; HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap) return nullptr;
    {
        Gdiplus::Bitmap target(size,size,size*4,PixelFormat32bppPARGB,static_cast<BYTE*>(bits));
        Gdiplus::Graphics graphics(&target); graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage(logoAtlas.bitmap,Gdiplus::Rect(0,0,size,size),index%BrandLogoColumns*BrandLogoCell,index/BrandLogoColumns*BrandLogoCell,BrandLogoCell,BrandLogoCell,Gdiplus::UnitPixel);
    }
    // The logos are white: their alpha alone, premultiplied with the tint.
    auto* pixel=static_cast<uint32_t*>(bits);
    for(int i=0;i<size*size;++i) { const uint32_t a=pixel[i]>>24; pixel[i]=a<<24|(GetRValue(tint)*a/255)<<16|(GetGValue(tint)*a/255)<<8|GetBValue(tint)*a/255; }
    o.logos.push_back({index,size,bitmap}); return bitmap;
}
void PaintScene(Overlay& o,HRGN dirty) {
    HDC dc=o.dc;
    SelectClipRgn(dc,dirty);
    int font=-1;
    for(const auto& c:o.scene) {
        if(dirty && !RectInRegion(dirty,&c.r)) continue;
        if(o.canvasPending && c.type!=DrawCommand::MapCar) FlushMapCanvas(o);
        switch(c.type) {
        case DrawCommand::Fill: SetDCBrushColor(dc,c.color); FillRect(dc,&c.r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); break;
        case DrawCommand::Text: if(font!=c.font) { SelectObject(dc,o.fonts[c.font]); font=c.font; } DrawLabel(dc,c); break;
        case DrawCommand::DottedLine: {
            HPEN pen=CreatePen(PS_DASH,1,c.color); const auto old=SelectObject(dc,pen);
            MoveToEx(dc,c.r.left,c.r.top,nullptr); LineTo(dc,c.r.right,c.r.top); SelectObject(dc,old); DeleteObject(pen); break;
        }
        case DrawCommand::WindRing: DrawWind(dc,c); break;
        case DrawCommand::Map: PaintMap(o,c); break;
        case DrawCommand::MapCar: PaintMapCar(o,c); break;
        case DrawCommand::Ring: {
            // An antialiased ring around a letter centered on its ink: cap height and glyph box, not the text cell.
            const float stroke=c.shape[0], w=static_cast<float>(c.r.right-c.r.left), h=static_cast<float>(c.r.bottom-c.r.top);
            {
                Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(c.color),GetGValue(c.color),GetBValue(c.color)),stroke);
                graphics.DrawEllipse(&pen,c.r.left+stroke/2,c.r.top+stroke/2,w-stroke,h-stroke);
            }
            if(c.text.empty()) break;
            if(font!=c.font) { SelectObject(dc,o.fonts[c.font]); font=c.font; }
            OUTLINETEXTMETRICW metrics{}; metrics.otmSize=sizeof(metrics); GetOutlineTextMetricsW(dc,sizeof(metrics),&metrics);
            ABC glyph{}; GetCharABCWidthsW(dc,c.text[0],c.text[0],&glyph);
            const int x=static_cast<int>(std::lround(c.r.left+w/2-glyph.abcB/2.f-glyph.abcA));
            const int y=static_cast<int>(std::lround(c.r.top+h/2-(metrics.otmTextMetrics.tmAscent-metrics.otmsCapEmHeight/2.f)));
            SetTextColor(dc,c.color); ExtTextOutW(dc,x,y,0,nullptr,c.text.c_str(),1,nullptr); break;
        }
        case DrawCommand::Logo: {
            const int size=c.r.right-c.r.left;
            if(HBITMAP logo=LogoBitmap(o,static_cast<int>(c.shape[0]),size,c.color)) {
                HDC source=CreateCompatibleDC(dc); const auto old=SelectObject(source,logo);
                AlphaBlend(dc,c.r.left,c.r.top,size,size,source,0,0,size,size,{AC_SRC_OVER,0,255,AC_SRC_ALPHA});
                SelectObject(source,old); DeleteDC(source);
            }
            break;
        }
        case DrawCommand::Sparkline: {
            const int count=static_cast<int>(c.text.size()); if(count<2) break;
            std::vector<Gdiplus::PointF> points(count); const float w=static_cast<float>(c.r.right-c.r.left), h=static_cast<float>(c.r.bottom-c.r.top);
            for(int i=0;i<count;++i) points[i]={c.r.left+w*i/(count-1),c.r.bottom-h*(c.text[i]-1)/1000.f};
            Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::Color ink(255,GetRValue(c.color),GetGValue(c.color),GetBValue(c.color));
            Gdiplus::Pen pen(ink,c.shape[0]); pen.SetLineJoin(Gdiplus::LineJoinRound); graphics.DrawLines(&pen,points.data(),count);
            Gdiplus::SolidBrush dot(ink); const float r=c.shape[0]*1.5f; graphics.FillEllipse(&dot,points.back().X-r,points.back().Y-r,2*r,2*r); break;
        }
        case DrawCommand::Hatch: {
            HBRUSH brush=CreateHatchBrush(HS_BDIAGONAL,c.color);
            SetBkMode(dc,OPAQUE); SetBkColor(dc,static_cast<COLORREF>(c.shape[0])); FillRect(dc,&c.r,brush); SetBkMode(dc,TRANSPARENT); DeleteObject(brush); break;
        }
        case DrawCommand::RoundFill: {
            const int diameter=2*static_cast<int>(c.shape[0]);
            SelectObject(dc,GetStockObject(NULL_PEN)); SelectObject(dc,GetStockObject(DC_BRUSH)); SetDCBrushColor(dc,c.color);
            RoundRect(dc,c.r.left,c.r.top,c.r.right+1,c.r.bottom+1,diameter,diameter); break;
        }
        case DrawCommand::Outline: {
            const int diameter=2*static_cast<int>(c.shape[0]), thick=std::max(1,static_cast<int>(c.shape[1]));
            const auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            if(thick==1 && c.shape[2]==0) {
                SelectObject(dc,GetStockObject(DC_PEN)); SetDCPenColor(dc,c.color);
                RoundRect(dc,c.r.left,c.r.top,c.r.right,c.r.bottom,diameter,diameter);
            } else {
                // A wider line runs inside the edge; dashes are three line widths long.
                LOGBRUSH ink{BS_SOLID,c.color,0}; const DWORD dashes[]={static_cast<DWORD>(3*thick),static_cast<DWORD>(2*thick)};
                HPEN pen=ExtCreatePen(PS_GEOMETRIC|(c.shape[2]!=0?PS_USERSTYLE:PS_SOLID)|PS_ENDCAP_FLAT|PS_JOIN_ROUND,thick,&ink,c.shape[2]!=0?2:0,c.shape[2]!=0?dashes:nullptr);
                const auto old=SelectObject(dc,pen); const int inset=thick/2;
                RoundRect(dc,c.r.left+inset,c.r.top+inset,c.r.right-inset+(thick%2?0:1),c.r.bottom-inset+(thick%2?0:1),diameter,diameter);
                SelectObject(dc,old); DeleteObject(pen);
            }
            SelectObject(dc,brush); break;
        }
        }
    }
    if(o.canvasPending) FlushMapCanvas(o);
    SelectClipRgn(dc,nullptr);
    if(logoAtlas.bitmap) ReleaseLogoAtlas();
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
    o.fonts={font(size,FW_NORMAL),font(size,FW_SEMIBOLD),font(scaled(32),FW_SEMIBOLD),font(std::max(7,scaled(11)),FW_NORMAL),font(scaled(22),FW_SEMIBOLD),font(std::max(7,scaled(11)),FW_BOLD)};
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
    // The buffer comes first: building the scene measures text with its fonts.
    const bool resized=EnsureBuffer(o,client.right,client.bottom,dpi);
    BuildScene(o,client.right,client.bottom,dpi,app.sceneBuffer);
    const bool full=resized || app.sceneBuffer.size()!=o.scene.size();
    HRGN dirty=nullptr;
    if(!full) {
        // The old and new areas of what changed. Many small areas, such as cars moving on the track map,
        // cost more to combine and to clip against than repainting the whole scene once.
        constexpr size_t MaxChanges=24; size_t changed[MaxChanges]; size_t count=0; bool many=false;
        for(size_t i=0;i<app.sceneBuffer.size() && !many;++i) if(!(app.sceneBuffer[i]==o.scene[i])) { if(count==MaxChanges) many=true; else changed[count++]=i; }
        if(!count) return;
        if(!many) for(size_t n=0;n<count;++n) for(const RECT* r:{&app.sceneBuffer[changed[n]].r,&o.scene[changed[n]].r}) {
            HRGN part=CreateRectRgnIndirect(r);
            if(!dirty) dirty=part; else { CombineRgn(dirty,dirty,part,RGN_OR); DeleteObject(part); }
        }
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
    o.place.x=x+(o.place.growLeft?MulDiv(OverlayWidth(o),dpi*o.place.scale,9600):0);
    o.place.y=y+(o.place.growUp?MulDiv(BaseHeight(o),dpi*o.place.scale,9600):0);
}
void ApplyOpacity(const Overlay& o) { SetLayeredWindowAttributes(o.window,MapOnly(o)?TrackMapTransparentKey:0,static_cast<BYTE>(o.place.opacity*255/100),LWA_ALPHA|(MapOnly(o)?LWA_COLORKEY:0)); }
void Fit(Overlay& o,bool recover=false) {
    InvalidateModel();
    const int dpi=GetDpiForWindow(o.window);
    const int w=MulDiv(OverlayWidth(o),dpi*o.place.scale,9600),h=MulDiv(BaseHeight(o),dpi*o.place.scale,9600);
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
    ApplyOpacity(o);
    const int diameter=2*MulDiv(CornerRadius,dpi*o.place.scale,9600);
    SetWindowRgn(o.window,o.place.rounded?CreateRoundRectRgn(0,0,w+1,h+1,diameter,diameter):nullptr,TRUE);
}
void CreateOverlayWindow(Overlay& o) {
    CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"RaceMate.Overlay",Name(o).c_str(),WS_POPUP,o.place.x,o.place.y,365,200,nullptr,nullptr,app.instance,&o);
    Fit(o,true);
}
// A ring just outside an overlay marks it: red while its settings are open, dashed yellow while overlays can be moved.
// It is a window of its own, so the overlay keeps its size and content.
enum FrameStyle { NoFrame, EditingFrame, MovingFrame };
constexpr int FrameWidth=3;
int FrameStyleOf(const Overlay& o) { return !o.window || !IsWindowVisible(o.window)?NoFrame:(Editing(o)?EditingFrame:(app.edit?MovingFrame:NoFrame)); }
void PaintFrame(HWND w,HDC dc) {
    const auto* o=reinterpret_cast<const Overlay*>(GetWindowLongPtrW(w,GWLP_USERDATA)); if(!o) return;
    RECT r{}; GetClientRect(w,&r); const int thick=MulDiv(FrameWidth,GetDpiForWindow(w),96), style=o->frameState/2;
    const int diameter=o->place.rounded?2*(MulDiv(CornerRadius,GetDpiForWindow(o->window)*o->place.scale,9600)+thick):0;
    SetDCBrushColor(dc,Ink::Panel); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH))); // The gaps between dashes.
    LOGBRUSH ink{BS_SOLID,style==EditingFrame?RGB(235,64,52):Ink::Yellow,0}; const DWORD dashes[]={static_cast<DWORD>(3*thick),static_cast<DWORD>(2*thick)};
    const bool dashed=style==MovingFrame;
    HPEN pen=ExtCreatePen(PS_GEOMETRIC|(dashed?PS_USERSTYLE:PS_SOLID)|PS_ENDCAP_FLAT|PS_JOIN_ROUND,thick,&ink,dashed?2:0,dashed?dashes:nullptr);
    const auto oldPen=SelectObject(dc,pen); const auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH)); const int inset=thick/2;
    RoundRect(dc,r.left+inset,r.top+inset,r.right-inset+(thick%2?0:1),r.bottom-inset+(thick%2?0:1),diameter,diameter);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
}
LRESULT CALLBACK FrameProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    switch(m) {
    case WM_PAINT: { PAINTSTRUCT p{}; HDC dc=BeginPaint(w,&p); PaintFrame(w,dc); EndPaint(w,&p); return 0; }
    case WM_PRINTCLIENT: PaintFrame(w,reinterpret_cast<HDC>(wp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(w,m,wp,lp);
}
// Shows, moves or hides an overlay's ring to match its state, place, size and corners.
void UpdateFrame(Overlay& o) {
    const int style=FrameStyleOf(o);
    if(style==NoFrame) { if(o.frame && IsWindowVisible(o.frame)) ShowWindow(o.frame,SW_HIDE); o.frameState=-1; return; }
    if(!o.frame) {
        o.frame=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"RaceMate.Frame",L"",WS_POPUP,0,0,0,0,nullptr,nullptr,app.instance,nullptr);
        SetWindowLongPtrW(o.frame,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&o)); SetLayeredWindowAttributes(o.frame,0,255,LWA_ALPHA);
    }
    RECT r{}; GetWindowRect(o.window,&r); const int thick=MulDiv(FrameWidth,GetDpiForWindow(o.window),96); InflateRect(&r,thick,thick);
    const int state=style*2+o.place.rounded;
    if(state==o.frameState && EqualRect(&r,&o.frameRect) && IsWindowVisible(o.frame)) return;
    // Only the ring between the overlay's edge and the frame's outer edge belongs to the window.
    const int w=r.right-r.left, h=r.bottom-r.top, inner=o.place.rounded?2*MulDiv(CornerRadius,GetDpiForWindow(o.window)*o.place.scale,9600):0;
    HRGN ring=CreateRoundRectRgn(0,0,w+1,h+1,inner?inner+2*thick:0,inner?inner+2*thick:0), hole=CreateRoundRectRgn(thick,thick,w-thick+1,h-thick+1,inner,inner);
    CombineRgn(ring,ring,hole,RGN_DIFF); DeleteObject(hole); SetWindowRgn(o.frame,ring,FALSE);
    o.frameState=state; o.frameRect=r;
    SetWindowPos(o.frame,HWND_TOPMOST,r.left,r.top,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW); InvalidateRect(o.frame,nullptr,FALSE);
}
// Whether a refresh is due on a timer tick at now. The deadline moves on by whole periods, so the rate
// holds on any timer: an earlier deadline from the tick time would drift (10 Hz on a 66 ms timer gave
// 7.6 Hz). Half a timer period of slack keeps an overlay at the timer's rate from skipping ticks;
// after a pause longer than a period the phase restarts instead of catching up.
bool RefreshDue(ULONGLONG& due,ULONGLONG now,int period,int slack) {
    if(now+slack<due) return false;
    due=due+period>now?due+period:now+period;
    return true;
}
// Ticks refresh each overlay at its own rate; everything else (settings, hide, diagnostics) forces a refresh.
void UpdateOverlays(bool force=true) {
    if(!app.overlayClass) return;
    UpdateTrackMap();
    InvalidateModel();
    const bool pitVisible=app.pitVisibility.Update(app.session,app.frame);
    for(auto& o:app.overlays) {
        bool visible=o.place.enabled && !app.hidden && (o.kind!=WeatherKind || std::any_of(o.weather.display.begin(),o.weather.display.begin()+WeatherPanelCount,[](bool value){return value;})) && (app.preview || (app.frame.connected && (o.kind!=PitLimiterKind || pitVisible)));
        auto style=GetWindowLongPtrW(o.window,GWL_EXSTYLE);
        const auto desired=app.edit?(style&~WS_EX_TRANSPARENT):(style|WS_EX_TRANSPARENT);
        if(style!=desired) SetWindowLongPtrW(o.window,GWL_EXSTYLE,desired);
        if(visible && !IsWindowVisible(o.window)) { ShowWindow(o.window,SW_SHOWNOACTIVATE); o.due=0; }
        if(!visible && IsWindowVisible(o.window)) ShowWindow(o.window,SW_HIDE);
        UpdateFrame(o);
        if(visible) {
            const int dpi=GetDpiForWindow(o.window);
            RECT client{}; GetClientRect(o.window,&client);
            if(force) o.due=app.now+1000/o.place.rate;
            else if(!RefreshDue(o.due,app.now,1000/o.place.rate,app.timerMs/2)) continue;
            if(client.bottom!=MulDiv(BaseHeight(o),dpi*o.place.scale,9600) || client.right!=MulDiv(OverlayWidth(o),dpi*o.place.scale,9600)) { Fit(o); InvalidateModel(); }
            RefreshOverlay(o);
        }
    }
}
// Telemetry is read at the chosen rate, or faster when an enabled overlay refreshes faster.
int ReadRate() {
    int rate=app.rate;
    for(const auto& o:app.overlays) if(o.place.enabled) rate=std::max(rate,o.place.rate);
    return rate;
}
void Timer() {
    const int ms=app.preview || app.frame.connected?1000/ReadRate():1000;
    if(app.timerMs!=ms) { KillTimer(app.main,1); SetTimer(app.main,1,ms,nullptr); app.timerMs=ms; }
}
// Sample weather for the preview and diagnostics: light rain, the track cooling over the last 20 minutes.
void PreviewWeather() {
    auto& f=app.frame; f.trackWetness=4; f.precipitation=.35; f.humidity=.82; f.fog=0; f.skies=3; f.declaredWet=true;
    if(app.weatherHistory.Count()) return;
    Frame sample=f; sample.replay=false; sample.connected=true;
    for(int i=0;i<WeatherHistory::Size;++i) {
        sample.time=f.time-1170+i*30; sample.trackTemperature=34.3-1.9*i/39; sample.precipitation=.25+.1*i/39; sample.trackWetness=i<24?3:4;
        app.weatherHistory.Update(app.session,sample);
    }
}
void PreviewData(double time) {
    app.session=DemoSession(); app.frame=DemoFrame(time); app.frame.pit=false;
    for(auto& d:app.session.drivers) { d.license="A 3.45"; d.licenseColor=0x55aaff; auto& c=app.frame.cars[d.index]; c.tireCompound=d.index%2; c.lastPitLap=8; c.lastPitDuration=42.3; c.lastPitStationary=21.6; }
    app.frame.cars[1].outLap=true; app.frame.cars[2].pit=true; app.frame.cars[2].pitStationary=8.4; app.frame.cars[5].surface=-1;
    for(int i=0;i<10;++i) { auto& c=app.frame.cars[i]; c.stintLaps=i==2?0:(i==6?-1:3+i); c.stops=i==6?-1:1; }
    // Without it every car behind you is also behind on track, leaving the Relative's top row empty.
    auto& lapped=app.frame.cars[9]; lapped.lap=7; lapped.pct=app.frame.cars[3].pct+.12; lapped.estTime=lapped.pct*138.2;
    PreviewWeather();
    for(auto& driver:app.session.drivers) if(driver.index>=6) {
        driver.classId=2; driver.className="GT4"; driver.classColor=0x67b7ff;
        app.frame.cars[driver.index].classPosition=driver.index-5;
    }
}
void UpdateStatus();
void RefreshPage();
void PollUpdates();
// Posted to the main window when the banner and the current page should show new state.
constexpr UINT RefreshUiMessage=WM_APP+2;
void ProcessFrame(bool connected) {
    if(!connected) { app.frame={}; app.events.Update(app.session,app.frame); app.session={}; app.pitLane.Reset(); app.refuel.Reset(); app.fuel.Reset(); app.predictor.Reset(); app.pitHistory.Reset(); app.weatherHistory.Reset(); }
    else { app.pitHistory.Update(app.session,app.frame); UpdatePitStop(); app.events.Update(app.session,app.frame); app.fuel.Update(app.session,app.frame,true); app.weatherHistory.Update(app.session,app.frame); app.predictor.Update(app.session,app.frame); }
}
void Tick() {
    app.now=GetTickCount64();
    if(app.preview) PreviewData(app.now/1000.0);
    else ProcessFrame(app.telemetry.Read(app.frame,app.session));
    UpdateStatus(); UpdateOverlays(false); UpdateRaceControl(); PollUpdates(); Timer();
}
void ShowSettings() { ShowWindow(app.main,IsIconic(app.main)?SW_RESTORE:SW_SHOW); SetForegroundWindow(app.main); UpdateOverlays(); }
void TogglePreview() { app.preview=!app.preview; app.pitLane.Reset(); app.refuel.Reset(); app.frame={}; app.session={}; app.weatherHistory.Reset(); app.fuel.Reset(); app.predictor.Reset(); app.pitHistory.Reset(); app.telemetry.Close(); app.events.Reset(); Tick(); RefreshPage(); }
void ToggleEdit() { app.edit=!app.edit; Tick(); UpdateOverlays(); RefreshPage(); }
void ToggleHidden() { app.hidden=!app.hidden; UpdateOverlays(); RefreshPage(); }
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
    case WM_WINDOWPOSCHANGED: UpdateFrame(*o); break; // The ring follows while the overlay is dragged or resized.
    case WM_DPICHANGED: Fit(*o); return 0;
    case WM_DISPLAYCHANGE: Fit(*o,true); return 0;
    case WM_DESTROY: o->Free(); if(o->frame) DestroyWindow(o->frame); o->frame=nullptr; return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
// Global shortcuts would swallow the keys being assigned, so release them while a shortcut field has focus.
LRESULT CALLBACK HotkeyFieldProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(m==WM_SETFOCUS && app.hotkeysEnabled) UnregisterHotkeys();
    if(m==WM_KILLFOCUS) { RegisterHotkeys(); PostMessageW(app.main,RefreshUiMessage,0,0); }
    if(m==WM_NCDESTROY) RemoveWindowSubclass(w,HotkeyFieldProc,id);
    return DefSubclassProc(w,m,wp,lp);
}
#include "Settings.inc"
#include "RaceControl.inc"
#include "MainWindow.inc"
#include "Updates.inc"
#include "OverlaysPage.inc"
#include "Bands.inc"
#include "TablePage.inc"
#include "ElementsPage.inc"
#include "TrackMapPage.inc"
#include "RaceControlPage.inc"
#include "GeneralPage.inc"
void TrayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"Open"); SetMenuDefaultItem(menu,1,FALSE); // Bold, like TPMate.
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
        ShowWindow(w,SW_HIDE); UpdateOverlays(); return 0;
    case WM_SIZE:
        // The edited overlay's red border shows only while the window does.
        if(wp==SIZE_MINIMIZED) { if(app.trayAvailable || app.diagnostics) ShowWindow(w,SW_HIDE); UpdateOverlays(); }
        else { LayoutMain(); if(wp==SIZE_MAXIMIZED) { RememberWindowPlacement(); Save(); } }
        return 0;
    case WM_EXITSIZEMOVE: RememberWindowPlacement(); Save(); return 0;
    case WM_GETMINMAXINFO: {
        const UINT dpi=GetDpiForWindow(w); RECT minimum{0,0,MulDiv(900,dpi,96),MulDiv(600,dpi,96)};
        AdjustWindowRectExForDpi(&minimum,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_CONTROLPARENT,dpi);
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top}; return 0;
    }
    case TrayMessage: if(lp==WM_LBUTTONDBLCLK || lp==WM_LBUTTONUP) ShowSettings(); else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) TrayMenu(); return 0;
    case WM_HOTKEY: if(wp==1) ToggleEdit(); if(wp==2) ToggleHidden(); if(wp==3) ShowSettings(); return 0;
    case RefreshUiMessage: UpdateStatus(); RefreshPage(); return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case NavOverlaysId: ShowPage(Page::Overlays); break;
        case NavSettingsId: ShowPage(Page::Settings); break;
        case NavRaceControlId: ShowPage(Page::RaceControl); break;
        case StatusButtonId: StatusButton(); break;
        case BackId: ShowPage(Page::Overlays); break;
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        const HDC dc=reinterpret_cast<HDC>(wp);
        SetBkColor(dc,Background); SetTextColor(dc,reinterpret_cast<HWND>(lp)==ui.hint?RGB(110,112,118):Text); return reinterpret_cast<LRESULT>(app.brush);
    }
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(w,&r); FillRect(reinterpret_cast<HDC>(wp),&r,app.brush); return 1; }
    case WM_DPICHANGED: {
        const auto* r=reinterpret_cast<RECT*>(lp); SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        ApplyUiDpi(); return 0;
    }
    case WM_DESTROY:
        trackMap.task.Cancel();
        if(!app.diagnostics) { RememberWindowPlacement(); Save(); }
        CloseRaceControl();
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
    // A copy of RaceMate started by the updater swaps in the new version and exits.
    if(const int update=UpdateChecker::HandleSelfUpdateCommandLine(); update>=0) return update;
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
        // One instance per kind with known settings, as the checks expect.
        for(auto it=app.overlays.begin();it!=app.overlays.end();) it=it->number>1?app.overlays.erase(it):std::next(it);
        app.rate=10; app.startWithWindows=app.startMinimized=false;
        for(auto& o:app.overlays) {
            o.weather.display.fill(true); o.bands=DefaultBands(o.kind); o.table.columnHeaders=false; o.table.rows=9; o.table.standings={}; o.fuel.figures.fill(true); o.fuel.display.fill(true);
            auto& p=o.place; p={}; p.x=DefaultPositions[o.kind].x; p.y=DefaultPositions[o.kind].y;
        }
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_HOTKEY_CLASS}; InitCommonControlsEx(&controls);
    app.brush=CreateSolidBrush(Background);
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=MainProc; cls.hInstance=instance; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); cls.hbrBackground=app.brush; cls.lpszClassName=L"RaceMate.Settings"; RegisterClassExW(&cls);
    constexpr DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
    const UINT dpi=GetDpiForSystem(); RECT size{0,0,MulDiv(1000,dpi,96),MulDiv(720,dpi,96)}; AdjustWindowRectExForDpi(&size,style,FALSE,WS_EX_CONTROLPARENT,dpi);
    app.main=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"RaceMate",style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    if(!app.main) { DeleteObject(app.brush); if(mutex) CloseHandle(mutex); return 1; }
    CreateMainUi();
    cls.lpfnWndProc=OverlayProc; cls.hbrBackground=nullptr; cls.lpszClassName=L"RaceMate.Overlay"; RegisterClassExW(&cls);
    cls.lpfnWndProc=FrameProc; cls.lpszClassName=L"RaceMate.Frame"; RegisterClassExW(&cls);
    app.overlayClass=true;
    for(auto& o:app.overlays) CreateOverlayWindow(o);
#ifdef RACEMATE_DIAGNOSTICS
    if(diagnostics) return RunDiagnostics();
    if(wcsstr(args,L"--benchmark-tick")) return RunTickBenchmark();
#endif
    if(benchmark) {
        app.hidden=true; Tick(); SetTimer(app.main,2,10000,nullptr);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
        DeleteObject(app.brush); return 0;
    }
    app.tray.cbSize=sizeof(app.tray); app.tray.hWnd=app.main; app.tray.uID=1; app.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; app.tray.uCallbackMessage=TrayMessage; app.tray.hIcon=cls.hIcon; wcscpy_s(app.tray.szTip,L"RaceMate");
    app.trayAvailable=Shell_NotifyIconW(NIM_ADD,&app.tray)!=FALSE;
    if(app.startWithWindows && !RegisterStartup(true)) MessageBoxW(app.main,L"Could not refresh the Windows startup path. Select Start with Windows again to retry.",L"RaceMate",MB_OK|MB_ICONWARNING);
    app.hotkeysEnabled=true; RegisterHotkeys();
    Tick(); RestoreWindowPlacement(StartupShowCommand(app.trayAvailable,show)); UpdateWindow(app.main);
    if(!app.hotkeyError.empty()) ShowPage(Page::Settings);
    if(app.checkUpdates) StartUpdateCheck(true);
    MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) if(!IsDialogMessageW(app.main,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    DeleteObject(app.brush); if(mutex) CloseHandle(mutex); return 0;
}
