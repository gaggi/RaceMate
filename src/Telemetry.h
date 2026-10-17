#pragma once
#include "Core.h"
#include "RaceControl.h"
#include <windows.h>
#include <cstdint>

namespace rm {
bool SendIRacingMessage(const SimMessage&, HWND target = HWND_BROADCAST);
// Wire layout of the public iRacing shared-memory protocol, not SDK code.
struct BufferHeader { int32_t tick, offset, pad[2]; };
struct MemoryHeader {
    int32_t version, status, tickRate, sessionUpdate, sessionLength, sessionOffset;
    int32_t numVars, varsOffset, numBuffers, bufferLength, pad[2];
    BufferHeader buffers[4];
};
struct VariableHeader {
    int32_t type, offset, count;
    uint8_t countAsTime, pad[3];
    char name[32], description[64], unit[32];
};
static_assert(sizeof(MemoryHeader) == 112 && sizeof(VariableHeader) == 144);
class Telemetry {
public:
    // Variables RaceMate reads; resolved once per connection instead of looked up by name every frame.
    enum Var {
        PlayerCarIdx, CamGroupNumber, ReplaySessionNum, ReplaySessionTime, CamCarIdx, SessionNum, IsOnTrack, OnPitRoad,
        PlayerCarInPitStall, IsReplayPlaying, EngineWarnings, FuelLevel, SessionTime, SessionTimeRemain, SessionLapsRemainEx,
        SessionLapsRemain, LapCompleted, LapDistPct, LapLastLapTime, LapBestLapTime, SessionState, SessionFlags,
        PlayerCarMyIncidentCount, AirTemp, TrackTempCrew, TrackTemp, WindDir, YawNorth, SessionTimeOfDay, WindVel, TrackWetness,
        CarIdxPosition, CarIdxClassPosition, CarIdxLap, CarIdxTrackSurface, CarIdxSessionFlags, CarIdxTireCompound,
        CarIdxLapCompleted, CarIdxLapDistPct, CarIdxEstTime, CarIdxF2Time, CarIdxBestLapTime, CarIdxLastLapTime, CarIdxOnPitRoad,
        PlayerCarIsTowing, PlayerCarTowTime, Speed, PlayerTrackSurface, ReplayFrameNumEnd, dcBrakeBias, SessionTimeTotal, SessionLapsTotal,
        WeatherDeclaredWet, Precipitation, RelativeHumidity, FogLevel, Skies, PitSvFlags, PitSvFuel, PitRepairLeft,
        dcABS, dcTractionControl, dcTractionControl2, dcThrottleShape, dcFuelMixture, dcEngineBraking, IsGarageVisible, VarCount
    };
    explicit Telemetry(const wchar_t* mappingName = L"Local\\IRSDKMemMapFileName") : name_(mappingName) {}
    ~Telemetry() { Close(); }
    bool Read(Frame&, Session&);
    // Time spent parsing the session YAML, in QueryPerformanceCounter ticks, and how often; for --profile.
    long long sessionParseTicks = 0; int sessionParses = 0;
    void Close();
private:
    bool Range(int offset, size_t size) const;
    bool Open();
    bool BuildVariables(const MemoryHeader&);
    bool Has(Var v) const { return vars_[v].count > 0; }
    double Value(Var, double fallback, int index = 0) const;
    std::wstring name_;
    HANDLE mapping_ = nullptr;
    const unsigned char* view_ = nullptr;
    size_t size_ = 0;
    int tick_ = -1, sessionUpdate_ = -1, sessionNumber_ = -1;
    bool resolved_ = false;
    std::vector<unsigned char> data_;
    std::array<VariableHeader, VarCount> vars_{};
};
}
