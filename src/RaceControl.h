#pragma once
#include "Core.h"
#include <cstdint>
namespace rm {
enum class EventType { Offtrack, Incident, Crash, PitEntry, PitStop, Overtake };
struct RaceEvent {
    uint64_t id=0; EventType type=EventType::Incident;
    int session=-1, car=-1, lap=-1; double time=0;
    std::string number, driver, detail;
    bool estimated=false;
};
class EventRecorder {
public:
    void Update(const Session&,const Frame&);
    // A reset keeps counting, so a revision never comes back with other events.
    void Reset() { const uint64_t revision=revision_+1; *this={}; revision_=revision; }
    const std::vector<RaceEvent>& Events() const { return events_; }
    // Changes with every added or amended event.
    uint64_t Revision() const { return revision_; }
private:
    struct Sample { bool seen=false,pit=false; int surface=-1,position=0,incidents=-1,lap=-1; double progress=-1,speed=-1,pitTime=-1,lastCrash=-100; unsigned flags=0; std::string driver; };
    struct Pass { int car=-1,other=-1; double time=0; };
    void Add(const Session&,const Frame&,const Driver&,EventType,double,const std::string&,bool=false);
    std::array<Sample,MaxCars> samples_{};
    std::vector<RaceEvent> events_; std::vector<Pass> passes_;
    std::string key_; int session_=-1; double time_=-1, frontier_=-1; uint64_t next_=1, revision_=0;
};
struct SimMessage { unsigned command=0,arg=0,data=0; };
int CameraNumber(const std::string&);
SimMessage CameraMessage(const std::string&,int group);
SimMessage ReplayMessage(int session,double eventTime,int leadSeconds);
}
