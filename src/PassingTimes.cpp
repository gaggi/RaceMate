#include "PassingTimes.h"
#include <algorithm>
#include <cmath>

namespace rm {
void PassingTimes::Reset() {
    cars_.assign(MaxCars,Car{}); references_.clear(); times_.clear(); profiles_.clear(); points_=slots_=0; key_.clear(); session_=-1; time_=-1; metres_=0;
}
void PassingTimes::Update(const Session& s,const Frame& f) {
    if(!f.connected) { if(points_) Reset(); return; }
    if(!std::isfinite(f.time)) return;
    // A rewind (replay seek, new session) makes every stored pass meaningless.
    if(!points_ || key_!=s.key || session_!=f.session || metres_!=s.trackMeters || f.time<time_) {
        Reset(); key_=s.key; session_=f.session; metres_=s.trackMeters;
        points_=std::clamp(s.trackMeters>0?static_cast<int>(std::lround(s.trackMeters/Spacing)):600,MinPoints,MaxPoints);
    }
    if(f.time==time_) return;
    time_=f.time;
    const int n=points_;
    constexpr unsigned FullCourse=SessionFlag::Caution|SessionFlag::CautionWaving|SessionFlag::Red;
    const bool racing=!(f.flags&FullCourse) && !(s.type=="Race" && f.state>0 && f.state<4);
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.spectator) continue;
        auto& c=cars_[d.index]; const auto& car=f.cars[d.index];
        // Another car on this index (a new entrant, or another class) starts without the old one's passes and lap.
        // Another driver in the same car keeps them only after a swap in the pits.
        if(c.car!=d.carPath || c.classId!=d.classId || (c.driver!=d.name && !car.pit && !c.driver.empty())) {
            if(!c.car.empty() || c.lapTime>0) { c.lapTime=0; Forget(c,f.time); c.seen=false; }
            c.car=d.carPath; c.classId=d.classId;
        }
        c.driver=d.name;
        // Frames without a position are bridged: iRacing briefly reports some driving cars as not in the world.
        if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1) continue;
        if(c.slot<0) { c.slot=slots_++; times_.resize(static_cast<size_t>(slots_)*n,-1); profiles_.resize(times_.size(),-1); } // Once per car and session.
        const double dt=f.time-c.time;
        double travel=car.pct-c.pct; if(travel<-.5) travel+=1;
        const double metres=s.trackMeters>0?travel*s.trackMeters:travel*5000;
        if(!c.seen || dt<=0 || dt>1 || metres<-5 || metres>120*dt+5) {
            // Telemetry gap, going backwards or a jump (tow, reset): its earlier passes no longer join up.
            Forget(c,f.time); c.seen=true; c.pct=car.pct; c.time=c.moved=f.time; continue;
        }
        // iRacing may switch from the formation lap to racing just after the line: the first seconds of a lap do
        // not spoil it, so the first racing lap becomes a profile.
        if(car.pit || car.towing || (car.surface>=0 && car.surface!=3) || (!racing && f.time-c.lapStart>5)) c.clean=false;
        if(car.pit) c.trailCount=0; // Pit lane driving says nothing about the pace on track.
        if(travel<=0) {
            // Standing; small jitter backwards stays put. Standing 2 s (a spin, a stop) keeps the lap from the profile:
            // replayed later, the wait would be timed as driving.
            c.time=f.time; if(f.time-c.moved>2) { c.clean=false; c.trailCount=0; c.stood=f.time; } continue;
        }
        double* times=times_.data()+static_cast<size_t>(c.slot)*n;
        const double from=c.pct*n,to=(c.pct+travel)*n;
        for(int k=static_cast<int>(std::floor(from))+1;k<=to;++k) {
            const double at=c.time+(k-from)/(to-from)*dt;
            if(k%n==0) {
                // The line: a clean lap watched from line to line becomes this car's profile.
                if(c.clean && c.lapStart>=c.since) {
                    double* profile=profiles_.data()+static_cast<size_t>(c.slot)*n; bool whole=true;
                    for(int i=1;i<n && whole;++i) whole=times[i]>times[i-1] && times[i]<at;
                    // Far slower than its stored lap or its class's fastest (damage, crawling back): not representative.
                    // A lap a little slower (tyres) still replaces it, and so does the second of two far slower laps
                    // within 5 % of each other: a new pace that stays, such as rain.
                    const int classBest=ClassProfile(c.classId);
                    const double usual=c.lapTime>0?c.lapTime:(classBest>=0?cars_[classBest].lapTime:0),lap=at-c.lapStart;
                    if(whole && usual>0 && lap>usual*1.25) {
                        const bool steady=c.slow>0 && std::abs(lap-c.slow)<=c.slow*.05;
                        c.slow=steady?0:lap; whole=steady;
                    } else if(whole) c.slow=0;
                    if(whole) {
                        for(int i=0;i<n;++i) profile[i]=times[i]-c.lapStart;
                        c.lapTime=at-c.lapStart;
                    }
                }
                // A lap begun within 30 s of standing still (a standing start from the grid, a stop just before the
                // line) starts with getting up to speed: its first stretch would be timed far too slow.
                c.lapStart=at; c.clean=at-c.stood>30; ++c.laps;
            }
            times[k%n]=at;
            const long long point=c.laps*n+k%n;
            if(point%10==0) { c.trail[c.trailEnd]={point,at}; c.trailEnd=(c.trailEnd+1)%static_cast<int>(c.trail.size()); c.trailCount=std::min(c.trailCount+1,static_cast<int>(c.trail.size())); }
        }
        c.pct=car.pct; c.time=c.moved=f.time;
    }
}
double PassingTimes::Gap(int ahead,int behind) const {
    const double unknown=std::numeric_limits<double>::quiet_NaN();
    if(ahead<0 || ahead>=MaxCars || behind<0 || behind>=MaxCars || ahead==behind || !points_) return unknown;
    const auto& a=cars_[ahead]; const auto& b=cars_[behind];
    if(!a.seen || !b.seen || a.slot<0) return unknown;
    const int n=points_; const int k=std::min(n-1,static_cast<int>(std::floor(b.pct*n)));
    const double point=static_cast<double>(k)/n,past=b.pct-point;
    double reached=a.pct-point; reached-=std::floor(reached);
    // Between that point and behind's position the car ahead is most of a lap ahead: its pass there is a lap old.
    if(reached<past) return unknown;
    const double* times=times_.data()+static_cast<size_t>(a.slot)*n;
    const double first=times[k];
    if(first<0 || first<a.since) return unknown;
    double last=a.time,span=reached;
    if(reached>1./n) { last=times[(k+1)%n]; span=1./n; if(last<first || last<a.since) return unknown; }
    const double at=span>0?first+(last-first)*past/span:first;
    return std::max(0.,b.time-at);
}
PassingTimes::Pace PassingTimes::PaceAgainst(int profile,int car) const {
    Pace pace;
    if(!HasProfile(profile) || car<0 || car>=MaxCars || !points_) return pace;
    const auto& c=cars_[car]; const int size=static_cast<int>(c.trail.size());
    if(c.trailCount<2) return pace;
    const Mark& last=c.trail[(c.trailEnd+size-1)%size];
    // The oldest mark still within the window: 30 s of driving, all in one piece.
    const Mark* first=nullptr;
    for(int i=c.trailCount;i>=2 && !first;--i) { const Mark& m=c.trail[(c.trailEnd+size-i)%size]; if(last.time-m.time<=PaceWindow) first=&m; }
    if(!first || last.time-first->time<5) return pace; // Too little to tell.
    const int n=points_; const long long span=last.point-first->point;
    const double from=static_cast<double>(first->point%n)/n,to=static_cast<double>(last.point%n)/n;
    double expected=(span/n)*Curve(profile).lapTime;
    if(span%n) expected+=Along(profile,from,to);
    if(!(expected>0)) return pace;
    pace.raw=(last.time-first->time)/expected;
    pace.ratio=std::clamp(pace.raw,.7,2.);
    pace.limited=pace.ratio!=pace.raw;
    return pace;
}
int PassingTimes::ClassProfile(int classId) const {
    int best=-1;
    for(int i=0;i<MaxCars;++i) if(cars_[i].lapTime>0 && cars_[i].classId==classId && (best<0 || cars_[i].lapTime<cars_[best].lapTime)) best=i;
    return best;
}
bool PassingTimes::AddReference(int classId,double lapTime,std::vector<double> times) {
    if(!points_ || !(lapTime>0) || static_cast<int>(times.size())!=points_ || times[0]!=0 || StoredProfile(classId)>=0) return false;
    for(int i=1;i<points_;++i) if(!(times[i]>times[i-1]) || !(times[i]<lapTime)) return false;
    references_.push_back({classId,lapTime,std::move(times)}); return true;
}
int PassingTimes::StoredProfile(int classId) const {
    for(size_t i=0;i<references_.size();++i) if(references_[i].classId==classId) return MaxCars+static_cast<int>(i);
    return -1;
}
bool PassingTimes::ClassLap(int classId,double& lapTime,std::vector<double>& times) const {
    const int car=ClassProfile(classId); if(car<0) return false;
    const auto lap=Curve(car); lapTime=lap.lapTime; times.assign(lap.times,lap.times+points_); return true;
}
PassingTimes::Lap PassingTimes::Curve(int profile) const {
    if(profile>=MaxCars) { const auto& r=references_[profile-MaxCars]; return {r.times.data(),r.lapTime}; }
    const auto& c=cars_[profile]; return {profiles_.data()+static_cast<size_t>(c.slot)*points_,c.lapTime};
}
double PassingTimes::Into(const Lap& lap,double pct) const {
    const int n=points_; const double* profile=lap.times;
    const double point=pct*n; const int k=std::min(n-1,static_cast<int>(std::floor(point)));
    const double next=k+1<n?profile[k+1]:lap.lapTime;
    return profile[k]+(next-profile[k])*(point-k);
}
double PassingTimes::Along(int profile,double from,double to) const {
    if(!HasProfile(profile) || !points_ || !(from>=0 && from<1) || !(to>=0 && to<1)) return std::numeric_limits<double>::quiet_NaN();
    const auto p=Curve(profile); const double start=Into(p,from),end=Into(p,to);
    return to>=from?std::max(0.,end-start):p.lapTime-start+end;
}
double PassingTimes::Locate(int profile,double seconds) const {
    if(!HasProfile(profile) || !points_ || !std::isfinite(seconds)) return std::numeric_limits<double>::quiet_NaN();
    const auto p=Curve(profile); const int n=points_; const double* profileTimes=p.times;
    seconds-=std::floor(seconds/p.lapTime)*p.lapTime;
    // The last point at or before those seconds, then linear to the next (or to the line).
    const int k=std::max(0,static_cast<int>(std::upper_bound(profileTimes,profileTimes+n,seconds)-profileTimes)-1);
    const double next=k+1<n?profileTimes[k+1]:p.lapTime,span=next-profileTimes[k];
    return std::min(.999999,(k+(span>0?(seconds-profileTimes[k])/span:0))/n);
}
double PassingTimes::Reach(int profile,int from,int to) const {
    const double unknown=std::numeric_limits<double>::quiet_NaN();
    if(!HasProfile(profile) || from<0 || from>=MaxCars || to<0 || to>=MaxCars || !points_) return unknown;
    const auto p=Curve(profile); const auto& a=cars_[from]; const auto& b=cars_[to];
    if(!a.seen || !b.seen) return unknown;
    if(from==to) return 0;
    // Forward from a to b; across the line the rest of the lap and the start of the next.
    const double start=Into(p,a.pct),end=Into(p,b.pct);
    return b.pct>=a.pct?std::max(0.,end-start):p.lapTime-start+end;
}
}
