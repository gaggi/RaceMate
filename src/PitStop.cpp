#include "Core.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace rm {
namespace {
bool Valid(double seconds) { return std::isfinite(seconds) && seconds>0; }
bool Pace(double seconds) { return std::isfinite(seconds) && seconds>10 && seconds<1800; }
// A reference time difference wrapped into one lap, in (-lap/2, lap/2].
double Wrap(double value,double lap) {
    value=std::fmod(value,lap); if(value<=-lap/2) value+=lap; if(value>lap/2) value-=lap; return value;
}
// Completed laps, from the current lap where the count is missing.
int Completed(const Car& car) { return car.completed>=0?car.completed:(car.lap>0?car.lap-1:-1); }
bool Has(std::string text,const char* part) {
    for(char& c:text) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text.find(part)!=std::string::npos;
}
const Driver* Find(const Session& s,int index) {
    const auto it=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){ return d.index==index; });
    return it==s.drivers.end()?nullptr:&*it;
}
}
void PitLaneLoss::Update(const Session& s,const Frame& f) {
    if(!f.connected) { visits_={}; time_=-1; return; }
    if(track_!=s.trackId) { *this={}; track_=s.trackId; }
    if(session_!=f.session || f.time<time_) { visits_={}; session_=f.session; }
    // Old footage would repeat or reorder visits.
    if(View(f)==ViewMode::Replay) { visits_={}; time_=f.time; return; }
    const double dt=f.time-time_; const bool continuous=time_>=0 && dt>0 && dt<=2;
    time_=f.time;
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) continue;
        const auto& car=f.cars[d.index]; auto& v=visits_[d.index];
        const double progress=Completed(car)>=0 && car.pct>=0 && car.pct<1?Completed(car)+car.pct:-1;
        if(!continuous || car.surface<0 || car.towing || progress<0 || car.estTime<0) { v={}; continue; }
        // Only a visit seen from its entry counts.
        if(car.pit && v.seen && !v.pit) { v.active=true; v.time=f.time; v.est=car.estTime; v.progress=progress; }
        if(!car.pit && v.active) {
            v.active=false;
            const double lap=d.estimatedLapTime, duration=f.time-v.time, distance=progress-v.progress, stationary=car.lastPitStationary;
            if(Valid(lap) && stationary>=0 && distance>0 && distance<1 && duration>=5 && duration<=300) {
                // The same stretch on track: CarIdxEstTime is the class reference time to each point of the lap.
                double reference=car.estTime-v.est; if(reference<0) reference+=lap;
                const double pace=Pace(car.best)?car.best:lap;
                const double loss=duration-stationary-reference*pace/lap;
                if(reference>0 && reference<lap && loss>=3 && loss<=120) {
                    samples_.push_back(loss);
                    if(samples_.size()>15) samples_.erase(samples_.begin());
                }
            }
        }
        v.seen=true; v.pit=car.pit;
    }
}
double PitLaneLoss::Loss() const {
    if(samples_.empty()) return seed_;
    auto sorted=samples_; std::sort(sorted.begin(),sorted.end());
    const size_t n=sorted.size();
    return n%2?sorted[n/2]:(sorted[n/2-1]+sorted[n/2])/2;
}
namespace {
// A lap fraction from a reference, wrapped to -0.5 to 0.5.
double Around(double pct,double reference) { double d=pct-reference; return d-std::round(d); }
// The median of lap fractions near each other, also across start/finish.
double CircularMedian(std::vector<double> values) {
    if(values.empty()) return -1;
    const double reference=values.front();
    for(double& v:values) v=Around(v,reference);
    std::nth_element(values.begin(),values.begin()+values.size()/2,values.end());
    const double m=reference+values[values.size()/2];
    return m-std::floor(m);
}
}
void PitLaneEnds::Update(const Session& s,const Frame& f) {
    if(!f.connected || View(f)==ViewMode::Replay) { cars_={}; time_=-1; return; }
    if(track_!=s.trackId) { *this={}; track_=s.trackId; }
    if(session_!=f.session || f.time<time_) { cars_={}; session_=f.session; }
    const double dt=f.time-time_; const bool continuous=time_>=0 && dt>0 && dt<=1;
    time_=f.time;
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) continue;
        const auto& car=f.cars[d.index]; auto& seen=cars_[d.index];
        if(!continuous || car.surface<0 || car.pct<0 || car.pct>=1) { seen={}; if(car.surface>=0 && car.pct>=0 && car.pct<1) seen={true,car.pit,car.pct}; continue; }
        if(seen.seen && car.pit!=seen.pit) {
            // Driven over the line, not towed or reset into the box: a short, plausible move.
            const double moved=Around(car.pct,seen.pct);
            const bool driven=car.surface!=1 && moved>=0 && (s.trackMeters<=0 || moved*s.trackMeters<=90*dt+10);
            if(driven) {
                const double at=seen.pct+moved/2-std::floor(seen.pct+moved/2);
                auto& samples=car.pit?entries_:exits_;
                samples.push_back(at); if(samples.size()>15) samples.erase(samples.begin());
            }
        }
        seen={true,car.pit,car.pct};
    }
}
double PitLaneEnds::Entry() const { return entries_.empty()?seedEntry_:CircularMedian(entries_); }
double PitLaneEnds::Exit() const { return exits_.empty()?seedExit_:CircularMedian(exits_); }
void BrakingRate::Update(const Frame& f) {
    const bool driving=f.connected && f.onTrack && View(f)==ViewMode::InCar && f.player>=0 && f.player<MaxCars && f.brake>=0;
    const double speed=driving?f.cars[f.player].speed:-1;
    const bool gap=lastTime_<0 || f.time<=lastTime_ || f.time-lastTime_>.5;
    const bool braking=driving && f.brake>=.6 && speed>12 && f.cars[f.player].surface==3 && !f.pit;
    if(startTime_>=0 && (!braking || gap)) {
        // A braking zone ends: its average, if it was long and hard enough to tell.
        const double span=lastTime_-startTime_, drop=startSpeed_-lastSpeed_;
        if(!gap && span>=.6 && drop>=8) { samples_.push_back(std::clamp(drop/span,3.0,40.0)); ++measured_; if(samples_.size()>15) samples_.erase(samples_.begin()); }
        startTime_=-1;
    }
    if(braking && startTime_<0) { startTime_=f.time; startSpeed_=speed; }
    lastTime_=driving?f.time:-1; lastSpeed_=speed;
}
double BrakingRate::Rate() const {
    if(samples_.empty()) return seed_;
    auto values=samples_; std::nth_element(values.begin(),values.begin()+values.size()/2,values.end());
    return values[values.size()/2];
}
double BrakingDistance(double speed,double target,double deceleration,double reaction) {
    if(!(speed>target) || !(deceleration>0)) return 0;
    return (speed*speed-target*target)/(2*deceleration)+reaction*speed;
}
void RefuelRate::Update(const Frame& f) {
    if(!f.connected || !f.inStall || !f.fuelKnown || View(f)==ViewMode::Replay) { lastTime_=lastFuel_=-1; startTime_=-1; return; }
    if(lastTime_>=0 && f.time>lastTime_ && f.fuel>lastFuel_+.005) {
        // From the first rise on: the flow, not the time before the hose connects.
        if(startTime_<0) { startTime_=lastTime_; startFuel_=lastFuel_; }
        const double span=f.time-startTime_, added=f.fuel-startFuel_;
        if(span>=2 && added>=1) rate_=added/span;
    }
    lastTime_=f.time; lastFuel_=f.fuel;
}
PitService PlannedService(const Session& s,const Frame& f) {
    PitService service;
    if(f.pitSvFlags&PitServiceFlag::Fuel) {
        double fuel=std::max(0.0,f.pitSvFuel);
        // The tank takes no more than it has room for.
        if(std::isfinite(s.fuelCapacity) && s.fuelCapacity>0 && f.fuelKnown) fuel=std::min(fuel,std::max(0.0,s.fuelCapacity-f.fuel));
        service.fuel=fuel;
    }
    for(unsigned flag:{PitServiceFlag::LeftFront,PitServiceFlag::RightFront,PitServiceFlag::LeftRear,PitServiceFlag::RightRear}) service.tires+=(f.pitSvFlags&flag)!=0;
    service.repair=std::isfinite(f.pitRepair) && f.pitRepair>0?f.pitRepair:0;
    return service;
}
bool SimultaneousService(const Session& s) { return Has(s.ruleSet,"IMSA"); }
double DefaultTireChange(const Session& s,const Driver& d) {
    // Source: iRacing 2026 Season 4 release notes, per class; other classes are left to the user.
    if(Has(d.className,"GT3")) return Has(s.ruleSet,"DTM")?7:20;
    if(Has(d.className,"GT4")) return 35;
    if(Has(d.className,"TCR")) return 40;
    if(Has(d.className,"GTE") || Has(d.className,"GT1") || Has(d.className,"LMP1")) return 20;
    return -1;
}
double ServiceTime(const PitService& service,double fuelRate,double tireChange,bool simultaneous) {
    const double fuel=service.fuel>0?(Valid(fuelRate)?service.fuel/fuelRate:-1):0;
    const double tires=service.tires>0?(Valid(tireChange)?tireChange*std::ceil(service.tires/2.0)/2:-1):0;
    if(fuel<0 || tires<0) return -1;
    // Default rules: tires and repairs only after the fueling. IMSA rules: everything at once.
    return simultaneous?std::max({fuel,tires,service.repair}):fuel+std::max(tires,service.repair);
}
PitRejoin PredictRejoin(const Session& s,const Frame& f,double loss,double pace) {
    PitRejoin r;
    if(!f.connected || View(f)==ViewMode::Replay || !Valid(loss) || !Pace(pace) || f.player<0 || f.player>=MaxCars) return r;
    const auto* own=Find(s,f.player); const auto& me=f.cars[f.player];
    const int completed=Completed(me)>=0?Completed(me):f.lapCompleted;
    if(!own || me.surface<0 || completed<0 || me.pct<0 || me.pct>=1 || me.estTime<0) return r;
    const double lap=Valid(own->estimatedLapTime)?own->estimatedLapTime:pace, scale=lap/pace;
    // Where the car would be loss seconds later, in reference time and in laps.
    double ghost=std::fmod(me.estTime-loss*scale,lap); if(ghost<0) ghost+=lap;
    const double progress=completed+me.pct-loss/pace;
    r.valid=true; r.loss=loss; r.pct=ghost/lap; r.position=r.classPosition=1;
    double nearest=lap;
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.index==f.player || d.paceCar || d.spectator) continue;
        const auto& car=f.cars[d.index];
        if(car.surface<0 || Completed(car)<0 || car.pct<0 || car.pct>=1) continue;
        const double lead=(Completed(car)+car.pct-progress)*pace; const bool sameClass=d.classId==own->classId;
        if(lead>0) { ++r.position; if(sameClass) ++r.classPosition; }
        // Neighbours in the race after the stop: smallest lead ahead and behind.
        auto keep=[&](int& index,double& gap,double seconds) { if(index<0 || seconds<gap) { index=d.index; gap=seconds; } };
        if(lead>0) { keep(r.overallAhead,r.overallAheadGap,lead); if(sameClass) keep(r.classAhead,r.classAheadGap,lead); }
        else { keep(r.overallBehind,r.overallBehindGap,-lead); if(sameClass) keep(r.classBehind,r.classBehindGap,-lead); }
        if(car.estTime<0) continue;
        const double gap=Wrap(car.estTime-ghost,lap);
        // The lap fraction follows the nearest car, so slow and fast sections place the marker correctly.
        if(std::abs(gap)<nearest) { nearest=std::abs(gap); r.pct=std::fmod(car.pct-gap/lap+1,1.0); }
        if(car.pit) continue;
        const double seconds=gap/scale;
        if(seconds>=0 && (r.ahead<0 || seconds<r.aheadGap)) { r.ahead=d.index; r.aheadGap=seconds; }
        if(seconds<0 && (r.behind<0 || -seconds<r.behindGap)) { r.behind=d.index; r.behindGap=-seconds; }
    }
    return r;
}
}
