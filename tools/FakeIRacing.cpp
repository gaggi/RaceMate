// A stand-in for iRacing on this machine, for benchmarks: the SDK's shared memory and data-valid event,
// 60 cars racing in three classes, a new frame 60 times a second like iRacing. Every 10 s the spotter reports
// a car passing you on the left, two cars on the right and three wide, with a car beside you to match.
// SessionTime is QueryPerformanceCounter seconds, so a reader in another process can tell how old a frame is.
//   RaceMateFakeIRacing.exe <seconds> [mapping name] [event name]
// Without names it takes iRacing's own, which iRacing's service may already hold (empty); then it stops.
#include "../src/Telemetry.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace rm;

namespace {
constexpr int Cars = 60, Player = 7, TickRate = 60;
struct Variable { const char* name; int type; int count; int offset = 0; };
// Types as in the SDK: 1 bool, 2 int, 4 float, 5 double.
std::vector<Variable> variables = {
    {"SessionTime",5,1},{"SessionTimeRemain",5,1},{"SessionNum",2,1},{"SessionState",2,1},{"SessionFlags",3,1},
    {"PlayerCarIdx",2,1},{"CamCarIdx",2,1},{"CamGroupNumber",2,1},{"IsOnTrack",1,1},{"IsReplayPlaying",1,1},{"OnPitRoad",1,1},
    {"PlayerCarInPitStall",1,1},{"IsGarageVisible",1,1},{"EngineWarnings",3,1},{"FuelLevel",4,1},{"LapCompleted",2,1},{"LapDistPct",4,1},
    {"LapLastLapTime",4,1},{"LapBestLapTime",4,1},{"PlayerCarMyIncidentCount",2,1},{"AirTemp",4,1},{"TrackTempCrew",4,1},
    {"WindDir",4,1},{"WindVel",4,1},{"YawNorth",4,1},{"TrackWetness",2,1},{"Speed",4,1},{"PlayerTrackSurface",2,1},{"Brake",4,1},
    {"dcBrakeBias",4,1},{"SessionTimeOfDay",4,1},{"SessionLapsRemainEx",2,1},{"CarLeftRight",2,1},
    {"CarIdxPosition",2,64},{"CarIdxClassPosition",2,64},{"CarIdxLap",2,64},{"CarIdxLapCompleted",2,64},{"CarIdxTrackSurface",2,64},
    {"CarIdxSessionFlags",3,64},{"CarIdxTireCompound",2,64},{"CarIdxLapDistPct",4,64},{"CarIdxEstTime",4,64},{"CarIdxF2Time",4,64},
    {"CarIdxBestLapTime",4,64},{"CarIdxLastLapTime",4,64},{"CarIdxOnPitRoad",1,64}};

std::string SessionYaml() {
    std::string y =
        "WeekendInfo:\n TrackName: benchmarkring\n TrackID: 99999\n TrackLength: 5.20 km\n TrackDisplayName: Benchmark Ring\n"
        " TrackConfigName: Grand Prix\n TrackPitSpeedLimit: 80.00 kph\n SessionID: 1\n SubSessionID: 2\n"
        "SessionInfo:\n Sessions:\n - SessionNum: 0\n   SessionType: Race\n   SessionLaps: unlimited\n"
        "DriverInfo:\n DriverCarIdx: 7\n DriverCarFuelMaxLtr: 120.0\n DriverCarMaxFuelPct: 1.000\n Drivers:\n";
    const char* classes[] = {"GTP","LMP2","GT3"}; const char* colors[] = {"0xffda59","0x33ceff","0xff5888"};
    const char* carNames[] = {"Porsche 963","Dallara P217","BMW M4 GT3"}; const char* carPaths[] = {"porsche963gtp","dallarap217","bmwm4gt3"};
    for (int i = 0; i < Cars; ++i) {
        const int c = i % 3;
        y += " - CarIdx: " + std::to_string(i) + "\n   UserName: Driver " + std::to_string(i + 1) + "\n   CarNumber: \"" + std::to_string(i + 1) +
             "\"\n   CarClassID: " + std::to_string(c + 1) + "\n   CarClassShortName: " + classes[c] + "\n   CarClassColor: " + colors[c] +
             "\n   CarScreenName: " + carNames[c] + "\n   CarPath: " + carPaths[c] + "\n   IRating: " + std::to_string(1500 + i * 37) +
             "\n   LicString: A 3.45\n   LicColor: 0x0153db\n   CurDriverIncidentCount: 0\n";
    }
    return y;
}
}

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 60;
    wchar_t mappingName[128] = L"Local\\IRSDKMemMapFileName", eventName[128] = L"Local\\IRSDKDataValidEvent";
    if (argc > 3) { MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, mappingName, 128); MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, eventName, 128); }
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1 << 20, mappingName);
    if (!map) { std::cerr << "Could not create the shared memory.\n"; return 2; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) { std::cerr << "iRacing (or another stand-in) is already running; close it first.\n"; CloseHandle(map); return 3; }
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, eventName);
    auto* memory = static_cast<unsigned char*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    auto* h = reinterpret_cast<MemoryHeader*>(memory); *h = {};
    h->version = 2; h->tickRate = TickRate; h->numBuffers = 3; h->varsOffset = 256;
    int offset = 0;
    for (auto& v : variables) {
        v.offset = offset; offset += (v.type == 5 ? 8 : v.type == 1 ? 1 : 4) * v.count; offset = (offset + 7) & ~7;
        auto* header = reinterpret_cast<VariableHeader*>(memory + h->varsOffset) + h->numVars++;
        *header = {}; header->type = v.type; header->offset = v.offset; header->count = v.count; strcpy_s(header->name, v.name);
    }
    h->bufferLength = offset;
    for (int b = 0; b < 3; ++b) h->buffers[b] = {-1, 64 * 1024 + b * 32 * 1024, {0, 0}};
    const std::string yaml = SessionYaml();
    h->sessionOffset = 200 * 1024; h->sessionLength = static_cast<int>(yaml.size() + 1); std::memcpy(memory + h->sessionOffset, yaml.c_str(), yaml.size() + 1);
    h->sessionUpdate = 1;

    auto find = [&](const char* name) -> const Variable& { for (const auto& v : variables) if (std::strcmp(v.name, name) == 0) return v; return variables[0]; };
    auto set = [&](unsigned char* row, const char* name, double value, int index = 0) {
        const auto& v = find(name); unsigned char* p = row + v.offset;
        switch (v.type) {
        case 1: p[index] = value != 0; break;
        case 2: case 3: { const int32_t x = static_cast<int32_t>(value); std::memcpy(p + index * 4, &x, 4); break; }
        case 4: { const float x = static_cast<float>(value); std::memcpy(p + index * 4, &x, 4); break; }
        case 5: { const double x = value; std::memcpy(p + index * 8, &x, 8); break; }
        }
    };

    LARGE_INTEGER frequency{}, start{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    double laps[Cars]; for (int i = 0; i < Cars; ++i) laps[i] = 10 + (Cars - i) * .015;
    h->status = 1;
    const int frames = static_cast<int>(seconds * TickRate);
    for (int tick = 0; tick < frames; ++tick) {
        // Absolute schedule: frame n is due at start + n/60 s, so the rate does not drift.
        const LONGLONG due = start.QuadPart + tick * frequency.QuadPart / TickRate;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        if (due > now.QuadPart) {
            LARGE_INTEGER relative{}; relative.QuadPart = -static_cast<LONGLONG>((due - now.QuadPart) * 10000000 / frequency.QuadPart);
            SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE); WaitForSingleObject(timer, INFINITE);
        }
        QueryPerformanceCounter(&now);
        const double time = static_cast<double>(now.QuadPart) / frequency.QuadPart;
        const int buffer = tick % 3; unsigned char* row = memory + h->buffers[buffer].offset;
        int order[Cars]; for (int i = 0; i < Cars; ++i) { laps[i] += 1.0 / TickRate / (100 + (i % 3) * 8 + (i % 7) * .3); order[i] = i; }
        std::sort(order, order + Cars, [&](int a, int b) { return laps[a] > laps[b]; });
        int classCount[3]{};
        for (int p = 0; p < Cars; ++p) {
            const int i = order[p]; const int completed = static_cast<int>(laps[i]); const double pct = laps[i] - completed;
            const double lapTime = 100 + (i % 3) * 8 + (i % 7) * .3;
            set(row, "CarIdxPosition", p + 1, i); set(row, "CarIdxClassPosition", ++classCount[i % 3], i);
            set(row, "CarIdxLap", completed + 1, i); set(row, "CarIdxLapCompleted", completed, i); set(row, "CarIdxTrackSurface", 3, i);
            set(row, "CarIdxLapDistPct", pct, i); set(row, "CarIdxEstTime", pct * lapTime, i);
            set(row, "CarIdxF2Time", (laps[order[0]] - laps[i]) * lapTime, i); set(row, "CarIdxBestLapTime", lapTime - .4, i);
            set(row, "CarIdxLastLapTime", lapTime, i); set(row, "CarIdxOnPitRoad", 0, i); set(row, "CarIdxTireCompound", 0, i);
        }
        for (int i = Cars; i < 64; ++i) { set(row, "CarIdxLapDistPct", -1, i); set(row, "CarIdxTrackSurface", -1, i); set(row, "CarIdxPosition", 0, i); }
        // The spotter in a 10 s cycle: clear, a car passing on the left (from 6 m behind to 6 m ahead), clear,
        // two cars on the right, three wide. The car beside you is moved there; the others keep racing.
        const int phase = tick % 600;
        const int spotter = phase < 150 ? 1 : phase < 330 ? 2 : phase < 420 ? 1 : phase < 540 ? 6 : 4;
        if (spotter != 1) {
            const double metres = spotter == 2 ? -6 + 12.0 * (phase - 150) / 180 : 1.5, pct = std::fmod(laps[Player] + metres / 5200, 1.0);
            set(row, "CarIdxLapDistPct", pct, (Player + 1) % Cars);
        }
        const int playerLaps = static_cast<int>(laps[Player]);
        set(row, "SessionTime", time); set(row, "SessionTimeRemain", 3600 - tick / 60.0); set(row, "SessionNum", 0); set(row, "SessionState", 4);
        set(row, "SessionFlags", 0x4); set(row, "PlayerCarIdx", Player); set(row, "CamCarIdx", Player); set(row, "CamGroupNumber", 1);
        set(row, "IsOnTrack", 1); set(row, "IsReplayPlaying", 0); set(row, "OnPitRoad", 0); set(row, "PlayerCarInPitStall", 0); set(row, "IsGarageVisible", 0);
        set(row, "FuelLevel", 80 - tick * .0006); set(row, "LapCompleted", playerLaps); set(row, "LapDistPct", laps[Player] - playerLaps);
        set(row, "LapLastLapTime", 108.4); set(row, "LapBestLapTime", 108.0); set(row, "PlayerCarMyIncidentCount", 0);
        set(row, "AirTemp", 22); set(row, "TrackTempCrew", 31); set(row, "WindDir", 1.2); set(row, "WindVel", 3.5);
        set(row, "YawNorth", std::fmod(tick * .01, 6.283)); set(row, "TrackWetness", 1); set(row, "Speed", 55); set(row, "PlayerTrackSurface", 3);
        set(row, "Brake", 0); set(row, "dcBrakeBias", 54.5); set(row, "SessionTimeOfDay", 50000 + tick / 60.0); set(row, "SessionLapsRemainEx", 32767);
        set(row, "CarLeftRight", spotter);
        MemoryBarrier(); h->buffers[buffer].tick = tick; MemoryBarrier();
        PulseEvent(event);
    }
    h->status = 0; PulseEvent(event);
    std::cout << "Fake iRacing: " << frames << " frames in " << seconds << " s\n";
    CloseHandle(timer); UnmapViewOfFile(memory); CloseHandle(event); CloseHandle(map);
    return 0;
}
