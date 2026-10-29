#pragma once
#include "Core.h"
#include <cstdint>

namespace rm {
// When each car passed fixed points about every 10 m of the lap, and a copy of its last clean lap of them.
// Gap: how long ago the car ahead was where the car behind is now, as on F1 timing screens. Measured, so it holds across classes
// and on tracks with very uneven speed; known once the car ahead has been watched for that long.
// Reach: how long a car took on its last clean lap from one car's position to another's. The Relative and Traffic
// time each pair by the car that has to cover the distance, so a faster class behind shows when it is there, and a
// stopped car ahead comes closer as the distance to it shrinks.
class PassingTimes {
public:
    PassingTimes()=default;
    PassingTimes(const PassingTimes&)=delete;
    PassingTimes& operator=(const PassingTimes&)=delete;
    static constexpr double Spacing=10; // Metres between points.
    static constexpr int MinPoints=64, MaxPoints=4096;
    void Update(const Session&,const Frame&);
    void Reset();
    // Seconds since ahead passed behind's current lap position, within its current lap of being ahead
    // (whole laps are the caller's). NaN while not observed long enough or after an interruption.
    double Gap(int ahead,int behind) const;
    // Seconds profile's last clean lap took from from's lap position forward to to's. NaN without such a lap.
    double Reach(int profile,int from,int to) const;
    // Profiles are cars (0 to MaxCars-1) or stored class references (from MaxCars on, see StoredProfile).
    bool HasProfile(int profile) const { return profile>=0 && (profile<MaxCars?cars_[profile].lapTime>0:profile-MaxCars<static_cast<int>(references_.size())); }
    double LapTime(int profile) const { return HasProfile(profile)?Curve(profile).lapTime:-1; }
    // On profile's clean lap: seconds from lap position from forward to to, and the lap position seconds into it.
    double Along(int profile,double from,double to) const;
    double Locate(int profile,double seconds) const;
    // How the car drives now against a profile: its actual time over its last 30 s of driving divided by the
    // profile's time for exactly those stretches. Stored are only the car's own passes, so the ratio always belongs
    // to the profile asked about: a new reference never doubles it. Standing, the pits and telemetry gaps end the
    // window; slow driving does not. limited: outside 0.7 to 2.0, which only catches broken windows.
    struct Pace { double ratio=1,raw=std::numeric_limits<double>::quiet_NaN(); bool limited=false; };
    Pace PaceAgainst(int profile,int car) const;
    static constexpr double PaceWindow=30;
    // The car of this class with the fastest stored clean lap, so it changes rarely; -1 before any.
    int ClassProfile(int classId) const;
    // A class's clean lap from an earlier session on this layout, kept on disk by the app: only a start reference,
    // after every profile of this session. Cleared with each new session; the app adds them again.
    bool AddReference(int classId,double lapTime,std::vector<double> times);
    int StoredProfile(int classId) const;
    // The fastest clean lap of a class in this session, to store: its times into the lap per point.
    bool ClassLap(int classId,double& lapTime,std::vector<double>& times) const;
    const std::string& SessionKey() const { return key_; }
    int Points() const { return points_; }
    size_t Memory() const { return (times_.capacity()+profiles_.capacity())*sizeof(double); }
private:
    // lapStart and clean: the lap under way, a clean-lap candidate while watched throughout, on track, out of
    // the pits, moving and without a full-course caution. lapTime: the stored clean lap's, 0 before there is one.
    // moved: when it last moved forward. car and classId: which car this index is; another car starts afresh.
    // slow: the last clean lap turned down as far slower than usual, 0 after a usual one.
    // trail: every 10th point passed, as points since the session began and the time, for the pace window; laps
    // counts the lines crossed, so points stay in order across them.
    struct Mark { long long point=0; double time=0; };
    struct Car {
        // stood: when it was last seen standing still for 2 s.
        int slot=-1,classId=0; std::string car,driver; double pct=-1,time=-1,since=-1,moved=-1,stood=-1e9,lapStart=-1,lapTime=0,slow=0; bool seen=false,clean=false;
        long long laps=0; std::array<Mark,64> trail{}; int trailEnd=0,trailCount=0;
    };
    std::vector<Car> cars_=std::vector<Car>(MaxCars); // On the heap: with their pace windows about 130 KB.
    std::vector<double> times_; // points_ per observed car, in slot order; only cars that appear get a slot.
    std::vector<double> profiles_; // The same layout: each point's time into the last clean lap.
    int points_=0,slots_=0;
    std::string key_; int session_=-1; double time_=-1,metres_=0;
    void Forget(Car& c,double time) { c.since=time; c.clean=false; c.trailCount=0; } // Older passes no longer count.
    struct Reference { int classId=0; double lapTime=0; std::vector<double> times; };
    std::vector<Reference> references_;
    struct Lap { const double* times=nullptr; double lapTime=0; };
    Lap Curve(int profile) const;
    double Into(const Lap& lap,double pct) const; // Time into a clean lap at a lap position.
};
}
