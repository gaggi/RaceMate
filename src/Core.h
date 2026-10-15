#pragma once
#include <array>
#include <string>
#include <vector>
#include <limits>

namespace rm {
constexpr int MaxCars = 128;
// SessionFlags / CarIdxSessionFlags bits from the iRacing SDK (irsdk_Flags).
namespace SessionFlag {
constexpr unsigned Yellow = 0x8u, Red = 0x10u, YellowWaving = 0x100u, Caution = 0x4000u, CautionWaving = 0x8000u;
constexpr unsigned Black = 0x10000u, Disqualify = 0x20000u, Servicible = 0x40000u, Furled = 0x80000u, Repair = 0x100000u;
// Laps under these flags are not representative racing laps.
constexpr unsigned Neutralized = Yellow | YellowWaving | Caution | CautionWaving | Red;
constexpr unsigned DriverFlags = Black | Disqualify | Servicible | Furled | Repair;
}
// EngineWarnings bits from the iRacing SDK (irsdk_EngineWarnings).
namespace EngineWarning { constexpr unsigned PitSpeedLimiter = 0x10u; }
struct Driver {
    int index = -1, classId = 0, rating = 0;
    unsigned classColor = 0xeac14c;
    bool paceCar = false, spectator = false;
    std::string name, number, className;
    std::string carName, carPath;
    int incidentCount = -1;
    std::string license; unsigned licenseColor = 0x989fa9;
    double estimatedLapTime = -1;
    std::string brand; // Manufacturer, from CarBrand().
};
std::string TireLabel(const Driver&, int compound);
// Manufacturer from CarPath, else the first word of CarScreenName; empty without either.
std::string CarBrand(const std::string& carPath, const std::string& carName);
bool HasMultipleDryCompounds(const Driver&);
struct SessionResult {
    int position = 0, classPosition = 0;
    double best = -1, last = -1;
    int completed = -1;
};
struct CameraGroup { int number=-1; std::string name; bool scenic=false; };
struct Session {
    std::string track, type, key, trackUsage;
    std::string trackName, trackConfig;
    int trackId = -1; // Exact layout identity for official track maps.
    int number = -1, player = -1, totalLaps = -1, incidentLimit = -1, incidentWarning = -1;
    double trackMeters = -1, fuelCapacity = -1;
    std::vector<Driver> drivers;
    std::vector<CameraGroup> cameras;
    std::array<SessionResult, MaxCars> results{};
    std::array<int,MaxCars> gridPositions{};
};
Session ParseSession(const std::string& yaml, int sessionNumber);
struct Car {
    int position = 0, classPosition = 0, lap = -1, surface = -1, completed = -1;
    double pct = -1, estTime = -1, f2Time = -1, best = -1, last = -1;
    bool pit = false, outLap = false, towing = false, towingKnown = false;
    unsigned sessionFlags = 0;
    int tireCompound = -1, lastPitLap = -1; double lastPitDuration = -1;
    double speed = -1, lastPitStationary = -1, pitStationary = -1;
    // Laps since the last pit exit and stops in this race; -1 while unknown (not watched since the start nor seen leaving the pits).
    int stintLaps = -1, stops = -1;
    // Time of an observed race start or serviced pit exit; unknown after lost telemetry.
    double fuelStintStart = -1;
    double stintStartTime = -1; // Observed race start or pit exit, independently of a refill.
};
struct Frame {
    bool connected = false, onTrack = false, pit = false, inStall = false, replay = false;
    bool incidentsKnown = false;
    double airTemperature = std::numeric_limits<double>::quiet_NaN(), trackTemperature = std::numeric_limits<double>::quiet_NaN();
    double windDirection = std::numeric_limits<double>::quiet_NaN(), yawNorth = std::numeric_limits<double>::quiet_NaN();
    double timeOfDay = -1, windSpeed = -1; int trackWetness = -1;
    double brakeBias = std::numeric_limits<double>::quiet_NaN(); // dcBrakeBias, own car only; missing on some cars.
    double timeTotal = -1; int lapsTotal = -1; // SessionTimeTotal / SessionLapsTotal; unlimited sessions report 604800 s and 32767 laps.
    // Precipitation, RelativeHumidity and FogLevel are fractions 0..1; Skies 0 clear to 3 overcast; WeatherDeclaredWet allows rain tires.
    bool declaredWet = false; int skies = -1;
    double precipitation = std::numeric_limits<double>::quiet_NaN(), humidity = std::numeric_limits<double>::quiet_NaN(), fog = std::numeric_limits<double>::quiet_NaN();
    bool limiterKnown = false, limiter = false, fuelKnown = false, timeLeftKnown = false;
    int player = -1, camera = -1, session = -1, lapCompleted = -1, incidents = 0, state = 0;
    int cameraGroup = 1, replaySession = -1;
    // ReplayFrameNumEnd: frames (60 per second) behind the live point; -1 when not reported.
    int replayFramesBehind = -1;
    double replayTime = -1;
    unsigned flags = 0;
    double time = 0, timeLeft = -1, lapsLeft = -1, fuel = 0, lapPct = 0, lastLap = -1, bestLap = -1;
    std::array<Car, MaxCars> cars{};
};
// Clockwise source bearing on screen: 0 is ahead, pi/2 is right.
double RelativeWindAngle(const Frame&);
// Track and air temperature, precipitation and wetness every 30 s of session time, 20 minutes back.
class WeatherHistory {
public:
    struct Sample {
        double time = -1, track = std::numeric_limits<double>::quiet_NaN(), air = std::numeric_limits<double>::quiet_NaN();
        double rain = std::numeric_limits<double>::quiet_NaN(), wetness = std::numeric_limits<double>::quiet_NaN();
    };
    static constexpr int Size = 40;
    void Reset() { *this = {}; }
    void Update(const Session&, const Frame&);
    int Count() const { return count_; }
    const Sample& At(int i) const { return samples_[(cursor_ + Size - count_ + i) % Size]; } // Oldest first.
    // Change over the last ten minutes once at least five are known; NaN before.
    double Change(double Sample::* value) const;
private:
    std::array<Sample, Size> samples_{};
    int count_ = 0, cursor_ = 0, session_ = -1;
    std::string key_;
};
class PitLimiterVisibility {
public:
    void Reset() { *this={}; }
    bool Update(const Session&,const Frame&);
private:
    std::string key_; int session_=-1,player_=-1; double time_=-1; bool inPit_=false, exited_=false; int surface_=-1;
};
class PitHistory {
public:
    void Reset() { *this={}; }
    void Update(const Session&, Frame&);
private:
    struct Visit { bool seen=false, pit=false, complete=false, out=false, tow=false; int outLap=-1, outCompleted=-1; double pct=-1; double entry=-1; int lap=-1, lastLap=-1; double duration=-1;
        int surface=-1; bool stopValid=false; double stopped=0, stillRun=0, lastStopped=-1;
        bool known=false; int stintStart=-1, stops=-1;
        double fuelStart=-1, stintTime=-1; int completed=-1; bool fuelVisit=false;
    };
    std::array<Visit,MaxCars> visits_{};
    std::string key_; int session_=-1; double time_=-1;
    int state_=-1;
};
int ReferenceCar(const Frame&);
// What the user is watching. IsReplayPlaying alone cannot tell: it is also set while
// spectating live, and a paused replay may clear it. Race Control, the event recorder
// and the race forecast all decide with this.
enum class ViewMode { InCar, Live, Replay };
ViewMode View(const Frame&);
struct Row {
    Driver driver;
    Car car;
    double gap = std::numeric_limits<double>::quiet_NaN();
    int lapDifference = 0;
    bool player = false;
    double interval = std::numeric_limits<double>::quiet_NaN();
    int intervalLaps = 0;
};
// -MaxCars denotes unavailable (missing grid or non-race session).
int PositionsGained(const Session&, const Row&, bool ownClass);
// Estimated iRating change per car index if the race ended in the current class order; NoRatingChange where unknown.
constexpr int NoRatingChange = -100000;
std::array<int, MaxCars> IRatingChanges(const Session&, const Frame&);
std::string DriverFlags(unsigned flags, bool black=true, bool slowdown=true, bool repair=true);
int RelativeRowCount(int requested);
std::vector<Row> Relative(const Session&, const Frame&, int count, bool ownClass, bool hidePit = false);
std::vector<Row> Standings(const Session&, const Frame&, int count, bool ownClass, bool fallbackByRating = false);
struct StandingsOptions {
    bool ownClass = false, separateClasses = true, liveGaps = false, fallbackByRating = false;
    int leaders = 3, ahead = 2, behind = 2;
};
struct StandingsGroup {
    int classId = -1;
    std::string name;
    unsigned color = 0;
    std::vector<Row> rows;
};
std::vector<StandingsGroup> SelectStandings(const Session&, const Frame&, const StandingsOptions&);
enum class FuelMetric { Last, Average10, Average5, Maximum, Minimum, Average, Average3, Count };
constexpr int FuelMetricCount = static_cast<int>(FuelMetric::Count);
struct FinishScenario {
    bool valid = false;
    int winner = -1, leaderCrossings = -1, ownCrossings = -1;
    double leaderTotalDistance = -1; // Total leader progress at timer expiry; at the finish for lap-limited races.
    double leaderDistance = -1, leaderFinishIn = -1, raceFinishIn = -1, ownDistance = -1, ownFinishIn = -1;
    double finalLapTimeLeft = -1; // Race clock at the start of the leader's projected final lap.
};
enum class PredictionStatus { Waiting, NotRace, Replay, Gridding, MissingLeader, MissingPace, Racing, Checkered, Finished };
// Assumes no further pit stops; cars currently in the pit lane carry their remaining pit loss.
struct RacePrediction {
    PredictionStatus status = PredictionStatus::Waiting;
    FinishScenario finish;
    int leader = -1;
    double leaderPace = -1, ownPace = -1;
    std::array<double,MaxCars> carFinishIn = [] { std::array<double,MaxCars> values; values.fill(-1); return values; }();
};
class RacePredictor {
public:
    void Reset();
    void Update(const Session&, const Frame&);
    RacePrediction Predict(const Session&, const Frame&) const;
private:
    struct History {
        bool seen = false, dirty = true, pitObserved = false, wasPit = false;
        int completed = -1, used = 0, cursor = 0, lossUsed = 0, lossCursor = 0;
        double progress = -1, sampleTime = -1, crossTime = -1, lastCross = -1;
        double entryTime = -1, entryProgress = -1;
        std::array<double,10> laps{};
        std::array<double,6> losses{};
    };
    std::array<History,MaxCars> history_{};
    std::string key_;
    int session_ = -1;
    double time_ = -1, checkeredTime_ = -1;
};
enum class PitWindowStatus { Unavailable, NoStopNeeded, ExtraStopNeeded, Open };
struct PitWindow { PitWindowStatus status=PitWindowStatus::Unavailable; int currentLap=-1, firstLap=-1, lastLap=-1, finishLap=-1, stops=-1; bool openNow=false; double fuelAtEnd=std::numeric_limits<double>::quiet_NaN(), timeEmpty=-1; };
PitWindow CalculatePitWindow(const Session&, const Frame&, double remainingDistance, double consumption, double lapTime=-1);
struct FuelEstimate { double consumption = 0, add = -1; int samples = 0; };
// A completed whole lap's fuel; counted laps feed the estimates, the others ran under caution or through the pits.
struct FuelLap { int lap = -1; double burn = 0; bool counted = false, caution = false; };
struct FuelPlan {
    int samples = 0, stintStartLap = -1;
    double lapsToFinish = -1;
    std::array<FuelEstimate, FuelMetricCount> estimates{};
    std::array<FuelLap, 10> history{}; int historyCount = 0; // Oldest first.
    RacePrediction prediction;
};
enum class OpponentFuelStatus { None, End, Save };
// Own-class estimate: a full tank at the last observed stop, using own consumption and pace.
OpponentFuelStatus OpponentFuel(const Session&, const Frame&, const FuelPlan&, int car, int extraLaps);
class FuelCalculator {
public:
    void Reset();
    void Update(const Session&, const Frame&, bool excludeYellowLaps = false);
    // Without a prediction the consumption estimates are still filled, but no finish distance or fuel to add.
    FuelPlan Plan(const Frame&, const RacePrediction* prediction = nullptr) const;
private:
    std::string key_;
    int session_ = -1, player_ = -1, lap_ = -1, samples_ = 0, used_ = 0, cursor_ = 0;
    bool anchored_ = false, dirty_ = false, partial_ = false, caution_ = false;
    std::array<FuelLap, 10> history_{}; int historyCount_ = 0, historyCursor_ = 0;
    double fuel_ = 0, previousFuel_ = 0, time_ = -1, last_ = 0;
    double preStartTime_ = -1;
    int stintStartLap_ = -1;
    double totalBurn_ = 0;
    double maximum_ = 0, minimum_ = std::numeric_limits<double>::max();
    std::array<double, 10> burns_{};
};
Session DemoSession();
Frame DemoFrame(double time);
}
