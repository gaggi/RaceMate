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
// irsdk_PitSvFlags: the boxes ticked in the pit menu.
namespace PitServiceFlag { constexpr unsigned LeftFront = 0x1u, RightFront = 0x2u, LeftRear = 0x4u, RightRear = 0x8u, Fuel = 0x10u; }
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
struct SessionResult {
    int position = 0, classPosition = 0;
    double best = -1, last = -1;
    int completed = -1;
};
struct CameraGroup { int number=-1; std::string name; bool scenic=false; };
struct Session {
    std::string track, type, key, trackUsage;
    std::string trackName, trackConfig;
    std::string ruleSet; // WeekendInfo DCRuleSet: "None" for iRacing's default car rules, else a rule set such as IMSA, NEC or DTM.
    int trackId = -1; // Exact layout identity for official track maps.
    int number = -1, player = -1, totalLaps = -1, incidentLimit = -1, incidentWarning = -1;
    double trackMeters = -1, fuelCapacity = -1;
    // DriverPitTrkPct: where the own pit box lies, as a lap fraction; TrackPitSpeedLimit in m/s. -1 while unknown.
    double pitStallPct = -1, pitSpeedLimit = -1;
    // WeekendInfo TrackNorthOffset in radians: how the track's own frame lies to north; NaN while unknown.
    double trackNorthOffset = std::numeric_limits<double>::quiet_NaN();
    std::vector<Driver> drivers;
    std::vector<CameraGroup> cameras;
    std::array<SessionResult, MaxCars> results{};
    std::array<int,MaxCars> gridPositions{};
    // CarSetup, own car only: every value with its key as iRacing lists it; setupUpdate is its UpdateCount.
    std::vector<std::pair<std::string,std::string>> setup; int setupUpdate = -1;
};
Session ParseSession(const std::string& yaml, int sessionNumber);
// In-car adjustments that can be changed while driving (the dc variables), against the loaded setup.
enum Dial { BrakeBiasDial, AbsDial, TractionDial, Traction2Dial, ThrottleDial, MixtureDial, EngineBrakingDial, DialCount };
const wchar_t* DialName(int dial);
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
    bool garage = false; // IsGarageVisible: the garage screen is open.
    double airTemperature = std::numeric_limits<double>::quiet_NaN(), trackTemperature = std::numeric_limits<double>::quiet_NaN();
    double windDirection = std::numeric_limits<double>::quiet_NaN(), yawNorth = std::numeric_limits<double>::quiet_NaN();
    double timeOfDay = -1, windSpeed = -1; int trackWetness = -1;
    double timeTotal = -1; int lapsTotal = -1; // SessionTimeTotal / SessionLapsTotal; unlimited sessions report 604800 s and 32767 laps.
    // Precipitation, RelativeHumidity and FogLevel are fractions 0..1; Skies 0 clear to 3 overcast; WeatherDeclaredWet allows rain tires.
    bool declaredWet = false; int skies = -1;
    double precipitation = std::numeric_limits<double>::quiet_NaN(), humidity = std::numeric_limits<double>::quiet_NaN(), fog = std::numeric_limits<double>::quiet_NaN();
    bool limiterKnown = false, limiter = false, fuelKnown = false, timeLeftKnown = false;
    int player = -1, camera = -1, session = -1, lapCompleted = -1, incidents = 0, state = 0;
    int cameraGroup = 1, replaySession = -1;
    // ReplayFrameNumEnd: frames (60 per second) behind the live point; -1 when not reported.
    int replayFramesBehind = -1;
    // iRacing's frame counter and frames per second (60): overlays refresh on whole frames.
    int tick = -1, tickRate = 0;
    double replayTime = -1;
    unsigned flags = 0;
    double time = 0, timeLeft = -1, lapsLeft = -1, fuel = 0, lapPct = 0, lastLap = -1, bestLap = -1;
    // The pit menu: PitSvFlags, PitSvFuel (litres to add) and PitRepairLeft (mandatory repair seconds).
    unsigned pitSvFlags = 0; double pitSvFuel = 0, pitRepair = 0;
    double brake = -1; // Brake pedal, 0 to 1; -1 when not reported.
    // CarLeftRight, the spotter for your car: 0 off, 1 clear, 2 car left, 3 car right, 4 cars left and right,
    // 5 two cars left, 6 two cars right; -1 when not reported.
    int spotter = -1;
    // The in-car adjustments this car has: bit d of dialsKnown marks dials[d].
    std::array<double, DialCount> dials{}; unsigned dialsKnown = 0;
    std::array<Car, MaxCars> cars{};
};
// A dial that no longer matches the setup: its value as in the setup (with that many decimals, % when given so) and now.
struct DialChange { int dial = 0; double setup = 0, current = 0; int decimals = 0; bool percent = false; };
// Each dial's value in the CarSetup, found under the names iRacing gives it; found is false where the setup has none.
struct SetupValue { bool found = false; double value = 0; int decimals = 0; bool percent = false; };
std::array<SetupValue, DialCount> SetupValues(const Session&);
// Each known dial against the setup value of the same meaning; dials the setup does not list are left out.
std::vector<DialChange> SetupChanges(const std::array<SetupValue, DialCount>&, const Frame&);
inline std::vector<DialChange> SetupChanges(const Session& s, const Frame& f) { return SetupChanges(SetupValues(s), f); }
// The same while you drive your own car. Right after getting in, the dials equal the setup; a dial that differs then
// is not the setup's value on this car and stays out until the setup or car changes.
class SetupDials {
public:
    void Reset() { *this={}; }
    const std::vector<DialChange>& Update(const Session&, const Frame&);
    const std::vector<DialChange>& Changes() const { return changes_; }
private:
    std::string key_; int session_=-1, player_=-1, setupUpdate_=-1; size_t setupSize_=0; bool inCar_=false; double entered_=-1;
    std::array<bool, DialCount> ignored_{}; std::array<SetupValue, DialCount> values_{}; std::vector<DialChange> changes_;
};
// Clockwise source bearing on screen: 0 is ahead, pi/2 is right.
// While you drive, always; while you watch another car or a replay, only with headingLive: iRacing then reports in
// YawNorth where the watched car points, but a value that stopped changing belongs to a car that is not moving.
// Otherwise mapHeading: the watched car's compass heading from the track map, as iRacing reports no heading
// while you watch another car live.
double RelativeWindAngle(const Frame&, bool headingLive = false, double mapHeading = std::numeric_limits<double>::quiet_NaN());
// Degrees (0 to 359) the official track map turns clockwise to put north up: iRacing draws it in the track's own
// frame, TrackNorthOffset plus three quarter turns. Right for most tracks, not all (Lime Rock is a quarter turn
// off), so the car in the pit box checks it. -1 without an offset.
int NorthTurnFromOffset(double trackNorthOffset);
// The same turn from a car that points along the map: its compass heading (YawNorth) less the map's direction
// there, both in radians, the map's clockwise from its top. A car in its pit stall stands along the pit lane.
// -1 when either is unknown.
int NorthTurnFromHeadings(double yawNorth, double mapHeading);
// Cars beside your car as the spotter reports them (CarLeftRight), none to two a side, while you drive it; and how
// far the nearest is along your car, in metres ahead (+) or behind. The lap distance cannot tell the sides apart,
// so with cars on both sides neither has a place.
struct SideTraffic { int left = 0, right = 0; double leftAt = std::numeric_limits<double>::quiet_NaN(), rightAt = std::numeric_limits<double>::quiet_NaN(); };
SideTraffic SideTrafficOf(const Session& s, const Frame& f);
// The cars around your car for the radar: how far each is ahead (+) or behind along the lap, in metres, and its lane:
// -1 left, 0 yours, +1 right, and -2 or +2 for a second car on one side. iRacing reports no positions across the
// track, so lanes are estimated:
// - The cars beside you, as many as the spotter reports, take the lanes it reports: a car within 5.5 m that had one
//   keeps it, else the nearest within 8 m.
// - A car overlapping yours (within 3.5 m) that the spotter does not report is beside you all the same: on the one
//   side it reports, else the side the car was last on, else the free one.
// - A car keeps its lane for 3 s after it was last beside you, and as long as it is within 8 m or beside another
//   car; then it counts as in yours.
// - Two cars in your lane overlapping each other (3.5 m; once so, until 4.6 m) drive side by side: offset half a lane
//   either way, each keeping the side it was last on, else the one ahead goes left; a third goes beside its neighbour.
// Only while you drive.
// The cars behind yours within threshold seconds while you drive, closest first, with their gap as the Relative
// shows it: how far behind in time by iRacing's estimate of each class's time around the lap (CarIdxEstTime), in the
// car's own class time. fasterOnly keeps cars of a faster class: its estimated lap (CarClassEstLapTime) at least 2 %
// quicker than yours. Cars in the pits are left out, as are all while you are in them.
struct CarBehind { int index = -1; double gap = 0; };
std::vector<CarBehind> CarsBehind(const Session& s, const Frame& f, double threshold, bool fasterOnly);
struct RadarCar { int index = -1; double metres = 0; int lane = 0; double offset = 0; };
class RadarTracker {
public:
    // SideBySide: closer than this along the lap, two cars overlap clearly; a car's 4.6 m less the lap distance's noise.
    static constexpr double Overlap = 5.5, Reach = 8, Hold = 3, CarLength = 4.6, SideBySide = 3.5;
    // Cars within reach metres ahead or behind, nearest first.
    void Update(const Session& s, const Frame& f, double reach);
    const std::vector<RadarCar>& Cars() const { return cars_; }
    // Counts the times the lanes were forgotten, for a display that smooths them to start afresh too.
    unsigned Resets() const { return resets_; }
    void Reset() { cars_.clear(); lane_.fill(0); guess_.fill(false); side_.fill(0); seen_.fill(-1e9); sided_.fill(-1e9); paired_.fill(-1e9); ++resets_; }
private:
    std::vector<RadarCar> cars_;
    std::string key_; int session_ = -1, player_ = -1; unsigned resets_ = 0;
    // Each car's lane and side when it was last beside you or beside another, when, and when last beside another.
    // guess_: the lane was not reported by the spotter but taken for a car overlapping yours.
    std::array<int, MaxCars> lane_{}, side_{}; std::array<bool, MaxCars> guess_{}; std::array<double, MaxCars> seen_ = Unseen(), sided_ = Unseen(), paired_ = Unseen();
    static std::array<double, MaxCars> Unseen() { std::array<double, MaxCars> a{}; a.fill(-1e9); return a; }
};
// Track and air temperature, precipitation and wetness every 30 s of session time, 20 minutes back.
class WeatherHistory {
public:
    struct Sample {
        double time = -1, track = std::numeric_limits<double>::quiet_NaN(), air = std::numeric_limits<double>::quiet_NaN();
        double rain = std::numeric_limits<double>::quiet_NaN(), wetness = std::numeric_limits<double>::quiet_NaN();
        double humidity = std::numeric_limits<double>::quiet_NaN(); // Fraction 0..1.
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
    // Past pit exit, or out on track, with the limiter still on.
    bool Warning() const { return warning_; }
    // On pit road or in the stall.
    bool InPits() const { return inPit_; }
private:
    std::string key_; int session_=-1,player_=-1; double time_=-1; bool inPit_=false, exited_=false, warning_=false; int surface_=-1;
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
// Where the user is, for when each overlay shows: driving, spotting live, watching a replay, or on the garage screen.
enum Situation { InCarSituation, SpottingSituation, ReplaySituation, GarageSituation, SituationCount };
int CurrentSituation(const Frame&);
struct Row {
    Driver driver;
    Car car;
    double gap = std::numeric_limits<double>::quiet_NaN();
    int lapDifference = 0;
    bool player = false;
    bool pitExit = false; // The own car where it would rejoin after a stop now; not a real entry.
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
    // Separate classes: the classes without the reference car show their first otherClasses cars; 0 leaves their header only.
    int otherClasses = 3;
};
struct StandingsGroup {
    int classId = -1;
    std::string name;
    unsigned color = 0;
    std::vector<Row> rows;
    int cars = 0; double best = -1; // The whole class before rows are left out: its cars and fastest lap.
};
std::vector<StandingsGroup> SelectStandings(const Session&, const Frame&, const StandingsOptions&);
// QualifyMaximum: the most fuel a counted lap took in this event's qualifying, in this car; kept into the race.
enum class FuelMetric { Last, Average10, Average5, Maximum, Minimum, Average, Average3, QualifyMaximum, Count };
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
    // The most a counted qualifying lap of this event and car took, and how many there were; kept across its sessions.
    double qualifyMaximum_ = 0; int qualifySamples_ = 0;
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
// Pit stop rejoin estimate. The lane loss is what driving through the pit lane costs against the same
// stretch on track, learned from every observed visit of every car: time on pit road, minus the time
// standing still, minus the reference time of that stretch. CarIdxEstTime is iRacing's reference time to
// each point of the lap, so a slow entry after a corner counts as slow on track too, which a constant lap
// speed would get wrong; the class reference lap scales it to the car's pace.
class PitLaneLoss {
public:
    void Reset() { *this={}; }
    void Update(const Session&, const Frame&); // After PitHistory, which measures the time standing still.
    double Loss() const;                         // Median of the observed visits, else the seed; -1 while unknown.
    int Samples() const { return static_cast<int>(samples_.size()); }
    // A value saved for a track, used until a visit there is observed.
    void Seed(int track,double seconds) { if(track!=track_) { *this={}; track_=track; } seed_=seconds; }
    int Track() const { return track_; }
private:
    struct Visit { bool seen=false, pit=false, active=false; double time=-1, est=-1, progress=-1; };
    std::array<Visit,MaxCars> visits_{};
    std::vector<double> samples_; double seed_=-1, time_=-1;
    int track_=-1, session_=-1;
};
// Where the pit lane begins and ends on this track, as lap fractions. Learned from every car that drives in or out
// between two frames close together (not one towed into its box), as the median of the last visits; until then the
// values saved for the track.
class PitLaneEnds {
public:
    void Reset() { *this={}; }
    void Seed(int track,double entry,double exit) { if(track!=track_) { *this={}; track_=track; } seedEntry_=entry; seedExit_=exit; }
    void Update(const Session&, const Frame&);
    double Entry() const; // -1 while unknown.
    double Exit() const;
    int Samples() const { return static_cast<int>(entries_.size()+exits_.size()); }
    int Track() const { return track_; }
private:
    struct Seen { bool seen=false, pit=false; double pct=-1; };
    std::array<Seen,MaxCars> cars_{};
    std::vector<double> entries_, exits_; double seedEntry_=-1, seedExit_=-1, time_=-1;
    int track_=-1, session_=-1;
};
// How hard the own car slows under braking, in m/s²: each braking zone with the pedal well down at speed gives its
// average, and the median of the last ones counts; until then the value saved for the car. -1 while unknown.
class BrakingRate {
public:
    void Reset() { *this={}; }
    void Seed(double rate) { seed_=rate; }
    void Update(const Frame&);
    double Rate() const;
    int Samples() const { return static_cast<int>(samples_.size()); }
    // Braking zones measured since the reset, also those no longer kept.
    int Measured() const { return measured_; }
private:
    double startTime_=-1, startSpeed_=-1, lastTime_=-1, lastSpeed_=-1, seed_=-1; int measured_=0;
    std::vector<double> samples_;
};
// Distance in metres to slow from speed to target at this deceleration, both in m/s, with reaction seconds to react.
double BrakingDistance(double speed,double target,double deceleration,double reaction=.3);
// Litres per second, measured while the own car is filled in its stall.
class RefuelRate {
public:
    void Reset() { *this={}; }
    void Update(const Frame&);
    double Rate() const { return rate_; }
private:
    double startTime_=-1, startFuel_=-1, lastTime_=-1, lastFuel_=-1, rate_=-1;
};
// The next stop as set in the pit menu: litres to add (what fits), tires changed, mandatory repair seconds.
struct PitService { double fuel=0; int tires=0; double repair=0; };
PitService PlannedService(const Session&, const Frame&);
// iRacing's car rule sets (2026 Season 4 notes): by default tires and repairs follow the fueling; IMSA rules do it all at once.
bool SimultaneousService(const Session&);
// The four-tire time iRacing's release notes give for the car's class and rule set; -1 for classes they do not list.
double DefaultTireChange(const Session&, const Driver&);
// Seconds standing in the box, -1 when a needed time is unknown. tireChange is for four tires; two wheel guns change
// any pair at once, so one or three tires take as long as two or four.
double ServiceTime(const PitService&, double fuelRate, double tireChange, bool simultaneous);
struct PitRejoin {
    bool valid = false;
    double loss = -1, pct = -1;            // Total seconds lost and the lap fraction where the own car rejoins.
    int position = -1, classPosition = -1; // Overall and in class after the stop, if everyone keeps their pace.
    int ahead = -1, behind = -1;           // Nearest cars on track at the rejoin point and their gaps in seconds.
    double aheadGap = -1, behindGap = -1;
    // The cars just ahead and just behind in the race after the stop, in class and overall, with gaps in seconds.
    int classAhead = -1, classBehind = -1, overallAhead = -1, overallBehind = -1;
    double classAheadGap = -1, classBehindGap = -1, overallAheadGap = -1, overallBehindGap = -1;
};
PitRejoin PredictRejoin(const Session&, const Frame&, double loss, double pace);
Session DemoSession();
Frame DemoFrame(double time);
}
