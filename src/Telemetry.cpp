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
    tick_ = sessionUpdate_ = sessionNumber_ = -1; variables_.clear(); data_.clear();
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
bool Telemetry::BuildVariables(const MemoryHeader& h) {
    if (h.numVars < 1 || h.numVars > 4096 || !Range(h.varsOffset, static_cast<size_t>(h.numVars)*sizeof(VariableHeader))) return false;
    variables_.clear();
    for (int i=0;i<h.numVars;++i) {
        VariableHeader v{}; std::memcpy(&v,view_ + h.varsOffset + i*sizeof(v),sizeof(v));
        constexpr size_t widths[] = {1,1,4,4,4,8};
        if (v.type < 0 || v.type > 5 || v.count < 1 || v.count > 4096 || v.offset < 0 ||
            static_cast<size_t>(v.offset) + widths[v.type]*v.count > data_.size() || !std::memchr(v.name,0,sizeof(v.name))) return false;
        variables_.emplace(v.name,v);
    }
    return true;
}
double Telemetry::Value(const char* name, double fallback, int index) const {
    const auto it = variables_.find(name);
    if (it == variables_.end() || index < 0 || index >= it->second.count) return fallback;
    const auto& v = it->second; double result = fallback;
    const auto* p = data_.data() + v.offset;
    if (v.type == 0 || v.type == 1) result = p[index];
    if (v.type == 2) { int32_t x; std::memcpy(&x,p+index*4,4); result=x; }
    if (v.type == 3) { uint32_t x; std::memcpy(&x,p+index*4,4); result=x; }
    if (v.type == 4) { float x; std::memcpy(&x,p+index*4,4); result=x; }
    if (v.type == 5) { double x; std::memcpy(&x,p+index*8,8); result=x; }
    return std::isfinite(result) ? result : fallback;
}
bool Telemetry::Read(Frame& f, Session& s) {
    if (!view_ && !Open()) return false;
    MemoryHeader h{};
    MemoryBarrier(); std::memcpy(&h,view_,sizeof(h));
    if (!(h.status & 1) || h.version < 1 || h.version > 2 || h.numBuffers < 1 || h.numBuffers > 4 ||
        h.bufferLength < 1 || h.bufferLength > 1024*1024) { Close(); return false; }
    if (data_.size() != static_cast<size_t>(h.bufferLength)) { data_.resize(h.bufferLength); variables_.clear(); }
    if (variables_.empty() && !BuildVariables(h)) { Close(); return false; }
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
    Frame next; next.connected=true;
    next.player=static_cast<int>(Value("PlayerCarIdx",-1));
    next.cameraGroup=static_cast<int>(Value("CamGroupNumber",1));
    next.replaySession=static_cast<int>(Value("ReplaySessionNum",-1)); next.replayTime=Value("ReplaySessionTime",-1);
    next.camera=static_cast<int>(Value("CamCarIdx",-1));
    next.session=static_cast<int>(Value("SessionNum",-1));
    next.onTrack=Value("IsOnTrack",0)!=0;
    next.pit=Value("OnPitRoad",0)!=0; next.inStall=Value("PlayerCarInPitStall",0)!=0;
    next.replay=Value("IsReplayPlaying",0)!=0;
    next.limiterKnown=variables_.contains("EngineWarnings");
    next.limiter=(static_cast<unsigned>(Value("EngineWarnings",0))&0x10)!=0;
    next.fuel=Value("FuelLevel",-1); next.fuelKnown=next.fuel>=0;
    next.time=Value("SessionTime",0);
    next.timeLeft=Value("SessionTimeRemain",std::numeric_limits<double>::quiet_NaN());
    next.timeLeftKnown=std::isfinite(next.timeLeft);
    if(!next.timeLeftKnown) next.timeLeft=-1;
    next.lapsLeft=Value("SessionLapsRemainEx",Value("SessionLapsRemain",-1));
    next.lapCompleted=static_cast<int>(Value("LapCompleted",-1)); next.lapPct=Value("LapDistPct",0);
    next.lastLap=Value("LapLastLapTime",-1); next.bestLap=Value("LapBestLapTime",-1);
    next.state=static_cast<int>(Value("SessionState",0)); next.flags=static_cast<unsigned>(Value("SessionFlags",0));
    const double incidents=Value("PlayerCarMyIncidentCount",-1);
    next.incidentsKnown=incidents>=0 && incidents<=std::numeric_limits<int>::max() && incidents==std::floor(incidents);
    next.airTemperature=Value("AirTemp",std::numeric_limits<double>::quiet_NaN());
    next.trackTemperature=Value("TrackTempCrew",Value("TrackTemp",std::numeric_limits<double>::quiet_NaN()));
    next.windDirection=Value("WindDir",std::numeric_limits<double>::quiet_NaN()); next.yawNorth=Value("YawNorth",std::numeric_limits<double>::quiet_NaN());
    next.timeOfDay=Value("SessionTimeOfDay",-1); next.windSpeed=Value("WindVel",-1); next.trackWetness=static_cast<int>(Value("TrackWetness",-1));
    next.incidents=next.incidentsKnown?static_cast<int>(incidents):0;
    for(int i=0;i<MaxCars;++i) {
        auto& c=next.cars[i];
        c.position=static_cast<int>(Value("CarIdxPosition",0,i)); c.classPosition=static_cast<int>(Value("CarIdxClassPosition",0,i));
        c.lap=static_cast<int>(Value("CarIdxLap",-1,i)); c.surface=static_cast<int>(Value("CarIdxTrackSurface",-1,i));
        c.sessionFlags=static_cast<unsigned>(Value("CarIdxSessionFlags",0,i));
        c.tireCompound=static_cast<int>(Value("CarIdxTireCompound",-1,i));
        c.completed=static_cast<int>(Value("CarIdxLapCompleted",-1,i));
        c.pct=Value("CarIdxLapDistPct",-1,i); c.estTime=Value("CarIdxEstTime",-1,i); c.f2Time=Value("CarIdxF2Time",-1,i);
        c.best=Value("CarIdxBestLapTime",-1,i); c.last=Value("CarIdxLastLapTime",-1,i); c.pit=Value("CarIdxOnPitRoad",0,i)!=0;
    }
    if(h.sessionUpdate!=sessionUpdate_ || next.session!=sessionNumber_) {
        if(h.sessionLength < 1 || h.sessionLength > 2*1024*1024 || !Range(h.sessionOffset,h.sessionLength)) { Close(); return false; }
        const int before=live->sessionUpdate;
        std::string yaml(reinterpret_cast<const char*>(view_+h.sessionOffset),h.sessionLength);
        MemoryBarrier();
        if(before!=h.sessionUpdate || before!=live->sessionUpdate || h.sessionOffset!=live->sessionOffset || h.sessionLength!=live->sessionLength) return f.connected;
        if(auto nul=yaml.find('\0');nul!=yaml.npos) yaml.resize(nul);
        s=ParseSession(yaml,next.session); sessionUpdate_=before; sessionNumber_=next.session;
    }
    if(next.player<0) next.player=s.player;
    if(!next.replay && next.player>=0 && next.player<MaxCars) {
        auto& player=next.cars[next.player];
        if(!variables_.contains("CarIdxSessionFlags")) player.sessionFlags=next.flags & 0x1f0000u;
        player.towingKnown=variables_.contains("PlayerCarIsTowing") || variables_.contains("PlayerCarTowTime");
        player.towing=variables_.contains("PlayerCarIsTowing")?Value("PlayerCarIsTowing",0)!=0:Value("PlayerCarTowTime",0)>0;
    }
    if(next.onTrack && !next.replay && next.player>=0 && next.player<MaxCars) {
        auto& player=next.cars[next.player]; player.speed=Value("Speed",-1);
        player.surface=static_cast<int>(Value("PlayerTrackSurface",player.surface));
    }
    f=next; return true;
}
}
