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
void PitStopTests() {
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
