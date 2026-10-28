#include "PassingTimes.h"
#include <algorithm>
#include <cmath>

namespace rm {
void PassingTimes::Reset() {
    cars_={}; times_.clear(); points_=slots_=0; key_.clear(); session_=-1; time_=-1; metres_=0;
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
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.spectator) continue;
        auto& c=cars_[d.index]; const auto& car=f.cars[d.index];
        if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1 || car.surface<0) { c.seen=false; continue; }
        if(c.slot<0) { c.slot=slots_++; times_.resize(static_cast<size_t>(slots_)*n,-1); } // Once per car and session.
        const double dt=f.time-c.time;
        double travel=car.pct-c.pct; if(travel<-.5) travel+=1;
        const double metres=s.trackMeters>0?travel*s.trackMeters:travel*5000;
        if(!c.seen || dt<=0 || dt>1 || metres<-5 || metres>120*dt+5) {
            // Telemetry gap, going backwards or a jump (tow, reset): its earlier passes no longer join up.
            Forget(c,f.time); c.seen=true; c.pct=car.pct; c.time=f.time; continue;
        }
        if(travel<=0) { c.time=f.time; continue; } // Standing; small jitter backwards stays put.
        double* times=times_.data()+static_cast<size_t>(c.slot)*n;
        const double from=c.pct*n,to=(c.pct+travel)*n;
        for(int k=static_cast<int>(std::floor(from))+1;k<=to;++k) times[k%n]=c.time+(k-from)/(to-from)*dt;
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
}
