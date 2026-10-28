#include <cctype>
#include "Core.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace rm {
namespace {
std::string Trim(std::string_view v) {
    const auto a = v.find_first_not_of(" \t\r\n");
    if (a == v.npos) return {};
    v = v.substr(a, v.find_last_not_of(" \t\r\n") - a + 1);
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
        const char quote = v.front(); v.remove_prefix(1); v.remove_suffix(1);
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (quote == '\'' && v[i] == '\'' && i + 1 < v.size() && v[i+1] == '\'') ++i;
            else if (quote == '"' && v[i] == '\\' && i + 1 < v.size()) { ++i; }
            out += v[i];
        }
        return out;
    }
    return std::string(v);
}
double Number(const std::string& s) {
    char* end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    return end != s.c_str() && std::isfinite(value) ? value : std::numeric_limits<double>::quiet_NaN();
}
int Integer(const std::string& text, int fallback, int minimum = 0, int maximum = std::numeric_limits<int>::max()) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value) &&
        value >= minimum && value <= maximum && value == std::floor(value) ? static_cast<int>(value) : fallback;
}
const Driver* Player(const Session& s, const Frame& f) {
    for (const auto& d : s.drivers) if (d.index == ReferenceCar(f)) return &d;
    return nullptr;
}
bool ValidTime(double time) { return std::isfinite(time) && time > 0; }
bool CarNumberLess(const Driver& a,const Driver& b) {
    auto numeric=[](const std::string& number) { return !number.empty() && std::all_of(number.begin(),number.end(),[](unsigned char c){return std::isdigit(c)!=0;}); };
    const bool an=numeric(a.number),bn=numeric(b.number);
    if(an!=bn) return an;
    if(an) {
        auto normalized=[](const std::string& number) { const auto first=number.find_first_not_of('0'); return first==std::string::npos?std::string("0"):number.substr(first); };
        const auto av=normalized(a.number),bv=normalized(b.number);
        if(av.size()!=bv.size()) return av.size()<bv.size();
        if(av!=bv) return av<bv;
    }
    if(a.number.empty()!=b.number.empty()) return !a.number.empty();
    if(a.number!=b.number) return a.number<b.number;
    return a.index<b.index;
}

double BestTime(double a, double b) {
    if (!ValidTime(a)) return ValidTime(b) ? b : -1;
    return ValidTime(b) ? std::min(a,b) : a;
}
Car WithResults(const Session& s, const Frame& f, int index) {
    Car car=f.cars[index];
    if(s.number==f.session) {
        const auto& result=s.results[index];
        car.best=BestTime(car.best,result.best);
        if(!ValidTime(car.last)) car.last=ValidTime(result.last)?result.last:-1;
        if(car.position<=0) car.position=result.position;
        if(car.classPosition<=0) car.classPosition=result.classPosition;
    }
    // In practice/qualifying F2 reports a best lap, never a race leader gap.
    if(!ValidTime(car.best) && !s.type.empty() && s.type!="Race" && (s.number<0 || s.number==f.session))
        car.best=BestTime(car.best,car.f2Time);
    return car;
}
double DriverLapTime(const Session& s,const Frame& f,int index) {
    if(index<0 || index>=MaxCars) return -1;
    const auto car=WithResults(s,f,index);
    if(index==f.player && ValidTime(f.bestLap)) return f.bestLap;
    if(ValidTime(car.best)) return car.best;
    if(index==f.player && ValidTime(f.lastLap)) return f.lastLap;
    if(ValidTime(car.last)) return car.last;
    for(const auto& driver:s.drivers) if(driver.index==index && ValidTime(driver.estimatedLapTime)) return driver.estimatedLapTime;
    return -1;
}
double LapTime(const Session& s,const Frame& f) { return DriverLapTime(s,f,ReferenceCar(f)); }
double Progress(const Car& car) { return (car.completed>=0?car.completed:(car.lap>=0?car.lap-1:0))+car.pct; }
bool OnCircuit(const Car& car) { return car.surface>=0 && std::isfinite(car.pct) && car.pct>=0 && car.pct<1; }
double Phase(const Driver& driver,const Car& car,double fallback) {
    // SDK estimates are calibrated to the class estimate, not a driver's fastest lap.
    const double duration=ValidTime(driver.estimatedLapTime)?driver.estimatedLapTime:fallback;
    if(ValidTime(duration) && car.estTime>=0 && car.estTime<duration && (car.estTime>0 || car.pct<.02)) return car.estTime/duration;
    return car.pct;
}
double LiveGap(const Session& s,const Frame& f,const Row& ahead,const Row& behind) {
    if(ahead.driver.index==behind.driver.index) return 0;
    if(!OnCircuit(ahead.car) || !OnCircuit(behind.car)) return std::numeric_limits<double>::quiet_NaN();
    double pace=DriverLapTime(s,f,ahead.driver.index);
    if(!ValidTime(pace)) pace=DriverLapTime(s,f,behind.driver.index);
    if(!ValidTime(pace)) {
        // Nearby same-class estimates can already provide seconds before a first lap.
        if(ahead.driver.classId==behind.driver.classId && ahead.car.estTime>0 && behind.car.estTime>0 && ahead.car.pct>=behind.car.pct)
            return std::max(0.0,ahead.car.estTime-behind.car.estTime);
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double laps=Progress(ahead.car)-ahead.car.pct-Progress(behind.car)+behind.car.pct;
    return std::max(0.0,(laps+Phase(ahead.driver,ahead.car,pace)-Phase(behind.driver,behind.car,pace))*pace);
}
void SortKnownProgress(std::vector<Row>& rows, bool onlyUnranked = false) {
    // Preserve the original ordering when progress is missing or exactly tied.
    // Missing telemetry must not displace known cars or synthesize an overtake.
    std::vector<size_t> slots; std::vector<Row> known;
    slots.reserve(rows.size()); known.reserve(rows.size());
    for(size_t i=0;i<rows.size();++i) if((!onlyUnranked || rows[i].car.position<=0) && OnCircuit(rows[i].car) && (rows[i].car.completed>=0 || rows[i].car.lap>=0)) { slots.push_back(i); known.push_back(std::move(rows[i])); }
    std::stable_sort(known.begin(),known.end(),[](const Row& a,const Row& b){return Progress(a.car)>Progress(b.car);});
    for(size_t i=0;i<slots.size();++i) rows[slots[i]]=std::move(known[i]);
}
void LiveRaceOrder(const Session& s,const Frame& f,std::vector<Row>& rows) {
    if(s.type!="Race" || (f.state>0 && f.state!=4)) return;
    SortKnownProgress(rows);
    for(size_t i=0;i<rows.size();++i) {
        auto& row=rows[i]; row.car.position=static_cast<int>(i)+1; row.car.classPosition=1;
        for(size_t j=0;j<i;++j) if(rows[j].driver.classId==row.driver.classId) ++row.car.classPosition;
    }
}
}
namespace {
int TireFamily(const Driver& driver) {
    std::string name=driver.carName+" "+driver.carPath;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(name.find("ir18")!=std::string::npos || name.find("dw12")!=std::string::npos || name.find("dallara ir-18")!=std::string::npos) return 1;
    if(name.find("ir01")!=std::string::npos || name.find("ir-01")!=std::string::npos || name.find("mp4-30")!=std::string::npos || name.find("mp430")!=std::string::npos || name.find("mercedesw12")!=std::string::npos || name.find("mercedesw13")!=std::string::npos || name.find("mercedes-amg w12")!=std::string::npos || name.find("mercedes-amg w13")!=std::string::npos) return 2;
    return 0;
}
}
std::string TireLabel(const Driver& driver,int compound) {
    if(compound<0) return {};
    const int family=TireFamily(driver);
    if(family==1) {
        if(compound==0) return "Primary";
        if(compound==1) return "Alternate";
        if(compound==2) return "Wet";
        return {};
    }
    if(family==2) {
        const char* dry[]={"Soft","Medium","Hard"};
        return compound<3?dry[compound]:"";
    }
    return compound==0?"Dry":(compound==1?"Wet":"");
}
void PitHistory::Update(const Session& s, Frame& f) {
    if(!f.connected) { Reset(); return; }
    if(key_!=s.key || session_!=f.session || f.time<time_) { Reset(); key_=s.key; session_=f.session; }
    const double dt=f.time-time_;
    const bool continuous=time_>=0 && dt<=5;
    const bool raceStart=continuous && state_>0 && state_<4 && f.state==4 && s.type=="Race" && View(f)!=ViewMode::Replay;
    for(int i=0;i<MaxCars;++i) {
        auto& v=visits_[i]; auto& car=f.cars[i];
        if(!continuous || car.surface!=1) v.tow=false;
        if(continuous && dt>0 && v.seen && car.surface==1 && v.surface!=1 && s.type=="Race" && f.state==4) {
            double delta=car.pct-v.pct; delta-=std::round(delta);
            const bool teleport=s.trackMeters>0 && v.pct>=0 && car.pct>=0 && std::abs(delta)*s.trackMeters>120*dt+20;
            if((!v.pit && (v.surface==0 || v.surface==3)) || teleport) v.tow=true;
        }
        if(!car.towingKnown) car.towing=v.tow;
        // A missed visit or reset makes the assumed last fill unusable until another complete stop.
        if(!continuous || !v.seen || car.surface<0 || car.towing || View(f)==ViewMode::Replay ||
           (v.completed>=0 && car.completed<v.completed)) { v.fuelStart=v.stintTime=-1; v.fuelVisit=false; }
        if(raceStart && v.seen && car.completed==0 && !car.pit && !car.towing && car.surface>=0)
            v.fuelStart=v.stintTime=f.time;
        if(car.surface<0) { v.seen=false; v.complete=false; v.out=false; v.stopValid=false; v.stillRun=0; }
        else {
            if(!continuous) { v.seen=false; v.complete=false; v.out=false; v.stopValid=false; v.stillRun=0; }
            if(car.pit) v.out=false;
            if(v.out && ((v.outLap>=0 && car.lap>v.outLap) || (v.outCompleted>=0 && car.completed>v.outCompleted) || (v.outLap<0 && v.outCompleted<0 && v.pct>.8 && car.pct>=0 && car.pct<.2))) v.out=false;
            if(!car.pit && v.seen && v.pit) { v.out=true; v.outLap=car.lap; v.outCompleted=car.completed; }
            if(car.pit && v.seen && !v.pit) {
                v.entry=f.time; v.lap=car.lap; v.complete=true;
                v.fuelVisit=!car.towing && View(f)!=ViewMode::Replay;
                v.stopValid=true; v.stopped=v.stillRun=0; v.lastStopped=-1;
            }
            if(v.stopValid && v.seen && (car.pit || v.pit) && dt>0) {
                if(dt>1) { v.stopValid=false; v.stillRun=0; }
                else {
                    double speed=car.speed;
                    if(speed<0 && s.trackMeters>0 && v.pct>=0 && car.pct>=0) {
                        double delta=car.pct-v.pct; delta-=std::round(delta);
                        if(std::abs(delta)<.02) speed=std::abs(delta)*s.trackMeters/dt;
                    }
                    if(car.surface==1 && v.surface==1) {
                        if(speed<0) v.stopValid=false;
                        else if(speed<=(car.speed>=0?.1:.5)) v.stillRun+=dt;
                        else { if(v.stillRun>=.5) v.stopped+=v.stillRun; v.stillRun=0; }
                    } else { if(v.stillRun>=.5) v.stopped+=v.stillRun; v.stillRun=0; }
                }
            }
            v.pct=car.pct;
            if(!car.pit && v.seen && v.pit) {
                v.lastStopped=v.stopValid?v.stopped:-1; v.stopValid=false; v.stillRun=0;
                // Drive-throughs do not imply a refill. Require a continuously observed stationary stop.
                v.fuelStart=v.fuelVisit && v.complete && v.lastStopped>=2 && !car.towing && s.type=="Race" && f.state==4 && View(f)!=ViewMode::Replay?f.time:-1;
                v.fuelVisit=false;
            }
            if(!car.pit && v.seen && v.pit && v.complete) {
                v.lastLap=v.lap; v.duration=std::max(0.0,f.time-v.entry); v.complete=false;
            }
            // A car first seen before its first race lap is known from the start: no stops yet, stint from lap 0.
            if(!v.known) { v.known=true; if(s.type=="Race" && car.completed<=0) { v.stintStart=0; v.stops=0; } }
            if(!car.pit && v.seen && v.pit) {
                v.stintStart=std::max(0,car.completed);
                v.stintTime=View(f)==ViewMode::Replay || car.towing?-1:f.time;
                // Leaving the pits for the start is not a stop.
                if(v.stops>=0 && s.type=="Race" && f.state==4 && (car.completed>0 || car.lap>1)) ++v.stops;
            }
            v.seen=true; v.pit=car.pit; v.surface=car.surface;
        }
        car.lastPitStationary=v.lastStopped;
        car.pitStationary=car.pit && v.stopValid?v.stopped+(v.stillRun>=.5?v.stillRun:0):-1;
        car.outLap=v.out && !car.pit && car.surface>=0;
        car.lastPitLap=v.lastLap; car.lastPitDuration=v.duration;
        car.stintLaps=v.stintStart<0 || car.completed<0?-1:(car.pit?0:std::max(0,car.completed-v.stintStart));
        car.stops=v.stops;
        car.fuelStintStart=v.fuelStart;
        car.stintStartTime=v.stintTime;
        v.completed=car.completed;
    }
    time_=f.time;
    state_=f.state;
}
std::string DriverFlags(unsigned flags,bool black,bool slowdown,bool repair) {
    std::string text;
    auto add=[&](const char* value) { if(!text.empty()) text+=" "; text+=value; };
    if(black && (flags&SessionFlag::Disqualify)) add("DQ");
    else if(black && (flags&SessionFlag::Black)) add("BLACK");
    if(slowdown && (flags&SessionFlag::Furled)) add("SLOW");
    if(repair && (flags&SessionFlag::Repair)) add("REPAIR");
    return text;
}
int PositionsGained(const Session& s,const Row& row,bool ownClass) {
    const int index=row.driver.index;
    if(s.type!="Race" || index<0 || index>=MaxCars || s.gridPositions[index]<=0) return -MaxCars;
    int start=s.gridPositions[index];
    if(ownClass) {
        start=1;
        for(const auto& driver:s.drivers) if(driver.index>=0 && driver.index<MaxCars && !driver.paceCar && !driver.spectator && driver.classId==row.driver.classId && s.gridPositions[driver.index]>0 && s.gridPositions[driver.index]<s.gridPositions[index]) ++start;
    }
    const int current=ownClass?row.car.classPosition:row.car.position;
    return current>0?start-current:-MaxCars;
}
ViewMode View(const Frame& f) {
    if(f.onTrack && !f.replay) return ViewMode::InCar;
    // More than 2 s behind live, or another session, is old footage.
    if(f.replayFramesBehind>=0) return f.replayFramesBehind>120?ViewMode::Replay:ViewMode::Live;
    if(f.replayTime>=0) return f.replaySession!=f.session || std::abs(f.time-f.replayTime)>2?ViewMode::Replay:ViewMode::Live;
    return f.replay?ViewMode::Replay:ViewMode::Live;
}
int CurrentSituation(const Frame& f) {
    if(f.garage) return GarageSituation;
    switch(View(f)) {
    case ViewMode::InCar: return InCarSituation;
    case ViewMode::Live: return SpottingSituation;
    default: return ReplaySituation;
    }
}
int ReferenceCar(const Frame& f) {
    if((!f.onTrack || f.replay) && f.camera>=0 && f.camera<MaxCars) return f.camera;
    return f.player;
}
Session ParseSession(const std::string& yaml, int sessionNumber) {
    Session s; s.number = sessionNumber;
    std::string section, subSession, sessionId;
    Driver driver; double tank=-1, fuelFraction=-1;
    int listIndent = -1, driverIndent = -1, currentSession = -1;
    bool inDrivers = false;
    bool inResults = false, hasResult = false;
    int resultsIndent=-1, resultEntryIndent=-1, resultIndex=-1;
    SessionResult result;
    int gridIndex=-1,gridPosition=0;
    auto saveGrid=[&] { if(gridIndex>=0 && gridIndex<MaxCars && gridPosition>0 && gridPosition<=MaxCars) s.gridPositions[gridIndex]=gridPosition; gridIndex=-1; gridPosition=0; };
    auto saveResult = [&] {
        if(hasResult && currentSession==sessionNumber && resultIndex>=0 && resultIndex<MaxCars)
            s.results[resultIndex]=result;
        result={}; resultIndex=-1; hasResult=false;
    };
    auto save = [&] {
        if (driver.index >= 0 && driver.index < MaxCars && !driver.paceCar && !driver.spectator) {
            driver.brand=CarBrand(driver.carPath,driver.carName); s.drivers.push_back(driver);
        }
        driver = {};
    };
    size_t start = 0;
    while (start < yaml.size()) {
        auto end = yaml.find('\n', start); if (end == yaml.npos) end = yaml.size();
        std::string_view line(yaml.data() + start, end - start); start = end + 1;
        if (line.size() >= 3 && line.substr(0,3) == "\xef\xbb\xbf") line.remove_prefix(3);
        int indent = 0; while (indent < static_cast<int>(line.size()) && line[indent] == ' ') ++indent;
        line.remove_prefix(indent);
        if (line.empty() || line.front() == '#' || line == "\r") continue;
        const bool listEntry=line.substr(0,2)=="- ";
        if(inResults && (indent<resultsIndent || (indent==resultsIndent && !listEntry))) {
            saveResult(); inResults=false;
        }
        if (inDrivers && indent <= listIndent && line.substr(0,2) != "- ") { save(); inDrivers = false; }
        if (indent == 0 && line.substr(0,2) != "- ") {
            if(section=="QualifyResultsInfo") saveGrid();
            section = Trim(line.substr(0, line.find(':'))); currentSession = -1;
        }
        if (section == "DriverInfo" && !inDrivers && line.substr(0,16) == "DriverPitTrkPct:") {
            const double pct=Number(Trim(line.substr(16))); if(std::isfinite(pct) && pct>=0 && pct<1) s.pitStallPct=pct;
        }
        if (section == "DriverInfo" && line.substr(0,8) == "Drivers:") {
            inDrivers = true; listIndent = indent; continue;
        }
        if (inDrivers && line.substr(0,2) == "- " && (driverIndent < 0 || indent == driverIndent)) {
            save(); driverIndent = indent; line.remove_prefix(2);
        }
        const auto colon = line.find(':'); if (colon == line.npos) continue;
        const std::string key = Trim(line.substr(0,colon)), value = Trim(line.substr(colon+1));
        if(section=="QualifyResultsInfo") {
            if(listEntry && key=="- Position") { saveGrid(); const double n=Number(value); if(std::isfinite(n) && n>=0 && n<MaxCars) gridPosition=static_cast<int>(n)+1; }
            if(key=="CarIdx") { const double n=Number(value); if(std::isfinite(n) && n>=0 && n<MaxCars) gridIndex=static_cast<int>(n); }
        }
        if(section=="CameraInfo") {
            if(key=="- GroupNum") {
                const double n=Number(value);
                if(std::isfinite(n) && n>=0 && n<=65535) s.cameras.push_back({static_cast<int>(n)});
            } else if(!s.cameras.empty()) {
                if(key=="GroupName") s.cameras.back().name=value;
                if(key=="IsScenic" || key=="IsSenic") s.cameras.back().scenic=value=="true" || value=="1";
            }
        }
        if (section == "CarSetup" && !value.empty()) {
            if (key == "UpdateCount" && indent == 1) s.setupUpdate = Integer(value,-1);
            else s.setup.emplace_back(key,value);
        }
        if (section == "WeekendInfo") {
            if (key == "IncidentWarningInitialLimit" && value!="unlimited") { const double n=Number(value); if(std::isfinite(n) && n>0 && n<1000000) s.incidentWarning=static_cast<int>(n); }
            if (key == "IncidentLimit" && value!="unlimited") { const double n=Number(value); if(std::isfinite(n) && n>0 && n<1000000) s.incidentLimit=static_cast<int>(n); }
            if (key == "TrackLength") s.trackMeters = Number(value)*(value.find("km")!=std::string::npos?1000:1);
            if (key == "TrackPitSpeedLimit") { const double limit=Number(value); if(std::isfinite(limit) && limit>0) s.pitSpeedLimit=limit/(value.find("mph")!=std::string::npos?2.2369:3.6); }
            if (key == "TrackDisplayName") s.track = value;
            if (key == "DCRuleSet") s.ruleSet = value;
            if (key == "TrackID") s.trackId = Integer(value,-1);
            if (key == "TrackNorthOffset") s.trackNorthOffset = Number(value);
            if (key == "TrackName") s.trackName = value;
            if (key == "TrackConfigName") s.trackConfig = value;
            if (key == "SessionID") sessionId = value;
            if (key == "SubSessionID") subSession = value;
        }
        if (section == "SessionInfo") {
            if (key == "- SessionNum" || key == "SessionNum") {
                saveResult(); inResults=false; currentSession = Integer(value,-1);
            }
            if (key == "SessionType" && currentSession == sessionNumber) s.type = value;
            if(key=="SessionTrackRubberState" && currentSession==sessionNumber) s.trackUsage=value;
            if (key == "SessionLaps" && currentSession == sessionNumber && value!="unlimited") {
                const double n=Number(value); if(std::isfinite(n) && n>=1 && n<32767) s.totalLaps=static_cast<int>(n);
            }
            if(key=="ResultsPositions" && currentSession==sessionNumber) {
                inResults=true; resultsIndent=indent; resultEntryIndent=-1; continue;
            }
            if(inResults) {
                if(listEntry && (resultEntryIndent<0 || indent==resultEntryIndent)) {
                    saveResult(); hasResult=true; resultEntryIndent=indent;
                }
                std::string field=key;
                if(field.starts_with("- ")) field.erase(0,2);
                const double number=Number(value);
                if(hasResult && std::isfinite(number)) {
                    if(field=="CarIdx" && number>=0 && number<MaxCars && number==std::floor(number)) resultIndex=static_cast<int>(number);
                    if(field=="Position" && number>=1 && number<=MaxCars) result.position=static_cast<int>(number);
                    // SDK YAML class positions are zero based; telemetry positions are one based.
                    if(field=="ClassPosition" && number>=0 && number<MaxCars) result.classPosition=static_cast<int>(number)+1;
                    if(field=="FastestTime") result.best=ValidTime(number)?number:-1;
                    if(field=="LastTime") result.last=ValidTime(number)?number:-1;
                    if(field=="LapsComplete" && number>=0 && number<1000000) result.completed=static_cast<int>(number);
                }
            }
        }
        if(section=="DriverInfo" && !inDrivers) {
            if(key=="DriverCarFuelMaxLtr") tank=Number(value);
            if(key=="DriverCarMaxFuelPct") fuelFraction=Number(value);
        }
        if (section == "DriverInfo" && !inDrivers && key == "DriverCarIdx") s.player = Integer(value,-1,0,MaxCars-1);
        if (!inDrivers) continue;
        if (key == "CarIdx") driver.index = Integer(value,-1,0,MaxCars-1);
        if (key == "CarScreenName") driver.carName = value;
        if (key == "CarPath") driver.carPath = value;
        if (key == "UserName") driver.name = value;
        if (key == "CarNumber") driver.number = value;
        if(key=="CarClassEstLapTime") { const double n=Number(value); driver.estimatedLapTime=ValidTime(n)?n:-1; }
        if (key == "CarClassID") driver.classId = Integer(value,0);
        if (key == "CarClassShortName") driver.className = value;
        if (key == "CarClassColor") driver.classColor = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 0));
        if (key == "LicString") driver.license = value;
        if (key == "LicColor") driver.licenseColor = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 0));
        if (key == "CurDriverIncidentCount") driver.incidentCount = Integer(value,-1);
        if (key == "IRating") driver.rating = Integer(value,0);
        if (key == "CarIsPaceCar") driver.paceCar = value == "1" || value == "true";
        if (key == "IsSpectator") driver.spectator = value == "1" || value == "true";
    }
    if(section=="QualifyResultsInfo") saveGrid();
    if (inDrivers) save();
    if (inResults) saveResult();
    if(std::isfinite(tank) && tank>0 && std::isfinite(fuelFraction) && fuelFraction>0 && fuelFraction<=1) s.fuelCapacity=tank*fuelFraction;
    s.key = sessionId + ":" + subSession;
    return s;
}
std::string CarBrand(const std::string& carPath, const std::string& carName) {
    // CarPath fragments, most specific first; ^ matches only at the start. The logo atlas uses the same names.
    static constexpr std::pair<const char*,const char*> brands[]={
        {"astonmartin","Aston Martin"},{"mclaren","McLaren"},{"mercedes","Mercedes"},{"amg","Mercedes"},{"lamborghini","Lamborghini"},
        {"porsche","Porsche"},{"ferrari","Ferrari"},{"bmw","BMW"},{"audi","Audi"},{"acura","Acura"},{"cadillac","Cadillac"},
        {"corvette","Chevrolet"},{"camaro","Chevrolet"},{"chevy","Chevrolet"},{"chevrolet","Chevrolet"},{"silverado","Chevrolet"},
        {"mustang","Ford"},{"fordgt","Ford"},{"ford","Ford"},{"toyota","Toyota"},{"supra","Toyota"},{"gr86","Toyota"},{"camry","Toyota"},
        {"honda","Honda"},{"hyundai","Hyundai"},{"nissan","Nissan"},{"mazda","Mazda"},{"mx5","Mazda"},{"ligier","Ligier"},{"dallara","Dallara"},
        {"superformula","Dallara"},{"radical","Radical"},{"lotus","Lotus"},{"renault","Renault"},{"williams","Williams"},{"subaru","Subaru"},
        {"volkswagen","Volkswagen"},{"vw","Volkswagen"},{"kia","Kia"},{"pontiac","Pontiac"},{"riley","Riley"},{"tatuus","Tatuus"},
        {"dodge","Dodge"},{"holden","Holden"},{"buick","Buick"},{"lexus","Lexus"},{"skipbarber","Skip Barber"},{"^ruf","RUF"},{"^ray","Ray"}};
    std::string path=carPath; for(char& c:path) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for(const auto& [fragment,brand]:brands)
        if(fragment[0]=='^'?path.rfind(fragment+1,0)==0:path.find(fragment)!=std::string::npos) return brand;
    const auto first=carName.find_first_not_of(' ');
    return first==std::string::npos?std::string{}:carName.substr(first,carName.find(' ',first)-first);
}
std::array<int,MaxCars> IRatingChanges(const Session& s,const Frame& f) {
    std::array<int,MaxCars> change; change.fill(NoRatingChange);
    if(s.type!="Race") return change;
    // iRacing's rating curve: e = exp(-rating / (1600 / ln 2)) turns ratings into win chances.
    const double scale=1600/std::log(2.0);
    // Rated cars with a class position, grouped by class and ranked by class position.
    struct Entry { int classId, position, index; double e; };
    std::vector<Entry> entries;
    for(const auto& d:s.drivers)
        if(d.rating>0 && d.index>=0 && d.index<MaxCars && f.cars[d.index].classPosition>0)
            entries.push_back({d.classId,f.cars[d.index].classPosition,d.index,std::exp(-d.rating/scale)});
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.classId!=b.classId?a.classId<b.classId:a.position<b.position;});
    for(size_t begin=0;begin<entries.size();) {
        size_t end=begin; while(end<entries.size() && entries[end].classId==entries[begin].classId) ++end;
        const double n=static_cast<double>(end-begin);
        if(n>=2) for(size_t i=begin;i<end;++i) {
            const double ei=entries[i].e; double expected=-.5; // Drivers i is expected to beat; j==i adds 0.5.
            for(size_t j=begin;j<end;++j) { const double ej=entries[j].e; expected+=(1-ei)*ej/((1-ej)*ei+(1-ei)*ej); }
            const double position=static_cast<double>(i-begin+1), fudge=(n/2-position)/100;
            change[entries[i].index]=static_cast<int>(std::lround((n-position-expected-fudge)*200/n));
        }
        begin=end;
    }
    return change;
}
void WeatherHistory::Update(const Session& s,const Frame& f) {
    // Watching live counts even with iRacing's replay flag; old footage never does.
    if(!f.connected || View(f)==ViewMode::Replay) return;
    if(key_!=s.key || session_!=f.session || (count_ && f.time<At(count_-1).time)) { Reset(); key_=s.key; session_=f.session; }
    if(count_ && f.time-At(count_-1).time<30) return;
    samples_[cursor_]={f.time,f.trackTemperature,f.airTemperature,f.precipitation,f.trackWetness>0?static_cast<double>(f.trackWetness):std::numeric_limits<double>::quiet_NaN(),f.humidity};
    cursor_=(cursor_+1)%Size; count_=std::min(Size,count_+1);
}
double WeatherHistory::Change(double Sample::* value) const {
    if(count_<2) return std::numeric_limits<double>::quiet_NaN();
    const auto& now=At(count_-1);
    for(int i=0;i<count_-1;++i) {
        const auto& then=At(i);
        if(now.time-then.time>601) continue;
        return now.time-then.time>=299?now.*value-then.*value:std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}
int RelativeRowCount(int requested) { int count=std::clamp(requested,1,MaxCars-1); return count%2?count:count+1; }
std::vector<Row> Relative(const Session& s, const Frame& f, int count, bool ownClass, bool hidePit) {
    std::vector<Row> rows;
    rows.reserve(s.drivers.size());
    const Driver* me = Player(s,f);
    const int reference=ReferenceCar(f);
    if (!me || reference < 0 || reference >= MaxCars) return rows;
    const auto& mine = f.cars[reference];
    if (!OnCircuit(mine)) {
        Row row{*me,WithResults(s,f,reference)}; row.player=true; row.gap=0;
        if(s.type=="Race" && row.car.position<=0 && s.gridPositions[reference]>0) row.car.position=s.gridPositions[reference];
        rows.push_back(row); return rows;
    }
    const double lapTime = LapTime(s,f);
    std::array<int,MaxCars> livePositions{},liveClassPositions{};
    if(s.type=="Race") {
        auto field=Standings(s,f,MaxCars,false); LiveRaceOrder(s,f,field);
        for(const auto& row:field) { livePositions[row.driver.index]=row.car.position; liveClassPositions[row.driver.index]=row.car.classPosition; }
    }
    for (const auto& d : s.drivers) {
        if (d.index < 0 || d.index >= MaxCars || (ownClass && d.classId != me->classId)) continue;
        const auto c = WithResults(s,f,d.index);
        if (!OnCircuit(c) || (hidePit && c.pit && d.index!=reference)) continue;
        double diff = c.pct - mine.pct; diff -= std::floor(diff + 0.5);
        Row row{d,c}; row.player = d.index == reference;
        if(livePositions[d.index]>0) { row.car.position=livePositions[d.index]; row.car.classPosition=liveClassPositions[d.index]; }
        if(s.type=="Race" && c.lap >= 0 && mine.lap >= 0)
            row.lapDifference = static_cast<int>(std::round(c.lap + c.pct - mine.lap - mine.pct));
        if (row.player) row.gap = 0;
        else if (lapTime > 0) {
            // Same-class SDK estimates account for nonuniform speed around the circuit.
            if (c.estTime>=0 && mine.estTime>=0 && (ValidTime(d.estimatedLapTime) || d.classId==me->classId)) {
                double delta = (Phase(d,c,lapTime)-Phase(*me,mine,lapTime))*lapTime;
                delta -= std::floor(delta / lapTime + 0.5) * lapTime;
                row.gap = -delta;
            } else row.gap = -diff * lapTime;
        }
        if(!row.player && !std::isfinite(row.gap) && d.classId==me->classId && c.estTime>0 && mine.estTime>0 && std::abs(c.pct-mine.pct)<.5) row.gap=mine.estTime-c.estTime;
        rows.push_back(row);
    }
    // Select each side independently so a crowded side cannot displace the reference.
    const int half=RelativeRowCount(count)/2;
    auto distance=[&](const Row& row) { double value=row.car.pct-mine.pct; return value-std::floor(value+.5); };
    std::vector<Row> ahead,behind; ahead.reserve(rows.size()); behind.reserve(rows.size()); Row referenceRow; bool found=false;
    for(auto& row:rows) {
        if(row.player) { referenceRow=std::move(row); found=true; }
        else if(distance(row)>0 || (distance(row)==0 && row.driver.index<reference)) ahead.push_back(std::move(row));
        else behind.push_back(std::move(row));
    }
    auto closest=[&](const Row& a,const Row& b) { const double da=std::abs(distance(a)),db=std::abs(distance(b)); return da!=db?da<db:a.driver.index<b.driver.index; };
    std::sort(ahead.begin(),ahead.end(),closest); std::sort(behind.begin(),behind.end(),closest);
    if(ahead.size()>static_cast<size_t>(half)) ahead.resize(half);
    if(behind.size()>static_cast<size_t>(half)) behind.resize(half);
    rows.clear(); rows.reserve(ahead.size()+behind.size()+1);
    for(auto it=ahead.rbegin();it!=ahead.rend();++it) rows.push_back(std::move(*it));
    if(found) rows.push_back(std::move(referenceRow));
    for(auto& row:behind) rows.push_back(std::move(row));
    return rows;
}
std::vector<Row> Standings(const Session& s, const Frame& f, int count, bool ownClass, bool fallbackByRating) {
    std::vector<Row> rows;
    rows.reserve(s.drivers.size());
    count = std::max(1,count);
    const Driver* me = Player(s,f);
    for (const auto& d : s.drivers) {
        if (d.index < 0 || d.index >= MaxCars || (ownClass && me && d.classId != me->classId)) continue;
        auto c = WithResults(s,f,d.index);
        if(s.type=="Race" && s.gridPositions[d.index]>0 && (f.state<4 || c.position<=0)) { c.position=s.gridPositions[d.index]; if(f.state<4) c.classPosition=0; }
        Row r{d,c}; r.player = d.index == ReferenceCar(f);
        // F2 is a race leader gap; outside races display best lap times instead.
        if (s.type == "Race" && c.f2Time >= 0) r.gap = c.f2Time;
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        if(s.type!="Race") {
            const bool av=ValidTime(a.car.best), bv=ValidTime(b.car.best);
            if(av!=bv) return av;
            if(av && a.car.best!=b.car.best) return a.car.best<b.car.best;
            if(!av && fallbackByRating && a.driver.rating!=b.driver.rating) return a.driver.rating>b.driver.rating;
            return CarNumberLess(a.driver,b.driver);
        }
        const bool aRanked=a.car.position>0, bRanked=b.car.position>0;
        if(aRanked!=bRanked) return aRanked;
        if(aRanked && a.car.position!=b.car.position) return a.car.position<b.car.position;
        if(a.car.position<=0 && b.car.position<=0 && fallbackByRating && a.driver.rating!=b.driver.rating) return a.driver.rating>b.driver.rating;
        return CarNumberLess(a.driver,b.driver);
    });
    // Mixing progress comparisons with car-number fallback in one comparator can
    // form a cycle when a third car has no progress. Order known slots separately.
    if(s.type=="Race" && f.state==4) SortKnownProgress(rows,true);
    if(!rows.empty() && s.type!="Race") {
        // Re-rank from the merged best laps so an older result position cannot hide a newer lap.
        for(size_t i=0;i<rows.size();++i) {
            auto& row=rows[i]; row.car.position=static_cast<int>(i)+1;
            row.car.classPosition=1;
            for(size_t j=0;j<i;++j) if(rows[j].driver.classId==row.driver.classId) ++row.car.classPosition;
            if(ValidTime(row.car.best) && ValidTime(rows.front().car.best))
                row.gap=std::max(0.0,row.car.best-rows.front().car.best);
        }
    }
    if(!rows.empty() && s.type == "Race") {
        for(size_t i=0;i<rows.size();++i) {
            auto& row=rows[i]; if(row.car.position<=0) row.car.position=i?rows[i-1].car.position+1:1;
            if(row.car.classPosition<=0) { row.car.classPosition=1; for(size_t j=0;j<i;++j) if(rows[j].driver.classId==row.driver.classId) row.car.classPosition=std::max(row.car.classPosition,rows[j].car.classPosition+1); }
        }
        const auto leader=rows.front();
        for(auto& row:rows) {
            if(!std::isfinite(row.gap)) row.gap=LiveGap(s,f,leader,row);
            if(ownClass && std::isfinite(row.gap) && std::isfinite(leader.gap)) row.gap=std::max(0.0,row.gap-leader.gap);
            if(row.car.lap >= 0 && leader.car.lap >= 0 && row.car.pct >= 0 && leader.car.pct >= 0)
                row.lapDifference=std::max(0,static_cast<int>(std::floor(leader.car.lap+leader.car.pct-row.car.lap-row.car.pct)));
        }
    }
    if (rows.size() > static_cast<size_t>(std::max(1,count))) {
        auto p = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.player; });
        size_t offset = 0;
        if (p != rows.end() && p - rows.begin() >= count) offset = std::min(rows.size() - count, static_cast<size_t>(p - rows.begin() - count / 2));
        rows = std::vector<Row>(rows.begin() + offset, rows.begin() + offset + count);
    }
    return rows;
}
std::vector<StandingsGroup> SelectStandings(const Session& s, const Frame& f, const StandingsOptions& options) {
    // Rank the complete field first, then select leaders and the reference car's neighbours.
    auto field=Standings(s,f,MaxCars,false,options.fallbackByRating);
    if(options.liveGaps) LiveRaceOrder(s,f,field);
    const Driver* me=Player(s,f);
    std::vector<StandingsGroup> groups;
    for(const auto& row:field) {
        if(options.ownClass && (!me || row.driver.classId!=me->classId)) continue;
        const int id=options.separateClasses?row.driver.classId:-1;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return g.classId==id;});
        if(group==groups.end()) {
            groups.push_back({id,options.separateClasses?row.driver.className:"Overall",row.driver.classColor,{}});
            group=groups.end()-1;
            if(group->name.empty()) group->name="Class "+std::to_string(id);
        }
        group->rows.push_back(row);
    }
    const bool anyReference=std::any_of(field.begin(),field.end(),[](const auto& r){return r.player;});
    for(auto& group:groups) {
        auto& rows=group.rows;
        group.cars=static_cast<int>(rows.size());
        for(const auto& row:rows) if(row.car.best>0 && (group.best<0 || row.car.best<group.best)) group.best=row.car.best;
        for(size_t i=0;i<rows.size();++i) if(rows[i].car.classPosition<=0) rows[i].car.classPosition=static_cast<int>(i)+1;
        if(options.separateClasses && !rows.empty()) {
            const auto leader=rows.front();
            for(auto& row:rows) {
                if(std::isfinite(row.gap) && std::isfinite(leader.gap)) row.gap=std::max(0.0,row.gap-leader.gap);
                row.lapDifference=0;
                if(s.type=="Race" && row.car.lap>=0 && leader.car.lap>=0 && row.car.pct>=0 && leader.car.pct>=0)
                    row.lapDifference=std::max(0,static_cast<int>(std::floor(leader.car.lap+leader.car.pct-row.car.lap-row.car.pct)));
            }
        }
        if(s.type=="Race" && options.liveGaps && !rows.empty()) {
            const auto leader=rows.front();
            for(auto& row:rows) { row.gap=LiveGap(s,f,leader,row); row.lapDifference=OnCircuit(leader.car) && OnCircuit(row.car)?std::max(0,static_cast<int>(std::floor(Progress(leader.car)-Progress(row.car)))):0; }
        }
        // Calculate against the actual preceding car before hiding unselected rows.
        for(size_t i=1;i<rows.size();++i) {
            auto& row=rows[i]; const auto& ahead=rows[i-1];
            if(s.type=="Race" && options.liveGaps) row.interval=LiveGap(s,f,ahead,row);
            else if(std::isfinite(row.gap) && std::isfinite(ahead.gap) && row.gap>=ahead.gap) row.interval=row.gap-ahead.gap;
            if(s.type=="Race" && row.car.lap>=0 && ahead.car.lap>=0 && row.car.pct>=0 && ahead.car.pct>=0)
                row.intervalLaps=std::max(0,static_cast<int>(std::floor(ahead.car.lap+ahead.car.pct-row.car.lap-row.car.pct)));
        }
        const auto reference=std::find_if(rows.begin(),rows.end(),[](const auto& r){return r.player;});
        const int center=reference==rows.end()?-1:static_cast<int>(reference-rows.begin());
        // A class without the reference car has nobody to be ahead of or behind: it shows its own number of leaders.
        const int leaders=std::clamp(options.separateClasses && anyReference && center<0?options.otherClasses:options.leaders,0,MaxCars);
        std::vector<Row> selected;
        for(int i=0;i<static_cast<int>(rows.size());++i)
            if(i<leaders || (center>=0 && i>=center-std::clamp(options.ahead,0,MaxCars) && i<=center+std::clamp(options.behind,0,MaxCars)))
                selected.push_back(rows[i]);
        rows=std::move(selected);
    }
    // With no rows chosen a class keeps its header, so the field stays complete.
    std::erase_if(groups,[](const auto& g){return g.cars==0;});
    return groups;
}
bool PitLimiterVisibility::Update(const Session& s,const Frame& f) {
    if(key_!=s.key || session_!=f.session || player_!=f.player || f.time<time_) { Reset(); key_=s.key; session_=f.session; player_=f.player; }
    time_=f.time; warning_=false;
    if(!f.connected || !f.onTrack || f.replay || !std::isfinite(f.time)) { inPit_=exited_=false; surface_=-1; return false; }
    const int surface=f.player>=0 && f.player<MaxCars?f.cars[f.player].surface:-1;
    const bool inLane=f.pit || f.inStall || surface==1, on=f.limiterKnown && f.limiter;
    if(inPit_ && !inLane) { inPit_=false; exited_=true; surface_=surface; warning_=on; return on; }
    inPit_=inLane;
    if(inLane || (surface==2 && surface_!=2)) exited_=false;
    surface_=surface;
    // On the way in the limiter belongs on; anywhere else outside the pit lane it should be off.
    warning_=on && !inLane && (exited_ || surface!=2);
    return on || (!exited_ && (inLane || surface==2));
}
const wchar_t* DialName(int dial) {
    constexpr const wchar_t* names[]={L"Brake bias",L"ABS",L"TC",L"TC 2",L"Throttle map",L"Fuel mixture",L"Engine braking"};
    static_assert(std::size(names)==DialCount);
    return dial>=0 && dial<DialCount?names[dial]:L"";
}
namespace {
// A setup key in lower case without spaces or punctuation: "TC setting" and "TcSetting" alike.
std::string SetupKey(std::string_view key) {
    std::string out; for(char c:key) if(std::isalnum(static_cast<unsigned char>(c))) out+=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}
std::array<SetupValue,DialCount> SetupValues(const Session& s) {
    // What iRacing calls each dial in the setups that list it (InCarDials and the like), varying with the car.
    static const std::vector<std::string> keys[DialCount]={
        {"brakepressurebias","brakebias","frontbrakebias","brakebalance","basebrakebias","brakebiassetting"},
        {"abssetting","abs","abslevel","absmap"},
        {"tractioncontrolsetting","tcsetting","tractioncontrol","tc1setting","tractioncontrol1","tractioncontrolgain","tcgain"},
        {"tc2setting","tractioncontrol2","tractioncontrol2setting","tractioncontrolslip","tcslip"},
        {"throttleshapesetting","throttleshape","throttlemap","throttlemapsetting"},
        {"fuelmixture","fuelmixturesetting","fuelmap","fuelmapsetting"},
        {"enginebraking","enginebrakingsetting"}};
    std::array<SetupValue,DialCount> values{};
    for(const auto& [name,text]:s.setup) {
        const std::string key=SetupKey(name);
        for(int dial=0;dial<DialCount;++dial) {
            if(values[dial].found || std::find(keys[dial].begin(),keys[dial].end(),key)==keys[dial].end()) continue;
            char* end=nullptr; const double value=std::strtod(text.c_str(),&end);
            if(end==text.c_str() || !std::isfinite(value)) continue; // Not a number, such as "Off".
            const auto point=std::string_view(text.c_str(),end-text.c_str()).find('.');
            values[dial]={true,value,point==std::string_view::npos?0:static_cast<int>(end-text.c_str()-point-1),text.find('%')!=std::string::npos};
        }
    }
    return values;
}
std::vector<DialChange> SetupChanges(const std::array<SetupValue,DialCount>& values,const Frame& f) {
    std::vector<DialChange> changes;
    for(int dial=0;dial<DialCount;++dial) {
        const auto& setup=values[dial];
        if(!setup.found || !(f.dialsKnown>>dial&1) || !std::isfinite(f.dials[dial])) continue;
        // Within the setup's own rounding is the same.
        const double current=f.dials[dial];
        if(std::abs(current-setup.value)<=.5*std::pow(10.,-setup.decimals)+1e-6) continue;
        // A brake bias this far off is on another scale, such as an offset, not a changed value.
        if(dial==BrakeBiasDial && std::abs(current-setup.value)>25) continue;
        changes.push_back({dial,setup.value,current,setup.decimals,setup.percent});
    }
    return changes;
}
const std::vector<DialChange>& SetupDials::Update(const Session& s,const Frame& f) {
    if(key_!=s.key || session_!=f.session || player_!=f.player || setupUpdate_!=s.setupUpdate || setupSize_!=s.setup.size()) {
        key_=s.key; session_=f.session; player_=f.player; setupUpdate_=s.setupUpdate; setupSize_=s.setup.size(); ignored_={};
        values_=SetupValues(s);
    }
    const bool inCar=f.connected && f.onTrack && !f.replay;
    if(inCar && !inCar_) entered_=f.time;
    inCar_=inCar; changes_.clear();
    if(!inCar) return changes_;
    auto all=SetupChanges(values_,f);
    const bool justIn=entered_>=0 && f.time>=entered_ && f.time-entered_<2;
    for(const auto& change:all) {
        if(justIn) ignored_[change.dial]=true;
        if(!ignored_[change.dial]) changes_.push_back(change);
    }
    return changes_;
}
void FuelCalculator::Reset() { *this = {}; }
void FuelCalculator::Update(const Session& s, const Frame& f, bool excludeYellowLaps) {
    if (!f.connected || !f.fuelKnown || f.replay) { anchored_ = false; preStartTime_=-1; return; }
    if (key_ != s.key || session_ != f.session || player_ != f.player || f.time < time_) {
        Reset(); key_ = s.key; session_ = f.session; player_ = f.player;
    }
    if(!f.onTrack) { anchored_=false; preStartTime_=-1; return; }
    if(s.type=="Race" && f.state>0 && f.state<4) {
        // Formation and grid fuel must not be mixed into the first racing lap.
        anchored_=false; preStartTime_=f.time; previousFuel_=f.fuel; time_=f.time; return;
    }
    if(f.lapCompleted<0) { anchored_=false; return; }
    if(stintStartLap_<0 || f.fuel>previousFuel_+.05) stintStartLap_=f.lapCompleted+1;
    const bool neutralized = (f.flags & SessionFlag::Neutralized) != 0, caution = excludeYellowLaps && neutralized;
    if (!anchored_ || f.lapCompleted < lap_ || f.lapCompleted > lap_ + 1) {
        const bool observedStart=s.type=="Race" && f.state==4 && f.lapCompleted==0 && preStartTime_>=0 && f.time>=preStartTime_ && f.time-preStartTime_<=5;
        anchored_=true; dirty_=partial_=!observedStart; caution_=false; lap_=f.lapCompleted; fuel_=f.fuel; previousFuel_=f.fuel; time_=f.time;
        preStartTime_=-1;
    }
    dirty_ = dirty_ || f.pit || f.inStall || caution || f.fuel > previousFuel_ + 0.05;
    caution_ = caution_ || neutralized;
    if (f.lapCompleted != lap_) {
        const double burn = fuel_ - f.fuel, duration = f.time - time_;
        const bool whole = !partial_ && f.lapCompleted == lap_ + 1 && burn > 0.01 && duration > 10 && std::isfinite(burn);
        if (whole) {
            history_[historyCursor_] = {f.lapCompleted, burn, !dirty_, caution_};
            historyCursor_ = (historyCursor_ + 1) % 10; historyCount_ = std::min(10, historyCount_ + 1);
        }
        if (!dirty_ && burn > 0.01 && duration > 10 && std::isfinite(burn)) {
            burns_[cursor_] = burn; cursor_ = (cursor_ + 1) % 10;
            used_ = std::min(10, used_ + 1); ++samples_; totalBurn_+=burn; last_ = burn;
            maximum_=std::max(maximum_,burn); minimum_=std::min(minimum_,burn);
        }
        lap_ = f.lapCompleted; fuel_ = f.fuel; time_ = f.time;
        dirty_ = f.pit || f.inStall || caution; partial_ = false; caution_ = neutralized;
    }
    previousFuel_ = f.fuel;
}
PitWindow CalculatePitWindow(const Session& s,const Frame& f,double distance,double burn,double pace) {
    PitWindow window;
    if(f.lapCompleted>=0 && f.lapCompleted<1000000) window.currentLap=f.lapCompleted+1;
    const bool consumptionKnown=f.connected && f.fuelKnown && !f.replay && std::isfinite(burn) && burn>0 && std::isfinite(f.fuel) && f.fuel>=0;
    if(consumptionKnown && std::isfinite(pace) && pace>0) window.timeEmpty=f.fuel/burn*pace;
    if(consumptionKnown && std::isfinite(distance) && distance>=0) window.fuelAtEnd=f.fuel-distance*burn;
    if(s.type!="Race" || !consumptionKnown || f.pit || f.inStall || f.lapCompleted<0 || f.lapCompleted>=1000000 || !std::isfinite(f.lapPct) || f.lapPct<0 || f.lapPct>=1 || !std::isfinite(distance) || distance<=0 || !std::isfinite(s.fuelCapacity) || s.fuelCapacity<=0) return window;
    const double progress=f.lapCompleted+f.lapPct, finish=progress+distance, stint=s.fuelCapacity/burn;
    if(finish>1000000 || !std::isfinite(stint) || stint<=0) return window;
    window.finishLap=static_cast<int>(std::ceil(finish-1e-6));
    if(window.fuelAtEnd>=0) { window.status=PitWindowStatus::NoStopNeeded; window.stops=0; return window; }
    // The next stop may happen during a lap. Later stops use lap crossings as approximate anchors.
    const double latest=std::min(progress+f.fuel/burn-1e-6,finish-1e-6);
    const double fullStint=std::max(0.0,std::ceil(stint-1e-6)-1);
    const double nextAnchor=std::floor(latest+stint-1e-6);
    double visits=1;
    if(finish>latest+stint+1e-6) {
        const double extra=std::max(0.0,finish-nextAnchor-stint);
        if(extra>1e-6 && fullStint<1) { window.status=PitWindowStatus::ExtraStopNeeded; return window; }
        visits=2+(extra>1e-6?std::ceil((extra-1e-6)/fullStint):0);
    }
    if(visits>10000) return window;
    window.stops=static_cast<int>(visits);
    const double earliest=window.stops==1?finish-stint:std::ceil(finish-stint-1e-6)-(window.stops-2)*fullStint-stint+1e-6;
    window.firstLap=std::max(window.currentLap,static_cast<int>(std::floor(std::max(0.0,earliest)))+1);
    window.lastLap=static_cast<int>(std::floor(std::max(0.0,latest)))+1;
    window.status=window.firstLap<=window.lastLap?PitWindowStatus::Open:PitWindowStatus::ExtraStopNeeded;
    // A driver entering now must still be able to reach the remaining scheduled stops.
    double reachable=progress;
    for(int i=1;i<window.stops;++i) reachable=std::floor(reachable+stint-1e-6);
    window.openNow=window.status==PitWindowStatus::Open && finish<=reachable+stint+1e-6 && progress<=latest;
    return window;
}
FuelPlan FuelCalculator::Plan(const Frame& f, const RacePrediction* prediction) const {
    FuelPlan p; p.samples = samples_; p.stintStartLap=stintStartLap_;
    p.historyCount = f.replay ? 0 : historyCount_;
    for (int i = 0; i < p.historyCount; ++i) p.history[i] = history_[(historyCursor_ + 10 - historyCount_ + i) % 10];
    if(prediction) { p.prediction=*prediction; p.lapsToFinish=prediction->finish.valid?prediction->finish.ownDistance:-1; }
    if (!used_ || !f.fuelKnown || f.replay) return p;
    double average=0, recent5=0, recent3=0;
    for (int i = 0; i < used_; ++i) average += burns_[i];
    const int count5=std::min(5,used_), count3=std::min(3,used_);
    for(int i=0;i<count5;++i) recent5+=burns_[(cursor_+9-i)%10];
    for(int i=0;i<count3;++i) recent3+=burns_[(cursor_+9-i)%10];
    p.estimates[0]={last_,-1,1};
    p.estimates[1]={average/used_,-1,used_};
    p.estimates[2]={recent5/count5,-1,count5};
    p.estimates[3]={maximum_,-1,samples_};
    p.estimates[4]={minimum_,-1,samples_};
    p.estimates[5]={samples_?totalBurn_/samples_:0,-1,samples_};
    p.estimates[6]={recent3/count3,-1,count3};
    if(p.lapsToFinish>=0) for(auto& estimate:p.estimates) estimate.add=std::max(0.0,p.lapsToFinish*estimate.consumption-f.fuel);
    return p;
}
Session DemoSession() {
    Session s; s.track = "Spa-Francorchamps"; s.type = "Race"; s.key = "demo"; s.player = 3; s.fuelCapacity=30; s.trackUsage="moderately low usage";
    s.cameras={{1,"Scenic",true},{7,"Cockpit"},{11,"Far Chase"},{12,"TV1"}};
    const char* names[] = {"Luca Weber", "Daniel Fischer", "Alex Martin", "You", "Sofia Laurent", "Ben Clarke", "Marco Rossi", "Thomas Meyer", "Emma Wilson", "Nico Berg"};
    const char* paths[] = {"porsche992rgt3","bmwm4gt3","ferrari296gt3","bmwm4gt3","mclaren720sgt3","mercedesamgevogt3","audir8lmsevo2gt3","fordmustanggt3","lamborghinievogt3","astonmartinvantagegt3"};
    for (int i = 0; i < 10; ++i) { s.drivers.push_back({i,1,2400+i*117,0xe7c451,false,false,names[i],std::to_string(12+i),"GT3"}); s.drivers.back().carPath=paths[i]; s.drivers.back().brand=CarBrand(paths[i],""); }
    return s;
}
SideTraffic SideTrafficOf(const Session& s,const Frame& f) {
    SideTraffic t;
    if(!f.connected || !f.onTrack || f.replay || f.player<0 || f.player>=MaxCars) return t;
    switch(f.spotter) {
    case 2: t.left=1; break; case 3: t.right=1; break; case 4: t.left=t.right=1; break;
    case 5: t.left=2; break; case 6: t.right=2; break; default: return t;
    }
    if((t.left && t.right) || !(s.trackMeters>0)) return t;
    // The nearest car within 10 m along the lap: beside you, the spotter says on which side.
    const double own=f.cars[f.player].pct; double nearest=std::numeric_limits<double>::quiet_NaN();
    for(int i=0;i<MaxCars;++i) {
        const auto& c=f.cars[i];
        if(i==f.player || c.pct<0 || c.surface<0 || c.surface==1 || c.pit || own<0) continue;
        double apart=c.pct-own; apart-=std::floor(apart+.5); apart*=s.trackMeters;
        if(std::abs(apart)<=10 && !(std::abs(apart)>=std::abs(nearest))) nearest=apart;
    }
    (t.left?t.leftAt:t.rightAt)=nearest;
    return t;
}
std::vector<CarBehind> CarsBehind(const Session& s,const Frame& f,double threshold,bool fasterOnly) {
    std::vector<CarBehind> cars;
    if(!f.connected || !f.onTrack || f.replay || f.player<0 || f.player>=MaxCars) return cars;
    const auto& own=f.cars[f.player];
    const auto me=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){ return d.index==f.player; });
    const double ownLap=me==s.drivers.end()?-1:(ValidTime(me->estimatedLapTime)?me->estimatedLapTime:DriverLapTime(s,f,f.player));
    if(own.pct<0 || own.pit || !ValidTime(ownLap)) return cars;
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.index==f.player || d.paceCar || d.spectator) continue;
        const double lap=ValidTime(d.estimatedLapTime)?d.estimatedLapTime:ownLap;
        if(fasterOnly && (d.classId==me->classId || !ValidTime(d.estimatedLapTime) || !ValidTime(me->estimatedLapTime) || lap>me->estimatedLapTime*.98)) continue;
        const auto& c=f.cars[d.index];
        if(c.pct<0 || c.surface<0 || c.surface==1 || c.pit) continue;
        // Behind as a share of the lap in time, then in the seconds that car needs for it.
        double behind=Phase(*me,own,ownLap)-Phase(d,c,lap); behind-=std::floor(behind); if(behind>.5) continue;
        if(behind*lap<=threshold) cars.push_back({d.index,behind*lap});
    }
    std::sort(cars.begin(),cars.end(),[](const CarBehind& a,const CarBehind& b){ return a.gap<b.gap; });
    return cars;
}
void RadarTracker::Update(const Session& s,const Frame& f,double reach) {
    cars_.clear();
    if(!f.connected || !f.onTrack || f.replay || f.player<0 || f.player>=MaxCars || !(s.trackMeters>0)) { Reset(); return; }
    const auto& own=f.cars[f.player]; if(own.pct<0) return;
    // A session clock that went back (a new session, a reset) forgets the lanes.
    if(std::any_of(seen_.begin(),seen_.end(),[&](double t){ return t>f.time; })) Reset();
    for(int i=0;i<MaxCars;++i) {
        const auto& c=f.cars[i];
        // Cars in the pit lane only beside yours in it: on track the pit wall is between you.
        if(i==f.player || c.pct<0 || c.surface<0 || c.surface==1 || c.pit!=own.pit) continue;
        double apart=c.pct-own.pct; apart-=std::floor(apart+.5); apart*=s.trackMeters;
        if(std::abs(apart)<=std::max(reach,Reach)) cars_.push_back({i,apart,0});
    }
    std::sort(cars_.begin(),cars_.end(),[](const RadarCar& a,const RadarCar& b){ return std::abs(a.metres)<std::abs(b.metres); });
    int left=0,right=0;
    switch(f.spotter) { case 2: left=1; break; case 3: right=1; break; case 4: left=right=1; break; case 5: left=2; break; case 6: right=2; break; default: break; }
    // Beside you: as many of the nearest cars as the spotter reports, those that had a side keeping it.
    // Candidates nearest first: a car within 5.5 m that had the side keeps it, else the nearest within 8 m takes it.
    std::vector<RadarCar*> beside;
    for(auto& c:cars_) if(std::abs(c.metres)<=Reach) beside.push_back(&c);
    // Two cars on a side are in two lanes: you are at the edge of three wide.
    std::vector<int> lanes; for(int i=1;i<=left;++i) lanes.push_back(-i); for(int i=1;i<=right;++i) lanes.push_back(i);
    std::vector<int> open;
    // A lane the spotter gave counts before one only guessed.
    for(int l:lanes) {
        auto had=[&](bool guessed) { return std::find_if(beside.begin(),beside.end(),[&](const RadarCar* c){ return !c->lane && std::abs(c->metres)<=Overlap && lane_[c->index]==l && guess_[c->index]==guessed && f.time-seen_[c->index]<=Hold; }); };
        auto kept=had(false); if(kept==beside.end()) kept=had(true);
        if(kept!=beside.end()) (*kept)->lane=l; else open.push_back(l);
    }
    for(int l:open) for(auto* c:beside) if(!c->lane) { c->lane=l; break; }
    // The others keep the lane they had for 3 s, also while the spotter no longer reports them but they are still
    // close: it clears as soon as the cars no longer touch, while the lap distance still has them a few metres apart.
    for(auto& c:cars_) {
        if(c.lane) { lane_[c.index]=c.lane; guess_[c.index]=false; side_[c.index]=c.lane>0?1:-1; seen_[c.index]=sided_[c.index]=f.time; }
        else if(f.time-seen_[c.index]<=Hold) c.lane=lane_[c.index];
    }
    // A car still near yours (within 8 m) or beside another one stays on its side as long as it is: the 3 s start again.
    for(auto& c:cars_) if(c.lane && f.time-seen_[c.index]>0)
        if(std::abs(c.metres)<Reach || std::any_of(cars_.begin(),cars_.end(),[&](const RadarCar& other){ return &other!=&c && std::abs(other.metres-c.metres)<CarLength; })) seen_[c.index]=f.time;
    // A car that overlaps yours is beside it, reported or not (the spotter says nothing while you are off the track,
    // nor for a moment as a car comes alongside): on the one side the spotter reports, as the other is clear; else on
    // the side it was last on, else the one without a car.
    // A car that was just beside another one keeps that side as it comes alongside, from a little further away.
    for(auto& c:cars_) if(!c.lane && std::abs(c.metres)<(side_[c.index] && f.time-paired_[c.index]<=Hold?Overlap:SideBySide)) {
        const bool leftTaken=std::any_of(cars_.begin(),cars_.end(),[](const RadarCar& o){ return o.lane<0 && std::abs(o.metres)<SideBySide; });
        const int reported=left && !right?-1:(right && !left?1:0);
        c.lane=reported?reported:(side_[c.index]?side_[c.index]:(leftTaken?1:-1));
        if(side_[c.index]!=c.lane) { side_[c.index]=c.lane; sided_[c.index]=f.time; }
        lane_[c.index]=c.lane; guess_[c.index]=true; seen_[c.index]=f.time;
    }
    // Side by side in your lane, ahead or behind: the spotter knows nothing of them, so they keep the side they had.
    std::vector<RadarCar*> line; for(auto& c:cars_) if(!c.lane) line.push_back(&c);
    std::sort(line.begin(),line.end(),[](const RadarCar* a,const RadarCar* b){ return a->metres<b->metres; });
    for(size_t i=0;i+1<line.size();) {
        auto* behind=line[i]; auto* ahead=line[i+1];
        // Once side by side, they stay so until they no longer overlap at all.
        const bool were=side_[behind->index] && side_[behind->index]==-side_[ahead->index] && f.time-paired_[behind->index]<=.5 && f.time-paired_[ahead->index]<=.5;
        if(ahead->metres-behind->metres>=(were?CarLength:SideBySide)) { ++i; continue; }
        // The side learnt last wins when both had the same one.
        int back=side_[behind->index], front=side_[ahead->index];
        if(!back && !front) front=-1;
        if(front && (!back || (front==back && sided_[ahead->index]>=sided_[behind->index]))) back=-front; else front=-back;
        behind->offset=back*.5; ahead->offset=front*.5;
        for(auto* c:{behind,ahead}) { if(side_[c->index]!=(c->offset>0?1:-1)) { side_[c->index]=c->offset>0?1:-1; sided_[c->index]=f.time; } paired_[c->index]=f.time; }
        i+=2;
    }
    // A third car of such a row goes beside its neighbour, on the other side.
    for(auto* c:line) if(!c->offset)
        for(const auto* other:line) if(other->offset && std::abs(other->metres-c->metres)<SideBySide) { c->offset=-other->offset; side_[c->index]=c->offset>0?1:-1; sided_[c->index]=paired_[c->index]=f.time; break; }
    // A car whose neighbour just went beside you stays on its side a moment, rather than jumping into your lane.
    for(auto* c:line) if(!c->offset && side_[c->index] && f.time-paired_[c->index]<.3) c->offset=side_[c->index]*.5;
    std::erase_if(cars_,[&](const RadarCar& c){ return std::abs(c.metres)>reach; });
}
int NorthTurnFromOffset(double offset) {
    if(!std::isfinite(offset)) return -1;
    const int turn=static_cast<int>(std::lround(offset*180/3.141592653589793+270))%360;
    return turn<0?turn+360:turn;
}
int NorthTurnFromHeadings(double yawNorth,double mapHeading) {
    if(!std::isfinite(yawNorth) || !std::isfinite(mapHeading)) return -1;
    const int turn=static_cast<int>(std::lround((yawNorth-mapHeading)*180/3.141592653589793))%360;
    return turn<0?turn+360:turn;
}
double RelativeWindAngle(const Frame& f,bool headingLive,double mapHeading) {
    const bool driving=f.onTrack && !f.replay && ReferenceCar(f)==f.player;
    const double heading=(driving || headingLive) && std::isfinite(f.yawNorth)?f.yawNorth:(driving?std::numeric_limits<double>::quiet_NaN():mapHeading);
    if(!f.connected || !std::isfinite(heading) || !std::isfinite(f.windDirection) || !std::isfinite(f.windSpeed) || f.windSpeed<=0) return std::numeric_limits<double>::quiet_NaN();
    // WindDir (where the wind comes from) and YawNorth (where the car points) share one
    // convention, so their difference is the source bearing relative to the car.
    // Cross-checked against iRdashies, which shows WindDir - YawNorth (+ half a turn for
    // where the wind blows to). https://sajax.github.io/irsdkdocs/telemetry/yawnorth.html
    constexpr double turn=6.283185307179586;
    double angle=std::fmod(f.windDirection-heading,turn);
    return angle<0?angle+turn:angle;
}
Frame DemoFrame(double time) {
    Frame f; f.connected = f.onTrack = f.fuelKnown = f.limiterKnown = true; f.player = 3; f.session = 0;
    f.incidentsKnown=f.timeLeftKnown=true; f.airTemperature=21.3; f.trackTemperature=32.4; f.timeOfDay=67500; f.windSpeed=3.3; f.trackWetness=1;
    f.yawNorth=0; f.windDirection=-.7;
    f.time = time; f.lapCompleted = 7; f.lapPct = .45; f.fuel = 27.4; f.timeLeft = 960; f.timeTotal = 2400; f.lapsLeft = 32767; f.bestLap = 138.2; f.lastLap = 139.4;
    f.pit = true; f.limiter = static_cast<int>(time / 4) % 2 == 0;
    for (int i=0;i<10;++i) { auto& c=f.cars[i]; c.position=c.classPosition=i+1; c.lap=8; c.pct=.45+(3-i)*.018; c.estTime=c.pct*138.2; c.f2Time=i*2.6; c.best=138.2+i*.13; c.last=139.4+i*.16; c.surface=3; }
    f.cars[7].pit = true; return f;
}
}
