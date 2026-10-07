#pragma once
#include <array>
#include <string>
#include <vector>
#include <limits>

namespace rm {
constexpr int MaxCars = 128;
struct Driver {
    int index = -1, classId = 0, rating = 0;
    unsigned classColor = 0xeac14c;
    bool paceCar = false, spectator = false;
    std::string name, number, className;
    std::string carName, carPath;
    int incidentCount = -1;
    std::string license; unsigned licenseColor = 0x989fa9;
    double estimatedLapTime = -1;
};
std::string TireLabel(const Driver&, int compound);
bool HasMultipleDryCompounds(const Driver&);
struct SessionResult {
    int position = 0, classPosition = 0;
    double best = -1, last = -1;
    int completed = -1;
};
struct CameraGroup { int number=-1; std::string name; bool scenic=false; };
struct Session {
    std::string track, type, key, trackUsage;
    int number = -1, player = -1, totalLaps = -1, incidentLimit = -1;
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
};
struct Frame {
    bool connected = false, onTrack = false, pit = false, inStall = false, replay = false;
    bool incidentsKnown = false;
    double airTemperature = std::numeric_limits<double>::quiet_NaN(), trackTemperature = std::numeric_limits<double>::quiet_NaN();
    double windDirection = std::numeric_limits<double>::quiet_NaN(), yawNorth = std::numeric_limits<double>::quiet_NaN();
    double timeOfDay = -1, windSpeed = -1; int trackWetness = -1;
    bool limiterKnown = false, limiter = false, fuelKnown = false, timeLeftKnown = false;
    int player = -1, camera = -1, session = -1, lapCompleted = -1, incidents = 0, state = 0;
    int cameraGroup = 1, replaySession = -1;
    double replayTime = -1;
    unsigned flags = 0;
    double time = 0, timeLeft = -1, lapsLeft = -1, fuel = 0, lapPct = 0, lastLap = -1, bestLap = -1;
    std::array<Car, MaxCars> cars{};
};
// Clockwise source bearing on screen: 0 is ahead, pi/2 is right.
double RelativeWindAngle(const Frame&);
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
    };
    std::array<Visit,MaxCars> visits_{};
    std::string key_; int session_=-1; double time_=-1;
};
int ReferenceCar(const Frame&);
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
enum class FuelMetric { Last, Average10, Average5, Maximum, Minimum, Average, Count };
constexpr int FuelMetricCount = static_cast<int>(FuelMetric::Count);
enum class Confidence { Unknown, Low, Medium, High };
enum class StopMode { Automatic, None, One };
struct PredictionOptions {
    StopMode stops = StopMode::Automatic;
    int assumedPitLoss = 45, leaderStintLaps = 0, ownPitLoss = 0;
};
struct FinishScenario {
    bool valid = false;
    int winner = -1, leaderCrossings = -1, ownCrossings = -1, minCrossings = -1, maxCrossings = -1;
    double leaderTotalDistance = -1; // Total leader progress at timer expiry; at the finish for lap-limited races.
    double leaderDistance = -1, leaderFinishIn = -1, raceFinishIn = -1, ownDistance = -1, ownFinishIn = -1;
    double finalLapTimeLeft = -1; // Race clock at the start of the leader's projected final lap.
    double minDistance = -1, maxDistance = -1;
    Confidence confidence = Confidence::Unknown;
};
enum class PredictionStatus { Waiting, NotRace, Replay, Gridding, MissingLeader, MissingPace, Racing, Checkered, Finished };
struct RacePrediction {
    PredictionStatus status = PredictionStatus::Waiting;
    FinishScenario noStop, withStops;
    int leader = -1, projectedLeaderStops = 0, observedLeaderStops = 0;
    double leaderPace = -1, ownPace = -1, leaderLastPitLaps = -1, leaderLastPitSeconds = -1, leaderPitLoss = -1, leaderStint = -1;
    bool whatIf = false, measuredPitLoss = false, ownPitActive = false;
    Confidence stopConfidence = Confidence::Unknown;
};
class RacePredictor {
public:
    void Reset();
    void Update(const Session&, const Frame&);
    RacePrediction Predict(const Session&, const Frame&, const PredictionOptions&) const;
private:
    struct History {
        bool seen = false, dirty = true, pitObserved = false, wasPit = false;
        int completed = -1, used = 0, cursor = 0, stopUsed = 0, stopCursor = 0, stintUsed = 0, stintCursor = 0, stops = 0;
        double progress = -1, sampleTime = -1, crossTime = -1, lastCross = -1;
        double entryTime = -1, entryProgress = -1, exitProgress = -1, exitTime = -1;
        std::array<double,10> laps{};
        std::array<double,6> losses{}, stints{};
    };
    std::array<History,MaxCars> history_{};
    std::string key_;
    int session_ = -1;
    double time_ = -1, checkeredTime_ = -1;
};
enum class PitWindowStatus { Unavailable, NoStopNeeded, ExtraStopNeeded, Open };
struct PitWindow { PitWindowStatus status=PitWindowStatus::Unavailable; int currentLap=-1, firstLap=-1, lastLap=-1, finishLap=-1, stops=-1; bool openNow=false; double fuelAtEnd=std::numeric_limits<double>::quiet_NaN(), timeEmpty=-1; };
PitWindow CalculatePitWindow(const Session&, const Frame&, double remainingDistance, double consumption, double lapTime=-1);
struct FuelEstimate { double consumption = 0, add = -1; int samples = 0; double addWithStops = -1, safeAdd = -1; };
struct FuelPlan {
    int samples = 0, stintStartLap = -1;
    double average = 0, last = 0, maximum = 0, lapsAvailable = 0;
    double lapsToFinish = -1, required = -1, add = -1;
    std::array<FuelEstimate, FuelMetricCount> estimates{};
    Confidence confidence = Confidence::Unknown;
    RacePrediction prediction;
};
class FuelCalculator {
public:
    void Reset();
    void Update(const Session&, const Frame&, bool excludeYellowLaps = false);
    FuelPlan Plan(const Frame&, double reserveLaps, bool conservative, int manualLaps = 0, const RacePrediction* prediction = nullptr) const;
private:
    std::string key_;
    int session_ = -1, player_ = -1, lap_ = -1, samples_ = 0, used_ = 0, cursor_ = 0;
    bool anchored_ = false, dirty_ = false;
    double fuel_ = 0, previousFuel_ = 0, time_ = -1, last_ = 0;
    double preStartTime_ = -1;
    int stintStartLap_ = -1;
    double totalBurn_ = 0;
    double maximum_ = 0, minimum_ = std::numeric_limits<double>::max();
    std::array<double, 10> burns_{}, times_{};
};
Session DemoSession();
Frame DemoFrame(double time);
}
