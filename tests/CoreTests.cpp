#include "Telemetry.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <fstream>
using namespace rm;
void PredictionTests();
void RaceControlTests();
void PitStopTests();
void PreviewRaceTests();
void ReviewRegressionTests();
void Require(bool c,const char* m) { if(!c) throw std::runtime_error(m); }
void Near(double a,double b,const char* m) { Require(std::abs(a-b)<0.001,m); }
const char* Yaml=R"(WeekendInfo:
 TrackDisplayName: "Spa: Grand Prix"
 SessionID: 123
 SubSessionID: 456
SessionInfo:
 Sessions:
 - SessionNum: 0
   SessionType: Practice
   SessionTrackRubberState: clean
   SessionLaps: unlimited
 - SessionNum: 1
   SessionType: Race
   SessionTrackRubberState: moderately low usage
   SessionLaps: 17
DriverInfo:
 DriverCarIdx: 3
 Drivers:
 - CarIdx: 0
   UserName: Pace Car
   CarIsPaceCar: 1
 - CarIdx: 3
   UserName: 'O''Brien: Racer'
   CarNumber: "007"
   CarClassID: 42
   CarClassColor: 0xAABBCC
   IRating: 2500
 - CarIdx: 4
   UserName: Rival
   CarClassID: 99
 - CarIdx: 5
   UserName: Spectator
   IsSpectator: 1
CarSetup:
 CarIdx: 12
 UserName: Ignore
)";
void ParserTests() {
 Require(ParseSession(Yaml,0).trackUsage=="clean" && ParseSession(Yaml,1).trackUsage=="moderately low usage" && ParseSession(Yaml,2).trackUsage.empty(),"track usage belongs to selected session, unknown remains empty");
 auto cameras=ParseSession("CameraInfo:\n Groups:\n - GroupNum: 1\n   GroupName: Scenic\n   IsScenic: true\n   Cameras:\n   - CameraNum: 99\n     CameraName: Nested\n - GroupNum: 11\n   GroupName: Far Chase\nDriverInfo:\n DriverCarIdx: 3\n",0);
 Require(cameras.cameras.size()==2 && cameras.cameras[0].scenic && cameras.cameras[1].number==11 && cameras.cameras[1].name=="Far Chase","camera groups ignore nested camera entries");
 auto s=ParseSession(Yaml,1);
 Require(s.type=="Race" && s.key=="123:456" && s.player==3,"session scopes");
 Require(s.drivers.size()==2 && s.track=="Spa: Grand Prix","driver scope filtering");
 Require(s.drivers[0].name=="O'Brien: Racer" && s.drivers[0].number=="007","quoted scalars");
 Require(s.drivers[0].classColor==0xAABBCC && s.drivers[0].rating==2500,"class metadata");
 Require(ParseSession(Yaml,0).type=="Practice","session selection");
 Require(s.totalLaps==17 && ParseSession(Yaml,0).totalLaps<0,"race lap limit and unlimited markers");
}
std::string ResultsYaml() {
 std::string yaml=Yaml;
 const auto begin=yaml.find("SessionInfo:"), end=yaml.find("DriverInfo:");
 yaml.replace(begin,end-begin,R"(SessionInfo:
 Sessions:
 - SessionNum: 0
   SessionType: Practice
   ResultsPositions:
   - Position: 2
     ClassPosition: 0
     FastestTime: 121.500
     LastTime: 122.250
     CarIdx: 3
   - Position: 1
     ClassPosition: 0
     CarIdx: 4
     FastestTime: 120.000
     LastTime: 121.000
   - Position: 3
     CarIdx: 127
     FastestTime: nan
     LastTime: inf
   - Position: 4
     CarIdx: 128
     FastestTime: 1.000
   ResultsFastestLap:
   - CarIdx: 3
     FastestTime: 9.000
 - SessionNum: 1
   SessionType: Race
   ResultsPositions:
     - Position: 2
       ClassPosition: 0
       CarIdx: 3
       FastestTime: 140.000
       LastTime: 142.000
     - Position: 1
       ClassPosition: 0
       CarIdx: 4
       FastestTime: 138.200
       LastTime: 139.500
 - SessionNum: 2
   SessionType: Qualify
   ResultsPositions:
   - Position: 1
     ClassPosition: 0
     CarIdx: 3
     FastestTime: 119.800
     LastTime: 0.000
DriverSetup:
 ResultsPositions:
 - CarIdx: 3
   FastestTime: 1.000
)");
 return yaml;
}
void ResultsTests() {
 const auto yaml=ResultsYaml();
 auto s=ParseSession(yaml,0); Frame f; f.session=0; f.player=3;
 Near(s.results[3].best,121.5,"historical best from current session");
 Near(s.results[3].last,122.25,"historical last lap");
 Require(s.results[3].position==2 && s.results[3].classPosition==1,"YAML result position bases");
 Require(s.results[127].best<0 && s.results[127].last<0,"nonfinite result times excluded");
 auto rows=Standings(s,f,9,false);
 Require(rows.size()==2 && rows[0].driver.index==4,"late join includes results without live positions");
 Near(rows[1].car.best,121.5,"late join best fallback");
 Near(rows[1].car.last,122.25,"late join last fallback");
 Near(rows[1].gap,1.5,"practice gap from best laps");
 f.cars[3].best=125; f.cars[3].last=124;
 rows=Standings(s,f,9,false); Near(rows[1].car.best,121.5,"earlier server best beats slower observed best");
 Near(rows[1].car.last,124,"valid live last lap takes precedence");
 f.cars[3].best=119; rows=Standings(s,f,9,false);
 Require(rows[0].driver.index==3,"new live best updates ranking"); Near(rows[0].car.best,119,"faster live best");
 f.cars[3].best=-1; s.results[3].best=-1; f.cars[3].f2Time=118; rows=Standings(s,f,9,false);
 Near(rows[0].car.best,118,"nonrace F2 best fallback");
 f={}; f.session=1; f.player=3; f.cars[3].f2Time=.25;
 s=ParseSession(yaml,1); rows=Standings(s,f,9,false);
 Require(rows.size()==2,"race historical results included");
 Near(rows[1].car.best,140,"race gap never used as best lap");
 Near(rows[1].car.last,142,"race last lap fallback");
 auto own=Standings(s,f,9,true); Require(own.size()==1 && own[0].car.classPosition==1,"historical class filter");
 s=ParseSession(yaml,0); rows=Standings(s,f,9,false); Require(rows.size()==2 && std::all_of(rows.begin(),rows.end(),[](const Row& row){return row.car.best<0 && row.car.last<0;}),"practice roster survives without leaking results from another session");
 s=ParseSession(yaml,2); f.session=2; rows=Standings(s,f,9,false);
 Require(rows.size()==2 && rows[1].car.best<0 && rows[1].car.last<0,"qualifying retains untimed roster without leaking other session results"); Near(rows[0].car.best,119.8,"qualifying historical best");
 Require(rows[0].car.last<0,"zero time remains missing");
}
void RowTests() {
 { auto pitSession=DemoSession(); auto pitFrame=DemoFrame(0);
   const auto normal=Relative(pitSession,pitFrame,9,false);
   const auto filtered=Relative(pitSession,pitFrame,9,false,true);
   Require(filtered.size()==8 && std::none_of(filtered.begin(),filtered.end(),[](const Row& r){return r.car.pit;}),"pit filter fills relative rows with on-track drivers");
   pitFrame.cars[pitFrame.player].pit=true;
   const auto ownPit=Relative(pitSession,pitFrame,9,false,true);
   Require(std::any_of(ownPit.begin(),ownPit.end(),[](const Row& r){return r.player;}),"pit filter retains reference car in pit to keep center");
 }

 auto s=DemoSession(); auto f=DemoFrame(10);
 f.cars[3].pct=.99; f.cars[3].estTime=-1;
 f.cars[4].pct=.01; f.cars[4].estTime=-1; f.cars[4].lap=f.cars[3].lap+1;
 for(int i=0;i<10;++i) if(i!=3 && i!=4) f.cars[i].surface=-1;
 auto rows=Relative(s,f,9,false);
 Require(rows.size()==2 && rows[0].driver.index==4,"relative crosses start line");
 Require(Relative(s,f,1,false).front().player,"one relative row always retains reference car");
 Near(rows[0].gap,-2.764,"wrapped relative time");
 Require(rows[0].lapDifference==0,"no false lapping color at start line");
 f.cars[4].surface=-1; Require(Relative(s,f,9,false).size()==1,"exclude absent cars");
 f=DemoFrame(10); s.drivers[4].classId=99;
 Require(Relative(s,f,21,true).size()==9,"relative own class");
 f.cars[3].position=30; auto standings=Standings(s,f,5,false);
 Require(standings.size()==5 && standings.back().player,"standings keeps player visible");
 s.type="Practice"; for(auto& car:f.cars) car.f2Time=car.best;
 Near(Standings(s,f,21,false)[0].gap,0,"practice gap uses best laps");
 f.player=127; Require(Relative(s,f,9,false).empty(),"unknown player safe");
 f=DemoFrame(10); f.onTrack=false; f.camera=4; f.cars[3].surface=-1;
 auto watched=Relative(s,f,21,true);
 Require(watched.size()==1 && watched[0].driver.index==4 && watched[0].player,"spotting follows camera and its class");
 Near(watched[0].gap,0,"camera reference gap");
 auto watchedStandings=Standings(s,f,5,true);
 Require(watchedStandings.size()==1 && watchedStandings[0].driver.index==4 && watchedStandings[0].player,"standings follows camera class");
 f.camera=8; f.onTrack=true; f.replay=true;
 Require(ReferenceCar(f)==8,"replay camera overrides player");
 auto replayRows=Relative(s,f,21,false);
 auto target=std::find_if(replayRows.begin(),replayRows.end(),[](const Row& row){return row.player;});
 Require(target!=replayRows.end() && target->driver.index==8,"replay highlight");
 f.replay=false; Require(ReferenceCar(f)==3,"driving restores own car");
 f.onTrack=false; f.camera=MaxCars; Require(ReferenceCar(f)==3,"invalid camera fallback");
}
void RelativeCenterTests() {
 Require(RelativeRowCount(8)==9 && RelativeRowCount(128)==127 && RelativeRowCount(0)==1,"relative uses bounded odd row counts");
 auto session=DemoSession(); auto frame=DemoFrame(0); frame.pit=false;
 for(auto& car:frame.cars) { car.lap=1; car.completed=0; car.pit=false; }
 frame.cars[0].lap=5; frame.cars[4].lap=0;
 session.type="Practice"; auto rows=Relative(session,frame,9,false);
 Require(std::all_of(rows.begin(),rows.end(),[](const Row& row){return row.lapDifference==0;}),"practice has no lapping colors");
 session.type="Qualify"; rows=Relative(session,frame,9,false);
 Require(std::all_of(rows.begin(),rows.end(),[](const Row& row){return row.lapDifference==0;}),"qualifying has no lapping colors");
 session.type="Race"; rows=Relative(session,frame,9,false);
 Require(std::any_of(rows.begin(),rows.end(),[](const Row& row){return row.lapDifference>0;}) && std::any_of(rows.begin(),rows.end(),[](const Row& row){return row.lapDifference<0;}),"race retains lapping colors");
 frame.cars[frame.player].pct=.1;
 for(const auto& driver:session.drivers) if(driver.index!=frame.player) frame.cars[driver.index].pct=.15+driver.index*.01;
 rows=Relative(session,frame,5,false);
 const auto reference=std::find_if(rows.begin(),rows.end(),[](const Row& row){return row.player;});
 Require(rows.size()==3 && reference==rows.end()-1,"one-sided traffic uses only allocated side slots");
 for(const auto& driver:session.drivers) if(driver.index!=frame.player) frame.cars[driver.index].surface=-1;
 Require(Relative(session,frame,9,false).size()==1,"empty track retains center reference");
}
void StandingsSelectionTests() {
 Session s; s.type="Race"; Frame f; f.onTrack=true; f.player=7;
 for(int i=0;i<18;++i) {
  const int cls=i<10?1:2;
  s.drivers.push_back({i,cls,2000,cls==1?0xFFE040u:0x30C0FFu,false,false,"Driver",std::to_string(i),cls==1?"GT3":"GT4"});
  auto& car=f.cars[i]; car.position=i+1; car.classPosition=i<10?i+1:i-9; car.lap=10; car.pct=.9-i*.01; car.f2Time=i*2;
 }
 StandingsOptions options;
 auto groups=SelectStandings(s,f,options);
 Require(groups.size()==2 && groups[0].rows.size()==8 && groups[1].rows.size()==3,"leaders per class plus reference neighbours");
 Require(groups[0].rows[3].driver.index==5 && groups[0].rows.back().driver.index==9,"correct ahead and behind limits");
 Near(groups[1].rows[1].gap,2,"separate gap to class leader");
 Near(groups[0].rows[3].interval,2,"interval uses hidden preceding driver, not previous visible row");
 Require(!std::isfinite(groups[1].rows.front().interval),"class leader has no interval");
 f.cars[5].lap=8; auto lapped=SelectStandings(s,f,options); Require(lapped[0].rows[3].intervalLaps==2,"interval laps against preceding car"); f.cars[5].lap=10;
 for(const auto& group:groups) {
  int previous=-1; for(const auto& row:group.rows) { Require(row.driver.index>previous,"no duplicate or unsorted selected rows"); previous=row.driver.index; }
 }
 options.separateClasses=false; groups=SelectStandings(s,f,options);
 Require(groups.size()==1 && groups[0].classId==-1 && groups[0].rows.size()==8,"combined top count applies once overall");
 options.ownClass=true; groups=SelectStandings(s,f,options);
 Require(groups.size()==1 && groups[0].rows.size()==8,"independent driver class filter");
 f.onTrack=false; f.camera=14; options.separateClasses=true;
 groups=SelectStandings(s,f,options);
 Require(groups.size()==1 && groups[0].classId==2 && groups[0].rows.size()==7,"spotting selection follows watched class");
 options.leaders=options.ahead=options.behind=0; groups=SelectStandings(s,f,options);
 Require(groups.size()==1 && groups[0].rows.size()==1 && groups[0].rows[0].player,"zero limits retain reference car");
 f.camera=127; Require(SelectStandings(s,f,options).empty(),"unknown driver class is safe");
 options.ownClass=false; options.leaders=2; groups=SelectStandings(s,f,options);
 Require(groups.size()==2 && groups[0].rows.size()==2 && groups[1].rows.size()==2,"top rows without reference");
 f.onTrack=true; f.player=1; options.ahead=options.behind=3;
 groups=SelectStandings(s,f,options);
 Require(groups[0].rows.size()==5,"leader-neighbour overlap is deduplicated at field boundary");
 s.type="Practice";
 for(int i=0;i<18;++i) f.cars[i].best=100+i;
 groups=SelectStandings(s,f,options); Near(groups[1].rows[1].gap,1,"practice class gap from historical merged ranking");
}
void EarlyAndLiveGapTests() {
 const auto yaml=std::string(R"(SessionInfo:
 Sessions:
 - SessionNum: 1
   SessionType: Race
QualifyResultsInfo:
 Results:
 - Position: 0
   CarIdx: 0
 - Position: 1
   CarIdx: 1
 - Position: 2
   CarIdx: 2
DriverInfo:
 Drivers:
 - CarIdx: 0
   UserName: Leader
   CarClassID: 1
   CarClassEstLapTime: 100
 - CarIdx: 1
   UserName: Player
   CarClassID: 1
   CarClassEstLapTime: 100
 - CarIdx: 2
   UserName: Other Class
   CarClassID: 2
   CarClassEstLapTime: 200
)");
 auto session=ParseSession(yaml,1);
 Require(session.gridPositions[0]==1 && session.gridPositions[2]==3,"qualifying results provide one-based starting grid");
 Near(session.drivers[1].estimatedLapTime,100,"class estimated lap time available before first race lap");
 Frame frame; frame.connected=frame.onTrack=true; frame.session=1; frame.player=1; frame.state=3;
 for(int i=0;i<3;++i) { auto& car=frame.cars[i]; car.surface=3; car.pct=.5-i*.02; car.lap=0; car.estTime=car.pct*session.drivers[i].estimatedLapTime; }
 auto relative=Relative(session,frame,9,false);
 Require(relative.size()==3,"relative includes formation-lap cars with no completed laps");
 auto player=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.player;});
 auto leader=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.driver.index==0;});
 Near(player->gap,0,"formation reference gap is zero"); Near(leader->gap,-2,"formation relative uses SDK class pace before best or last lap exists");
 auto other=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.driver.index==2;});
 Near(other->gap,2,"multiclass estimates are normalized before comparing relative phases");
 auto rows=Standings(session,frame,9,false);
 Require(rows.size()==3 && rows[0].car.position==1 && rows[1].car.classPosition==2,"formation standings visible before SDK race positions exist");
 Near(rows[1].gap,2,"early leader gap is available before first crossing");
 StandingsOptions options; options.liveGaps=true; options.separateClasses=false; options.leaders=9;
 auto groups=SelectStandings(session,frame,options);
 Near(groups[0].rows[1].interval,2,"live interval available during formation lap");
 for(int i=0;i<3;++i) { auto& car=frame.cars[i]; car.lap=1; car.completed=0; car.position=i+1; car.classPosition=i==2?1:i+1; car.f2Time=i*10; }
 frame.state=4;
 groups=SelectStandings(session,frame,options);
 Near(groups[0].rows[1].gap,2,"live first-lap gap ignores stale F2 snapshot");
 frame.cars[1].pct=.47; frame.cars[1].estTime=47;
 groups=SelectStandings(session,frame,options);
 Near(groups[0].rows[1].gap,3,"live gap responds between start-finish crossings");
 Near(groups[0].rows[1].interval,3,"live interval responds between start-finish crossings");
 options.liveGaps=false; groups=SelectStandings(session,frame,options);
 Near(groups[0].rows[1].gap,10,"snapshot option preserves SDK race gap");
 Near(groups[0].rows[2].interval,10,"snapshot interval uses SDK differences");
 options.liveGaps=true; options.separateClasses=true;
 groups=SelectStandings(session,frame,options);
 Near(groups[1].rows.front().gap,0,"live separate-class leader gap is zero");
 // The preceding car can be hidden by top/neighbour selection.
 options.separateClasses=false; options.leaders=1; options.ahead=options.behind=0; frame.player=2;
 groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.size()==2,"live gaps preserve sparse standings selection");
 Near(groups[0].rows.back().interval,1,"live interval uses hidden preceding car and its class pace");
 // Start-line wrapping uses complete race progress in standings and nearest distance in relative.
 frame.player=1; frame.cars[0].completed=1; frame.cars[0].lap=2; frame.cars[0].pct=.01; frame.cars[0].estTime=1;
 frame.cars[1].pct=.99; frame.cars[1].estTime=99;
 options.leaders=9; groups=SelectStandings(session,frame,options);
 Near(groups[0].rows[1].gap,2,"live gap wraps start line without resetting to a full lap");
 relative=Relative(session,frame,9,false); leader=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.driver.index==0;});
 Near(leader->gap,-2,"first-lap relative wraps start line using estimated class lap time");
 frame.cars[0].completed=3; frame.cars[0].lap=4; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows[1].lapDifference==2,"live gaps retain genuine lapped-car indication");
 frame.cars[1].surface=-1; groups=SelectStandings(session,frame,options);
 Require(!std::isfinite(groups[0].rows[1].gap),"missing live position cannot manufacture zero gap");
 // No grid and no completed laps still show active drivers by track progress.
 session.gridPositions.fill(0); frame=Frame{}; frame.connected=frame.onTrack=true; frame.player=1; frame.state=4;
 for(int i=0;i<3;++i) { auto& car=frame.cars[i]; car.surface=3; car.pct=.5-i*.02; car.lap=1; }
 rows=Standings(session,frame,9,false);
 Require(rows.size()==3 && rows[0].driver.index==0 && rows[1].car.position==2,"active first-lap standings fallback does not need starting grid or results");
 // When every timing source is absent, driver lists remain useful but seconds stay unknown.
 for(auto& driver:session.drivers) driver.estimatedLapTime=-1;
 relative=Relative(session,frame,9,false); leader=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.driver.index==0;});
 Require(!std::isfinite(leader->gap),"missing timing inputs stay unknown instead of inventing seconds");
}
void LivePositionAndPitVisibilityTests() {
 Session session; session.type="Race"; session.key="live-position-test";
 Frame frame; frame.connected=frame.onTrack=true; frame.player=1; frame.state=4;
 for(int i=0;i<3;++i) {
     Driver driver; driver.index=i; driver.classId=i==2?2:1; driver.estimatedLapTime=100; session.drivers.push_back(driver);
     auto& car=frame.cars[i]; car.position=i+1; car.classPosition=i==2?1:i+1; car.completed=2; car.lap=3; car.pct=.8-i*.1; car.surface=3; car.estTime=car.pct*100;
 }
 StandingsOptions options; options.liveGaps=true; options.separateClasses=false; options.leaders=MaxCars;
 auto groups=SelectStandings(session,frame,options); Require(groups[0].rows.front().driver.index==0,"live standings initial ranking");
 frame.cars[1].pct=.85; frame.cars[1].estTime=85;
 groups=SelectStandings(session,frame,options);
 Require(groups[0].rows[0].driver.index==1 && groups[0].rows[0].car.position==1 && groups[0].rows[1].car.position==2,"live overtake changes both row order and overall P without SDK update");
 Near(groups[0].rows[0].gap,0,"new live leader gap is zero"); Near(groups[0].rows[1].interval,5,"interval follows new preceding driver after overtake");
 auto relative=Relative(session,frame,MaxCars,false);
 auto player=std::find_if(relative.begin(),relative.end(),[](const Row& row){return row.player;});
 Require(player->car.position==1 && player->car.classPosition==1,"relative P updates independently of standings checkbox");
 options.separateClasses=true; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.front().driver.index==1 && groups[0].rows[1].car.classPosition==2,"live class positions and class order update together");
 options.liveGaps=false; options.separateClasses=false; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.front().driver.index==0 && groups[0].rows[1].car.position==2,"disabled live mode retains SDK official ranks");
 frame.cars[2].completed=0; frame.cars[2].lap=1; frame.cars[2].pct=.99; frame.cars[2].estTime=99;
 options.liveGaps=true; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.back().driver.index==2 && groups[0].rows.back().car.position==3,"nearby lapped traffic never jumps ahead of lead lap cars");
 frame.cars[1].completed=3; frame.cars[1].lap=4; frame.cars[1].pct=.01; frame.cars[1].estTime=1;
 frame.cars[0].pct=.99; frame.cars[0].estTime=99; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.front().driver.index==1,"live position survives split start-line crossing"); Near(groups[0].rows[1].gap,2,"new live leader gap stays coherent across start line");
 frame.cars[0].surface=-1; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows[0].driver.index==0 && groups[0].rows[1].driver.index==1,"missing progress keeps SDK slot and does not generate a pass");
 frame.state=6; groups=SelectStandings(session,frame,options); Require(groups[0].rows.front().driver.index==0,"finished race retains official result positions"); frame.state=4;
 session.type="Practice"; frame.cars[0].best=100; frame.cars[1].best=101; groups=SelectStandings(session,frame,options);
 Require(groups[0].rows.front().driver.index==0,"practice ranking remains by best lap when live mode enabled");
 // Pit reminder appears before OnPitRoad and clears immediately on exit.
 PitLimiterVisibility reminder; frame=Frame{}; frame.connected=frame.onTrack=true; frame.player=1; frame.time=10; frame.cars[1].surface=3;
 Require(!reminder.Update(session,frame),"pit reminder hidden on normal track");
 frame.time=11; frame.cars[1].surface=2; Require(reminder.Update(session,frame),"pit approach shows limiter before pit-road flag");
 frame.time=12; frame.cars[1].surface=3; frame.pit=true; Require(reminder.Update(session,frame),"limiter stays shown in pit lane");
 frame.time=13; frame.pit=false; Require(!reminder.Update(session,frame),"pit exit immediately hides limiter");
 frame.time=16.9; Require(!reminder.Update(session,frame),"pit exit has no lingering reminder");
 frame.time=17; Require(!reminder.Update(session,frame),"grace period ends after five seconds");
 frame.time=18; frame.limiterKnown=frame.limiter=true; Require(reminder.Update(session,frame),"active limiter on track keeps reminder visible");
 frame.pit=true; reminder.Update(session,frame); frame.pit=false; frame.time+=.1; Require(reminder.Update(session,frame),"active limiter remains visible at actual pit exit");
 frame.time+=20; Require(reminder.Update(session,frame),"active limiter outside pits has no display timeout");
 frame.time+=1; frame.cars[1].surface=2; Require(reminder.Update(session,frame),"next pit approach rearms reminder"); frame.cars[1].surface=3;
 frame.time+=1; frame.limiter=false; Require(!reminder.Update(session,frame),"disengaging limiter outside pit clears reminder immediately");
 frame.replay=true; Require(!reminder.Update(session,frame),"replay suppresses and clears grace reminder");
 frame.replay=false; Require(!reminder.Update(session,frame),"return from replay does not retain stale grace timer");
 frame.pit=true; Require(reminder.Update(session,frame),"new pit entry rearms reminder"); frame.onTrack=false; Require(!reminder.Update(session,frame),"spotting clears own pit reminder");
 frame.onTrack=true; frame.pit=false; Require(!reminder.Update(session,frame),"return to own car does not carry spotting grace timer");
 // Past pit exit with the limiter still on: a warning instead of the green reminder.
 {
  PitLimiterVisibility w; Frame g; g.connected=g.onTrack=true; g.player=1; g.time=1; g.cars[1].surface=2; g.limiterKnown=g.limiter=true;
  Require(w.Update(session,g) && !w.Warning(),"limiter on during pit approach is right");
  g.time=2; g.pit=true; Require(w.Update(session,g) && !w.Warning() && w.InPits(),"limiter on in the pit lane is right");
  g.time=3; g.pit=false; g.cars[1].surface=3; Require(w.Update(session,g) && w.Warning() && !w.InPits(),"limiter on at pit exit warns");
  g.time=4; Require(w.Update(session,g) && w.Warning(),"the warning stays while the limiter is on");
  g.time=5; g.limiter=false; Require(!w.Update(session,g) && !w.Warning(),"limiter off ends the warning");
 }
 // In-car adjustments against the loaded setup.
 {
  const auto setup=ParseSession("CarSetup:\n UpdateCount: 3\n Chassis:\n  InCarDials:\n   BrakePressureBias: 52.5%\n   AbsSetting: 5 (ABS)\n   TractionControlSetting: 4 (TC)\n   ThrottleShapeSetting: Off\n",0);
  Require(setup.setupUpdate==3 && setup.setup.size()==4,"car setup values parsed");
  Frame g; g.dialsKnown=1u<<BrakeBiasDial|1u<<AbsDial|1u<<TractionDial|1u<<ThrottleDial;
  g.dials[BrakeBiasDial]=52.5; g.dials[AbsDial]=5; g.dials[TractionDial]=4; g.dials[ThrottleDial]=2;
  Require(SetupChanges(setup,g).empty(),"dials as in the setup; a setup value that is no number is skipped");
  g.dials[AbsDial]=3; g.dials[BrakeBiasDial]=53.04;
  const auto changes=SetupChanges(setup,g);
  Require(changes.size()==2 && changes[0].dial==BrakeBiasDial && changes[0].percent && changes[0].decimals==1 && changes[1].dial==AbsDial && changes[1].setup==5 && changes[1].current==3,"changed brake bias and ABS");
  g.dials[BrakeBiasDial]=52.53; Require(SetupChanges(setup,g).size()==1,"within the setup's rounding is unchanged");
  g.dialsKnown&=~(1u<<AbsDial); Require(SetupChanges(setup,g).empty(),"a dial the car does not have is left out");
  g.dialsKnown|=1u<<AbsDial;
  // A dial that already differs on getting in is not comparable on this car; one changed later is.
  SetupDials dials; g.connected=g.onTrack=true; g.player=0; g.session=0; g.time=10;
  Require(dials.Update(setup,g).empty(),"a dial differing on entry is left out");
  g.time=20; g.dials[TractionDial]=2;
  Require(dials.Update(setup,g).size()==1 && dials.Changes()[0].dial==TractionDial,"a dial changed after getting in is reported");
  g.onTrack=false; Require(dials.Update(setup,g).empty(),"nothing outside the car");
 }
 // Where the user is, for when overlays show.
 {
  Frame g; g.connected=g.onTrack=true; Require(CurrentSituation(g)==InCarSituation,"driving is in car");
  g.onTrack=false; Require(CurrentSituation(g)==SpottingSituation,"live outside the car is spotting");
  g.replay=true; g.replayFramesBehind=600; Require(CurrentSituation(g)==ReplaySituation,"old footage is a replay");
  g.garage=true; Require(CurrentSituation(g)==GarageSituation,"the garage screen wins");
 }
 frame.pit=true; reminder.Update(session,frame); frame.pit=false; session.key="new-session"; Require(!reminder.Update(session,frame),"new session clears old pit grace period");
 frame.pit=true; reminder.Update(session,frame); frame.pit=false; frame.time=1; Require(!reminder.Update(session,frame),"rewind clears future pit grace period");
 frame.inStall=true; Require(reminder.Update(session,frame),"pit stall remains visible without lane flag"); frame.connected=false; Require(!reminder.Update(session,frame),"disconnect clears limiter reminder");
}
void UntimedPracticeTests() {
 Session session; session.type="Practice"; session.number=1;
 Frame frame; frame.session=1; frame.player=3;
 const char* numbers[]={"100","7","002","11","A4",""};
 for(int i=0;i<6;++i) { Driver driver; driver.index=i; driver.classId=i%2; driver.number=numbers[i]; driver.name="Driver"; session.drivers.push_back(driver); }
 auto rows=Standings(session,frame,MaxCars,false);
 const int expected[]={2,1,3,0,4,5};
 Require(rows.size()==6,"practice lists every driver before any lap and without active telemetry");
 for(int i=0;i<6;++i) { Require(rows[i].driver.index==expected[i] && rows[i].car.position==i+1,"untimed practice sorts start numbers numerically and reranks"); Require(!std::isfinite(rows[i].gap),"untimed practice never invents timing gaps"); }
 frame.cars[0].best=100; rows=Standings(session,frame,MaxCars,false);
 Require(rows.front().driver.index==0 && rows[1].driver.index==2,"first lap moves ahead of untimed drivers regardless of car number");
 frame.cars[3].best=98; rows=Standings(session,frame,MaxCars,false);
 Require(rows[0].driver.index==3 && rows[1].driver.index==0 && rows[2].driver.index==2,"timed drivers rank by best lap before numeric untimed roster");
 Near(rows[1].gap,2,"timed practice gap uses best laps"); Require(!std::isfinite(rows[2].gap),"untimed follower still has unknown gap");
 frame.cars[0].best=97; rows=Standings(session,frame,MaxCars,false);
 Require(rows.front().driver.index==0,"improved practice lap updates ranking");
 session.results[1].best=96; rows=Standings(session,frame,MaxCars,false);
 Require(rows.front().driver.index==1,"late join merges server lap times ahead of untimed cars");
 frame.session=2; rows=Standings(session,frame,MaxCars,false);
 Require(rows[0].driver.index==0 && rows[2].driver.index==2,"results from another session do not move an untimed car to the top");
 frame.session=1;
 StandingsOptions options; options.leaders=MaxCars; auto groups=SelectStandings(session,frame,options);
 Require(groups.size()==2 && groups[0].rows.size()==3 && groups[1].rows.size()==3,"separate practice classes retain untimed drivers");
 for(const auto& group:groups) for(size_t i=0;i<group.rows.size();++i) Require(group.rows[i].car.classPosition==static_cast<int>(i)+1,"class ranks include timed and untimed cars");
 auto own=Standings(session,frame,MaxCars,true); Require(own.size()==3 && own.front().driver.index==1,"practice own-class filter works without first lap from reference driver");
 options.separateClasses=false; groups=SelectStandings(session,frame,options);
 Require(groups.size()==1 && groups.front().rows.size()==6,"combined practice roster includes all entrants");
 // Former SDK ranks must not reorder equal times or untimed cars ahead of car-number order.
 session.results={}; for(auto& car:frame.cars) { car.best=-1; car.position=1; }
 rows=Standings(session,frame,MaxCars,false); Require(rows[0].driver.index==2 && rows[1].driver.index==1,"stale SDK positions do not affect numeric untimed sort");
 frame.cars[0].best=frame.cars[1].best=100; rows=Standings(session,frame,MaxCars,false);
 Require(rows[0].driver.index==1 && rows[1].driver.index==0,"equal lap times use numeric car number as deterministic tie-break");
}
void MissingDriverDataTests() {
 auto s=DemoSession(); auto f=DemoFrame(0); s.number=f.session; s.results={};
 for(auto& car:f.cars) car=Car{};
 f.camera=-1; f.player=3; f.onTrack=true; f.state=2;
 for(const auto& type:{"Qualify","Lone Qualify","Open Qualify","Practice","Race"}) {
  s.type=type; auto rows=Standings(s,f,MaxCars,false);
  Require(rows.size()==s.drivers.size(),"all sessions retain roster with no laps and no positions");
  Require(rows.front().driver.index==0,"missing telemetry defaults to car number order");
 }
 s.type="Qualify"; s.drivers[4].rating=9999;
 auto ranked=Standings(s,f,MaxCars,false,true); Require(ranked.front().driver.index==4,"untimed iRating order descending");
 f.cars[2].best=100; ranked=Standings(s,f,MaxCars,false,true); Require(ranked.front().driver.index==2 && ranked[1].driver.index==4,"timed drivers precede optional rating order");
 s.type="Race"; s.gridPositions[3]=1; s.gridPositions[0]=2;
 ranked=Standings(s,f,MaxCars,false,true); Require(ranked[0].driver.index==3 && ranked[1].driver.index==0,"starting grid beats optional rating order before start");
 auto relative=Relative(s,f,7,false,true); Require(relative.size()==1 && relative[0].player && relative[0].driver.index==3 && relative[0].car.position==1,"relative retains reference without track progress and uses known grid"); Near(relative[0].gap,0,"stationary missing progress reference gap zero");
 s.type="Qualify"; relative=Relative(s,f,7,false,true); Require(relative.size()==1 && relative[0].car.position==0,"relative does not invent position when none known");
 f.cars[3].position=5; relative=Relative(s,f,7,false); Require(relative[0].car.position==5,"relative missing progress keeps SDK position");
 f.onTrack=false; f.camera=4; relative=Relative(s,f,7,false); Require(relative.size()==1 && relative[0].driver.index==4 && relative[0].player,"missing progress spotting follows camera driver");
}
void FuelMetricsTests() {
 auto s=DemoSession(); auto f=DemoFrame(0); FuelCalculator calculator;
 f.pit=false; f.fuel=250; f.lapCompleted=0; f.lapPct=0; f.lapsLeft=5; f.timeLeft=604800;
 calculator.Update(s,f);
 f.time=100; f.lapCompleted=1; f.fuel-=1; calculator.Update(s,f);
 for(int burn=1;burn<=12;++burn) {
  f.time+=100; ++f.lapCompleted; f.fuel-=burn; calculator.Update(s,f);
  if(burn==3) {
   const auto early=calculator.Plan(f);
   Require(early.estimates[1].samples==3 && early.estimates[2].samples==3,"partial rolling windows report actual sample counts");
   Near(early.estimates[1].consumption,2,"partial average");
  }
 }
 auto plan=calculator.Plan(f);
 Near(plan.estimates[0].consumption,12,"last lap fuel");
 Near(plan.estimates[1].consumption,7.5,"last ten average wraps ring buffer");
 Near(plan.estimates[2].consumption,10,"last five average wraps ring buffer");
 Near(plan.estimates[3].consumption,12,"session maximum");
 Near(plan.estimates[4].consumption,1,"session minimum retained beyond ten-lap window");
 Near(plan.estimates[5].consumption,6.5,"complete average retains all twelve samples beyond rolling history");
 Near(plan.estimates[6].consumption,11,"last three average"); Require(plan.estimates[6].samples==3,"last three count");
 Require(plan.historyCount==10 && plan.history[9].lap==f.lapCompleted && std::abs(plan.history[9].burn-12)<1e-9 && plan.history[0].burn<plan.history[9].burn,"history keeps the last ten whole laps, oldest first");
 Require(plan.estimates[5].samples==12,"complete average reports all observed samples");
 Require(plan.estimates[1].samples==10 && plan.estimates[2].samples==5,"full rolling window counts");
 RacePrediction forecast; forecast.finish.valid=true; forecast.finish.ownDistance=5;
 f.fuel=10; plan=calculator.Plan(f,&forecast);
 for(const auto& estimate:plan.estimates) Near(estimate.add,std::max(0.0,estimate.consumption*5-10),"fuel to add per strategy");
 plan=calculator.Plan(f);
 Require(plan.estimates[0].consumption>0 && plan.estimates[0].add<0,"unknown finish keeps consumption but no fabricated refuel amount");
 f.replay=true; plan=calculator.Plan(f);
 Require(plan.estimates[0].samples==0 && plan.estimates[0].add<0,"replay fuel estimates unavailable");
 f.replay=false; f.time+=1; f.fuel=200; calculator.Update(s,f);
 plan=calculator.Plan(f); Near(plan.estimates[5].consumption,6.5,"refueling retains complete race average");
 Require(plan.stintStartLap==f.lapCompleted+1,"refueling updates the full-stint display anchor");
 calculator.Reset(); Require(calculator.Plan(f).estimates[5].samples==0,"complete average resets with fuel history");
 {
  // Caution and pit laps appear in the history but not in the estimates.
  FuelCalculator laps; auto g=DemoFrame(0); g.pit=false; g.fuel=100; g.lapCompleted=0; g.lapPct=0; laps.Update(s,g);
  auto lap=[&](double burn,unsigned flags,bool pit) { g.time+=50; g.flags=flags; g.pit=pit; laps.Update(s,g,true); g.time+=50; g.flags=0; g.pit=false; ++g.lapCompleted; g.fuel-=burn; laps.Update(s,g,true); };
  lap(3,0,false); lap(3,0,false); lap(2,0x4000,false); lap(3.5,0,true);
  const auto history=laps.Plan(g);
  Require(history.historyCount==3,"the anchoring partial lap is not shown");
  Require(history.history[0].counted && history.history[1].caution && !history.history[1].counted && !history.history[2].counted && !history.history[2].caution,"counted, caution and pit laps are told apart");
  Require(history.samples==1,"caution and pit laps stay out of the average");
 }
 Require(calculator.Plan(f).estimates[4].samples==0,"metric history resets");
}
void FirstRaceLapFuelTests() {
 auto session=DemoSession(); session.type="Race";
 auto frame=DemoFrame(0); frame.pit=frame.inStall=false; frame.flags=0; frame.state=2; frame.fuel=55; frame.lapCompleted=-1; frame.lapPct=.95;
 FuelCalculator calculator; calculator.Update(session,frame);
 frame.time=10; frame.state=3; frame.fuel=52; calculator.Update(session,frame);
 frame.time=20; frame.fuel=50; frame.lapCompleted=0; calculator.Update(session,frame);
 Require(calculator.Plan(frame).samples==0,"formation fuel excluded from race learning");
 frame.time=20.1; frame.state=4; frame.fuel=49.9; frame.lapPct=.01; calculator.Update(session,frame);
 frame.time=120.1; frame.lapCompleted=1; frame.lapPct=.01; frame.fuel=46.7; calculator.Update(session,frame);
 auto plan=calculator.Plan(frame);
 Require(plan.samples==1 && plan.estimates[0].samples==1,"observed clean first race lap immediately provides Last Lap fuel");
 Near(plan.estimates[0].consumption,3.2,"first-race-lap burn excludes grid and formation consumption");
 Near(plan.estimates[0].consumption,3.2,"Last Lap row is populated after lap one");
 Near(plan.estimates[5].consumption,3.2,"whole-race average starts with observed first racing lap");
 Require(plan.estimates[5].samples==1 && plan.estimates[1].samples==1,"partial averages report first sample immediately");
 calculator.Update(session,frame); Require(calculator.Plan(frame).samples==1,"duplicate first-lap frame never counts twice");
 frame.time=220.1; frame.lapCompleted=2; frame.fuel=43.7; calculator.Update(session,frame); plan=calculator.Plan(frame);
 Near(plan.estimates[0].consumption,3,"second lap replaces Last Lap fuel"); Near(plan.estimates[5].consumption,3.1,"race average contains both observed race laps");
 // A late join still cannot claim to have measured a complete lap.
 calculator.Reset(); frame.state=4; frame.time=50; frame.lapCompleted=0; frame.lapPct=.5; frame.fuel=50; calculator.Update(session,frame);
 frame.time=100; frame.lapCompleted=1; frame.lapPct=0; frame.fuel=48.5; calculator.Update(session,frame);
 Require(calculator.Plan(frame).samples==0,"mid-lap join continues to discard first partial sample");
 frame.time=200; frame.lapCompleted=2; frame.fuel=45.5; calculator.Update(session,frame); Near(calculator.Plan(frame).estimates[0].consumption,3,"first full lap after late join provides consumption");
 // Start evidence expires on long gaps, disconnects, replay and driver/session changes.
 auto grid=frame; grid.state=3; grid.time=0; grid.lapCompleted=0; grid.fuel=50;
 calculator.Reset(); calculator.Update(session,grid); auto start=grid; start.state=4; start.time=100; calculator.Update(session,start);
 start.time=200; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start); Require(calculator.Plan(start).samples==0,"stale pre-start evidence cannot validate an unobserved lap");
 calculator.Reset(); calculator.Update(session,grid); auto invalid=grid; invalid.connected=false; calculator.Update(session,invalid);
 start=grid; start.state=4; start.time=1; calculator.Update(session,start); start.time=101; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start); Require(calculator.Plan(start).samples==0,"disconnect clears race-start evidence");
 calculator.Reset(); calculator.Update(session,grid); invalid=grid; invalid.replay=true; calculator.Update(session,invalid);
 start=grid; start.state=4; start.time=1; calculator.Update(session,start); start.time=101; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start); Require(calculator.Plan(start).samples==0,"replay clears race-start evidence");
 calculator.Reset(); calculator.Update(session,grid); start=grid; start.state=4; start.time=1; start.session+=1; calculator.Update(session,start); start.time=101; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start); Require(calculator.Plan(start).samples==0,"new session does not inherit another race start");
 calculator.Reset(); calculator.Update(session,grid); start=grid; start.state=4; start.time=1; calculator.Update(session,start);
 start.time=30; start.flags=0x4000; calculator.Update(session,start,true); start.time=101; start.flags=0; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start,true); Require(calculator.Plan(start).samples==0,"first lap caution excluded when the filter is enabled");
 calculator.Reset(); calculator.Update(session,grid); start=grid; start.state=4; start.time=1; calculator.Update(session,start);
 start.time=30; start.pit=true; calculator.Update(session,start); start.time=101; start.pit=false; start.lapCompleted=1; start.fuel=47; calculator.Update(session,start); Require(calculator.Plan(start).samples==0,"first lap pit visit stays excluded");
}
void FuelTests() {
 auto s=DemoSession(); auto f=DemoFrame(0); FuelCalculator c;
 f.pit=false; f.fuel=50; f.lapCompleted=0; f.lapsLeft=10; f.lapPct=0; f.timeLeft=604800;
 c.Update(s,f);
 f.time=100; f.lapCompleted=1; f.fuel=47; c.Update(s,f);
 Require(c.Plan(f).samples==0,"initial partial lap excluded");
 f.time=200; f.lapCompleted=2; f.fuel=44; c.Update(s,f);
 Near(c.Plan(f).estimates[1].consumption,3,"clean lap consumption");
 f.time=300; f.lapCompleted=3; f.fuel=40; c.Update(s,f);
 Near(c.Plan(f).estimates[3].consumption,4,"maximum consumption"); Near(c.Plan(f).estimates[1].consumption,3.5,"mean consumption");
 f.time=350; f.fuel=45; c.Update(s,f); f.time=400; f.lapCompleted=4; f.fuel=43; c.Update(s,f);
 Require(c.Plan(f).samples==2,"refueling lap excluded");
 f.time=410; f.flags=0x4000; c.Update(s,f,true); f.time=500; f.lapCompleted=5; f.fuel=40; f.flags=0; c.Update(s,f,true);
 Require(c.Plan(f).samples==2,"yellow lap excluded when the filter is enabled");
 f.time=600; f.lapCompleted=6; f.fuel=37; c.Update(s,f);
 f.replay=true; c.Update(s,f); Require(c.Plan(f).estimates[1].samples==0,"no replay plan");
 f.replay=false; f.session=1; c.Update(s,f); Require(c.Plan(f).samples==0,"session reset");
}
void TelemetryTests() {
 const std::wstring name=L"Local\\RaceMate.Test."+std::to_wstring(GetCurrentProcessId());
 HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,name.c_str()); Require(map!=nullptr,"test mapping");
 auto* memory=static_cast<unsigned char*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,0)); Require(memory!=nullptr,"test view");
 auto* h=reinterpret_cast<MemoryHeader*>(memory); *h={}; h->version=2; h->status=1; h->numBuffers=2; h->bufferLength=512; h->varsOffset=128;
 h->buffers[0]={1,12000,{0,0}}; h->buffers[1]={2,13000,{0,0}};
 h->sessionOffset=18000; h->sessionLength=static_cast<int>(strlen(Yaml)+1); h->sessionUpdate=1; std::memcpy(memory+h->sessionOffset,Yaml,h->sessionLength);
 auto add=[&](const char* name,int type,int offset,int count=1) {
  auto* v=reinterpret_cast<VariableHeader*>(memory+h->varsOffset)+h->numVars++; *v={}; v->type=type; v->offset=offset; v->count=count; strcpy_s(v->name,name);
 };
 add("SessionNum",2,0); add("PlayerCarIdx",2,4); add("FuelLevel",4,8); add("EngineWarnings",3,12);
 add("OnPitRoad",1,16); add("IsOnTrack",1,17); add("IsReplayPlaying",1,18); add("SessionFlags",3,20); add("CamCarIdx",2,24); add("CarIdxLapDistPct",4,32,8);
 add("CarIdxPosition",2,64,8); add("CarIdxTrackSurface",2,96,8); add("SessionTime",5,144);
 add("CarIdxLapCompleted",2,160,8); add("SessionTimeRemain",5,200); add("PlayerCarIsTowing",1,208); add("PlayerCarTowTime",4,212);
 int session=1,player=3,warnings=0x10,position=4,surface=3; float fuel=22.5f,pct=.42f; double time=150;
 auto* data=memory+13000; std::memcpy(data,&session,4); std::memcpy(data+4,&player,4); std::memcpy(data+8,&fuel,4); std::memcpy(data+12,&warnings,4); data[16]=data[17]=1;
 uint32_t flags=0x80000000u; std::memcpy(data+20,&flags,4);
 int camera=4; std::memcpy(data+24,&camera,4);
 std::memcpy(data+32+3*4,&pct,4); std::memcpy(data+64+3*4,&position,4); std::memcpy(data+96+3*4,&surface,4); std::memcpy(data+144,&time,8);
 int completed=12; double timeLeft=600;
 std::memcpy(data+160+3*4,&completed,4); std::memcpy(data+200,&timeLeft,8);
 {
  Telemetry reader(name.c_str()); Frame f; Session s;
  Require(reader.Read(f,s),"connect");
  Require(f.session==1 && f.player==3 && f.onTrack && f.pit && f.limiter,"integer boolean bitfield");
  Require(f.flags==0x80000000u,"unsigned high bitfield bit");
  Near(f.fuel,22.5,"newest buffer"); Near(f.time,150,"double"); Near(f.cars[3].pct,.42,"array offsets");
  Require(f.cars[99].pct==-1 && f.lastLap==-1,"missing variables and array bounds");
  Require(s.type=="Race" && s.drivers.size()==2,"integrated yaml");
  Require(f.cars[3].completed==12 && f.timeLeftKnown && s.totalLaps==17,"race projection inputs through shared memory");
  Near(f.timeLeft,600,"remaining race seconds");
  Require(reader.Read(f,s),"unchanged tick");
  flags=0x190000u; std::memcpy(data+20,&flags,4); ++h->buffers[1].tick; Require(reader.Read(f,s) && DriverFlags(f.cars[3].sessionFlags)=="BLACK SLOW REPAIR" && !f.cars[4].sessionFlags,"personal session flags fallback applies only to own car");
  data[208]=1; ++h->buffers[1].tick; Require(reader.Read(f,s) && f.cars[3].towingKnown && f.cars[3].towing,"own tow state through shared memory");
  data[208]=0; float towTime=20; std::memcpy(data+212,&towTime,4); ++h->buffers[1].tick;
  Require(reader.Read(f,s) && !f.cars[3].towing,"explicit towing false takes precedence over timer");
  data[17]=0; data[18]=1; ++h->buffers[1].tick;
  Require(reader.Read(f,s) && !f.onTrack && f.replay && f.camera==4 && ReferenceCar(f)==4,"spectator/replay telemetry");
  data[18]=0; ++h->buffers[1].tick; Require(reader.Read(f,s),"paused replay snapshot");
  Sleep(2100);
  Require(reader.Read(f,s) && f.connected && ReferenceCar(f)==4,"paused replay remains connected after two seconds");
  auto resultsYaml=ResultsYaml();
  std::memcpy(memory+h->sessionOffset,resultsYaml.c_str(),resultsYaml.size()+1);
  h->sessionLength=static_cast<int>(resultsYaml.size()+1); ++h->sessionUpdate;
  Require(reader.Read(f,s),"session results update while replay tick is paused");
  auto standings=Standings(s,f,9,false);
  auto car3=std::find_if(standings.begin(),standings.end(),[](const Row& row){return row.driver.index==3;});
  Require(car3!=standings.end(),"historical driver included through shared memory");
  Near(car3->car.best,140,"historical best through shared memory");
  resultsYaml.replace(resultsYaml.find("140.000"),7,"136.000");
  std::memcpy(memory+h->sessionOffset,resultsYaml.c_str(),resultsYaml.size()+1); ++h->sessionUpdate;
  Require(reader.Read(f,s),"second metadata-only update");
  standings=Standings(s,f,9,false);
  car3=std::find_if(standings.begin(),standings.end(),[](const Row& row){return row.driver.index==3;});
  Near(car3->car.best,136,"best lap refresh without new telemetry tick");
  warnings=0; std::memcpy(data+12,&warnings,4); ++h->buffers[1].tick;
  Require(reader.Read(f,s) && !f.limiter,"limiter transition");
  fuel=std::numeric_limits<float>::quiet_NaN(); timeLeft=std::numeric_limits<double>::infinity();
  std::memcpy(data+8,&fuel,4); std::memcpy(data+200,&timeLeft,8); ++h->buffers[1].tick;
  Require(reader.Read(f,s) && !f.fuelKnown && !f.timeLeftKnown,"invalid fuel and clock values remain unavailable instead of becoming an empty tank or expired race");
  fuel=0; timeLeft=-2;
  std::memcpy(data+8,&fuel,4); std::memcpy(data+200,&timeLeft,8); ++h->buffers[1].tick;
  Require(reader.Read(f,s) && f.fuelKnown && f.fuel==0 && f.timeLeftKnown && f.timeLeft==-2,"genuine empty tank and overtime clock remain valid");
  h->status=0; Require(!reader.Read(f,s),"disconnect");
  h->status=1; Require(reader.Read(f,s),"reconnect");
  reader.Close(); h->varsOffset=65530; Require(!reader.Read(f,s),"descriptor bounds");
  h->varsOffset=128; h->buffers[1].offset=65530; Require(!reader.Read(f,s),"buffer bounds");
  h->buffers[1].offset=13000; h->sessionOffset=65530; Require(!reader.Read(f,s),"session bounds");
 }
 UnmapViewOfFile(memory); CloseHandle(map);
}
void WindTests() {
 constexpr double pi=3.141592653589793;
 Frame f=DemoFrame(0); f.windSpeed=4; f.yawNorth=0;
 for(double heading:{0.0,.7,-2.8,2*pi+.5}) for(double source:{0.0,pi/2,pi,3*pi/2,.436332313}) {
  f.yawNorth=heading; f.windDirection=heading+source+4*pi; Near(RelativeWindAngle(f),source,"wind source angle follows heading continuously and wraps full turns");
 }
 // Fixed cases: wind from the east while facing north comes from the right; facing east it is a headwind.
 f.yawNorth=0; f.windDirection=pi/2; Near(RelativeWindAngle(f),pi/2,"east wind facing north comes from the right");
 f.yawNorth=pi/2; Near(RelativeWindAngle(f),0,"east wind facing east is a headwind");
 f.yawNorth=pi; Near(RelativeWindAngle(f),3*pi/2,"east wind facing south comes from the left");
 f.windDirection=std::numeric_limits<double>::quiet_NaN(); Require(!std::isfinite(RelativeWindAngle(f)),"missing wind direction has no arrow");
 f.windDirection=0; f.windSpeed=0; Require(!std::isfinite(RelativeWindAngle(f)),"calm wind has no direction");
 f.windSpeed=4; f.onTrack=false; Require(!std::isfinite(RelativeWindAngle(f)),"spotting does not reuse a heading that stopped changing");
 Near(RelativeWindAngle(f,true),pi,"spotting uses the watched car's heading while it is live");
 f.onTrack=true; f.replay=true; Require(!std::isfinite(RelativeWindAngle(f)),"replay does not reuse a heading that stopped changing");
 Near(RelativeWindAngle(f,true),pi,"replay uses the replayed car's heading while it is live");
 f.replay=false; f.connected=false; Require(!std::isfinite(RelativeWindAngle(f)),"disconnect has no direction");
}
void WeatherHistoryTests() {
 auto s=DemoSession(); auto f=DemoFrame(0); WeatherHistory history;
 f.trackTemperature=26; f.time=0; history.Update(s,f);
 for(int second=10;second<=900;second+=10) { f.time=second; f.trackTemperature=26-second/600.0; f.precipitation=.2; history.Update(s,f); if(second==240) Require(!std::isfinite(history.Change(&WeatherHistory::Sample::track)),"no trend before five minutes"); }
 Require(history.Count()==31,"one sample every 30 seconds");
 Near(history.Change(&WeatherHistory::Sample::track),-1,"ten-minute change"); Near(history.Change(&WeatherHistory::Sample::rain),0,"steady rain");
 f.replay=true; f.time=2000; history.Update(s,f); Require(history.Count()==31,"replays are not sampled");
 f.replay=false; f.time=10; history.Update(s,f); Require(history.Count()==1,"time going back restarts the history");
 for(int second=40;second<=1500;second+=30) { f.time=second; history.Update(s,f); }
 Require(history.Count()==WeatherHistory::Size && history.At(WeatherHistory::Size-1).time==1480,"twenty minutes kept, oldest first");
 // Watching live from outside the car: iRacing sets the replay flag, but the footage is live.
 WeatherHistory spectating; f.onTrack=false; f.replay=true; f.replayFramesBehind=0;
 for(int second=0;second<=60;second+=30) { f.time=second; spectating.Update(s,f); }
 Require(spectating.Count()==3,"live spectating is sampled");
 f.time=90; spectating.Update(s,f); Require(spectating.Count()==4,"spectating keeps sampling");
 const int live=spectating.Count();
 f.time=95; spectating.Update(s,f); Require(spectating.Count()==live,"a paused or short step adds no sample");
 f.replayFramesBehind=600; f.time=10; spectating.Update(s,f); Require(spectating.Count()==live && spectating.At(live-1).time==90,"rewound footage neither samples nor resets");
 f.replayFramesBehind=0; f.time=120; spectating.Update(s,f); Require(spectating.Count()==live+1,"back to live continues the history");
 f.replayFramesBehind=-1; f.replayTime=10; f.replaySession=f.session; f.time=150; spectating.Update(s,f); Require(spectating.Count()==live+1,"replay time far behind is old footage");
 f.replayTime=150; spectating.Update(s,f); Require(spectating.Count()==live+2,"replay time at the session time is live");
 f.replayTime=-1; f.replaySession=-1; f.replay=false; f.onTrack=true; f.session=f.session+1; f.time=10; spectating.Update(s,f); Require(spectating.Count()==1,"a new session restarts the history");
}
int main(int argc,char** argv) {
 if(argc==2 && std::string(argv[1])=="--benchmark-prediction") {
  auto session=DemoSession(); auto frame=DemoFrame(0); session.drivers.clear(); session.number=0;
  frame.pit=false; frame.state=4; frame.timeLeft=7200;
  for(int i=0;i<MaxCars;++i) {
   session.drivers.push_back({i,1,2000,0xffcc44,false,false,"Driver","1","GT3"});
   auto& car=frame.cars[i]; car.completed=20-i/20; car.lap=car.completed+1; car.pct=.9-(i%20)*.03;
   car.last=car.best=130+i*.1; car.estTime=car.pct*car.best; car.position=i+1; car.surface=3; car.pit=false;
  }
  RacePredictor predictor; predictor.Update(session,frame); int crossings=0;
  const auto start=std::chrono::steady_clock::now();
  for(int i=0;i<1000;++i) crossings+=predictor.Predict(session,frame).finish.ownCrossings;
  const auto micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/1000;
  std::cout<<"128-car finish forecast: "<<micros<<" microseconds per update; checksum "<<crossings<<'\n'; return 0;
 }
 if(argc==2 && std::string(argv[1])=="--benchmark-telemetry") {
  // iRacing publishes roughly 300 variables with 64-entry CarIdx arrays.
  const std::wstring name=L"Local\\RaceMate.Benchmark."+std::to_wstring(GetCurrentProcessId());
  HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,1<<20,name.c_str());
  auto* memory=static_cast<unsigned char*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,0));
  auto* h=reinterpret_cast<MemoryHeader*>(memory); *h={}; h->version=2; h->status=1; h->numBuffers=1; h->varsOffset=256;
  h->buffers[0]={1,200000,{0,0}}; h->sessionOffset=400000; h->sessionLength=static_cast<int>(strlen(Yaml)+1); h->sessionUpdate=1; std::memcpy(memory+h->sessionOffset,Yaml,h->sessionLength);
  int offset=0;
  auto add=[&](const std::string& variable,int type,int count) {
   auto* v=reinterpret_cast<VariableHeader*>(memory+h->varsOffset)+h->numVars++; *v={}; v->type=type; v->offset=offset; v->count=count; strcpy_s(v->name,variable.c_str());
   offset+=(type==5?8:4)*count;
  };
  for(const char* array:{"CarIdxBestLapTime","CarIdxClassPosition","CarIdxEstTime","CarIdxLap","CarIdxLapCompleted","CarIdxLapDistPct","CarIdxLastLapTime","CarIdxOnPitRoad","CarIdxPosition","CarIdxSessionFlags","CarIdxTireCompound","CarIdxTrackSurface","CarIdxF2Time"}) add(array,4,64);
  for(const char* scalar:{"AirTemp","CamCarIdx","CamGroupNumber","EngineWarnings","FuelLevel","IsOnTrack","IsReplayPlaying","LapBestLapTime","LapCompleted","LapDistPct","LapLastLapTime","OnPitRoad","PlayerCarIdx","PlayerCarInPitStall","PlayerCarIsTowing","PlayerCarMyIncidentCount","PlayerCarTowTime","PlayerTrackSurface","ReplaySessionNum","ReplaySessionTime","SessionFlags","SessionLapsRemain","SessionLapsRemainEx","SessionNum","SessionState","SessionTime","SessionTimeOfDay","SessionTimeRemain","Speed","TrackTemp","TrackTempCrew","TrackWetness","WindDir","WindVel","YawNorth"}) add(scalar,4,1);
  for(int i=0;h->numVars<300;++i) add("Unused"+std::to_string(i),4,1);
  h->bufferLength=offset;
  Telemetry reader(name.c_str()); Frame frame; Session session; reader.Read(frame,session);
  constexpr int iterations=20000; int checksum=0;
  const auto start=std::chrono::steady_clock::now();
  for(int i=0;i<iterations;++i) { ++h->buffers[0].tick; reader.Read(frame,session); checksum+=frame.cars[3].position; }
  const auto micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/iterations;
  std::cout<<"Telemetry read with 300 variables: "<<micros<<" microseconds per frame; checksum "<<checksum<<'\n';
  UnmapViewOfFile(memory); CloseHandle(map); return 0;
 }
 if(argc==2 && std::string(argv[1])=="--live-prediction") {
  Telemetry reader; Frame frame; Session session;
  if(!reader.Read(frame,session)) { std::cout<<"No connected iRacing telemetry available.\n"; return 2; }
  RacePredictor predictor; predictor.Update(session,frame); const auto forecast=predictor.Predict(session,frame);
  std::cout<<"Session: "<<session.type<<"; state: "<<frame.state<<"; time left: "<<frame.timeLeft<<" s; lap limit: "<<session.totalLaps
   <<"; forecast status: "<<static_cast<int>(forecast.status)<<"; leader finish in: "<<forecast.finish.leaderFinishIn
   <<" s; own distance: "<<forecast.finish.ownDistance<<" laps; own crossings: "<<forecast.finish.ownCrossings<<'\n'; return 0;
 }
 if(argc==2 && std::string(argv[1])=="--dump-vars") {
  // Every telemetry variable of a running session with its current value, plus the session YAML, to check SDK names.
  HANDLE map=OpenFileMappingW(FILE_MAP_READ,FALSE,L"Local\\IRSDKMemMapFileName");
  const auto* memory=map?static_cast<const unsigned char*>(MapViewOfFile(map,FILE_MAP_READ,0,0,0)):nullptr;
  if(!memory) { std::cout<<"No iRacing shared memory; start a session first.\n"; if(map) CloseHandle(map); return 2; }
  const auto* h=reinterpret_cast<const MemoryHeader*>(memory);
  const auto* vars=reinterpret_cast<const VariableHeader*>(memory+h->varsOffset);
  int latest=0; for(int i=1;i<h->numBuffers && i<4;++i) if(h->buffers[i].tick>h->buffers[latest].tick) latest=i;
  const unsigned char* data=memory+h->buffers[latest].offset;
  std::ofstream out("irsdk-dump.txt");
  const std::string yaml(reinterpret_cast<const char*>(memory+h->sessionOffset),strnlen(reinterpret_cast<const char*>(memory+h->sessionOffset),h->sessionLength));
  out<<"Wanted by the overlay redesign:\n";
  for(const char* name:{"WeatherDeclaredWet","Precipitation","RelativeHumidity","FogLevel","Skies","dcBrakeBias","TrackWetness","AirPressure","SolarAltitude",
      "PlayerCarDriverIncidentCount","PlayerCarTeamIncidentCount","SessionTimeTotal","SessionLapsTotal","Yaw","YawNorth","WindDir","WindVel"}) {
   bool found=false; for(int i=0;i<h->numVars && !found;++i) found=std::strcmp(vars[i].name,name)==0;
   out<<"  "<<(found?"found   ":"MISSING ")<<name<<'\n';
  }
  for(const char* key:{"FlairName:","FlairID:","CountryCode:","IncidentLimit:","IncidentWarning","CarScreenName:","CarPath:"})
   out<<"  YAML "<<(yaml.find(key)!=std::string::npos?"found   ":"MISSING ")<<key<<'\n';
  const char* types[]={"char","bool","int","bitfield","float","double"};
  out<<"\nVariables ("<<h->numVars<<"): name, type[count], unit, value of entry 0, description\n";
  for(int i=0;i<h->numVars;++i) {
   const auto& v=vars[i]; const unsigned char* p=data+v.offset; double value=0;
   switch(v.type) { case 0: case 1: value=*p; break; case 2: case 3: value=*reinterpret_cast<const int*>(p); break; case 4: value=*reinterpret_cast<const float*>(p); break; case 5: value=*reinterpret_cast<const double*>(p); break; }
   out<<"  "<<v.name<<"  "<<(v.type>=0 && v.type<6?types[v.type]:"?")<<'['<<v.count<<"]  "<<v.unit<<"  "<<value<<"  "<<v.description<<'\n';
  }
  out<<"\nSession YAML:\n"<<yaml;
  UnmapViewOfFile(memory); CloseHandle(map);
  std::cout<<"Wrote irsdk-dump.txt ("<<h->numVars<<" variables).\n"; return 0;
 }
 if(argc==2 && std::string(argv[1])=="--live-summary") {
  Telemetry reader; Frame frame; Session session;
  if(!reader.Read(frame,session)) { std::cout<<"No connected iRacing telemetry available.\n"; return 2; }
  int liveBest=0,historicalBest=0,historicalLast=0,missingBest=0;
  for(const auto& driver:session.drivers) {
   liveBest+=frame.cars[driver.index].best>0;
   historicalBest+=session.results[driver.index].best>0;
   historicalLast+=session.results[driver.index].last>0;
  }
  const auto rows=Standings(session,frame,MaxCars,false);
  for(const auto& row:rows) missingBest+=row.car.best<=0;
  std::cout<<"Session: "<<session.type<<"; drivers: "<<session.drivers.size()<<"; standings rows: "<<rows.size()
   <<"; live bests: "<<liveBest<<"; historical bests: "<<historicalBest<<"; historical lasts: "<<historicalLast
   <<"; missing merged bests: "<<missingBest<<'\n';
  // What View() decides from; compare in the car, spectating live, a playing and a paused replay.
  const char* views[]={"in car","live","replay"};
  std::cout<<"View: "<<views[static_cast<int>(View(frame))]<<"; IsOnTrack: "<<frame.onTrack<<"; IsReplayPlaying: "<<frame.replay
   <<"; ReplayFrameNumEnd: "<<frame.replayFramesBehind<<"; SessionTime: "<<frame.time<<"; ReplaySessionTime: "<<frame.replayTime
   <<"; SessionNum: "<<frame.session<<"; ReplaySessionNum: "<<frame.replaySession<<'\n';
  return 0;
 }
 try { ReviewRegressionTests(); WindTests(); WeatherHistoryTests(); ParserTests(); ResultsTests(); RowTests(); RelativeCenterTests(); StandingsSelectionTests(); EarlyAndLiveGapTests(); UntimedPracticeTests(); MissingDriverDataTests(); LivePositionAndPitVisibilityTests(); FuelMetricsTests(); FirstRaceLapFuelTests(); FuelTests(); PredictionTests(); RaceControlTests(); PitStopTests(); PreviewRaceTests(); TelemetryTests(); std::cout<<"PASS: review regressions, parser, historical results, relative, standings, fuel metrics, finish projection, leader changes, pit learning, confidence, replay and shared memory\n"; }
 catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
