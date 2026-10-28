#include "SectorTiming.h"
#include <algorithm>
#include <cmath>

namespace rm {
namespace {
// Full-course cautions and red flags only: a local yellow is real racing for every car not near it.
constexpr unsigned FullCourse=SessionFlag::Caution|SessionFlag::CautionWaving|SessionFlag::Red;
constexpr int NoLap=std::numeric_limits<int>::min();
}
void SectorTiming::Reset() {
    cars_.clear(); times_.clear(); laps_.clear(); improvements_.clear(); classes_.clear(); fastest_.clear(); sectors_.clear(); configured_.clear(); cuts_.clear(); key_.clear(); session_=track_=-1; time_=-1;
    ++revision_; ++layoutRevision_; ++recordRevision_;
}
int SectorTiming::ClassSlot(int classId) {
    const auto at=std::find(classes_.begin(),classes_.end(),classId);
    if(at!=classes_.end()) return static_cast<int>(at-classes_.begin());
    classes_.push_back(classId); fastest_.resize(classes_.size()*Published(),-1); // Once per class and session.
    return static_cast<int>(classes_.size())-1;
}
void SectorTiming::Update(const Session& s,const Frame& f) {
    if(!f.connected) { if(!sectors_.empty()) Reset(); return; }
    if(View(f)==ViewMode::Replay) { for(auto& c:cars_) c.seen=false; return; }
    if(!std::isfinite(f.time)) return;
    if(key_!=s.key || session_!=f.session || track_!=s.trackId || configured_!=s.sectors || f.time<time_) {
        Reset(); key_=s.key; session_=f.session; track_=s.trackId; configured_=s.sectors;
        if(s.sectors.empty() || s.sectors.size()>MaxSectors || s.sectors.front().start!=0) return;
        double previous=-1;
        for(const auto& b:s.sectors) { if(!std::isfinite(b.start) || b.start<=previous || b.start>=1) return; previous=b.start; }
        sectors_=s.sectors; cuts_.reserve(sectors_.size()*Parts+1);
        for(size_t i=0;i<sectors_.size();++i) {
            const double start=sectors_[i].start,end=i+1<sectors_.size()?sectors_[i+1].start:1;
            for(int p=0;p<Parts;++p) cuts_.push_back(start+(end-start)*p/Parts);
        }
        cuts_.push_back(1); cars_.resize(MaxCars);
        const size_t count=cuts_.size()-1,published=Published(),stride=count+published*2;
        times_.assign(MaxCars*stride,-1); laps_.assign(MaxCars*published,NoLap); improvements_.assign(MaxCars*(published+count),0);
        for(size_t i=0;i<cars_.size();++i) {
            auto* at=times_.data()+i*stride;
            cars_[i].current={at,count}; cars_[i].recent={at+count,published}; cars_[i].personal={at+count+published,published};
            cars_[i].laps={laps_.data()+i*published,published};
            cars_[i].improved={improvements_.data()+i*(published+count),published}; cars_[i].counted={improvements_.data()+i*(published+count)+published,count};
        }
    }
    if(cars_.empty() || f.time==time_) return;
    time_=f.time;
    const int segments=static_cast<int>(cuts_.size())-1;
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) continue;
        auto& c=cars_[d.index]; const auto& car=f.cars[d.index];
        if(c.driver!=d.name || c.car!=d.carPath) {
            const auto current=c.current,recent=c.recent,personal=c.personal; const auto laps=c.laps; const auto improved=c.improved,counted=c.counted;
            c=Sample{}; c.current=current; c.recent=recent; c.personal=personal; c.laps=laps; c.improved=improved; c.counted=counted;
            for(const auto values:{current,recent,personal}) std::fill(values.begin(),values.end(),-1);
            std::fill(laps.begin(),laps.end(),NoLap); std::fill(improved.begin(),improved.end(),static_cast<unsigned char>(0));
            c.driver=d.name; c.car=d.carPath; c.revision=++revision_;
        }
        if(c.classSlot<0 || classes_[c.classSlot]!=d.classId) c.classSlot=ClassSlot(d.classId);
        // Every observed split is shown, pit lane and off-tracks included; only a lost position or a tow ends the
        // observation. A frame without a position (iRacing briefly reports some cars as not in the world) is bridged.
        if(car.towing) { c.seen=false; c.entered=-1; continue; }
        if(!std::isfinite(car.pct) || car.pct<0 || car.pct>=1) continue;
        const double dt=f.time-c.time;
        double travel=-1;
        if(c.seen && dt>0 && dt<=1) { travel=car.pct-c.pct; if(travel<-.5) travel+=1; }
        const int incidents=d.index==f.player && f.incidentsKnown?f.incidents:d.incidentCount;
        // Records take only representative splits: on track (or position unknown), out of the pits, racing.
        const bool clean=!(f.flags&FullCourse) && !(car.sessionFlags&(SessionFlag::Disqualify|SessionFlag::Furled))
            && !(s.type=="Race" && f.state>0 && f.state<4) && !car.pit && (car.surface==3 || car.surface<0);
        double progress;
        if(travel<0 || travel>.1 || (s.trackMeters>0 && travel*s.trackMeters>150*dt+2)) {
            // Restart: the lap counter only seeds the lap number, which never goes back for this car.
            const int counted=car.completed>=0?car.completed:std::max(0,car.lap-1);
            progress=counted+car.pct;
            if(c.progress>=0 && progress<c.progress) {
                const double lap=std::floor(c.progress);
                progress=lap+car.pct+(car.pct<c.progress-lap-.5?1:0); // Jitter while stopped stays on its lap.
            }
            std::fill(c.current.begin(),c.current.end(),-1); c.clean=clean; c.entered=-1;
        } else {
            progress=c.progress+travel;
            if(!clean || (incidents>=0 && c.incidents>=0 && incidents>c.incidents)) c.clean=false;
            int base=static_cast<int>(std::floor(c.progress));
            int next=static_cast<int>(std::upper_bound(cuts_.begin(),cuts_.end(),c.progress-base+1e-9)-cuts_.begin());
            while(next<=segments && base+cuts_[next]<=progress+1e-9 && travel>0) {
                const double crossed=base+cuts_[next],at=c.time+std::clamp((crossed-c.progress)/travel,0.,1.)*dt;
                if(c.entered>=0) {
                    c.current[next-1]=at-c.entered; c.counted[next-1]=c.clean; Publish(c,next-1,c.current[next-1],base,c.clean);
                    // Official records use actual sector totals, never sums of independent best minis.
                    if(next%Parts==0) {
                        double total=0; bool counted=true;
                        for(int i=next-Parts;i<next;++i) { if(c.current[i]<0) { total=-1; break; } total+=c.current[i]; counted=counted && c.counted[i]; }
                        if(total>0) Publish(c,segments+next/Parts-1,total,base,counted);
                    }
                }
                c.entered=at; c.clean=clean; // Each mini-sector starts representative or not by itself.
                if(next==segments) { std::fill(c.current.begin(),c.current.end(),-1); ++base; next=1; }
                else ++next;
            }
        }
        c.seen=true; c.progress=progress; c.pct=car.pct; c.time=f.time; c.incidents=incidents;
    }
}
void SectorTiming::Publish(Sample& c,int split,double value,int lap,bool counted) {
    if(!std::isfinite(value) || value<=0) return;
    c.recent[split]=value; c.laps[split]=lap;
    (split<static_cast<int>(cuts_.size())-1?c.miniLap:c.fullLap)=lap;
    if(!counted) { c.improved[split]=NotCounted; c.revision=++revision_; return; }
    c.improved[split]=c.personal[split]<0 || value<c.personal[split]-1e-6;
    if(c.improved[split]) c.personal[split]=value;
    auto& fastest=fastest_[static_cast<size_t>(c.classSlot)*Published()+split];
    if(fastest<0 || value<fastest-1e-6) { fastest=value; ++recordRevision_; }
    c.revision=++revision_;
}
double SectorTiming::Latest(int car,int sector,int part) const {
    if(car<0 || car>=static_cast<int>(cars_.size()) || sector<0 || sector>=Count() || part>=Parts || part< -1) return -1;
    const auto& c=cars_[car]; const int split=part<0?Count()*Parts+sector:sector*Parts+part;
    return c.laps[split]==(part<0?c.fullLap:c.miniLap)?c.recent[split]:-1;
}
SectorTiming::Pace SectorTiming::Status(int car,int sector,int part) const {
    const double value=Latest(car,sector,part);
    if(value<0) return Pace::Unknown;
    const auto& c=cars_[car]; const int split=part<0?Count()*Parts+sector:sector*Parts+part;
    if(c.improved[split]==NotCounted) return Pace::Slower;
    if(value<=fastest_[static_cast<size_t>(c.classSlot)*Published()+split]+1e-6) return Pace::SessionBest;
    return c.improved[split]?Pace::PersonalBest:Pace::Slower;
}
}
