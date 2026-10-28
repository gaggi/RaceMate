#include "PassingTimes.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <numbers>
#include <algorithm>
#include <fstream>
#include <windows.h>
#include "Telemetry.h"
using namespace rm;
namespace {
void Check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
void Close(double a,double b,double tolerance,const char* text) { if(!(std::abs(a-b)<=tolerance)) throw std::runtime_error(text); }
// Very uneven speed: lap position over time, 100 s a lap, always moving forward.
double Position(double t) { const double lap=t/100+.06*std::sin(2*std::numbers::pi*t/100); return lap-std::floor(lap); }
Session Track(int cars,int classes=1) {
    Session s; s.key="gaps"; s.type="Race"; s.trackMeters=5000;
    for(int i=0;i<cars;++i) { Driver d; d.index=i; d.classId=1+i%classes; d.name="Driver "+std::to_string(i); d.estimatedLapTime=100+10*(i%classes); s.drivers.push_back(d); }
    return s;
}
// Car i runs offsets[i] seconds behind the clock on the same uneven profile.
Frame At(double t,const std::vector<double>& offsets) {
    Frame f; f.connected=f.onTrack=true; f.player=0; f.session=0; f.time=t; f.bestLap=100;
    for(size_t i=0;i<offsets.size();++i) { auto& c=f.cars[i]; const double p=Position(t-offsets[i]); c.pct=p; c.completed=static_cast<int>(std::floor((t-offsets[i])/100)); c.lap=c.completed+1; c.surface=3; c.estTime=-1; }
    return f;
}
}
void PassingTimesTests() {
    // Car 1 is 2 s ahead of car 0 on the uneven profile: lap distance alone says anything from 1.6 s to 2.4 s.
    auto s=Track(2); PassingTimes passes; const std::vector<double> offsets{2,0};
    for(int n=0;n<=60;++n) passes.Update(s,At(n/60.,offsets));
    Check(std::isnan(passes.Gap(1,0)),"unknown until the car ahead has been seen for the gap");
    for(int n=61;n<=200*60;++n) {
        passes.Update(s,At(n/60.,offsets));
        if(n>3*60 && n%97==0) Close(passes.Gap(1,0),2,.002,"measured gap on an uneven profile");
    }
    Check(passes.Points()==500 && passes.Memory()<=2*500*sizeof(double),"10 m points, storage only for cars that appear");
    // Side by side: zero once the car ahead has passed one point under observation.
    PassingTimes beside; const std::vector<double> together{0,0.001};
    for(int n=0;n<=60;++n) beside.Update(s,At(n/60.,together));
    Close(beside.Gap(0,1),.001,.002,"side by side reads zero within the first second");
    // A gap in telemetry or a jump forgets the earlier passes until they are seen again.
    PassingTimes broken; for(int n=0;n<=600;++n) broken.Update(s,At(n/60.,offsets));
    broken.Update(s,At(12,offsets)); Check(std::isnan(broken.Gap(1,0)),"a 2 s gap forgets earlier passes");
    for(int n=721;n<=1000;++n) broken.Update(s,At(n/60.,offsets));
    Close(broken.Gap(1,0),2,.002,"known again after re-observing the gap");
    broken.Update(s,At(5,offsets)); Check(std::isnan(broken.Gap(1,0)),"a rewind forgets every pass");

    // Relative, Standings and Traffic. Different classes with their own SDK curves side by side read zero.
    auto multi=Track(2,2); PassingTimes field; const std::vector<double> side{0,0.02};
    auto estimates=[&](Frame f) { for(int i=0;i<2;++i) { auto& c=f.cars[i]; c.estTime=(i?.97:1.03)*c.pct*multi.drivers[i].estimatedLapTime; } return f; };
    for(int n=0;n<=120;++n) field.Update(multi,estimates(At(n/60.,side)));
    auto f=estimates(At(2,side));
    auto rows=Relative(multi,f,9,false,false,&field);
    const auto other=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
    Check(other!=rows.end() && other->gap>=0 && other->gap<.05,"another class beside you is beside you");
    // Without measurements two classes share the lap distance, not two different time curves.
    rows=Relative(multi,f,9,false,false,nullptr);
    const double distance=(f.cars[0].pct-f.cars[1].pct)*100;
    const auto fallback=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
    Close(fallback->gap,distance,.001,"other classes fall back to lap distance on one scale");
    // Same class, one estimate unusable (0 mid-lap): both use lap distance, never time minus distance.
    auto same=Track(2); auto g=At(50,{0,0}); g.cars[0].pct=.500; g.cars[1].pct=.505; g.cars[0].estTime=40; g.cars[1].estTime=0;
    rows=Relative(same,g,9,false,false,nullptr);
    const auto ahead=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
    Close(ahead->gap,-.5,.001,"an unusable estimate makes both cars use distance");
    g.cars[1].estTime=g.cars[0].estTime-.1; rows=Relative(same,g,9,false,false,nullptr);
    Check(std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;})->gap<=0,"the car ahead on track never reads as behind");
    // Traffic: the side comes from the position, the seconds from the passes.
    g=estimates(At(2,side)); g.cars[1].pct=g.cars[0].pct-.0004; field.Update(multi,g);
    auto traffic=CarsBehind(multi,g,3,false,&field);
    Check(traffic.size()==1 && traffic[0].index==1,"a car just behind on track is behind in Traffic");
    g.cars[1].pct=g.cars[0].pct+.0004; traffic=CarsBehind(multi,g,3,false,nullptr);
    Check(traffic.empty(),"a car just ahead on track is never behind in Traffic");
}
int PassingTimesBenchmark() {
    for(int cars:{60,128}) {
        auto s=Track(cars); PassingTimes passes; std::vector<double> offsets(cars); for(int i=0;i<cars;++i) offsets[i]=i*.7;
        constexpr int frames=36000; double sink=0;
        const auto begin=std::chrono::steady_clock::now();
        for(int n=0;n<frames;++n) passes.Update(s,At(n/60.,offsets));
        const auto middle=std::chrono::steady_clock::now();
        for(int n=0;n<frames;++n) for(int i=1;i<cars;++i) sink+=passes.Gap(i-1,i);
        const auto end=std::chrono::steady_clock::now();
        const double update=std::chrono::duration<double,std::micro>(middle-begin).count()/frames;
        const double gaps=std::chrono::duration<double,std::nano>(end-middle).count()/frames/(cars-1);
        std::cout<<cars<<" cars, 5 km: frame construction + passing times "<<update<<" us/frame; one gap "<<gaps<<" ns; "
            <<passes.Memory()/1024<<" KB"<<(sink<0?"!":"")<<"\n";
    }
    return 0;
}
// Live or TapeMate playback: Relative with measured gaps against the fallback alone, for every car within 100 m
// of the reference car. Close cars must read close; the summary says how often the gap was measured.
int PassingTimesLog(int seconds,const char* path) {
    Telemetry reader; Frame frame; Session session; PassingTimes passes; std::ofstream out(path);
    out<<"time,car,class,metres,measured,fallback\n";
    long rows=0,measured=0,close=0,closeBad=0,fallbackBad=0; double worst=0;
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds); double last=-1; int frames=0;
    while(std::chrono::steady_clock::now()<end) {
        if(!reader.Read(frame,session) || frame.time==last) { Sleep(1); continue; }
        last=frame.time; ++frames; passes.Update(session,frame);
        if(frames%6 || session.trackMeters<=0) continue; // 10 Hz, like the Relative.
        const int reference=ReferenceCar(frame); if(reference<0 || reference>=MaxCars) continue;
        const auto with=Relative(session,frame,15,false,false,&passes),without=Relative(session,frame,15,false,false,nullptr);
        for(const auto& row:with) {
            if(row.player) continue;
            double apart=frame.cars[row.driver.index].pct-frame.cars[reference].pct; apart-=std::floor(apart+.5); apart*=session.trackMeters;
            if(std::abs(apart)>100) continue;
            const auto other=std::find_if(without.begin(),without.end(),[&](const Row& r){return r.driver.index==row.driver.index;});
            const double fallback=other==without.end()?std::numeric_limits<double>::quiet_NaN():other->gap;
            const bool known=std::isfinite(passes.Gap(apart>0?row.driver.index:reference,apart>0?reference:row.driver.index));
            ++rows; measured+=known;
            if(std::abs(apart)<5) { ++close; if(std::abs(row.gap)>.15) ++closeBad; if(std::abs(fallback)>.15) ++fallbackBad; worst=std::max(worst,std::abs(row.gap)); }
            out<<frame.time<<','<<row.driver.index<<','<<row.driver.classId<<','<<apart<<','<<row.gap<<','<<fallback<<'\n';
        }
    }
    std::cout<<frames<<" frames, "<<passes.Points()<<" points, "<<passes.Memory()/1024<<" KB; cars within 100 m: "<<rows<<", measured "<<(rows?100.*measured/rows:0)
        <<" %; within 5 m: "<<close<<", over 0.15 s: "<<closeBad<<" (fallback alone: "<<fallbackBad<<"), largest "<<worst<<" s\n";
    return 0;
}
