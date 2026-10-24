#include "Core.h"
#include <cmath>

using namespace rm;
void Require(bool, const char*);
void Near(double, double, const char*);

namespace {
constexpr double Lap=100, Pi=3.141592653589793;
// A lap that is slow around 75 % and fast around 25 %: reference time is not proportional to distance.
double Reference(double pct) { return Lap*(pct+.05*std::sin(2*Pi*pct)); }
Session TwoCars() {
    Session s; s.type="Race"; s.key="pit"; s.trackId=7; s.fuelCapacity=100;
    for(int i=0;i<2;++i) { Driver d; d.index=i; d.classId=1; d.estimatedLapTime=Lap; s.drivers.push_back(d); }
    return s;
}
Frame Base(double time) {
    Frame f; f.connected=f.onTrack=true; f.player=0; f.session=0; f.state=4; f.time=time;
    for(int i=0;i<2;++i) { auto& c=f.cars[i]; c.surface=3; c.completed=3; c.pct=.5; c.estTime=Reference(.5); c.best=Lap; }
    return f;
}
void Place(Car& c,int completed,double pct) { c.completed=completed; c.pct=pct; c.estTime=Reference(pct); }
}
namespace {
// Where the pit lane begins and ends, learned from cars driving in and out; and braking from the own car.
void PitLaneEndsTests() {
    Session s; s.key="ends"; s.trackId=353; s.trackMeters=2470;
    for(int i=0;i<2;++i) s.drivers.push_back({i,1,2000,0xffffff,false,false,"Driver","1","GT3"});
    Frame f; f.connected=f.onTrack=true; f.player=0; f.session=0; f.time=10;
    PitLaneEnds ends; ends.Seed(353,.9,.1);
    Require(std::abs(ends.Entry()-.9)<1e-9 && std::abs(ends.Exit()-.1)<1e-9,"saved pit lane ends are used until learned");
    auto drive=[&](int car,double pct,bool pit,int surface=3) { f.cars[car].pct=pct; f.cars[car].pit=pit; f.cars[car].surface=surface; };
    drive(0,.935,false); drive(1,.5,false); ends.Update(s,f);
    f.time+=.1; drive(0,.937,true); ends.Update(s,f);
    Require(std::abs(ends.Entry()-.936)<1e-9,"an entry is learned between the two frames");
    // Across start/finish to the exit; a car towed into its box teaches nothing.
    f.time+=.1; drive(0,.999,true); ends.Update(s,f); f.time+=.1; drive(0,.001,true); ends.Update(s,f);
    f.time+=.1; drive(0,.071,true); ends.Update(s,f); f.time+=.1; drive(0,.073,false); ends.Update(s,f);
    Require(std::abs(ends.Exit()-.072)<1e-9,"an exit is learned after start/finish");
    f.time+=.1; drive(1,.03,true,1); ends.Update(s,f);
    Require(std::abs(ends.Entry()-.936)<1e-9 && ends.Samples()==2,"a tow into the box is not an entry");
    // The median holds across start/finish too.
    PitLaneEnds wrap; Frame g=f; g.time=20;
    for(double at:{.998,.002,.0}) { g.cars[0].pct=at-.001+(at<.001?1:0); g.cars[0].pct-=std::floor(g.cars[0].pct); g.cars[0].pit=false; g.cars[0].surface=3; wrap.Update(s,g); g.time+=.1; g.cars[0].pct=at+.001-std::floor(at+.001); g.cars[0].pit=true; wrap.Update(s,g); g.time+=.1; g.cars[0].pit=false; g.cars[0].pct=.5; wrap.Update(s,g); g.time+=5; wrap.Update(s,g); g.time+=.1; }
    Require(wrap.Entry()<.003 || wrap.Entry()>.997,"the entry's median wraps around start/finish");
}
void BrakingTests() {
    Frame f; f.connected=f.onTrack=true; f.player=0; f.time=1; f.cars[0].surface=3; f.brake=0; f.cars[0].speed=60;
    BrakingRate rate; rate.Seed(9); rate.Update(f);
    Require(rate.Rate()==9,"the saved deceleration counts until a braking zone is seen");
    // Two seconds from 60 to 30 m/s with the pedal down: 15 m/s².
    for(int i=1;i<=20;++i) { f.time+=.1; f.brake=.9; f.cars[0].speed=60-1.5*i; rate.Update(f); }
    f.time+=.1; f.brake=0; rate.Update(f);
    Require(rate.Samples()==1 && std::abs(rate.Rate()-15)<.8,"a braking zone gives its average deceleration");
    // A light lift or a short tap is not a braking zone.
    for(int i=1;i<=3;++i) { f.time+=.1; f.brake=.9; f.cars[0].speed=40-i; rate.Update(f); }
    f.time+=.1; f.brake=0; rate.Update(f); Require(rate.Samples()==1,"a short tap is not counted");
    // Past the 15 kept zones the count of measured ones still grows, so the value is saved again at the session's end.
    for(int zone=0;zone<16;++zone) {
        for(int i=1;i<=20;++i) { f.time+=.1; f.brake=.9; f.cars[0].speed=60-1.5*i; rate.Update(f); }
        f.time+=.1; f.brake=0; rate.Update(f);
    }
    Require(rate.Samples()==15 && rate.Measured()==17,"the last 15 zones are kept, every measured one is counted");
    Require(std::abs(BrakingDistance(50,15.65,8,0)-(50*50-15.65*15.65)/16)<1e-9,"braking distance without reaction time");
    Require(std::abs(BrakingDistance(50,15.65,8)-(50*50-15.65*15.65)/16-15)<1e-9 && BrakingDistance(10,15,8)==0,"braking distance with reaction time, none below the target");
    const auto session=ParseSession("WeekendInfo:\n TrackLength: 2.3360 km\n TrackPitSpeedLimit: 56.33 kph\nDriverInfo:\n DriverCarIdx: 0\n DriverPitTrkPct: 0.032358\n Drivers:\n - CarIdx: 0\n   UserName: You\n",0);
    Require(std::abs(session.pitStallPct-.032358)<1e-9 && std::abs(session.pitSpeedLimit-56.33/3.6)<1e-9,"own pit box and pit speed limit from the session info");
}
}
void PitStopTests() {
    PitLaneEndsTests(); BrakingTests();
    {
        // Car 1 enters at 90 %, stands 25 s, leaves at 5 % of the next lap; the lane itself costs 22 s.
        const auto s=TwoCars(); PitLaneLoss lane; double t=0;
        auto tick=[&](auto setup) { Frame f=Base(t); setup(f.cars[1]); lane.Update(s,f); t+=.5; };
        for(int i=0;i<4;++i) tick([&](Car& c){ Place(c,3,.80+i*.02); });
        const double stationary=25, laneLoss=22, through=Reference(.05)+Lap-Reference(.90);
        const double entry=t, exit=entry+through+stationary+laneLoss;
        while(t<exit-1e-9) tick([&](Car& c){ c.pit=true; const double p=.90+.15*(t-entry)/(exit-entry); Place(c,p>=1?4:3,std::fmod(p,1.0)); });
        t=exit; tick([&](Car& c){ Place(c,4,.05); c.lastPitStationary=stationary; });
        Require(lane.Samples()==1,"an observed pit visit is learned");
        Near(lane.Loss(),laneLoss,"lane loss uses the reference time of the bypassed stretch");
        Require(std::abs(through-.15*Lap)>4,"this lap is uneven enough that a constant speed would be wrong");
    }
    {
        // A car first seen in the pit lane, a visit across lost telemetry and replays are not learned.
        const auto s=TwoCars(); PitLaneLoss lane; lane.Seed(7,30);
        Frame f=Base(0); f.cars[1].pit=true; lane.Update(s,f);
        f=Base(.5); lane.Update(s,f); Require(lane.Samples()==0 && lane.Loss()==30,"no entry seen, the saved value stays");
        f=Base(1); f.replay=true; f.onTrack=false; f.cars[1].pit=true; lane.Update(s,f);
        f=Base(1.5); lane.Update(s,f); Require(lane.Samples()==0,"replays are not learned");
        Session other=s; other.trackId=8; f=Base(2); lane.Update(other,f); Require(lane.Loss()<0,"another track starts unknown");
    }
    {
        // The pit menu: only what fits in the tank; default rules in sequence, IMSA rules at once.
        Session s=TwoCars(); Frame f=Base(0); f.fuelKnown=true; f.fuel=70; f.pitSvFuel=50;
        f.pitSvFlags=PitServiceFlag::LeftFront|PitServiceFlag::RightFront|PitServiceFlag::LeftRear|PitServiceFlag::RightRear|PitServiceFlag::Fuel; f.pitRepair=5;
        const auto service=PlannedService(s,f);
        Require(service.tires==4 && service.repair==5,"four tires and the repair");
        Near(service.fuel,30,"fuel limited to the room in the tank");
        Near(ServiceTime(service,2.5,20,false),32,"default rules: 12 s fuel, then 20 s tires; the repair fits in the tire change");
        Near(ServiceTime(service,2.5,20,true),20,"IMSA rules: everything at once");
        Near(ServiceTime({60,2,0},2.5,20,true),24,"fuel takes longer than a pair of tires");
        Near(ServiceTime({0,1,0},2.5,20,false),10,"one tire takes as long as a pair");
        Near(ServiceTime({0,3,0},2.5,20,false),20,"three tires take as long as four");
        Require(ServiceTime(service,2.5,-1,false)<0,"unknown tire time leaves the service unknown");
        f.pitSvFlags=PitServiceFlag::Fuel; f.pitRepair=0; Require(ServiceTime(PlannedService(s,f),-1,-1,false)<0,"unknown fuel rate");
        f.pitSvFlags=0; Near(ServiceTime(PlannedService(s,f),-1,-1,false),0,"a drive-through needs no service");
        // Rule set from the session and the tire times of iRacing's notes.
        Session rules=s; Driver gt3; gt3.className="GT3 Class"; Driver gt4; gt4.className="GT4"; Driver other; other.className="Mazda MX-5 Cup";
        rules.ruleSet="None"; Require(!SimultaneousService(rules) && DefaultTireChange(rules,gt3)==20 && DefaultTireChange(rules,gt4)==35 && DefaultTireChange(rules,other)<0,"default rules");
        rules.ruleSet="IMSA"; Require(SimultaneousService(rules),"IMSA rules service at once");
        rules.ruleSet="DTM"; Require(!SimultaneousService(rules) && DefaultTireChange(rules,gt3)==7,"DTM tire change");
        Require(ParseSession("WeekendInfo:\n TrackDisplayName: Spa\n DCRuleSet: IMSA\n",0).ruleSet=="IMSA","rule set read from the session");
    }
    {
        // Filling in the stall: 2.5 l/s from the first rise on.
        RefuelRate rate; Frame f=Base(0); f.inStall=true; f.fuelKnown=true; f.fuel=10;
        for(int i=0;i<10;++i) { f.time=i*.1; rate.Update(f); }
        for(int i=0;i<=40;++i) { f.time=1+i*.1; f.fuel=10+2.5*i*.1; rate.Update(f); }
        Near(rate.Rate(),2.5,"refuel rate");
        f.inStall=false; f.time=6; rate.Update(f); Near(rate.Rate(),2.5,"kept after the stop");
    }
    {
        // Own car at 50 %, 20 s lost: car 1 just behind passes, car 2 behind the rejoin point stays behind.
        Session s=TwoCars(); for(int i=2;i<4;++i) { Driver d; d.index=i; d.classId=i==3?2:1; d.estimatedLapTime=Lap; s.drivers.push_back(d); }
        Frame f=Base(0); auto set=[&](int i,double pct) { auto& c=f.cars[i]; c.surface=3; c.estTime=pct*Lap; c.pct=pct; c.completed=10; };
        set(0,.50); set(1,.35); set(2,.25); set(3,.60);
        const auto r=PredictRejoin(s,f,20,Lap);
        Require(r.valid && r.position==3 && r.classPosition==2,"one car passes during the stop, one stays behind; other class counted overall");
        Require(r.ahead==1 && r.behind==2,"nearest cars at the rejoin point");
        Require(r.classAhead==1 && r.classBehind==2 && r.overallAhead==1 && r.overallBehind==2,"neighbours in the race after the stop");
        Near(r.classAheadGap,5,"5 s to the class car ahead"); Near(r.classBehindGap,5,"5 s to the class car behind");
        Near(r.aheadGap,5,"5 s to the car ahead"); Near(r.behindGap,5,"5 s to the car behind"); Near(r.pct,.30,"rejoins at 30 %");
        f.replay=true; f.onTrack=false; Require(!PredictRejoin(s,f,20,Lap).valid,"no estimate while old footage plays");
    }
}
