#include "Core.h"
#include <algorithm>
#include <cmath>

namespace rm {
namespace {
constexpr double Epsilon=1e-7;
bool PaceTime(double value) { return std::isfinite(value) && value>10 && value<1800; }
bool Caution(const Frame& f) { return (f.flags&SessionFlag::Neutralized)!=0; }
int Completed(const Car& car) { return car.completed>=0?car.completed:(car.lap>0?car.lap-1:-1); }
double Mean(const double* values,int count) {
    if(!count) return -1;
    double sum=0; for(int i=0;i<count;++i) sum+=values[i]; return sum/count;
}
void Record(double* values,int capacity,int& used,int& cursor,double value) {
    values[cursor]=value; cursor=(cursor+1)%capacity; used=std::min(capacity,used+1);
}
double FallbackPace(const Session& s,const Frame& f,int index) {
    const auto& car=f.cars[index];
    if(index==f.player && PaceTime(f.lastLap)) return f.lastLap;
    if(PaceTime(car.last)) return car.last;
    if(s.number==f.session && PaceTime(s.results[index].last)) return s.results[index].last;
    if(index==f.player && PaceTime(f.bestLap)) return f.bestLap;
    if(PaceTime(car.best)) return car.best;
    if(s.number==f.session && PaceTime(s.results[index].best)) return s.results[index].best;
    return -1;
}
// Pit lane time lost compared with driving the same distance, used until that car's own stop has been observed.
constexpr double AssumedPitLoss=45;
struct Candidate {
    int index=-1, completed=0;
    double pct=0, phase=0, pace=0, activeDelay=0;
    double Progress() const { return completed+pct; }
    double First() const { return std::max(.001,(1-phase)*pace); }
    double DrivingTime(double progress) const {
        if(progress<=Progress()+Epsilon) return 0;
        if(progress<completed+1) return First()*(progress-Progress())/(1-pct);
        return First()+(progress-completed-1)*pace;
    }
    double Arrival(double progress) const { return progress<=Progress()+Epsilon?0:DrivingTime(progress)+activeDelay; }
    double At(double seconds) const {
        const double driving=std::max(0.0,seconds-activeDelay);
        if(driving<First()) return Progress()+(1-pct)*driving/First();
        return completed+1+(driving-First())/pace;
    }
};
FinishScenario Finish(const std::vector<Candidate>& cars,int leader,int player,double expiry,int lapLimit,std::array<double,MaxCars>& finishTimes) {
    FinishScenario result;
    if(cars.empty()) return result;
    double target=-1;
    if(expiry>=0) {
        double progress=-1;
        for(const auto& car:cars) progress=std::max(progress,car.At(expiry));
        // The first completion of the next leading race lap after time expires.
        // Taking the earliest arrival among contenders also permits a change of leader.
        target=std::floor(progress+Epsilon)+1;
    }
    if(lapLimit>0) target=target<0?lapLimit:std::min(target,static_cast<double>(lapLimit));
    if(target<0) return result;
    double finish=std::numeric_limits<double>::max();
    for(const auto& car:cars) {
        const double time=car.Arrival(target);
        if(time<finish-Epsilon || (std::abs(time-finish)<=Epsilon && car.index==leader)) { finish=time; result.winner=car.index; }
    }
    result.valid=true; result.raceFinishIn=finish;
    for(const auto& car:cars) {
        // Project each car independently to the overall winner's flag time. Using
        // the leader's lap count (or subtracting only the current lap deficit)
        // would overestimate fuel for slower classes that will be lapped again.
        double goal=std::max(static_cast<double>(car.completed)+1,std::ceil(car.At(finish)-Epsilon));
        // A pause exactly on a line can hold progress at an already completed crossing.
        if(car.Arrival(goal)<finish-Epsilon) ++goal;
        finishTimes[car.index]=car.Arrival(goal);
        if(car.index==leader) { result.leaderTotalDistance=expiry>=0?car.At(std::min(expiry,finish)):goal; result.leaderDistance=goal-car.Progress(); result.leaderFinishIn=car.Arrival(goal); result.leaderCrossings=static_cast<int>(goal)-car.completed; }
        if(car.index!=player) continue;
        result.ownCrossings=static_cast<int>(goal)-car.completed;
        result.ownDistance=goal-car.Progress(); result.ownFinishIn=car.Arrival(goal);
    }
    return result;
}
}
void RacePredictor::Reset() { *this={}; }
void RacePredictor::Update(const Session& s,const Frame& f) {
    if(!f.connected) { Reset(); return; }
    if(key_!=s.key || session_!=f.session || f.time<time_-Epsilon) {
        Reset(); key_=s.key; session_=f.session;
    }
    if(View(f)==ViewMode::Replay || s.type!="Race" || (f.state>0 && f.state<4)) {
        for(auto& h:history_) { h.seen=false; h.dirty=true; h.crossTime=-1; h.pitObserved=false; }
        time_=f.time; return;
    }
    if(f.time<=time_+Epsilon) return;
    for(const auto& driver:s.drivers) {
        const int index=driver.index; if(index<0 || index>=MaxCars) continue;
        const auto& car=f.cars[index]; auto& h=history_[index];
        const int completed=Completed(car);
        if(completed<0 || car.pct<0 || car.pct>=1 || car.surface<0) { h.seen=false; h.dirty=true; h.crossTime=-1; h.pitObserved=false; continue; }
        const double progress=completed+car.pct;
        const bool continuous=h.seen && f.time-h.sampleTime<=5 && progress>=h.progress-.02 && progress-h.progress<1.5;
        if(!continuous) { h.crossTime=-1; h.dirty=true; h.pitObserved=false; }
        if(car.pit && (!continuous || !h.wasPit)) {
            h.pitObserved=continuous && !h.wasPit;
            h.entryTime=h.pitObserved?f.time:-1; h.entryProgress=progress;
        }
        if(!car.pit && continuous && h.wasPit && h.pitObserved) {
            const double duration=f.time-h.entryTime, distance=progress-h.entryProgress;
            const double pace=h.used?Mean(h.laps.data(),h.used):FallbackPace(s,f,index);
            if(duration>=2 && duration<900 && distance>=0 && distance<3 && PaceTime(pace)) {
                const double loss=duration-distance*pace;
                if(loss>2 && loss<600) Record(h.losses.data(),6,h.lossUsed,h.lossCursor,loss);
            }
            h.pitObserved=false;
        }
        h.dirty=h.dirty || car.pit || car.surface!=3 || Caution(f);
        if(continuous && completed==h.completed+1) {
            const double travel=progress-h.progress;
            const double cross=f.time-(travel>Epsilon?car.pct/travel*(f.time-h.sampleTime):0);
            if(h.crossTime>=0 && !h.dirty && PaceTime(cross-h.crossTime))
                Record(h.laps.data(),10,h.used,h.cursor,cross-h.crossTime);
            h.crossTime=h.lastCross=cross;
            h.dirty=car.pit || car.surface!=3 || Caution(f);
        }
        h.seen=true; h.wasPit=car.pit; h.completed=completed; h.progress=progress; h.sampleTime=f.time;
    }
    if(f.state>=5 && checkeredTime_<0) {
        checkeredTime_=f.time;
        for(const auto& driver:s.drivers) if(driver.index>=0 && driver.index<MaxCars && f.cars[driver.index].position==1) {
            const auto cross=history_[driver.index].lastCross;
            if(cross>=f.time-2 && cross<=f.time) checkeredTime_=cross;
        }
    }
    time_=f.time;
}
RacePrediction RacePredictor::Predict(const Session& s,const Frame& f) const {
    RacePrediction p;
    if(!f.connected) return p;
    if(View(f)==ViewMode::Replay) { p.status=PredictionStatus::Replay; return p; }
    if(s.type!="Race") { p.status=PredictionStatus::NotRace; return p; }
    if(f.state>0 && f.state<4) { p.status=PredictionStatus::Gridding; return p; }
    std::vector<Candidate> cars;
    int bestPosition=MaxCars+1;
    for(const auto& driver:s.drivers) {
        const int index=driver.index; if(index<0 || index>=MaxCars) continue;
        const auto& car=f.cars[index]; const auto& h=history_[index];
        if(car.position>0 && car.position<bestPosition) { bestPosition=car.position; p.leader=index; }
        int completed=Completed(car);
        if(completed<0 && index==f.player) completed=f.lapCompleted;
        if(completed<0 || car.pct<0 || car.pct>=1 || car.surface<0) continue;
        const double pace=h.used?Mean(h.laps.data(),h.used):FallbackPace(s,f,index);
        if(!PaceTime(pace)) continue;
        Candidate model; model.index=index; model.completed=completed; model.pct=car.pct; model.phase=car.pct; model.pace=pace;
        // iRacing's estimate (CarIdxEstTime) runs on its class's reference lap (CarClassEstLapTime), not on this car's
        // best: divided by that, it is the share of the lap's time already behind the car.
        const double reference=driver.estimatedLapTime;
        if(PaceTime(reference) && car.estTime>=0 && car.estTime<reference && (car.estTime>0 || car.pct<.02) && !car.pit) model.phase=car.estTime/reference;
        if(car.pit) {
            const double loss=h.lossUsed?Mean(h.losses.data(),h.lossUsed):AssumedPitLoss;
            model.activeDelay=loss;
            if(h.pitObserved && h.entryTime>=0)
                model.activeDelay=std::max(0.0,loss-std::max(0.0,f.time-h.entryTime-(model.Progress()-h.entryProgress)*pace));
        }
        cars.push_back(model);
    }
    const auto leader=std::find_if(cars.begin(),cars.end(),[&](const auto& car){return car.index==p.leader;});
    const auto own=std::find_if(cars.begin(),cars.end(),[&](const auto& car){return car.index==f.player;});
    if(own!=cars.end()) p.ownPace=own->pace;
    if(f.state>=5) {
        p.status=f.state>=6?PredictionStatus::Finished:PredictionStatus::Checkered;
        auto& scene=p.finish; scene.valid=true; scene.winner=bestPosition==1?p.leader:-1;
        if(p.leader>=0 && p.leader<MaxCars && Completed(f.cars[p.leader])>=0) scene.leaderTotalDistance=Completed(f.cars[p.leader]);
        scene.leaderDistance=scene.leaderFinishIn=scene.raceFinishIn=0; scene.leaderCrossings=0;
        const bool ownFinished=f.state>=6 || (bestPosition==1 && f.player==p.leader) || (f.player>=0 && f.player<MaxCars && checkeredTime_>=0 && history_[f.player].lastCross>=checkeredTime_-Epsilon);
        if(ownFinished) { scene.ownDistance=scene.ownFinishIn=0; scene.ownCrossings=0; }
        else if(own!=cars.end()) { scene.ownDistance=1-own->pct; scene.ownFinishIn=own->Arrival(own->completed+1); scene.ownCrossings=1; }
        return p;
    }
    if(leader==cars.end() || bestPosition!=1) { p.status=PredictionStatus::MissingLeader; return p; }
    p.leaderPace=leader->pace;
    const bool timed=(f.timeLeftKnown || f.timeLeft>=0) && std::isfinite(f.timeLeft) && f.timeLeft<604800;
    const double expiry=timed?std::max(0.0,f.timeLeft):-1;
    p.finish=Finish(cars,p.leader,f.player,expiry,s.totalLaps,p.carFinishIn);
    if(!p.finish.valid) { p.status=PredictionStatus::Waiting; return p; }
    if(timed && p.finish.leaderDistance>=0) p.finish.finalLapTimeLeft=f.timeLeft-(p.finish.leaderFinishIn-leader->pace);
    p.status=own==cars.end()?PredictionStatus::MissingPace:PredictionStatus::Racing;
    return p;
}
OpponentFuelStatus OpponentFuel(const Session& s,const Frame& f,const FuelPlan& plan,int index,int extraLaps) {
    const auto none=OpponentFuelStatus::None;
    if(!f.connected || View(f)==ViewMode::Replay || s.type!="Race" || f.state!=4 ||
       index<0 || index>=MaxCars || index==f.player || f.player<0 || f.player>=MaxCars ||
       plan.prediction.status!=PredictionStatus::Racing || !plan.prediction.finish.valid) return none;
    const auto own=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){ return d.index==f.player; });
    const auto driver=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){ return d.index==index; });
    if(own==s.drivers.end() || driver==s.drivers.end() || own->classId<=0 || driver->classId!=own->classId || driver->paceCar || driver->spectator) return none;
    const auto& car=f.cars[index]; const auto& estimate=plan.estimates[static_cast<int>(FuelMetric::Average10)];
    const double pace=plan.prediction.ownPace, remaining=plan.prediction.carFinishIn[index];
    if(car.pit || car.towing || car.surface<0 || !std::isfinite(car.fuelStintStart) || car.fuelStintStart<0 || car.fuelStintStart>f.time ||
       !std::isfinite(f.time) || !PaceTime(pace) || !std::isfinite(remaining) || remaining<=0 ||
       !std::isfinite(s.fuelCapacity) || s.fuelCapacity<=0 || estimate.samples<3 || !std::isfinite(estimate.consumption) || estimate.consumption<=0) return none;
    // Convert our full-tank range and the configurable lap tolerance to seconds.
    // Green uses the normal range; only yellow may use the configurable saving tolerance.
    const double reserve=s.fuelCapacity/estimate.consumption*pace-(f.time-car.fuelStintStart)-remaining;
    if(!std::isfinite(reserve)) return none;
    if(reserve>=-Epsilon) return OpponentFuelStatus::End;
    if(reserve>=-std::clamp(extraLaps,0,10)*pace-Epsilon) return OpponentFuelStatus::Save;
    return none;
}
}
