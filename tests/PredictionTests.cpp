#include "Core.h"
#include <cmath>
#include <algorithm>
using namespace rm;
void Require(bool,const char*);
void Near(double,double,const char*);
namespace {
Session Race() {
    Session s; s.type="Race"; s.number=1; s.key="prediction-test";
    s.drivers.push_back({0,1,2000,0xffcc44,false,false,"Leader","12","GT3"});
    s.drivers.push_back({1,2,2000,0x55aaff,false,false,"Own car","24","GT4"});
    return s;
}
void Progress(Car& car,double progress) {
    car.completed=static_cast<int>(std::floor(progress)); car.lap=car.completed+1; car.pct=progress-car.completed;
    car.estTime=car.pct*100; car.best=car.last=100; car.surface=3;
}
Frame RacingFrame() {
    Frame f; f.connected=f.onTrack=f.fuelKnown=f.timeLeftKnown=true; f.session=1; f.player=1; f.state=4;
    f.time=1200; f.timeLeft=1103; f.fuel=20; f.lastLap=f.bestLap=100; f.lapsLeft=32767;
    Progress(f.cars[0],10.98); f.cars[0].position=1;
    Progress(f.cars[1],8.5); f.cars[1].position=2;
    f.lapCompleted=8; f.lapPct=.5; return f;
}
void PitHistoryTests() {
    Driver gt; gt.carName="Porsche 911 GT3 R (992)";
    Require(TireLabel(gt,0)=="Dry" && TireLabel(gt,1)=="Wet" && TireLabel(gt,-1).empty(),"GT3 tire labels");
    gt.carName="Porsche 963 GTP"; Require(TireLabel(gt,0)=="Dry" && TireLabel(gt,1)=="Wet","GTP tire labels");
    Driver indy; indy.carName="Dallara IR18";
    Require(TireLabel(indy,0)=="Primary" && TireLabel(indy,1)=="Alternate" && TireLabel(indy,2)=="Wet","IndyCar compounds distinguish dry and wet");
    Driver formula; formula.carName="Dallara iR-01";
    Require(TireLabel(formula,0)=="Soft" && TireLabel(formula,1)=="Medium" && TireLabel(formula,2)=="Hard" && TireLabel(formula,3).empty(),"three dry compounds do not become wet");
    formula.carName="Mercedes-AMG W13 E Performance"; Require(TireLabel(formula,1)=="Medium","W13 compound labels");

    auto s=Race(); auto f=RacingFrame(); PitHistory history;
    s.gridPositions[0]=4; s.gridPositions[1]=7;
    Row row; row.driver=s.drivers[1]; row.car.position=5; row.car.classPosition=1;
    Require(PositionsGained(s,row,false)==2,"overall gained positions use starting grid");
    Require(PositionsGained(s,row,true)==0,"class gained excludes other classes");
    s.drivers.push_back({2,2}); s.gridPositions[2]=3;
    Require(PositionsGained(s,row,true)==1,"class starting position counts same class only");
    row.car.position=9; Require(PositionsGained(s,row,false)==-2,"lost positions have negative sign");
    s.type="Practice"; Require(PositionsGained(s,row,false)==-MaxCars,"practice has no gained positions"); s.type="Race";
    history.Update(s,f); f.time+=.1; f.cars[0].pit=true; f.cars[0].surface=1; history.Update(s,f);
    Require(f.cars[0].towing,"direct track to stall return detected as tow");
    f.time+=1; history.Update(s,f); Require(f.cars[0].towing,"inferred tow persists in stall");
    f.cars[0].towingKnown=true; f.cars[0].towing=false; f.time+=1; history.Update(s,f);
    Require(!f.cars[0].towing,"direct SDK tow state overrides inferred state");
    f.cars[0].towingKnown=false; f.cars[0].surface=2; f.time+=.1; history.Update(s,f); Require(!f.cars[0].towing,"leaving stall clears tow");
    f.time+=.1; f.cars[0].surface=1; history.Update(s,f); Require(!f.cars[0].towing,"normal lane to stall visit is not tow");
    history.Reset(); history.Update(s,f); Require(!f.cars[0].towing,"joining at stall does not invent tow");
    f=RacingFrame(); history.Reset();
    history.Update(s,f); f.time+=1; f.cars[0].pit=true; f.cars[0].lap=12; history.Update(s,f);
    f.time+=4; history.Update(s,f); f.time+=1; f.cars[0].pit=false; history.Update(s,f);
    Require(f.cars[0].lastPitLap==12,"pit entry lap retained"); Near(f.cars[0].lastPitDuration,5,"pit entry to exit duration");
    Require(f.cars[0].outLap,"observed pit exit starts outlap");
    f.time+=1; history.Update(s,f); Require(f.cars[0].outLap,"outlap persists between crossings");
    f.time+=1; f.cars[0].lap=13; history.Update(s,f); Require(!f.cars[0].outLap,"finish crossing ends outlap");
    history.Reset(); f.cars[0].pit=true; history.Update(s,f); f.time+=1; f.cars[0].pit=false; history.Update(s,f);
    Require(f.cars[0].lastPitLap<0,"joining during pit visit does not invent duration");
    Require(f.cars[0].outLap,"outlap needs observed exit but not full pit entry history");
    f.time+=1; f.cars[0].pit=true; history.Update(s,f); f.time+=10; f.cars[0].pit=false; history.Update(s,f);
    Require(f.cars[0].lastPitLap<0 && !f.cars[0].outLap,"telemetry gaps discard partial visits and unknown outlap");
    f.time-=100; history.Update(s,f); Require(f.cars[0].lastPitDuration<0,"replay rewind clears stop history");
    // Separate time stopped inside the stall from transit, creeping and waiting elsewhere.
    history.Reset(); f=RacingFrame(); s.trackMeters=1000; auto& c=f.cars[0]; c.pct=.5;
    history.Update(s,f); f.time+=1; c.pit=true; c.pct=.6; history.Update(s,f);
    f.time+=.5; c.surface=1; c.pct=.601; history.Update(s,f);
    f.time+=.5; history.Update(s,f); Near(c.pitStationary,.5,"live stationary time starts after sustained stillness");
    history.Update(s,f); Near(c.pitStationary,.5,"duplicate snapshots do not add stationary time");
    f.time+=1; history.Update(s,f); Near(c.pitStationary,1.5,"stationary time accumulates at stall");
    f.time+=.5; c.pct=.602; history.Update(s,f);
    f.time+=.5; c.surface=3; c.pct=.65; history.Update(s,f);
    f.time+=.5; history.Update(s,f); // stopped in the lane, outside the stall
    f.time+=.5; c.pit=false; history.Update(s,f);
    Near(c.lastPitStationary,1.5,"moving in stall and lane waiting do not count as service standstill");
    Near(c.lastPitDuration,4,"lane duration remains independent of stationary time");
    f.time+=1; c.pit=true; history.Update(s,f); f.time+=.5; c.surface=1; history.Update(s,f);
    f.time+=2; history.Update(s,f); f.time+=.5; c.pit=false; c.surface=3; history.Update(s,f);
    Require(c.lastPitStationary<0,"sample gaps invalidate stationary result");
    history.Reset(); c.pit=true; c.surface=1; history.Update(s,f);
    f.time+=1; history.Update(s,f); f.time+=1; c.pit=false; c.surface=3; history.Update(s,f);
    Require(c.lastPitStationary<0,"joining mid-stop does not invent stationary duration");
    history.Reset(); s.trackMeters=-1; c.pit=false; history.Update(s,f);
    f.time+=.5; c.pit=true; history.Update(s,f); f.time+=.5; c.surface=1; history.Update(s,f);
    f.time+=.5; history.Update(s,f); f.time+=.5; c.pit=false; c.surface=3; history.Update(s,f);
    Require(c.lastPitStationary<0,"missing position speed information is unavailable, not zero");
    history.Reset(); c.speed=0; c.pit=false; history.Update(s,f);
    f.time+=.5; c.pit=true; history.Update(s,f); f.time+=.5; c.surface=1; history.Update(s,f);
    f.time+=1; history.Update(s,f); f.time+=.5; c.speed=2; history.Update(s,f);
    f.time+=.5; c.pit=false; c.surface=3; history.Update(s,f);
    Near(c.lastPitStationary,1,"direct own-car speed works without track length");
    const auto parsed=ParseSession("DriverInfo:\n Drivers:\n - CarIdx: 0\n   UserName: Test\n   LicString: A 3.45\n   LicColor: 0x123456\n   IRating: 2345\n",0);
    Require(parsed.drivers.size()==1 && parsed.drivers[0].license=="A 3.45" && parsed.drivers[0].licenseColor==0x123456 && parsed.drivers[0].rating==2345,"license color and rating parsed");
}
void PitWindowTests() {
    auto s=Race(); auto f=RacingFrame(); s.fuelCapacity=30; f.lapCompleted=7; f.lapPct=.45; f.fuel=27.4; f.pit=false;
    auto window=CalculatePitWindow(s,f,12.55,3.12);
    Require(window.status==PitWindowStatus::Open && window.currentLap==8 && window.firstLap==11 && window.lastLap==17 && window.finishLap==20,"one final tank opens when remaining consumption fits capacity and closes before empty");
    const auto later=f; f.lapCompleted=9; f.fuel-=2*3.12; window=CalculatePitWindow(s,f,10.55,3.12);
    Require(window.firstLap==11 && window.lastLap==17,"window stays stable as laps and fuel advance together");
    f=later; f.fuel=50; Require(CalculatePitWindow(s,f,12.55,3.12).status==PitWindowStatus::NoStopNeeded,"no green window when current fuel already reaches finish");
    f.fuel=2; Require(CalculatePitWindow(s,f,30,3.12).stops==4,"multi-stop race provides next-stop window instead of suppressing it");
    f.lapCompleted=10; f.lapPct=0; f.fuel=9; s.fuelCapacity=30;
    window=CalculatePitWindow(s,f,10,3); Require(window.firstLap==11 && window.lastLap==13,"exact empty crossing is excluded and current lap number is completed plus one");
    f.replay=true; Require(CalculatePitWindow(s,f,10,3).status==PitWindowStatus::Unavailable,"replay pit window unavailable"); f.replay=false;
    s.fuelCapacity=-1; Require(CalculatePitWindow(s,f,10,3).status==PitWindowStatus::Unavailable,"missing tank capacity does not invent a window");
    s.fuelCapacity=30; f.pit=true; Require(CalculatePitWindow(s,f,10,3).status==PitWindowStatus::Unavailable,"pit and refueling suppress old window"); f.pit=false;
    Require(CalculatePitWindow(s,f,-1,3).status==PitWindowStatus::Unavailable && CalculatePitWindow(s,f,10,0).status==PitWindowStatus::Unavailable,"unknown forecast or consumption unavailable");
    s.type="Practice"; Require(CalculatePitWindow(s,f,10,3).status==PitWindowStatus::Unavailable,"only race finish has final pit window");
    s.type="Race"; s.fuelCapacity=100; f=RacingFrame(); f.lapCompleted=0; f.lapPct=0; f.fuel=100;
    window=CalculatePitWindow(s,f,75,5,120);
    Require(window.stops==3 && window.firstLap==17 && window.lastLap==20 && !window.openNow,"three-stop race waits until early fill can preserve three stops");
    Near(window.timeEmpty,2400,"time empty uses average burn and own lap pace"); Near(window.fuelAtEnd,-275,"fuel at end retains signed shortfall");
    f.lapCompleted=5; f.fuel=75; window=CalculatePitWindow(s,f,70,5,120);
    Require(window.stops==3 && !window.openNow,"early pit window remains closed despite needing fuel later");
    f.fuel=100; Require(CalculatePitWindow(s,f,70,5).stops==3,"an early top-up would still leave three more stops and thus cost an extra stop");
    f.lapCompleted=17; f.fuel=15; window=CalculatePitWindow(s,f,58,5,120);
    Require(window.stops==3 && window.openNow,"scheduled first-stop window is open");
    f.fuel=100; window=CalculatePitWindow(s,f,58,5,120); Require(window.stops==2 && !window.openNow,"after first fill plan recomputes two stops and next window");
    f.lapCompleted=36; f.fuel=5; window=CalculatePitWindow(s,f,39,5,120); Require(window.stops==2 && window.openNow,"second-stop window preserves two remaining stops");
    f.fuel=100; Require(CalculatePitWindow(s,f,39,5).stops==1,"second fill leaves one final stop");
    f.lapCompleted=55; f.fuel=5; window=CalculatePitWindow(s,f,20,5); Require(window.stops==1 && window.openNow,"final stop window opens");
    f.fuel=100; window=CalculatePitWindow(s,f,20,5); Require(window.stops==0 && window.fuelAtEnd==0,"final fill reaches finish with zero planned surplus");
    f.fuel=110; Near(CalculatePitWindow(s,f,20,5).fuelAtEnd,10,"positive finish fuel remains signed surplus");
    s.fuelCapacity=-1; window=CalculatePitWindow(s,f,20,5,120); Require(window.stops<0 && window.timeEmpty>0 && window.fuelAtEnd==10,"missing capacity does not hide independently known time and finish fuel");
    auto restricted=ParseSession("DriverInfo:\n DriverCarFuelMaxLtr: 100\n DriverCarMaxFuelPct: 0.3\n Drivers:\n - CarIdx: 0\n   CarClassMaxFuelPct: 0.5\n",0);
    Near(restricted.fuelCapacity,30,"capacity respects series restriction from own-driver metadata");
    Require(ParseSession("DriverInfo:\n DriverCarFuelMaxLtr: 100\n",0).fuelCapacity<0,"missing restriction remains unknown");
}
void TimedTests() {
    PitHistoryTests();
    auto s=Race(); auto f=RacingFrame(); RacePredictor calculator; calculator.Update(s,f);
    PredictionOptions options; auto p=calculator.Predict(s,f,options);
    Require(p.status==PredictionStatus::Racing && p.noStop.valid && p.withStops.valid,"timed race forecast available before fuel learning");
    Near(p.noStop.leaderTotalDistance,22.01,"leader total at expiry retains fractional laps");
    Near(p.noStop.leaderDistance,12.02,"leader remaining finish distance includes partial lap");
    Require(p.noStop.leaderCrossings==13 && p.withStops.leaderCrossings==12,"leader integer laps are displayed independently of fractional distance");
    Near(p.noStop.finalLapTimeLeft,1,"clock remaining when leader starts final lap");
    Near(p.noStop.leaderFinishIn,1202,"leader crossing after expiry, no blanket extra lap");
    auto shortRace=f; shortRace.timeLeft=49;
    Near(calculator.Predict(s,shortRace,options).noStop.finalLapTimeLeft,47,"short race clock at upcoming final lap start");
    shortRace.timeLeft=0;
    Near(calculator.Predict(s,shortRace,options).noStop.finalLapTimeLeft,98,"clock at already started final lap remains anchored to lap start");
    Near(p.noStop.ownFinishIn,1250,"own crossing after leader");
    Near(p.noStop.ownDistance,12.5,"own fuel distance, not leader distance");
    Require(p.noStop.ownCrossings==13,"own integer crossings are separate from distance");
    Require(p.whatIf && p.projectedLeaderStops==1 && !p.measuredPitLoss && p.stopConfidence==Confidence::Low,"unknown future stop explicitly hypothetical");
    Near(p.withStops.leaderDistance,11.02,"pit delay can remove a leader lap");
    Near(p.withStops.leaderFinishIn,1147,"pit scenario leader finish time");
    Require(p.withStops.ownCrossings==12,"pit scenario can remove one own lap");
    Near(p.withStops.ownDistance,11.5,"pit scenario own fuel distance");
    options.stops=StopMode::None; p=calculator.Predict(s,f,options);
    Near(p.withStops.ownDistance,p.noStop.ownDistance,"no further stop mode matches baseline");
    Require(!p.whatIf && p.projectedLeaderStops==0,"no-stop mode does not manufacture a stop");
    options.stops=StopMode::One; options.ownPitLoss=35; p=calculator.Predict(s,f,options);
    Near(p.noStop.ownFinishIn,1285,"planned own pit delay included");
    options.stops=StopMode::Automatic; options.leaderStintLaps=20;
    Require(calculator.Predict(s,f,options).whatIf,"stint override cannot invent a missing last-stop time");
    f.player=0; f.lapCompleted=10; f.lapPct=.98; options.ownPitLoss=0;
    p=calculator.Predict(s,f,options);
    Near(p.noStop.ownFinishIn,p.noStop.leaderFinishIn,"player is leader does not add another lap at simultaneous finish");
    Require(p.noStop.ownCrossings==13,"leader's own crossing count");
    f=RacingFrame(); f.cars[1].pct=.99; f.cars[1].estTime=99;
    p=calculator.Predict(s,f,options);
    Require(p.noStop.minCrossings<=p.noStop.ownCrossings && p.noStop.maxCrossings>=p.noStop.ownCrossings && p.noStop.minCrossings<p.noStop.maxCrossings,"pace sensitivity exposes uncertain extra lap");
    Require(p.noStop.confidence==Confidence::Low,"crossing ambiguity lowers confidence");
}
void LeaderChangeTests() {
    auto s=Race(); auto f=RacingFrame();
    s.drivers.push_back({2,1,2100,0xffcc44,false,false,"Challenger","13","GT3"});
    Progress(f.cars[2],10.7); f.cars[2].position=2; f.cars[1].position=3;
    RacePredictor calculator; calculator.Update(s,f); auto p=calculator.Predict(s,f,{});
    Require(p.noStop.winner==0 && p.withStops.winner==2,"leader pit stop can change race winner");
    Near(p.withStops.raceFinishIn,1130,"challenger can trigger checkered before original leader");
    Near(p.withStops.leaderFinishIn,1147,"original leader crossing differs from start of checkered");
    Near(p.withStops.ownFinishIn,1150,"own finish uses actual predicted winner's flag time");
    f.cars[2].best=f.cars[2].last=90; f.cars[2].estTime=f.cars[2].pct*90;
    p=calculator.Predict(s,f,{}); Require(p.noStop.winner==2,"pace-based leadership change without a pit stop");
}
void CrossingInvariantTests() {
    for(double leaderPct:{0.0,.1,.5,.98}) for(double ownPct:{0.0,.2,.7,.99}) for(double expiry:{0.0,49.0,100.0,250.0,1103.0}) {
        auto s=Race(); auto f=RacingFrame(); f.timeLeft=expiry;
        Progress(f.cars[0],10+leaderPct); Progress(f.cars[1],8+ownPct);
        f.lastLap=f.bestLap=f.cars[1].last=f.cars[1].best=121;
        f.cars[1].estTime=ownPct*121;
        RacePredictor predictor; predictor.Update(s,f); const auto forecast=predictor.Predict(s,f,{});
        for(const auto& scene:{forecast.noStop,forecast.withStops}) {
            Require(scene.valid && scene.ownFinishIn>=scene.raceFinishIn-1e-6,"own crossing never precedes predicted checkered");
            Near(scene.ownDistance,scene.ownCrossings-ownPct,"fuel distance equals partial lap plus full laps");
            if(scene.ownCrossings>1) Require(scene.ownFinishIn-121<scene.raceFinishIn+1e-6,"own finish uses first eligible crossing, not an extra lap");
            Require(scene.raceFinishIn>expiry-1e-6,"timed race flag cannot precede timer expiry");
        }
    }
}
void LimitAndStateTests() {
    auto s=Race(); auto f=RacingFrame(); RacePredictor calculator; calculator.Update(s,f);
    s.totalLaps=20;
    auto p=calculator.Predict(s,f,{});
    Near(p.noStop.leaderFinishIn,902,"combined limits use earlier lap limit");
    Near(p.noStop.leaderTotalDistance,20,"earlier lap limit caps total distance instead of driving past race finish");
    Near(p.noStop.ownDistance,9.5,"lapped driver's distance follows leader lap limit");
    f.timeLeft=-1; f.timeLeftKnown=false;
    Near(calculator.Predict(s,f,{}).noStop.leaderFinishIn,902,"pure lap race supported");
    s.totalLaps=-1; Require(!calculator.Predict(s,f,{}).noStop.valid,"unknown race limits are not guessed");
    f.timeLeftKnown=true;
    Near(calculator.Predict(s,f,{}).noStop.leaderFinishIn,2,"expired timer still waits for leader line crossing");
    f=RacingFrame(); f.state=3; Require(calculator.Predict(s,f,{}).status==PredictionStatus::Gridding,"no pre-race forecast");
    f.state=4; f.replay=true; Require(calculator.Predict(s,f,{}).status==PredictionStatus::Replay,"no replay race prediction");
    f.replay=false; f.cars[0].surface=-1; Require(calculator.Predict(s,f,{}).status==PredictionStatus::MissingLeader,"missing leader position cannot silently use another car");
    f=RacingFrame(); f.cars[1].surface=-1;
    p=calculator.Predict(s,f,{}); Require(p.noStop.valid && p.noStop.ownDistance<0 && p.status==PredictionStatus::MissingPace,"leader forecast remains visible when own car unavailable");
    f=RacingFrame(); f.state=5; calculator.Update(s,f);
    p=calculator.Predict(s,f,{}); Require(p.noStop.ownCrossings==1 && p.noStop.raceFinishIn==0,"checkered does not keep forecasting extra race laps");
    for(int t=1;t<=51;++t) { f.time=1200+t; Progress(f.cars[1],8.5+t*.01); calculator.Update(s,f); }
    Require(calculator.Predict(s,f,{}).noStop.ownCrossings==0,"own finish is latched after post-checkered crossing");
    f.state=6; p=calculator.Predict(s,f,{}); Require(p.status==PredictionStatus::Finished && p.noStop.ownDistance==0,"cooldown needs no fuel to finish");
    f.cars[0].surface=-1; f.cars[0].position=0;
    Require(calculator.Predict(s,f,{}).noStop.ownDistance==0,"finished state remains authoritative after leader disconnects");
    s.type="Practice"; Require(calculator.Predict(s,f,{}).status==PredictionStatus::NotRace,"practice does not claim a race finish");
}
void HistoryTests() {
    auto s=Race(); auto f=RacingFrame(); RacePredictor calculator;
    for(int t=0;t<=1000;++t) {
        f.time=t; f.timeLeft=2000-t;
        double progress;
        bool pit=false;
        if(t<400) progress=.2+t*.01;
        else if(t<440) { progress=4.2+(t-400)*.0025; pit=true; }
        else if(t<900) progress=4.3+(t-440)*.01;
        else if(t<930) { progress=8.9+(t-900)/300.0; pit=true; }
        else progress=9+(t-930)*.01;
        Progress(f.cars[0],progress); f.cars[0].pit=pit;
        Progress(f.cars[1],std::max(.05,progress-.1)); f.lapCompleted=f.cars[1].completed; f.lapPct=f.cars[1].pct;
        calculator.Update(s,f);
    }
    auto p=calculator.Predict(s,f,{});
    Require(p.observedLeaderStops==2 && p.measuredPitLoss,"observed pit entry and exit learn stops");
    Near(p.leaderPitLoss,25,"pit loss excludes normal travel through pit segment");
    Near(p.leaderStint,4.4,"complete observed stints used instead of late-join partial stint");
    Near(p.leaderLastPitLaps,.7,"laps since leader last pit exit");
    Near(p.leaderLastPitSeconds,70,"seconds since leader last pit exit");
    Near(p.leaderPace,100,"pit laps excluded from learned race pace");
    Require(!p.whatIf && p.stopConfidence==Confidence::Medium && p.projectedLeaderStops>0,"automatic stop forecast uses observed stint evidence");
    f.timeLeft=30000; Require(calculator.Predict(s,f,{}).projectedLeaderStops>16,"long races project repeated stops without a short fixed stop-list limit");
    calculator.Update(s,f); Require(calculator.Predict(s,f,{}).observedLeaderStops==2,"unchanged telemetry does not duplicate stops");
    f.time=0; calculator.Update(s,f); Require(calculator.Predict(s,f,{}).observedLeaderStops==0,"rewind resets learned pit history");
    s.key="another-server"; f.time=1001; calculator.Update(s,f);
    Require(calculator.Predict(s,f,{}).observedLeaderStops==0,"server change resets learned pit history");
    // Uniform full laps establish high pace confidence; yellow laps cannot establish it.
    calculator.Reset(); s=Race(); f=RacingFrame();
    for(int t=0;t<=1400;++t) {
        f.time=t; f.timeLeft=500;
        Progress(f.cars[0],.2+t*.01); Progress(f.cars[1],.1+t*.01);
        calculator.Update(s,f);
    }
    PredictionOptions options; options.stops=StopMode::None;
    p=calculator.Predict(s,f,options);
    Require(p.noStop.confidence==Confidence::High,"stable observed ten-lap pace supports high conditional confidence");
    f.flags=0x4000; Require(calculator.Predict(s,f,options).noStop.confidence==Confidence::Low,"caution lowers race confidence");
    f.flags=0; s.drivers.push_back({2,1,2200,0xffcc44,false,false,"Unknown pace","99","GT3"});
    f.cars[2].position=3; f.cars[2].surface=3;
    Require(calculator.Predict(s,f,options).noStop.confidence==Confidence::Low,"missing front-runner pace prevents high confidence");
}
void MulticlassEnduranceTests() {
    auto s=Race(); s.fuelCapacity=100;
    auto f=RacingFrame(); f.timeLeft=3600; f.pit=false;
    Progress(f.cars[0],100.25); f.cars[0].best=f.cars[0].last=60; f.cars[0].estTime=15;
    Progress(f.cars[1],50.5); f.cars[1].best=f.cars[1].last=120; f.cars[1].estTime=60;
    f.lastLap=f.bestLap=120; f.lapCompleted=50; f.lapPct=.5;
    // A faster car in our own class must not replace the overall race leader.
    s.drivers.push_back({2,2,2200,0x55aaff,false,false,"Class leader","25","GT4"});
    Progress(f.cars[2],51.1); f.cars[2].position=2; f.cars[2].best=f.cars[2].last=118; f.cars[2].estTime=11.8;
    f.cars[1].position=3;
    PredictionOptions options; options.stops=StopMode::None;
    RacePredictor predictor; predictor.Update(s,f);
    auto prediction=predictor.Predict(s,f,options);
    Require(prediction.leader==0 && prediction.noStop.winner==0,"multiclass uses overall leader, not own class leader");
    Near(prediction.noStop.raceFinishIn,3645,"overall leader triggers flag after timed expiry");
    Near(prediction.noStop.leaderTotalDistance,160.25,"leader total is fractional progress at timer expiry including completed laps");
    Near(prediction.noStop.leaderDistance,60.75,"fast class remaining distance");
    Near(prediction.noStop.ownDistance,30.5,"slow class needs half the distance plus its eligible finish crossing");
    Near(prediction.noStop.ownFinishIn,3660,"lapped slow class finishes at first own line crossing after flag");
    Require(prediction.noStop.leaderCrossings==61 && prediction.noStop.ownCrossings==31,"thirty further laps lost over one remaining hour");
    // Learn fuel independently, then feed the slow-class forecast into the entire fuel plan.
    FuelCalculator fuel; auto learning=f; learning.time=0; learning.lapCompleted=0; learning.fuel=100;
    fuel.Update(s,learning);
    for(int lap=1;lap<=11;++lap) { learning.time=lap*120; learning.lapCompleted=lap; learning.fuel-=3; fuel.Update(s,learning); }
    f.fuel=20; const auto plan=fuel.Plan(f,0,false,0,&prediction);
    Near(plan.lapsToFinish,30.5,"fuel plan preserves slow class distance despite being many laps behind");
    Near(plan.estimates[5].consumption,3,"whole-race average learned for multiclass fuel plan");
    Near(plan.estimates[5].add,71.5,"fuel add uses own 91.5 liters required, not leader 182.25 liters");
    const auto window=CalculatePitWindow(s,f,plan.lapsToFinish,plan.estimates[5].consumption,prediction.ownPace);
    Near(window.fuelAtEnd,-71.5,"finish fuel uses same multiclass distance as refuel amount");
    Near(window.timeEmpty,800,"time empty uses own slower pace");
    Require(window.stops==1 && window.finishLap==81,"multiclass pit plan needs one stop and own lap 81 finish");
    Require(CalculatePitWindow(s,f,prediction.noStop.leaderDistance,3).stops==2,"using leader distance would wrongly require an extra stop");
    auto lapRace=s; lapRace.totalLaps=161; auto untimed=f; untimed.timeLeft=-1; untimed.timeLeftKnown=false;
    const auto lapPrediction=predictor.Predict(lapRace,untimed,options);
    Near(lapPrediction.noStop.leaderTotalDistance,161,"pure lap race total remains its finish lap limit");
    Near(lapPrediction.noStop.ownDistance,30.5,"lapped own finish also follows overall leader in lap-limited races");
    f.timeLeft=10800;
    prediction=predictor.Predict(s,f,options);
    Near(prediction.noStop.leaderTotalDistance,280.25,"three-hour leader total uses expiry position rather than integer finish goal");
    Near(prediction.noStop.ownDistance,90.5,"three-hour slow-class forecast includes repeated future lapping");
    Require(prediction.noStop.leaderCrossings-prediction.noStop.ownCrossings==90,"ninety further leader crossings over three hours");
    f.timeLeft=3600; f.lastLap=f.bestLap=f.cars[1].best=f.cars[1].last=150; f.cars[1].estTime=75;
    prediction=predictor.Predict(s,f,options);
    Near(prediction.noStop.ownDistance,24.5,"slower updated own pace lowers remaining distance without manual lap adjustment");
    Near(fuel.Plan(f,0,false,0,&prediction).estimates[5].add,53.5,"updated class pace propagates into required fuel");
}
void FuelForecastTests() {
    auto s=Race(); auto f=RacingFrame(); FuelCalculator fuel;
    f.pit=false; f.time=0; f.lapCompleted=0; f.fuel=100; fuel.Update(s,f);
    for(int i=1;i<=11;++i) { f.time=i*100; f.lapCompleted=i; f.fuel-=3; fuel.Update(s,f); }
    f.fuel=20;
    RacePredictor predictor; predictor.Update(s,f); const auto forecast=predictor.Predict(s,f,{});
    const auto plan=fuel.Plan(f,0,false,0,&forecast);
    Require(plan.confidence==Confidence::High,"stable ten-lap fuel usage supports high consumption confidence");
    Near(plan.estimates[1].add,forecast.noStop.ownDistance*3-20,"baseline refuel uses own projected finish distance");
    Near(plan.estimates[1].addWithStops,forecast.withStops.ownDistance*3-20,"stop scenario has separate refuel amount");
    Require(plan.estimates[1].safeAdd>=plan.estimates[1].add && plan.estimates[1].safeAdd>=plan.estimates[1].addWithStops,"range upper covers both modelled scenarios");
    RacePrediction missing;
    Require(fuel.Plan(f,0,false,0,&missing).estimates[1].add<0,"missing prediction never falls back to the obsolete time-plus-one rule");
    f.replay=true; Require(fuel.Plan(f,0,false,0,&forecast).confidence==Confidence::Unknown,"no fuel confidence in replay");
}
}
void PredictionTests() { PitWindowTests(); TimedTests(); LeaderChangeTests(); CrossingInvariantTests(); LimitAndStateTests(); HistoryTests(); MulticlassEnduranceTests(); FuelForecastTests(); }
