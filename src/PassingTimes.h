#pragma once
#include "Core.h"
#include <cstdint>

namespace rm {
// Measured time gaps: when each car last passed fixed points about every 10 m of the lap. The gap of a car
// behind is how long ago the car ahead was where it is now, as on F1 timing screens. It needs no lap-time model,
// so it is exact across classes and on tracks with very uneven speed, and it is known as soon as the car ahead
// has been observed for that long: side by side at once, 1.5 s apart after 1.5 s.
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
    int Points() const { return points_; }
    size_t Memory() const { return times_.capacity()*sizeof(double); }
private:
    struct Car { int slot=-1; double pct=-1,time=-1,since=-1; bool seen=false; };
    std::array<Car,MaxCars> cars_{};
    std::vector<double> times_; // points_ per observed car, in slot order; only cars that appear get a slot.
    int points_=0,slots_=0;
    std::string key_; int session_=-1; double time_=-1,metres_=0;
    void Forget(Car& c,double time) { c.since=time; } // Older passes no longer count.
};
}
