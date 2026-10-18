#pragma once
#include "Core.h"
#include <cstdint>
#include <random>

namespace rm {
// The preview's race: a random GT3 and GT4 field at Lime Rock Park over 45 minutes, each car with its own pace,
// one pit stop with fuel and tires, the odd offtrack and incident, and weather that drifts. It produces the
// session and frames iRacing would, so the real analyses (fuel, finish forecast, pit history, events) run on it
// and every overlay shows one consistent race.
class PreviewRace {
public:
    // A new race from this seed; the clock stands a few seconds before the green flag.
    void Start(uint32_t seed);
    // Moves the race on in steps of at most half a second, so pit stops and offtracks are seen as iRacing shows them.
    void Advance(double seconds);
    // Where the race is shown from: a random moment in its middle part.
    double ShowFrom() const { return showFrom_; }
    double Time() const { return time_; }
    bool Finished() const { return time_>=Length+90; }
    const Session& CurrentSession() const { return session_; }
    const Frame& CurrentFrame() const { return frame_; }
    static constexpr double Length = 45*60;   // A 45-minute race.
private:
    struct Runner {
        int index = 0; double pace = 0, burn = 0, progress = 0, lapStart = 0, best = -1, last = -1;
        int stopLap = 0; bool stopped = false;
        enum Phase { Track, Lane, Stall, Exit } phase = Track;
        double stall = 0, offUntil = -1, fuel = 0, refuelTo = 0;
        bool outLap = false; int outCompleted = -1;
    };
    void Step(double dt);
    void Publish();
    double Noise(int a, int b, int c) const;
    std::mt19937 random_;
    uint32_t seed_ = 0;
    std::vector<Runner> runners_;
    Session session_; Frame frame_;
    int player_ = 0;
    double time_ = 0, showFrom_ = 0, capacity_ = 100;
    double air_ = 20, track_ = 30, rain_ = 0, windSpeed_ = 3, windDirection_ = 0;
    int wetness_ = 1, skies_ = 1; double humidity_ = .5; bool wet_ = false;
};
}
