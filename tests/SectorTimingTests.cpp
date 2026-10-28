#include "SectorTiming.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <array>
#include <fstream>
#include <windows.h>
#include "Telemetry.h"
using namespace rm;
namespace {
void Check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
void Close(double a,double b,const char* text) { Check(std::abs(a-b)<.002,text); }
Session SessionOf(int count=2) {
    Session s; s.key="sectors"; s.number=0; s.trackId=353; s.trackMeters=1000; s.type="Practice";
    s.sectors={{0,0},{1,.25},{2,.75}};
    for(int i=0;i<count;++i) { Driver d; d.index=i; d.name="Driver "+std::to_string(i); s.drivers.push_back(d); }
    return s;
}
void Progress(Car& car,double distance) {
    car.completed=static_cast<int>(std::floor(distance)); car.lap=car.completed+1; car.pct=distance-car.completed;
}
Frame At(double t,int count=2) {
    Frame f; f.connected=f.onTrack=true; f.player=0; f.session=0; f.time=t;
    for(int i=0;i<count;++i) { auto& c=f.cars[i]; Progress(c,t/(30+i*3)); c.surface=3; }
    return f;
}
}
void SectorTimingTests() {
    const auto parsed=ParseSession("SplitTimeInfo:\n Sectors:\n - SectorNum: 0\n   SectorStartPct: 0.000\n - SectorNum: 1\n   SectorStartPct: 0.250\n - SectorNum: 2\n   SectorStartPct: 0.750\n",0);
    Check(parsed.sectors.size()==3,"parse SDK sectors"); Close(parsed.sectors[1].start,.25,"sector start fraction");
    Check(ParseSession("SplitTimeInfo:\n Sectors:\n - SectorNum: 0\n   SectorStartPct: 0\n - SectorNum: 0\n   SectorStartPct: 0.5\n",0).sectors.empty(),"reject duplicate sectors");
    Check(ParseSession("SplitTimeInfo:\n Sectors:\n - SectorNum: 0\n   SectorStartPct: nan\n",0).sectors.empty(),"reject nonfinite sectors");
    Check(ParseSession("SplitTimeInfo:\n Sectors:\n - SectorNum: 0\n   SectorStartPct: 0.000garbage\n",0).sectors.empty(),"reject malformed numbers");
    auto s=SessionOf(); SectorTiming splits;
    for(int n=0;n<=1351;++n) splits.Update(s,At(n/60.));
    Close(splits.Latest(0,1),15,"publish a full sector before completing a lap");
    for(int n=1352;n<=3601;++n) splits.Update(s,At(n/60.));
    Close(splits.Latest(0,0),7.5,"interpolated S1"); Close(splits.Latest(0,1),15,"interpolated S2"); Close(splits.Latest(0,2),7.5,"S3 across line");
    Close(splits.Latest(1,0),8.25,"opponent timing"); Close(splits.Latest(0,0,0),2.5,"mini timing");
    Check(splits.Status(0,0)==SectorTiming::Pace::SessionBest,"fastest observed sector is purple");
    const auto revision=splits.Revision(); splits.Update(s,At(3601/60.)); Check(splits.Revision()==revision,"same frame is idempotent");
    auto varied=[](double t) {
        auto f=At(t); Progress(f.cars[0],t<=60?t/30.:2+(t-60)/33.); Progress(f.cars[1],t<=66?t/33.:2+(t-66)/31.); return f;
    };
    for(int n=3602;n<=4020;++n) splits.Update(s,varied(n/60.));
    Close(splits.Latest(0,0),7.5,"retain previous S1 until the new S1 finishes");
    for(int n=4021;n<=4470;++n) splits.Update(s,varied(n/60.));
    Close(splits.Latest(0,0),8.25,"replace S1 immediately"); Check(splits.Latest(0,1)<0 && splits.Latest(0,2)<0,"a new S1 clears the previous lap's S2 and S3");
    Close(splits.Latest(0,1,0),5.5,"mini-sectors follow their own latest lap"); Check(splits.Latest(0,1,1)<0,"and clear their previous lap too"); Check(splits.Latest(0,0,0)>0,"the new lap's first mini replaces it");
    Check(splits.Status(0,0)==SectorTiming::Pace::Slower,"slower split is yellow");
    Close(splits.Latest(1,0),7.75,"each driver publishes independently");
    Check(splits.Status(1,0)==SectorTiming::Pace::PersonalBest,"personal improvement below session record is green");
    Check(splits.Status(1,0,0)==SectorTiming::Pace::PersonalBest,"mini has its own personal record");
    SectorTiming partial;
    for(int n=600;n<=1351;++n) partial.Update(s,At(n/60.));
    Check(partial.Latest(0,0)<0 && partial.Latest(0,1)<0,"late joining cannot manufacture a partial sector");
    for(int n=1352;n<=1801;++n) partial.Update(s,At(n/60.));
    Close(partial.Latest(0,2),7.5,"publish the first fully observed sector without requiring a lap");
    SectorTiming independent;
    for(int n=0;n<=4501;++n) {
        const double t=n/60.; auto f=At(t);
        if(t>66) {
            const double elapsed=t-66,mini=.25/3;
            const double pct=elapsed<=2?elapsed*mini/2:(elapsed<=5.5?mini+(elapsed-2)*mini/3.5:2*mini+(elapsed-5.5)*mini/3.5);
            Progress(f.cars[1],2+pct);
        }
        independent.Update(s,f);
    }
    Close(independent.Latest(1,0),9,"official sector uses the actual total");
    Check(independent.Status(1,0)==SectorTiming::Pace::Slower && independent.Status(1,0,0)==SectorTiming::Pace::SessionBest,"a purple mini does not manufacture a green or purple official sector");
    SectorTiming record;
    for(int n=0;n<=4450;++n) {
        const double t=n/60.; auto f=At(t); if(t>66) Progress(f.cars[1],2+(t-66)/24.); record.Update(s,f);
    }
    Check(record.Status(1,0)==SectorTiming::Pace::SessionBest && record.Status(0,0)!=SectorTiming::Pace::SessionBest,"purple follows a subsequent faster session record");
    // Each interrupted observation retains completed values but must not publish a short fake split.
    for(int failure=0;failure<8;++failure) {
        SectorTiming interrupted;
        for(int n=600;n<=3601;++n) interrupted.Update(s,At(n/60.));
        for(int n=3602;n<=4051;++n) {
            if(failure==0 && n>=3660 && n<3680) continue;
            auto sample=At(n/60.);
            if(n==3660) {
                if(failure==1) sample.cars[0].pit=true;
                if(failure==2) sample.cars[0].surface=0;
                if(failure==3) sample.cars[0].towing=true;
                if(failure==4) sample.cars[0].outLap=true;
                if(failure==5) sample.replayFramesBehind=300;
                if(failure==6) sample.flags=SessionFlag::Neutralized;
            }
            if(failure==7) { sample.incidentsKnown=true; sample.incidents=n>=3660?1:0; }
            interrupted.Update(s,sample);
        }
        Close(interrupted.Latest(0,0),7.5,"interrupted observations never publish a partial S1");
        Check(interrupted.Status(-1,0)==SectorTiming::Pace::Unknown && interrupted.Latest(0,64)<0,"invalid lookups remain unknown");
    }
    auto f=varied(74.6); s.drivers[0].name="replacement driver"; splits.Update(s,f);
    Check(splits.Latest(0,0)<0,"driver replacement clears history");
    s.key="new event"; splits.Update(s,At(80)); Check(splits.Latest(1,0)<0,"new event clears history at an increasing clock");
    s.sectors={{0,0},{1,.5}}; splits.Update(s,At(80.1)); Check(splits.Count()==2 && splits.Latest(1,0)<0,"layout change resets history");
    splits.Update(s,At(1)); Check(splits.Latest(1,0)<0,"rewind clears history");
    s.sectors.clear(); splits.Update(s,At(1.1)); Check(splits.Count()==0,"missing boundaries never invent thirds");
    f=At(2); f.connected=false; partial.Update(s,f); Check(partial.Count()==0,"disconnect clears history");
    {
        // iRacing may count the lap a few frames after the lap distance wraps; the split across the line still counts.
        SectorTiming late; auto one=SessionOf(1);
        for(int n=0;n<=4100;++n) { auto sample=At(n/60.,1); auto& c=sample.cars[0]; if(c.pct<.003 && c.completed>0) { --c.completed; --c.lap; } late.Update(one,sample); }
        Close(late.Latest(0,0),7.5,"S1 after a late lap counter"); Check(late.Latest(0,2)<0,"and the previous S3 cleared");
        SectorTiming early;
        for(int n=0;n<=3601;++n) { auto sample=At(n/60.,1); auto& c=sample.cars[0]; if(c.pct>.997) { ++c.completed; ++c.lap; } early.Update(one,sample); }
        Close(early.Latest(0,2),7.5,"S3 with an early lap counter"); Close(early.Latest(0,0),7.5,"keeps its lap's S1");
    }
    {
        // Pit stop: in the lane nothing counts; the out lap starts at its first complete sector.
        SectorTiming stop; auto one=SessionOf(1);
        for(int n=0;n<=4980;++n) {
            auto sample=At(n/60.,1); const double t=n/60.;
            if(t>61 && t<=66) sample.cars[0].pit=true;
            if(t>66) sample.cars[0].outLap=true;
            stop.Update(one,sample);
        }
        Close(stop.Latest(0,1),15,"out-lap S2"); Check(stop.Latest(0,0)<0 && stop.Latest(0,2)<0,"only the out lap's sectors remain");
    }
    {
        // Purple per class; a local yellow does not stop the timing.
        SectorTiming classes; auto two=SessionOf(2); two.drivers[1].classId=2;
        for(int n=0;n<=4100;++n) { auto sample=At(n/60.); sample.flags=SessionFlag::Yellow; classes.Update(two,sample); }
        Check(classes.Status(0,0)==SectorTiming::Pace::SessionBest && classes.Status(1,0)==SectorTiming::Pace::SessionBest,"each class has its own purple under a local yellow");
        SectorTiming caution;
        for(int n=0;n<=4100;++n) { auto sample=At(n/60.); sample.flags=SessionFlag::Caution; caution.Update(two,sample); }
        Check(caution.Latest(0,0)<0,"a full-course caution stops the timing");
    }
    for(bool formation:{false,true}) {
        SectorTiming invalid; auto race=SessionOf(1); race.type="Race";
        for(int n=0;n<=3700;++n) { f=At(n/60.,1); f.state=formation?3:4; if(!formation) f.flags=SessionFlag::CautionWaving; invalid.Update(race,f); }
        Check(invalid.Latest(0,0)<0,"formation and caution splits never create records");
    }
}
int SectorTimingBenchmark() {
    for(int cars:{60,128}) {
        auto s=SessionOf(cars); SectorTiming timing; constexpr int frames=36000;
        const auto begin=std::chrono::steady_clock::now();
        for(int n=0;n<frames;++n) timing.Update(s,At(n/60.,cars));
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/frames;
        std::cout<<cars<<" cars: frame construction + sector timing "<<us<<" us/frame ("<<us*60/1000<<" ms/s at 60 Hz)\n";
    }
    return 0;
}
// Live or TapeMate playback: how often the lap counter and the lap distance change in different frames
// at the line, and how many S1/S2/S3 each car published, per car, as a check against a real session.
int SectorTimingLog(int seconds,const char* path) {
    Telemetry reader; Frame frame; Session session; SectorTiming timing; std::ofstream out(path);
    struct Car { double pct=-1; int completed=-1,wraps=0,early=0,late=0; std::array<double,3> last{-1,-1,-1}; std::array<int,3> splits{}; };
    std::array<Car,MaxCars> cars{}; std::array<bool,MaxCars> pending{}; // Wrapped, lap not yet counted.
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds); double last=-1; int frames=0;
    while(std::chrono::steady_clock::now()<end) {
        if(!reader.Read(frame,session) || frame.time==last) { Sleep(1); continue; }
        last=frame.time; ++frames; timing.Update(session,frame);
        for(const auto& d:session.drivers) {
            if(d.index<0 || d.index>=MaxCars || d.paceCar || d.spectator) continue;
            auto& c=cars[d.index]; const auto& car=frame.cars[d.index];
            const bool wrapped=c.pct>.9 && car.pct>=0 && car.pct<.1, counted=c.completed>=0 && car.completed>c.completed;
            if(wrapped) { ++c.wraps; if(!counted) pending[d.index]=true; }
            if(counted && !wrapped) { if(pending[d.index]) ++c.late; else ++c.early; }
            if(counted) pending[d.index]=false;
            c.pct=car.pct; c.completed=car.completed;
            for(int s=0;s<std::min(3,timing.Count());++s) { const double v=timing.Latest(d.index,s); if(v>0 && v!=c.last[s]) ++c.splits[s]; c.last[s]=v; }
        }
    }
    out<<"car,driver,class,line crossings,counter late,counter early,S1,S2,S3\n";
    for(const auto& d:session.drivers) if(d.index>=0 && d.index<MaxCars && !d.paceCar && !d.spectator) {
        const auto& c=cars[d.index];
        out<<d.index<<','<<d.name<<','<<d.classId<<','<<c.wraps<<','<<c.late<<','<<c.early<<','<<c.splits[0]<<','<<c.splits[1]<<','<<c.splits[2]<<'\n';
    }
    std::cout<<frames<<" frames, "<<timing.Count()<<" sectors\n"; return 0;
}
