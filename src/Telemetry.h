#pragma once
#include "Core.h"
#include "RaceControl.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

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
        dcABS, dcTractionControl, dcTractionControl2, dcThrottleShape, dcFuelMixture, dcEngineBraking, IsGarageVisible, Brake, VarCount
    };
    explicit Telemetry(const wchar_t* mappingName = L"Local\\IRSDKMemMapFileName") : name_(mappingName) {}
    ~Telemetry() { Close(); }
    bool Read(Frame&, Session&);
    // Time spent parsing the session YAML, in QueryPerformanceCounter ticks, and how often; for --profile.
    long long sessionParseTicks = 0; int sessionParses = 0;
    void Close();
    // Another shared memory, such as the benchmark's stand-in for iRacing.
    void Rename(const wchar_t* mappingName) { Close(); name_=mappingName; }
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
// Reads iRacing on a thread of its own, once per iRacing frame: iRacing signals every new frame (60 a second)
// with its data-valid event, so reading follows iRacing instead of a window timer, which only fires in steps
// of about 15.6 ms. The main thread takes the newest frame when it gets to it; frames it did not take in time
// are skipped, never queued.
class TelemetryPump {
public:
    explicit TelemetryPump(const wchar_t* mappingName = L"Local\\IRSDKMemMapFileName", const wchar_t* eventName = L"Local\\IRSDKDataValidEvent")
        : telemetry_(mappingName), eventName_(eventName) { InitializeSRWLock(&lock_); }
    ~TelemetryPump() { Stop(); }
    TelemetryPump(const TelemetryPump&) = delete;
    TelemetryPump& operator=(const TelemetryPump&) = delete;
    // Posts message to window after each new frame or change of connection; never again before Take.
    void Start(HWND window, UINT message);
    void Stop();
    bool Running() const { return thread_ != nullptr; }
    // Another shared memory and event, such as the benchmark's stand-in for iRacing; stops the thread.
    void Rename(const wchar_t* mappingName, const wchar_t* eventName);
    // The newest frame since the last Take, and the session when it changed (sessionChanged); false when
    // nothing is new. A lost connection comes as a frame that is not connected.
    bool Take(Frame&, Session&, bool& sessionChanged);
    // Session YAML parsing in QueryPerformanceCounter ticks, and how often; for --profile.
    long long ParseTicks() const { return parseTicks_.load(); }
    int Parses() const { return parses_.load(); }
private:
    static DWORD WINAPI Thread(void* self) { static_cast<TelemetryPump*>(self)->Run(); return 0; }
    void Run();
    void Publish(const Frame&, const Session*);
    Telemetry telemetry_;
    std::wstring eventName_;
    HANDLE thread_ = nullptr, stop_ = nullptr;
    HWND window_ = nullptr; UINT message_ = 0;
    SRWLOCK lock_{};
    Frame frame_; Session session_; bool fresh_ = false, sessionFresh_ = false; // Guarded by lock_.
    std::atomic_bool posted_{false};
    std::atomic<long long> parseTicks_{0}; std::atomic_int parses_{0};
};
}
