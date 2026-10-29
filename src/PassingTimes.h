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
    bool HasProfile(int car) const { return car>=0 && car<MaxCars && cars_[car].lapTime>0; }
    double LapTime(int car) const { return HasProfile(car)?cars_[car].lapTime:-1; }
    // On profile's clean lap: seconds from lap position from forward to to, and the lap position seconds into it.
    double Along(int profile,double from,double to) const;
    double Locate(int profile,double seconds) const;
    // The car of this class with the fastest stored clean lap, so it changes rarely; -1 before any.
    int ClassProfile(int classId) const;
    int Points() const { return points_; }
    size_t Memory() const { return (times_.capacity()+profiles_.capacity())*sizeof(double); }
private:
    // lapStart and clean: the lap under way, a clean-lap candidate while watched throughout, on track, out of
    // the pits, moving and without a full-course caution. lapTime: the stored clean lap's, 0 before there is one.
    // moved: when it last moved forward. car and classId: which car this index is; another car starts afresh.
    struct Car { int slot=-1,classId=0; std::string car,driver; double pct=-1,time=-1,since=-1,moved=-1,lapStart=-1,lapTime=0; bool seen=false,clean=false; };
    std::array<Car,MaxCars> cars_{};
    std::vector<double> times_; // points_ per observed car, in slot order; only cars that appear get a slot.
    std::vector<double> profiles_; // The same layout: each point's time into the last clean lap.
    int points_=0,slots_=0;
    std::string key_; int session_=-1; double time_=-1,metres_=0;
    void Forget(Car& c,double time) { c.since=time; c.clean=false; } // Older passes no longer count.
    double Into(const Car& c,double pct) const; // Time into its clean lap at a lap position.
};
}
