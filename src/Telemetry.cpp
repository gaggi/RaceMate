#include "Telemetry.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace rm {
bool SendIRacingMessage(const SimMessage& message, HWND target) {
    if(message.command>65535 || message.arg>65535) return false;
    const UINT id=RegisterWindowMessageW(L"IRSDK_BROADCASTMSG");
    return id && SendNotifyMessageW(target,id,static_cast<WPARAM>(message.command | (message.arg<<16)),static_cast<LPARAM>(message.data));
}

void Telemetry::Close() {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    view_ = nullptr; mapping_ = nullptr; size_ = 0;
    tick_ = sessionUpdate_ = sessionNumber_ = -1; resolved_ = false; data_.clear();
}
bool Telemetry::Range(int offset, size_t length) const {
    return offset >= 0 && static_cast<size_t>(offset) <= size_ && length <= size_ - offset;
}
bool Telemetry::Open() {
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, name_.c_str());
    if (!mapping_) return false;
    view_ = static_cast<const unsigned char*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0));
    MEMORY_BASIC_INFORMATION info{};
    if (!view_ || !VirtualQuery(view_,&info,sizeof(info))) { Close(); return false; }
    size_ = info.RegionSize;
    if (size_ < sizeof(MemoryHeader)) { Close(); return false; }
    return true;
}
namespace {
constexpr const char* VarNames[] = {
    "PlayerCarIdx","CamGroupNumber","ReplaySessionNum","ReplaySessionTime","CamCarIdx","SessionNum","IsOnTrack","OnPitRoad",
    "PlayerCarInPitStall","IsReplayPlaying","EngineWarnings","FuelLevel","SessionTime","SessionTimeRemain","SessionLapsRemainEx",
    "SessionLapsRemain","LapCompleted","LapDistPct","LapLastLapTime","LapBestLapTime","SessionState","SessionFlags",
    "PlayerCarMyIncidentCount","AirTemp","TrackTempCrew","TrackTemp","WindDir","YawNorth","SessionTimeOfDay","WindVel","TrackWetness",
    "CarIdxPosition","CarIdxClassPosition","CarIdxLap","CarIdxTrackSurface","CarIdxSessionFlags","CarIdxTireCompound",
    "CarIdxLapCompleted","CarIdxLapDistPct","CarIdxEstTime","CarIdxF2Time","CarIdxBestLapTime","CarIdxLastLapTime","CarIdxOnPitRoad",
    "PlayerCarIsTowing","PlayerCarTowTime","Speed","PlayerTrackSurface","ReplayFrameNumEnd","dcBrakeBias","SessionTimeTotal","SessionLapsTotal",
    "WeatherDeclaredWet","Precipitation","RelativeHumidity","FogLevel","Skies","PitSvFlags","PitSvFuel","PitRepairLeft",
    "dcABS","dcTractionControl","dcTractionControl2","dcThrottleShape","dcFuelMixture","dcEngineBraking","IsGarageVisible","Brake"};
static_assert(std::size(VarNames) == Telemetry::VarCount);
}
bool Telemetry::BuildVariables(const MemoryHeader& h) {
    if (h.numVars < 1 || h.numVars > 4096 || !Range(h.varsOffset, static_cast<size_t>(h.numVars)*sizeof(VariableHeader))) return false;
    vars_ = {};
    for (int i=0;i<h.numVars;++i) {
        VariableHeader v{}; std::memcpy(&v,view_ + h.varsOffset + i*sizeof(v),sizeof(v));
        constexpr size_t widths[] = {1,1,4,4,4,8};
        if (v.type < 0 || v.type > 5 || v.count < 1 || v.count > 4096 || v.offset < 0 ||
            static_cast<size_t>(v.offset) + widths[v.type]*v.count > data_.size() || !std::memchr(v.name,0,sizeof(v.name))) return false;
        for (int id=0;id<VarCount;++id) if (!vars_[id].count && std::strcmp(v.name,VarNames[id])==0) { vars_[id]=v; break; }
    }
    resolved_ = true;
    return true;
}
double Telemetry::Value(Var id, double fallback, int index) const {
    const auto& v = vars_[id];
    if (index < 0 || index >= v.count) return fallback;
    double result = fallback;
    const auto* p = data_.data() + v.offset;
    switch (v.type) {
    case 0: case 1: result = p[index]; break;
    case 2: { int32_t x; std::memcpy(&x,p+index*4,4); result=x; break; }
    case 3: { uint32_t x; std::memcpy(&x,p+index*4,4); result=x; break; }
    case 4: { float x; std::memcpy(&x,p+index*4,4); result=x; break; }
    case 5: { double x; std::memcpy(&x,p+index*8,8); result=x; break; }
    }
    return std::isfinite(result) ? result : fallback;
}
bool Telemetry::Read(Frame& f, Session& s) {
    if (!view_ && !Open()) return false;
    MemoryHeader h{};
    MemoryBarrier(); std::memcpy(&h,view_,sizeof(h));
    if (!(h.status & 1) || h.version < 1 || h.version > 2 || h.numBuffers < 1 || h.numBuffers > 4 ||
        h.bufferLength < 1 || h.bufferLength > 1024*1024) { Close(); return false; }
    if (data_.size() != static_cast<size_t>(h.bufferLength)) { data_.resize(h.bufferLength); resolved_ = false; }
    if (!resolved_ && !BuildVariables(h)) { Close(); return false; }
    int latest=0; for(int i=1;i<h.numBuffers;++i) if(h.buffers[i].tick>h.buffers[latest].tick) latest=i;
    if (h.buffers[latest].tick == tick_ && h.sessionUpdate == sessionUpdate_) {
        // Paused replays can keep a valid connected snapshot without new ticks.
        return f.connected;
    }
    const auto* live = reinterpret_cast<const MemoryHeader*>(view_);
    bool copied=false;
    for(int attempt=0;attempt<3;++attempt) {
        MemoryBarrier();
        const int before=live->buffers[latest].tick, offset=live->buffers[latest].offset;
        if(!Range(offset,data_.size())) { Close(); return false; }
        std::memcpy(data_.data(),view_+offset,data_.size()); MemoryBarrier();
        if(before==live->buffers[latest].tick && (live->status&1)) { tick_=before; copied=true; break; }
    }
    if(!copied) return f.connected;
    Frame next; next.connected=true; next.tick=tick_; next.tickRate=h.tickRate;
    next.player=static_cast<int>(Value(PlayerCarIdx,-1));
    next.cameraGroup=static_cast<int>(Value(CamGroupNumber,1));
    next.replaySession=static_cast<int>(Value(ReplaySessionNum,-1)); next.replayTime=Value(ReplaySessionTime,-1);
    next.camera=static_cast<int>(Value(CamCarIdx,-1));
    next.session=static_cast<int>(Value(SessionNum,-1));
    next.onTrack=Value(IsOnTrack,0)!=0;
    next.pit=Value(OnPitRoad,0)!=0; next.inStall=Value(PlayerCarInPitStall,0)!=0;
    next.replay=Value(IsReplayPlaying,0)!=0; next.garage=Value(IsGarageVisible,0)!=0; next.brake=Has(Brake)?Value(Brake,-1):-1;
    next.replayFramesBehind=Has(ReplayFrameNumEnd)?static_cast<int>(Value(ReplayFrameNumEnd,-1)):-1;
    next.limiterKnown=Has(EngineWarnings);
    next.limiter=(static_cast<unsigned>(Value(EngineWarnings,0))&EngineWarning::PitSpeedLimiter)!=0;
    next.fuel=Value(FuelLevel,-1); next.fuelKnown=next.fuel>=0;
    next.time=Value(SessionTime,0);
    next.timeLeft=Value(SessionTimeRemain,std::numeric_limits<double>::quiet_NaN());
    next.timeLeftKnown=std::isfinite(next.timeLeft);
    if(!next.timeLeftKnown) next.timeLeft=-1;
    next.lapsLeft=Value(SessionLapsRemainEx,Value(SessionLapsRemain,-1));
    next.lapCompleted=static_cast<int>(Value(LapCompleted,-1)); next.lapPct=Value(LapDistPct,0);
    next.lastLap=Value(LapLastLapTime,-1); next.bestLap=Value(LapBestLapTime,-1);
    next.state=static_cast<int>(Value(SessionState,0)); next.flags=static_cast<unsigned>(Value(SessionFlags,0));
    const double incidents=Value(PlayerCarMyIncidentCount,-1);
    next.incidentsKnown=incidents>=0 && incidents<=std::numeric_limits<int>::max() && incidents==std::floor(incidents);
    next.airTemperature=Value(AirTemp,std::numeric_limits<double>::quiet_NaN());
    next.trackTemperature=Value(TrackTempCrew,Value(TrackTemp,std::numeric_limits<double>::quiet_NaN()));
    next.windDirection=Value(WindDir,std::numeric_limits<double>::quiet_NaN()); next.yawNorth=Value(YawNorth,std::numeric_limits<double>::quiet_NaN());
    next.timeOfDay=Value(SessionTimeOfDay,-1); next.windSpeed=Value(WindVel,-1); next.trackWetness=static_cast<int>(Value(TrackWetness,-1));
    next.incidents=next.incidentsKnown?static_cast<int>(incidents):0;
    next.timeTotal=Value(SessionTimeTotal,-1); next.lapsTotal=static_cast<int>(Value(SessionLapsTotal,-1));
    next.declaredWet=Value(WeatherDeclaredWet,0)!=0; next.skies=static_cast<int>(Value(Skies,-1));
    next.precipitation=Value(Precipitation,std::numeric_limits<double>::quiet_NaN()); next.humidity=Value(RelativeHumidity,std::numeric_limits<double>::quiet_NaN());
    next.fog=Value(FogLevel,std::numeric_limits<double>::quiet_NaN());
    next.pitSvFlags=static_cast<unsigned>(Value(PitSvFlags,0)); next.pitSvFuel=Value(PitSvFuel,0); next.pitRepair=Value(PitRepairLeft,0);
    // In-car adjustments, in Dial order; each car has only some.
    constexpr Var dials[]={dcBrakeBias,dcABS,dcTractionControl,dcTractionControl2,dcThrottleShape,dcFuelMixture,dcEngineBraking};
    static_assert(std::size(dials)==DialCount);
    for(int d=0;d<DialCount;++d) if(Has(dials[d])) { next.dials[d]=Value(dials[d],0); next.dialsKnown|=1u<<d; }
    // Entries beyond the published array length keep the Car defaults, which equal the fallbacks below.
    int published=0; for(int id=CarIdxPosition;id<=CarIdxOnPitRoad;++id) published=std::max(published,vars_[id].count);
    for(int i=0;i<std::min(published,MaxCars);++i) {
        auto& c=next.cars[i];
        c.position=static_cast<int>(Value(CarIdxPosition,0,i)); c.classPosition=static_cast<int>(Value(CarIdxClassPosition,0,i));
        c.lap=static_cast<int>(Value(CarIdxLap,-1,i)); c.surface=static_cast<int>(Value(CarIdxTrackSurface,-1,i));
        c.sessionFlags=static_cast<unsigned>(Value(CarIdxSessionFlags,0,i));
        c.tireCompound=static_cast<int>(Value(CarIdxTireCompound,-1,i));
        c.completed=static_cast<int>(Value(CarIdxLapCompleted,-1,i));
        c.pct=Value(CarIdxLapDistPct,-1,i); c.estTime=Value(CarIdxEstTime,-1,i); c.f2Time=Value(CarIdxF2Time,-1,i);
        c.best=Value(CarIdxBestLapTime,-1,i); c.last=Value(CarIdxLastLapTime,-1,i); c.pit=Value(CarIdxOnPitRoad,0,i)!=0;
    }
    if(h.sessionUpdate!=sessionUpdate_ || next.session!=sessionNumber_) {
        if(h.sessionLength < 1 || h.sessionLength > 2*1024*1024 || !Range(h.sessionOffset,h.sessionLength)) { Close(); return false; }
        const int before=live->sessionUpdate;
        std::string yaml(reinterpret_cast<const char*>(view_+h.sessionOffset),h.sessionLength);
        MemoryBarrier();
        if(before!=h.sessionUpdate || before!=live->sessionUpdate || h.sessionOffset!=live->sessionOffset || h.sessionLength!=live->sessionLength) return f.connected;
        if(auto nul=yaml.find('\0');nul!=yaml.npos) yaml.resize(nul);
        LARGE_INTEGER parseStart{},parseEnd{}; QueryPerformanceCounter(&parseStart);
        s=ParseSession(yaml,next.session); sessionUpdate_=before; sessionNumber_=next.session;
        QueryPerformanceCounter(&parseEnd); sessionParseTicks+=parseEnd.QuadPart-parseStart.QuadPart; ++sessionParses;
    }
    if(next.player<0) next.player=s.player;
    if(!next.replay && next.player>=0 && next.player<MaxCars) {
        auto& player=next.cars[next.player];
        if(!Has(CarIdxSessionFlags)) player.sessionFlags=next.flags & SessionFlag::DriverFlags;
        player.towingKnown=Has(PlayerCarIsTowing) || Has(PlayerCarTowTime);
        player.towing=Has(PlayerCarIsTowing)?Value(PlayerCarIsTowing,0)!=0:Value(PlayerCarTowTime,0)>0;
    }
    if(next.onTrack && !next.replay && next.player>=0 && next.player<MaxCars) {
        auto& player=next.cars[next.player]; player.speed=Value(Speed,-1);
        player.surface=static_cast<int>(Value(PlayerTrackSurface,player.surface));
    }
    f=next; return true;
}

void TelemetryPump::Start(HWND window, UINT message) {
    if (thread_) return;
    window_ = window; message_ = message; posted_ = false;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = stop_ ? CreateThread(nullptr, 0, Thread, this, 0, nullptr) : nullptr;
    if (!thread_ && stop_) { CloseHandle(stop_); stop_ = nullptr; }
}
void TelemetryPump::Stop() {
    if (!thread_) return;
    SetEvent(stop_); WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_); CloseHandle(stop_); thread_ = stop_ = nullptr;
    AcquireSRWLockExclusive(&lock_);
    frame_ = {}; session_ = {}; fresh_ = sessionFresh_ = false;
    ReleaseSRWLockExclusive(&lock_);
}
void TelemetryPump::Rename(const wchar_t* mappingName, const wchar_t* eventName) {
    Stop(); telemetry_.Rename(mappingName); eventName_ = eventName;
}
void TelemetryPump::Publish(const Frame& frame, const Session* session) {
    AcquireSRWLockExclusive(&lock_);
    frame_ = frame; fresh_ = true;
    if (session) { session_ = *session; sessionFresh_ = true; }
    ReleaseSRWLockExclusive(&lock_);
    if (!posted_.exchange(true)) PostMessageW(window_, message_, 0, 0);
}
bool TelemetryPump::Take(Frame& frame, Session& session, bool& sessionChanged) {
    // Cleared first: a frame published while this runs posts again rather than going unnoticed.
    posted_ = false;
    AcquireSRWLockExclusive(&lock_);
    const bool fresh = fresh_;
    if (fresh) { frame = frame_; fresh_ = false; }
    sessionChanged = sessionFresh_;
    if (sessionFresh_) { session = std::move(session_); session_ = {}; sessionFresh_ = false; }
    ReleaseSRWLockExclusive(&lock_);
    return fresh;
}
void TelemetryPump::Run() {
    // The work per frame is a few microseconds; reading it promptly keeps the data fresh.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HANDLE event = nullptr; Frame frame; Session session;
    bool connected = false; int lastTick = -1, parses = telemetry_.sessionParses, idle = 0;
    for (;;) {
        if (!event) event = OpenEventW(SYNCHRONIZE, FALSE, eventName_.c_str());
        const HANDLE handles[2] = {stop_, event};
        // Without the event (no iRacing) it looks once a second; with it, a missed signal costs at most 200 ms.
        const DWORD wait = WaitForMultipleObjects(event ? 2 : 1, handles, FALSE, event && connected ? 200 : 1000);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) { if (event) CloseHandle(event); event = nullptr; Sleep(100); continue; }
        const bool was = connected;
        connected = telemetry_.Read(frame, session);
        if (!connected) frame = {};
        parseTicks_ = telemetry_.sessionParseTicks; parses_ = telemetry_.sessionParses;
        const bool sessionChanged = telemetry_.sessionParses != parses; parses = telemetry_.sessionParses;
        const bool newFrame = connected && frame.tick != lastTick;
        if (newFrame || connected != was || sessionChanged) { lastTick = frame.tick; Publish(frame, sessionChanged ? &session : nullptr); }
        // An event left signalled must not keep the thread spinning.
        if (wait == WAIT_OBJECT_0 + 1 && !newFrame) { if (++idle > 3) Sleep(5); } else idle = 0;
        // iRacing makes its event anew when it starts again.
        if (!connected && event) { CloseHandle(event); event = nullptr; }
    }
    if (event) CloseHandle(event);
    telemetry_.Close();
}
}
