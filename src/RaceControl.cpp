#include "RaceControl.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rm {
int CameraNumber(const std::string& number) {
    if(number.empty() || number.size()>3 || !std::all_of(number.begin(),number.end(),[](char c){return c>='0' && c<='9';})) return -1;
    const int value=std::stoi(number);
    const bool padded=number.size()>1 && number[0]=='0';
    return value+(padded?1000*static_cast<int>(number.size()):0);
}
SimMessage CameraMessage(const std::string& number,int group) {
    const int encoded=CameraNumber(number);
    return {1,static_cast<unsigned>(std::max(0,encoded)),static_cast<unsigned>(std::clamp(group,0,65535))};
}
SimMessage ReplayMessage(int session,double time,int lead) {
    const double ms=std::clamp((time-lead)*1000,0.0,static_cast<double>(std::numeric_limits<int32_t>::max()));
    return {12,static_cast<unsigned>(std::clamp(session,0,65535)),static_cast<unsigned>(std::llround(ms))};
}
void EventRecorder::Add(const Session&,const Frame& f,const Driver& d,EventType type,double time,const std::string& detail,bool estimated) {
    RaceEvent event; event.id=next_++; event.type=type; event.session=f.session; event.car=d.index;
    event.lap=f.cars[d.index].lap; event.time=time; event.number=d.number; event.driver=d.name; event.detail=detail; event.estimated=estimated;
    events_.push_back(std::move(event));
    if(events_.size()>2000) events_.erase(events_.begin(),events_.begin()+100);
}
void EventRecorder::Update(const Session& s,const Frame& f) {
    if(!f.connected) { for(auto& sample:samples_) sample.seen=false; time_=-1; passes_.clear(); return; }
    if(key_!=s.key || session_!=f.session) { Reset(); key_=s.key; session_=f.session; }
    // Reviewing old footage must not add duplicate events or overwrite the live baseline.
    if(View(f)==ViewMode::Replay) {
        for(auto& sample:samples_) sample.seen=false; time_=-1; passes_.clear(); return;
    }
    if(f.time<frontier_-1e-7) { for(auto& sample:samples_) sample.seen=false; time_=-1; passes_.clear(); return; }
    frontier_=std::max(frontier_,f.time);
    const double dt=f.time-time_;
    if(dt==0) return;
    const bool continuous=time_>=0 && dt>0 && dt<=2;
    if(!continuous) { for(auto& sample:samples_) sample.seen=false; passes_.clear(); }
    for(const auto& d:s.drivers) {
        if(d.index<0 || d.index>=MaxCars) continue;
        const auto& car=f.cars[d.index]; auto& previous=samples_[d.index];
        if(previous.driver!=d.name) previous={};
        if(car.surface<0 || car.pct<0) { previous.seen=false; previous.pitTime=-1; continue; }
        const int incidents=d.index==f.player && f.incidentsKnown?f.incidents:d.incidentCount;
        const double progress=car.completed>=0?car.completed+car.pct:(car.lap>=0?car.lap-1+car.pct:-1);
        double speed=-1;
        const bool seen=previous.seen && continuous;
        if(seen && progress>=0 && previous.progress>=0 && s.trackMeters>0) {
            const double travel=progress-previous.progress;
            if(travel>=0 && travel<.1) speed=travel*s.trackMeters/dt;
            if(speed>120) speed=-1;
        }
        if(seen) {
            if(!car.pit && car.surface==0 && previous.surface!=0)
                Add(s,f,d,EventType::Offtrack,f.time,"Left the track");
            const int delta=incidents>=0 && previous.incidents>=0?incidents-previous.incidents:0;
            if(delta>0) Add(s,f,d,EventType::Incident,f.time,"+"+std::to_string(delta)+" incident point(s)");
            const unsigned newPenalty=(car.sessionFlags & ~previous.flags)&(SessionFlag::Black|SessionFlag::Disqualify);
            if(newPenalty) Add(s,f,d,EventType::Incident,f.time,(newPenalty&SessionFlag::Disqualify)?"Disqualified flag":"Black flag");
            const bool repair=(car.sessionFlags&SessionFlag::Repair) && !(previous.flags&SessionFlag::Repair);
            const bool impact=speed>=0 && previous.speed>18 && previous.speed-speed>12 && dt<=1 && car.surface==0;
            if(!car.pit && (repair || impact) && f.time-previous.lastCrash>=10) {
                Add(s,f,d,EventType::Crash,f.time,repair?"New repair flag; possible crash":"Sudden speed loss off track; possible crash",true); previous.lastCrash=f.time;
            }
            if(car.pit && !previous.pit) { previous.pitTime=f.time; previous.lap=car.lap; Add(s,f,d,EventType::PitEntry,f.time,"Entered pit lane"); }
            if(!car.pit && previous.pit && previous.pitTime>=0) {
                Add(s,f,d,EventType::PitStop,previous.pitTime,"Pit lane visit: "+std::to_string(static_cast<int>(std::round(f.time-previous.pitTime)))+" s");
                if(car.lastPitStationary>=0) events_.back().detail+="; stationary: ~"+std::to_string(static_cast<int>(std::round(car.lastPitStationary)))+" s";
                events_.back().lap=previous.lap; previous.pitTime=-1;
            }
        } else previous.pitTime=-1;
        // Keep previous positions until pair-wise pass detection has finished.
        previous.speed=speed; previous.incidents=incidents; previous.flags=car.sessionFlags;
    }
    if(continuous && s.type=="Race" && f.state==4 && !(f.flags&SessionFlag::Neutralized)) {
        for(auto it=passes_.begin();it!=passes_.end();) {
            const auto& a=f.cars[it->car]; const auto& b=f.cars[it->other];
            if(a.pit || b.pit || a.surface!=3 || b.surface!=3 || a.position<=0 || b.position<=0 || a.position>=b.position) it=passes_.erase(it);
            else if(f.time-it->time>=2) {
                const auto driver=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){return d.index==it->car;});
                const auto other=std::find_if(s.drivers.begin(),s.drivers.end(),[&](const Driver& d){return d.index==it->other;});
                if(driver!=s.drivers.end() && other!=s.drivers.end()) Add(s,f,*driver,EventType::Overtake,it->time,"Passed #"+other->number+" "+other->name,true);
                it=passes_.erase(it);
            } else ++it;
        }
        for(const auto& a:s.drivers) for(const auto& b:s.drivers) {
            if(a.index<0 || b.index<0 || a.index>=MaxCars || b.index>=MaxCars || a.index==b.index || a.classId!=b.classId) continue;
            const auto& ca=f.cars[a.index]; const auto& cb=f.cars[b.index]; const auto& pa=samples_[a.index]; const auto& pb=samples_[b.index];
            if(!pa.seen || !pb.seen || pa.pit || pb.pit || ca.pit || cb.pit || ca.surface!=3 || cb.surface!=3 || pa.surface!=3 || pb.surface!=3) continue;
            const double da=ca.completed+ca.pct, db=cb.completed+cb.pct;
            if(ca.completed<0 || cb.completed<0 || std::abs(da-db)>.15 || ca.position<=0 || cb.position<=0) continue;
            if(pa.position==pb.position+1 && ca.position<cb.position && std::none_of(passes_.begin(),passes_.end(),[&](const Pass& p){return p.car==a.index && p.other==b.index;})) passes_.push_back({a.index,b.index,f.time});
        }
    } else passes_.clear();
    for(const auto& d:s.drivers) if(d.index>=0 && d.index<MaxCars) {
        const auto& car=f.cars[d.index]; auto& previous=samples_[d.index];
        previous.seen=car.surface>=0 && car.pct>=0; previous.driver=d.name; previous.pit=car.pit; previous.surface=car.surface; previous.position=car.position;
        previous.progress=car.completed>=0?car.completed+car.pct:(car.lap>=0?car.lap-1+car.pct:-1);
    }
    time_=f.time;
}
}
