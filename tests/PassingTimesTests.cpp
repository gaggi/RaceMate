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
    Check(passes.Points()==500 && passes.Memory()<=2*2*500*sizeof(double),"10 m points and a lap profile, only for cars that appear");
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

    // The Relative times each pair by the car behind on its last clean lap. A car 3 s ahead stops at 210 s, or
    // crawls on at a quarter of its pace: you come closer as the distance shrinks, instead of keeping 3 s.
    for(double crawl:{0.,.25}) {
        PassingTimes trouble; const auto one=Track(2);
        auto frame=[&](double t) {
            auto f=At(t,{3,0}); const double driven=t<=210?t:210+(t-210)*crawl;
            f.cars[1].pct=Position(driven); return f;
        };
        for(int n=0;n<=60*60;++n) trouble.Update(one,frame(n/60.));
        Check(!trouble.HasProfile(0),"no profile before a clean lap from line to line");
        for(int n=60*60+1;n<=212*60;++n) trouble.Update(one,frame(n/60.));
        Check(trouble.HasProfile(0) && trouble.HasProfile(1),"a clean lap becomes the profile");
        // At 212 s you are where the other was at 209 s; you need to get to where it is now.
        const auto rows=Relative(one,frame(212),9,false,false,&trouble);
        const auto other=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
        Close(-other->gap,crawl?1.5:1.0,.02,crawl?"a crawling car ahead: the time to reach it":"a stopped car ahead: the time to reach it");
        for(int n=212*60+1;n<=static_cast<int>((crawl?213.8:212.8)*60);++n) trouble.Update(one,frame(n/60.));
        Close(trouble.Reach(0,0,1),crawl?.15:.2,.02,"nearly there, nearly zero");
    }
    // A faster class behind is timed at its own pace: 2.5 s of your lap, which it covers in 2.0 s.
    {
        PassingTimes faster; auto two=Track(2,2);
        auto frame=[&](double t) { auto f=At(t,{0,0}); f.cars[1].pct=Position(1.25*t-77.5); return f; };
        for(int n=0;n<=300*60;++n) faster.Update(two,frame(n/60.));
        const auto f=frame(300);
        const auto rows=Relative(two,f,9,false,false,&faster);
        const auto other=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
        Close(other->gap,2.0,.02,"a faster class behind shows when it is there");
        Close(faster.Reach(0,1,0),2.5,.02,"your own lap would say 2.5 s");
        const auto traffic=CarsBehind(two,f,3,false,&faster);
        Check(traffic.size()==1 && std::abs(traffic[0].gap-2.0)<.02,"Traffic shows the same");
    }
    // Rolling start: iRacing still reports the formation lap for a second after the line. Lap one still counts.
    {
        PassingTimes start; auto one=Track(1); one.type="Race";
        for(int n=0;n<=112*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(t+90); f.state=t<11?3:4; start.Update(one,f); }
        Check(start.HasProfile(0),"the first racing lap after a rolling start becomes a profile");
    }
    // Before any profile, two cars of one class 3 s apart keep 3 s all lap, also on a fast straight where lap
    // distance at the average pace says far more, and where iRacing's estimate runs past the class lap time.
    {
        PassingTimes early; auto one=Track(2); one.type="Race";
        auto uneven=[](double t) { const double lap=t/100+.12*std::sin(2*std::numbers::pi*t/100); return lap-std::floor(lap); };
        double worst=0;
        for(int n=0;n<=102*60;++n) {
            const double t=n/60.; auto f=At(t,{3,0}); f.state=4;
            for(int i=0;i<2;++i) { auto& c=f.cars[i]; const double u=t-(i?0:3); c.pct=uneven(u); c.estTime=(u-100*std::floor(u/100))*1.05; } // iRacing's estimate follows the pace, here 5 % past the class lap time.
            early.Update(one,f);
            if(t<5) continue;
            const auto rows=Relative(one,f,9,false,false,&early);
            const auto other=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
            worst=std::max(worst,std::abs(-other->gap-3));
        }
        if(!(!early.HasProfile(0) && worst<.02)) std::cerr<<"worst "<<worst<<"\n";
        Check(!early.HasProfile(0) && worst<.02,"without profiles the measured gap holds all lap");
    }
    // A lap with a 20 s stop on track, or one crawled at half pace, never becomes the profile: replayed later, the
    // wait would be timed as driving. 50 s of normal driving ahead stays 50 s.
    for(int trouble=0;trouble<2;++trouble) {
        PassingTimes kept; const auto two=Track(2);
        auto driven=[&](double t) {
            if(t<250) return t;
            if(trouble==0) return t<270?250.:t-20; // Stands 20 s, then drives on.
            return t<350?250+(t-250)/2:t-50; // Half pace for 100 s.
        };
        for(int n=0;n<=380*60;++n) { const double t=n/60.; auto f=At(t,{0,-30}); f.cars[0].pct=Position(driven(t)); kept.Update(two,f); }
        // At 380 s the other car is at 410 on its clock; ours at 360 after the stop, at 330 after the crawl.
        Close(kept.Reach(0,0,1),trouble==0?50:80,.5,trouble==0?"a stop on track keeps the lap from the profile":"a crawled lap keeps the lap from the profile");
    }
    // Another car on the same index starts without the old car's lap; another driver after a swap in the pits keeps it.
    {
        PassingTimes swap; auto two=Track(2);
        for(int n=0;n<=210*60;++n) swap.Update(two,At(n/60.,{0,2}));
        Check(swap.HasProfile(1) && swap.ClassProfile(1)>=0,"profiles after clean laps");
        auto inPits=two; inPits.drivers[1].name="Teammate";
        auto f=At(210.1,{0,2}); f.cars[1].pit=true; swap.Update(inPits,f);
        Check(swap.HasProfile(1),"a driver swap in the pits keeps the car's lap");
        auto other=inPits; other.drivers[1].carPath="othercar"; other.drivers[1].name="Somebody else";
        swap.Update(other,At(210.2,{0,2}));
        Check(!swap.HasProfile(1),"another car on the index starts afresh");
        other.drivers[0].classId=2; swap.Update(other,At(210.3,{0,2}));
        Check(!swap.HasProfile(0) && swap.ClassProfile(1)<0,"another class too, and no class profile is left from it");
    }
    // Without profiles and class estimates, a car behind is timed at its own known pace, not the reference car's.
    {
        auto two=Track(2); for(auto& d:two.drivers) d.estimatedLapTime=-1;
        auto f=At(50,{0,0}); f.cars[0].pct=.50; f.cars[1].pct=.49; f.cars[1].best=50; f.cars[0].estTime=f.cars[1].estTime=-1;
        const auto rows=Relative(two,f,9,false,false,nullptr);
        const auto behind=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
        Close(behind->gap,.5,.001,"1 % of a 50 s lap behind is 0.5 s, not 1 % of the reference car's 100 s");
    }
    // Pit exit: 10 s lost from where the car is now puts it where it was 10 s ago on its clean lap, behind it; the
    // SDK's time share is not a distance share. Without a lap and without cars around, lap distance at pace.
    {
        PassingTimes pits; auto one=Track(1);
        for(int n=0;n<=210*60;++n) pits.Update(one,At(n/60.,{0}));
        auto f=At(210,{0}); f.cars[0].estTime=80;
        const auto rejoin=PredictRejoin(one,f,10,100,&pits);
        Close(rejoin.pct,Position(200),.002,"rejoin where the car was 10 s ago on its own lap");
        f.cars[0].pct=.5; const auto plain=PredictRejoin(one,f,10,100,nullptr);
        Close(plain.pct,.4,.001,"without a lap: 10 % back at a 100 s pace, behind the car rather than ahead");
    }
    // Rain: clean laps 40 % slower from now on. The first is turned down like a damaged lap, the second as steady
    // as the first becomes the new pace.
    {
        PassingTimes rain; const auto one=Track(1);
        auto driven=[](double t) { return t<200?t:200+(t-200)/1.4; };
        for(int n=0;n<=200*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); rain.Update(one,f); }
        Close(rain.LapTime(0),100,.05,"dry pace");
        for(int n=200*60+1;n<=341*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); rain.Update(one,f); }
        Close(rain.LapTime(0),100,.05,"one wet lap is not yet a new pace");
        for(int n=341*60+1;n<=482*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); rain.Update(one,f); }
        Close(rain.LapTime(0),140,.1,"the second steady wet lap is the new pace");
    }
    // iRacing's estimates sampled against the order on track: 100 m ahead stays 100 m ahead, never a zero gap.
    {
        auto two=Track(2); auto g=At(50,{0,0}); g.cars[0].pct=.50; g.cars[1].pct=.52; g.cars[0].estTime=40; g.cars[1].estTime=39.9;
        const auto rows=Relative(two,g,9,false,false,nullptr);
        Close(std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;})->gap,-2,.001,"contradicting estimates fall back to the distance");
    }
    // Pit exit: an estimate of 0 mid-lap is no time; the car rejoins 10 % back, not next to a car at 90 %.
    {
        auto two=Track(2); auto g=At(50,{0,0}); g.cars[0].pct=.5; g.cars[0].estTime=0; g.cars[1].pct=.9; g.cars[1].estTime=90;
        Close(PredictRejoin(two,g,10,100,nullptr).pct,.4,.001,"an estimate of 0 mid-lap is not used");
    }
    // Pace window: the last 30 s of driving against the profile asked about.
    {
        // Rain from 250 s at 1.3 times the time, then dry again from 400 s: the ratio follows within 30 s each way.
        PassingTimes wet; const auto one=Track(1);
        auto driven=[](double t) { return t<250?t:(t<400?250+(t-250)/1.3:250+150/1.3+(t-400)); };
        auto run=[&](double until) { for(int n=0;n<=static_cast<int>(until*60);++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); wet.Update(one,f); } };
        run(249); Close(wet.PaceAgainst(0,0).ratio,1,.01,"dry pace against the dry lap");
        run(285); Close(wet.PaceAgainst(0,0).ratio,1.3,.02,"30 s into the rain: 1.3");
        run(435); Close(wet.PaceAgainst(0,0).ratio,1,.02,"30 s after it dried: 1.0 again");
    }
    {
        // A new wet reference while the ratio is active: against it about 1, against the dry one still 1.3, so the
        // gap stays near 130 % and never 169 %.
        PassingTimes swap; const auto two=Track(2);
        auto driven=[](double t) { return t<200?t:200+(t-200)/1.3; };
        for(int n=0;n<=500*60;++n) { const double t=n/60.; auto f=At(t,{0,0}); f.cars[0].pct=Position(driven(t)); swap.Update(two,f); }
        Close(swap.LapTime(0),130,.5,"the wet laps became the car's profile");
        Close(swap.PaceAgainst(0,0).ratio,1,.02,"no ratio against its own wet lap");
        Close(swap.PaceAgainst(1,0).ratio,1.3,.02,"1.3 against a dry lap of its class");
    }
    {
        // Standing ends the window; the pits too. Slow but moving driving does not.
        PassingTimes stop; const auto one=Track(1);
        auto driven=[](double t) { return t<250?t:(t<255?250.:t-5); };
        for(int n=0;n<=262*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); stop.Update(one,f); }
        Check(std::isnan(stop.PaceAgainst(0,0).raw),"5 s standing: no ratio until 5 s of driving again");
        for(int n=262*60+1;n<=290*60;++n) { const double t=n/60.; auto f=At(t,{0}); f.cars[0].pct=Position(driven(t)); stop.Update(one,f); }
        Close(stop.PaceAgainst(0,0).ratio,1,.01,"then the window fills again, without the standing time");
    }
    {
        // A 500 m track, 10 s a lap: 30 s span three lines. 1.2 times the time reads 1.2.
        // Against the lap of a car still at the old pace: its own laps soon become the slower ones themselves.
        PassingTimes oval; auto two=Track(2); two.trackMeters=500;
        auto at=[&](double t,double scale) { auto f=At(t,{0,0}); const double u=t<200?t*10:2000+(t-200)*10/scale; f.cars[0].pct=Position(u); f.cars[1].pct=Position(t*10); return f; };
        for(int n=0;n<=200*60;++n) oval.Update(two,at(n/60.,1.2));
        Close(oval.PaceAgainst(1,0).ratio,1,.01,"short laps: steady pace reads 1");
        for(int n=200*60+1;n<=240*60;++n) oval.Update(two,at(n/60.,1.2));
        Close(oval.PaceAgainst(1,0).ratio,1.2,.02,"short laps: 1.2 over several lines");
        Close(oval.PaceAgainst(0,0).ratio,1,.02,"and against its own new slower laps 1");
    }
    // A class lap stored from an earlier session is the start reference of the next one, after every lap of it.
    {
        PassingTimes earlier; auto two=Track(2);
        for(int n=0;n<=210*60;++n) earlier.Update(two,At(n/60.,{0,2}));
        double lap=0; std::vector<double> times; Check(earlier.ClassLap(1,lap,times) && times.size()==500,"a class lap to store");
        PassingTimes next; auto later=two; later.key="next session";
        next.Update(later,At(1000,{0,2}));
        Check(next.AddReference(1,lap,times) && !next.AddReference(1,lap,times),"one stored reference per class");
        times[5]=-1; Check(!next.AddReference(2,lap,times),"a broken file is not used");
        for(int n=1;n<=5*60;++n) next.Update(later,At(1000+n/60.,{0,2}));
        auto f=At(1005,{0,2}); const auto rows=Relative(later,f,9,false,false,&next);
        const auto behind=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
        Check(behind->gapSource==Row::GapSource::Stored,"before a lap of this session: the stored one");
        Close(behind->gap,2,.02,"which times the 2 s gap right away");
        for(int n=5*60+1;n<=220*60;++n) next.Update(later,At(1000+n/60.,{0,2}));
        f=At(1220,{0,2}); const auto own=Relative(later,f,9,false,false,&next);
        Check(std::find_if(own.begin(),own.end(),[](const Row& r){return r.driver.index==1;})->gapSource==Row::GapSource::Own,"then the car's own lap of this session");
    }
    // Standing start: 20 s on the grid just before the line, then getting up to speed. The first lap starts slow, so it
    // is no profile, neither the car's own nor its class's; the second is. A Formula Vee race showed the gaps across
    // the line jumping at the end of laps one and two when the first lap was taken.
    {
        PassingTimes grid; auto two=Track(2); two.type="Race";
        auto driven=[](double t,double slot) { return t<20?-slot:-slot+(t-20)-3+3*std::exp(-(t-20)/3); };
        auto frame=[&](double t) { auto f=At(t,{0,0}); f.state=4; f.cars[0].pct=Position(driven(t,2)); f.cars[1].pct=Position(driven(t,1)); return f; };
        for(int n=0;n<=128*60;++n) grid.Update(two,frame(n/60.));
        Check(!grid.HasProfile(0) && !grid.HasProfile(1) && grid.ClassProfile(1)<0,"the lap from the grid is no profile");
        for(int n=128*60+1;n<=230*60;++n) grid.Update(two,frame(n/60.));
        Check(grid.HasProfile(0) && grid.HasProfile(1),"the second lap is");
        Close(grid.LapTime(0),100,.05,"at the car's normal pace");
    }
    // A pit visit or an off-track spoils the lap: the earlier profile stays.
    {
        PassingTimes spoiled; const auto one=Track(1);
        for(int n=0;n<=290*60;++n) { auto f=At(n/60.,{0}); if(n/60.>250 && n/60.<251) f.cars[0].surface=0; spoiled.Update(one,f); }
        Check(spoiled.HasProfile(0),"earlier clean laps remain");
        Close(spoiled.Reach(0,0,0),0,1e-9,"a car to itself is zero");
    }
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
    const double distance=(f.cars[0].pct-f.cars[1].pct)*multi.drivers[1].estimatedLapTime; // In the car behind's class time.
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
// Debug sweep: two cars of one class 3 s apart; every frame's Relative and live Standings gap, worst deviation.
int PassingTimesSweep() {
    auto one=Track(2); one.type="Race"; PassingTimes passes; double worstRelative=0,worstStanding=0,atRelative=-1,atStanding=-1;
    for(int n=0;n<=400*60;++n) {
        const double t=n/60.; auto f=At(t,{3,0}); f.state=4; passes.Update(one,f);
        if(t<150) continue;
        const auto rows=Relative(one,f,9,false,false,&passes);
        const auto other=std::find_if(rows.begin(),rows.end(),[](const Row& r){return r.driver.index==1;});
        if(other!=rows.end() && std::abs(std::abs(other->gap)-3)>worstRelative) { worstRelative=std::abs(std::abs(other->gap)-3); atRelative=t; }
        StandingsOptions options; options.liveGaps=true; options.separateClasses=false; options.leaders=MaxCars;
        for(const auto& g:SelectStandings(one,f,options,&passes)) for(const auto& r:g.rows) if(r.driver.index==0 && std::isfinite(r.interval) && std::abs(r.interval-3)>worstStanding) { worstStanding=std::abs(r.interval-3); atStanding=t; }
    }
    std::cout<<"worst Relative deviation "<<worstRelative<<" s at "<<atRelative<<" (pct "<<Position(atRelative)<<"); worst Standings interval deviation "<<worstStanding<<" s at "<<atStanding<<" (pct "<<Position(atStanding)<<")\n";
    return 0;
}
// Live or TapeMate: every Relative row with its inputs, to find jumps: gap, gap without passes, measured,
// the car behind's profile time, both lap positions, lap counters and SDK estimates.
int PassingTimesTrace(int seconds,const char* path) {
    Telemetry reader; Frame frame; Session session; PassingTimes passes; std::ofstream out(path);
    out<<"time,reference,car,ahead,gap,source,pace,limited,estimate,measured,reach,classReach,pct,refPct,completed,refCompleted,est,refEst,surface,pit\n";
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds); double last=-1; int frames=0;
    while(std::chrono::steady_clock::now()<end) {
        if(!reader.Read(frame,session) || frame.time==last) { Sleep(1); continue; }
        last=frame.time; ++frames; passes.Update(session,frame);
        if(frames%3) continue;
        const int reference=ReferenceCar(frame); if(reference<0 || reference>=MaxCars) continue;
        const auto with=Relative(session,frame,15,false,false,&passes),without=Relative(session,frame,15,false,false,nullptr);
        for(const auto& row:with) {
            if(row.player) continue;
            const int car=row.driver.index; const bool ahead=row.gap<0;
            const int front=ahead?car:reference,back=ahead?reference:car;
            const auto other=std::find_if(without.begin(),without.end(),[&](const Row& r){return r.driver.index==car;});
            int backClass=-1; for(const auto& d:session.drivers) if(d.index==back) backClass=d.classId;
            const auto& c=frame.cars[car]; const auto& m=frame.cars[reference];
            static const char* sources[]={"none","own","class","stored","measured","capped","estimate"};
            out<<frame.time<<','<<reference<<','<<car<<','<<ahead<<','<<row.gap<<','<<sources[static_cast<int>(row.gapSource)]<<','<<row.gapPace<<','<<row.gapPaceLimited<<','<<(other==without.end()?NAN:other->gap)<<','<<passes.Gap(front,back)<<','
               <<passes.Reach(back,back,front)<<','<<passes.Reach(passes.ClassProfile(backClass),back,front)<<','<<c.pct<<','<<m.pct<<','<<c.completed<<','<<m.completed<<','
               <<c.estTime<<','<<m.estTime<<','<<c.surface<<','<<c.pit<<'\n';
        }
    }
    std::cout<<frames<<" frames\n"; return 0;
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
