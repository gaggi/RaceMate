#include "PassingTimes.h"
#include <algorithm>
#include <cmath>

namespace rm {
void PassingTimes::Reset() {
    cars_={}; times_.clear(); profiles_.clear(); points_=slots_=0; key_.clear(); session_=-1; time_=-1; metres_=0;
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
        auto& c=cars_[d.index]; const auto& car=f.cars[d.index]; c.classId=d.classId;
        // Frames without a position are bridged: iRacing briefly reports some driving cars as not in the world.
        if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1) continue;
        if(c.slot<0) { c.slot=slots_++; times_.resize(static_cast<size_t>(slots_)*n,-1); profiles_.resize(times_.size(),-1); } // Once per car and session.
        const double dt=f.time-c.time;
        double travel=car.pct-c.pct; if(travel<-.5) travel+=1;
        const double metres=s.trackMeters>0?travel*s.trackMeters:travel*5000;
        if(!c.seen || dt<=0 || dt>1 || metres<-5 || metres>120*dt+5) {
            // Telemetry gap, going backwards or a jump (tow, reset): its earlier passes no longer join up.
            Forget(c,f.time); c.seen=true; c.pct=car.pct; c.time=f.time; continue;
        }
        // iRacing may switch from the formation lap to racing just after the line: the first seconds of a lap do
        // not spoil it, so the first racing lap becomes a profile.
        if(car.pit || car.towing || (car.surface>=0 && car.surface!=3) || (!racing && f.time-c.lapStart>5)) c.clean=false;
        if(travel<=0) { c.time=f.time; continue; } // Standing; small jitter backwards stays put.
        double* times=times_.data()+static_cast<size_t>(c.slot)*n;
        const double from=c.pct*n,to=(c.pct+travel)*n;
        for(int k=static_cast<int>(std::floor(from))+1;k<=to;++k) {
            const double at=c.time+(k-from)/(to-from)*dt;
            if(k%n==0) {
                // The line: a clean lap watched from line to line becomes this car's profile.
                if(c.clean && c.lapStart>=c.since) {
                    double* profile=profiles_.data()+static_cast<size_t>(c.slot)*n; bool whole=true;
                    for(int i=1;i<n && whole;++i) whole=times[i]>times[i-1] && times[i]<at;
                    if(whole) {
                        for(int i=0;i<n;++i) profile[i]=times[i]-c.lapStart;
                        c.lapTime=at-c.lapStart;
                    }
                }
                c.lapStart=at; c.clean=true;
            }
            times[k%n]=at;
        }
        c.pct=car.pct; c.time=f.time;
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
int PassingTimes::ClassProfile(int classId) const {
    int best=-1;
    for(int i=0;i<MaxCars;++i) if(cars_[i].lapTime>0 && cars_[i].classId==classId && (best<0 || cars_[i].lapTime<cars_[best].lapTime)) best=i;
    return best;
}
double PassingTimes::Into(const Car& c,double pct) const {
    const int n=points_; const double* profile=profiles_.data()+static_cast<size_t>(c.slot)*n;
    const double point=pct*n; const int k=std::min(n-1,static_cast<int>(std::floor(point)));
    const double next=k+1<n?profile[k+1]:c.lapTime;
    return profile[k]+(next-profile[k])*(point-k);
}
double PassingTimes::Reach(int profile,int from,int to) const {
    const double unknown=std::numeric_limits<double>::quiet_NaN();
    if(profile<0 || profile>=MaxCars || from<0 || from>=MaxCars || to<0 || to>=MaxCars || !points_) return unknown;
    const auto& p=cars_[profile]; const auto& a=cars_[from]; const auto& b=cars_[to];
    if(p.lapTime<=0 || !a.seen || !b.seen) return unknown;
    if(from==to) return 0;
    // Forward from a to b; across the line the rest of the lap and the start of the next.
    const double start=Into(p,a.pct),end=Into(p,b.pct);
    return b.pct>=a.pct?std::max(0.,end-start):p.lapTime-start+end;
}
}
