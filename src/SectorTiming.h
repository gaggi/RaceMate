#pragma once
#include "Core.h"
#include <cstdint>
#include <span>

namespace rm {
// Observed, interpolated timing. No official lap-validity or timing claims.
// Three fixed samples per SDK sector let every consumer choose full or mini sectors without re-learning.
class SectorTiming {
public:
    SectorTiming()=default;
    SectorTiming(const SectorTiming&)=delete;
    SectorTiming& operator=(const SectorTiming&)=delete;
    static constexpr int Parts=3, MaxSectors=64, MaxSegments=MaxSectors*Parts;
    enum class Pace { Unknown, Slower, PersonalBest, SessionBest };
    void Update(const Session&,const Frame&);
    void Reset();
    int Count() const { return static_cast<int>(sectors_.size()); }
    const std::vector<SectorBoundary>& Sectors() const { return sectors_; }
    double Start(int segment) const { return cuts_[segment]; }
    double End(int segment) const { return cuts_[segment+1]; }
    // Splits of the lap the car last completed a split in, as on F1 timing screens: a new S1 clears the
    // previous lap's S2 and S3; a lap begun after the pits starts at the first split it completes.
    // Full sectors and mini-sectors each follow their own latest split.
    double Latest(int car,int sector,int part=-1) const;
    // Purple: fastest observed in the car's class this session. Splits with a pit visit, an off-track, an incident
    // or under caution are shown but never records: yellow.
    Pace Status(int car,int sector,int part=-1) const;
    uint64_t Revision() const { return revision_; }
    uint64_t LayoutRevision() const { return layoutRevision_; }
    uint64_t RecordRevision() const { return recordRevision_; }
    uint64_t CarRevision(int car) const { return car>=0 && car<static_cast<int>(cars_.size())?cars_[car].revision:0; }
private:
    struct Sample {
        std::string driver,car;
        std::span<double> current,recent,personal;
        std::span<int> laps; // Lap of each recent split.
        std::span<unsigned char> improved; // Slower, improved, or NotCounted.
        std::span<unsigned char> counted; // Per mini-sector of the current lap: representative for records.
        // progress: laps travelled, followed through the lap-distance wrap rather than the lap counter,
        // which iRacing may update a frame before or after the distance.
        double time=-1,progress=-1,pct=-1,entered=-1;
        int incidents=-1,classSlot=-1,fullLap=std::numeric_limits<int>::min(),miniLap=std::numeric_limits<int>::min();
        uint64_t revision=0;
        bool seen=false,clean=false;
    };
    std::vector<Sample> cars_; // One bounded allocation at configuration, never per telemetry frame.
    std::vector<double> times_; // Sized to actual SDK sectors, rather than worst-case arrays for every car.
    std::vector<int> laps_;
    std::vector<unsigned char> improvements_;
    std::vector<int> classes_; // Class IDs; fastest_ holds one row of session records per class.
    std::vector<double> fastest_;
    int Published() const { return static_cast<int>(cuts_.size())-1+Count(); }
    int ClassSlot(int classId);
    static constexpr unsigned char NotCounted=2; // Shown, never a record: pit lane, off track, incident, caution.
    void Publish(Sample&,int,double,int,bool);
    std::vector<SectorBoundary> sectors_;
    std::vector<SectorBoundary> configured_;
    std::vector<double> cuts_;
    std::string key_; int session_=-1,track_=-1;
    double time_=-1;
    uint64_t revision_=0,layoutRevision_=0,recordRevision_=0;
};
}
