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
double Deviation(const double* values,int count,double mean) {
    if(!count) return 0;
    double sum=0; for(int i=0;i<count;++i) sum+=(values[i]-mean)*(values[i]-mean); return std::sqrt(sum/count);
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
struct Candidate {
    int index=-1, completed=0, samples=0, stopCount=0;
    double pct=0, phase=0, pace=0, spread=0, activeDelay=0, stopLoss=0, firstStop=-1, stint=0;
    double Progress() const { return completed+pct; }
    double First() const { return std::max(.001,(1-phase)*pace); }
    double DrivingTime(double progress) const {
        if(progress<=Progress()+Epsilon) return 0;
        if(progress<completed+1) return First()*(progress-Progress())/(1-pct);
        return First()+(progress-completed-1)*pace;
    }
    double Arrival(double progress) const {
        if(progress<=Progress()+Epsilon) return 0;
        double time=DrivingTime(progress)+activeDelay;
        time+=StopsBefore(progress-Epsilon)*stopLoss;
        return time;
    }
    int StopsBefore(double progress) const {
        if(!stopCount || progress<firstStop) return 0;
        if(stint<=0) return 1;
        return std::min(stopCount,static_cast<int>(std::floor((progress-firstStop)/stint))+1);
    }
    double At(double seconds) const {
        double driving=std::max(0.0,seconds-activeDelay);
        if(stopCount) {
            const double first=DrivingTime(firstStop);
            if(driving>first) {
                if(stint<=0) driving-=std::min(stopLoss,driving-first);
                else {
                    const double cycle=stint*pace+stopLoss;
                    const int completedStops=std::min(stopCount-1,static_cast<int>((driving-first)/cycle));
                    const double after=driving-first-completedStops*cycle;
                    driving=first+completedStops*stint*pace+std::max(0.0,after-stopLoss);
                }
            }
        }
        if(driving<First()) return Progress()+(1-pct)*driving/First();
        return completed+1+(driving-First())/pace;
    }
};
FinishScenario Finish(const std::vector<Candidate>& cars,int leader,int player,double expiry,int lapLimit) {
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
        if(car.index!=player && car.index!=leader) continue;
        // Project each car independently to the overall winner's flag time. Using
        // the leader's lap count (or subtracting only the current lap deficit)
        // would overestimate fuel for slower classes that will be lapped again.
        double goal=std::max(static_cast<double>(car.completed)+1,std::ceil(car.At(finish)-Epsilon));
        // A pause exactly on a line can hold progress at an already completed crossing.
        if(car.Arrival(goal)<finish-Epsilon) ++goal;
        if(car.index==leader) { result.leaderTotalDistance=expiry>=0?car.At(std::min(expiry,finish)):goal; result.leaderDistance=goal-car.Progress(); result.leaderFinishIn=car.Arrival(goal); result.leaderCrossings=static_cast<int>(goal)-car.completed; }
        if(car.index!=player) continue;
        result.ownCrossings=static_cast<int>(goal)-car.completed;
        result.ownDistance=goal-car.Progress(); result.ownFinishIn=car.Arrival(goal);
        result.minCrossings=result.maxCrossings=result.ownCrossings;
        result.minDistance=result.maxDistance=result.ownDistance;
    }
    return result;
}
Confidence PaceConfidence(const Candidate& car) {
    const double cv=car.spread/car.pace;
    return car.samples>=10 && cv<=.02?Confidence::High:(car.samples>=5 && cv<=.04?Confidence::Medium:Confidence::Low);
}
Confidence Lower(Confidence a,Confidence b) { return static_cast<Confidence>(std::min(static_cast<int>(a),static_cast<int>(b))); }
void Bounds(FinishScenario& scenario,const std::vector<Candidate>& cars,int leader,int player,double expiry,int lapLimit,bool stopUncertain) {
    if(!scenario.valid || scenario.ownCrossings<0) return;
    Confidence confidence=Confidence::High;
    for(const auto& car:cars) if(car.index==player || car.index==scenario.winner) confidence=Lower(confidence,PaceConfidence(car));
    for(int raceDirection:{-1,1}) for(int ownDirection:{-1,1}) {
        auto varied=cars;
        for(auto& car:varied) {
            // Sensitivity band, not a probability interval. Fallback laps use a wider band.
            const double spread=std::max(car.spread,car.pace*(car.samples>=5?.005:.02));
            car.pace=std::max(10.01,car.pace+(car.index==player?ownDirection:raceDirection)*spread);
            if(stopUncertain) car.stopLoss*=raceDirection<0?.8:1.2;
        }
        const auto alternative=Finish(varied,leader,player,expiry,lapLimit);
        if(!alternative.valid || alternative.ownCrossings<0) continue;
        scenario.minCrossings=std::min(scenario.minCrossings,alternative.ownCrossings);
        scenario.maxCrossings=std::max(scenario.maxCrossings,alternative.ownCrossings);
        scenario.minDistance=std::min(scenario.minDistance,alternative.ownDistance);
        scenario.maxDistance=std::max(scenario.maxDistance,alternative.ownDistance);
    }
    if(scenario.minCrossings!=scenario.maxCrossings) confidence=Confidence::Low;
    if(stopUncertain) confidence=Lower(confidence,Confidence::Medium);
    scenario.confidence=confidence;
}
}
void RacePredictor::Reset() { *this={}; }
void RacePredictor::Update(const Session& s,const Frame& f) {
    if(!f.connected) { Reset(); return; }
    if(key_!=s.key || session_!=f.session || f.time<time_-Epsilon) {
        Reset(); key_=s.key; session_=f.session;
    }
    if(f.replay || s.type!="Race" || (f.state>0 && f.state<4)) {
        for(auto& h:history_) { h.seen=false; h.dirty=true; h.crossTime=-1; h.pitObserved=false; }
        time_=f.time; return;
    }
    if(f.time<=time_+Epsilon) return;
    for(const auto& driver:s.drivers) {
        const int index=driver.index; if(index<0 || index>=MaxCars) continue;
        const auto& car=f.cars[index]; auto& h=history_[index];
        const int completed=Completed(car);
        if(completed<0 || car.pct<0 || car.pct>=1 || car.surface<0) { h.seen=false; h.dirty=true; h.crossTime=-1; h.pitObserved=false; h.exitProgress=-1; continue; }
        const double progress=completed+car.pct;
        const bool continuous=h.seen && f.time-h.sampleTime<=5 && progress>=h.progress-.02 && progress-h.progress<1.5;
        if(!continuous) {
            if(h.seen) h.exitProgress=-1;
            h.crossTime=-1; h.dirty=true; h.pitObserved=false;
            if(progress<=1.05 && !car.pit && h.stops==0) h.exitProgress=0;
        }
        if(car.pit && (!continuous || !h.wasPit)) {
            h.pitObserved=continuous && !h.wasPit;
            h.entryTime=h.pitObserved?f.time:-1; h.entryProgress=progress;
        }
        if(!car.pit && continuous && h.wasPit && h.pitObserved) {
            const double duration=f.time-h.entryTime, distance=progress-h.entryProgress;
            const double pace=h.used?Mean(h.laps.data(),h.used):FallbackPace(s,f,index);
            if(duration>=2 && duration<900 && distance>=0 && distance<3 && PaceTime(pace)) {
                const double loss=duration-distance*pace;
                if(loss>2 && loss<600) { Record(h.losses.data(),6,h.stopUsed,h.stopCursor,loss); ++h.stops; }
                const double stint=h.entryProgress-h.exitProgress;
                if(h.exitProgress>=0 && stint>=3 && stint<200) Record(h.stints.data(),6,h.stintUsed,h.stintCursor,stint);
                h.exitProgress=progress; h.exitTime=f.time;
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
RacePrediction RacePredictor::Predict(const Session& s,const Frame& f,const PredictionOptions& options) const {
    RacePrediction p;
    if(!f.connected) return p;
    if(f.replay) { p.status=PredictionStatus::Replay; return p; }
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
        model.samples=h.used; model.spread=h.used?Deviation(h.laps.data(),h.used,pace):pace*.02;
        double best=car.best;
        if(!PaceTime(best) && s.number==f.session) best=s.results[index].best;
        if(PaceTime(best) && car.estTime>=0 && car.estTime<best && !car.pit) model.phase=car.estTime/best;
        const double measuredLoss=Mean(h.losses.data(),h.stopUsed);
        const double loss=measuredLoss>0?measuredLoss:std::clamp(options.assumedPitLoss,1,600);
        if(car.pit) {
            model.activeDelay=loss;
            if(h.pitObserved && h.entryTime>=0)
                model.activeDelay=std::max(0.0,loss-std::max(0.0,f.time-h.entryTime-(model.Progress()-h.entryProgress)*pace));
        }
        if(index==f.player) model.activeDelay+=std::clamp(options.ownPitLoss,0,600);
        cars.push_back(model);
    }
    const auto leader=std::find_if(cars.begin(),cars.end(),[&](const auto& car){return car.index==p.leader;});
    const auto own=std::find_if(cars.begin(),cars.end(),[&](const auto& car){return car.index==f.player;});
    if(own!=cars.end()) p.ownPace=own->pace;
    if(f.state>=5) {
        p.status=f.state>=6?PredictionStatus::Finished:PredictionStatus::Checkered;
        auto& scene=p.noStop; scene.valid=true; scene.winner=bestPosition==1?p.leader:-1;
        if(p.leader>=0 && p.leader<MaxCars && Completed(f.cars[p.leader])>=0) scene.leaderTotalDistance=Completed(f.cars[p.leader]);
        scene.leaderDistance=scene.leaderFinishIn=scene.raceFinishIn=0; scene.leaderCrossings=0;
        const bool ownFinished=f.state>=6 || (bestPosition==1 && f.player==p.leader) || (f.player>=0 && f.player<MaxCars && checkeredTime_>=0 && history_[f.player].lastCross>=checkeredTime_-Epsilon);
        if(ownFinished) { scene.ownDistance=scene.ownFinishIn=0; scene.ownCrossings=0; }
        else if(own!=cars.end()) { scene.ownDistance=1-own->pct; scene.ownFinishIn=own->Arrival(own->completed+1); scene.ownCrossings=1; }
        scene.minDistance=scene.maxDistance=scene.ownDistance; scene.minCrossings=scene.maxCrossings=scene.ownCrossings;
        scene.confidence=Confidence::Medium; p.withStops=scene; return p;
    }
    if(leader==cars.end() || bestPosition!=1) { p.status=PredictionStatus::MissingLeader; return p; }
    p.leaderPace=leader->pace;
    const auto& leaderHistory=history_[p.leader];
    p.observedLeaderStops=leaderHistory.stops;
    p.leaderLastPitLaps=leaderHistory.stops && leaderHistory.exitProgress>=0?std::max(0.0,leader->Progress()-leaderHistory.exitProgress):-1;
    p.leaderLastPitSeconds=leaderHistory.stops && leaderHistory.exitTime>=0?std::max(0.0,f.time-leaderHistory.exitTime):-1;
    p.measuredPitLoss=leaderHistory.stopUsed>0;
    p.leaderPitLoss=p.measuredPitLoss?Mean(leaderHistory.losses.data(),leaderHistory.stopUsed):std::clamp(options.assumedPitLoss,1,600);
    p.leaderStint=options.leaderStintLaps>0?options.leaderStintLaps:Mean(leaderHistory.stints.data(),leaderHistory.stintUsed);
    p.ownPitActive=f.player>=0 && f.player<MaxCars && f.cars[f.player].pit;
    const bool timed=(f.timeLeftKnown || f.timeLeft>=0) && std::isfinite(f.timeLeft) && f.timeLeft<604800;
    const double expiry=timed?std::max(0.0,f.timeLeft):-1;
    p.noStop=Finish(cars,p.leader,f.player,expiry,s.totalLaps);
    Bounds(p.noStop,cars,p.leader,f.player,expiry,s.totalLaps,false);
    if(timed && p.noStop.valid && p.noStop.leaderDistance>=0)
        p.noStop.finalLapTimeLeft=f.timeLeft-(p.noStop.leaderFinishIn-leader->pace);
    if(!p.noStop.valid) { p.status=PredictionStatus::Waiting; return p; }
    p.status=own==cars.end()?PredictionStatus::MissingPace:PredictionStatus::Racing;
    auto stopped=cars;
    p.whatIf=options.stops==StopMode::Automatic && (p.leaderStint<=0 || leaderHistory.exitProgress<0);
    for(auto& car:stopped) {
        const auto& h=history_[car.index];
        if(options.stops==StopMode::None) continue;
        if(car.index==f.player && car.index!=p.leader) continue; // Own future pit loss is an explicit plan.
        if(car.index==p.leader && (options.stops==StopMode::One || p.whatIf)) {
            car.stopLoss=p.leaderPitLoss; car.firstStop=car.Progress()+.01; car.stopCount=1;
            if(car.index==f.player && options.ownPitLoss>0) car.stopLoss=std::max(0.0,car.stopLoss-options.ownPitLoss);
        } else if(options.stops==StopMode::Automatic) {
            const double stint=car.index==p.leader?p.leaderStint:Mean(h.stints.data(),h.stintUsed);
            if(stint<=0 || h.exitProgress<0 || f.cars[car.index].pit) continue;
            car.stopLoss=h.stopUsed?Mean(h.losses.data(),h.stopUsed):std::clamp(options.assumedPitLoss,1,600);
            car.firstStop=std::max(car.Progress()+.01,h.exitProgress+stint); car.stint=stint; car.stopCount=32767;
        }
    }
    p.withStops=Finish(stopped,p.leader,f.player,expiry,s.totalLaps);
    Bounds(p.withStops,stopped,p.leader,f.player,expiry,s.totalLaps,options.stops!=StopMode::None);
    for(const auto& car:stopped) if(car.index==p.leader && p.withStops.valid)
        p.projectedLeaderStops=car.StopsBefore(car.At(p.withStops.raceFinishIn)+Epsilon);
    const double stintVariation=leaderHistory.stintUsed?Deviation(leaderHistory.stints.data(),leaderHistory.stintUsed,Mean(leaderHistory.stints.data(),leaderHistory.stintUsed))/Mean(leaderHistory.stints.data(),leaderHistory.stintUsed):1;
    p.stopConfidence=options.stops==StopMode::None?Confidence::Unknown:(p.whatIf || !p.measuredPitLoss || options.stops==StopMode::One || leaderHistory.stintUsed<2 || stintVariation>.2?Confidence::Low:Confidence::Medium);
    if(p.whatIf || p.stopConfidence==Confidence::Low) p.withStops.confidence=Lower(p.withStops.confidence,Confidence::Low);
    if(Caution(f) || p.ownPitActive || f.cars[p.leader].pit) {
        p.noStop.confidence=p.withStops.confidence=Confidence::Low;
    }
    for(const auto& driver:s.drivers) if(driver.index>=0 && driver.index<MaxCars && f.cars[driver.index].surface>=0 && f.cars[driver.index].position>0 && f.cars[driver.index].position<=5) {
        if(std::none_of(cars.begin(),cars.end(),[&](const auto& car){return car.index==driver.index;}))
            p.noStop.confidence=p.withStops.confidence=Confidence::Low;
    }
    return p;
}
}
