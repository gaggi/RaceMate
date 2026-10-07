#include "RaceControl.h"
#include "Telemetry.h"
#include <algorithm>
using namespace rm;
void Require(bool,const char*);
namespace {
WPARAM receivedCommand=0; LPARAM receivedData=0;
LRESULT CALLBACK MessageReceiver(HWND w,UINT message,WPARAM wp,LPARAM lp) {
    if(message==RegisterWindowMessageW(L"IRSDK_BROADCASTMSG")) { receivedCommand=wp; receivedData=lp; return 0; }
    return DefWindowProcW(w,message,wp,lp);
}

Session TestSession() {
    Session s; s.type="Race"; s.number=0; s.key="event-session"; s.trackMeters=5000;
    s.drivers.push_back({0,1,2400,0xffcc44,false,false,"One","007","GT3"});
    s.drivers.push_back({1,1,2200,0xffcc44,false,false,"Two","12","GT3"});
    s.drivers[0].incidentCount=s.drivers[1].incidentCount=0; return s;
}
Frame TestFrame() {
    Frame f; f.connected=true; f.onTrack=true; f.session=0; f.player=1; f.state=4; f.time=100;
    for(int i=0;i<2;++i) { auto& c=f.cars[i]; c.surface=3; c.completed=3; c.lap=4; c.pct=.5-i*.005; c.position=i+1; }
    return f;
}
int Count(const EventRecorder& recorder,EventType type) {
    return static_cast<int>(std::count_if(recorder.Events().begin(),recorder.Events().end(),[&](const RaceEvent& e){return e.type==type;}));
}
}
void RaceControlTests() {
    Require(DriverFlags(0).empty() && DriverFlags(0x40000u|0x8u).empty(),"service eligibility and global yellow are not penalties");
    Require(DriverFlags(0x10000u)=="BLACK" && DriverFlags(0x80000u)=="SLOW" && DriverFlags(0x100000u)=="REPAIR","driver penalty bit definitions");
    Require(DriverFlags(0x190000u)=="BLACK SLOW REPAIR","simultaneous driver flags remain visible");
    Require(DriverFlags(0x190000u,false,true,false)=="SLOW" && DriverFlags(0x30000u)=="DQ","independent flag selection and disqualification");
    auto s=TestSession(); auto f=TestFrame(); EventRecorder events;
    events.Update(s,f); Require(events.Events().empty(),"joining does not manufacture earlier events");
    f.time+=.1; f.cars[0].surface=0; s.drivers[0].incidentCount=1; events.Update(s,f);
    Require(Count(events,EventType::Offtrack)==1 && Count(events,EventType::Incident)==1,"offtrack transition and counter delta captured for opponent");
    events.Update(s,f); f.time+=.1; events.Update(s,f); Require(events.Events().size()==2,"paused and sustained offtrack are deduplicated");
    f.time+=.1; f.cars[0].sessionFlags=0x100000; events.Update(s,f);
    Require(Count(events,EventType::Crash)==1 && events.Events().back().estimated,"repair flag is explicitly a possible crash");
    f.time+=.1; f.cars[0].surface=3; f.cars[0].pit=true; events.Update(s,f);
    const double entry=f.time;
    for(int i=0;i<5;++i) { f.time+=1; events.Update(s,f); }
    f.time+=1; f.cars[0].pit=false; f.cars[0].lastPitStationary=3; events.Update(s,f);
    Require(events.Events().back().detail.find("stationary: ~3 s")!=std::string::npos,"completed event includes independently observed stationary time");
    Require(Count(events,EventType::PitEntry)==1 && Count(events,EventType::PitStop)==1,"completed pit visit recorded");
    Require(events.Events().back().time==entry && events.Events().back().lap==4,"pit replay anchors to entry and entry lap");
    auto replay=f; replay.onTrack=false; replay.replayTime=90; replay.replaySession=0; replay.cars[0].surface=0; events.Update(s,replay);
    Require(Count(events,EventType::Offtrack)==1,"reviewing old replay does not duplicate event feed");
    f.time-=50; f.cars[0].surface=0; events.Update(s,f); Require(Count(events,EventType::Offtrack)==1,"rewind remains below recording frontier");
    events.Reset(); f=TestFrame(); f.cars[0].pit=true; events.Update(s,f); f.time+=1; f.cars[0].pit=false; events.Update(s,f);
    Require(Count(events,EventType::PitStop)==0,"incomplete late-join pit visit is not invented");
    events.Reset(); f=TestFrame(); events.Update(s,f);
    f.time+=.1; f.cars[0].position=2; f.cars[1].position=1; events.Update(s,f);
    for(int i=0;i<3;++i) { f.time+=1; events.Update(s,f); }
    Require(Count(events,EventType::Overtake)==1 && events.Events().back().car==1 && events.Events().back().estimated,"stable adjacent race pass captured once");
    events.Reset(); f=TestFrame(); events.Update(s,f); f.time+=.1; f.cars[0].pit=true; f.cars[0].position=2; f.cars[1].position=1; events.Update(s,f);
    for(int i=0;i<3;++i) { f.time+=1; events.Update(s,f); } Require(Count(events,EventType::Overtake)==0,"pit position swaps are not track overtakes");
    s.key="next-session"; events.Update(s,f); Require(events.Events().empty(),"new session resets event history");
    Require(CameraNumber("7")==7 && CameraNumber("007")==3007 && CameraNumber("00")==2000 && CameraNumber("abc")<0,"camera numbers preserve leading zero identities");
    const auto seek=ReplayMessage(3,3723.5,10); Require(seek.command==12 && seek.arg==3 && seek.data==3713500,"long replay timestamp retains high bits");
    Require(ReplayMessage(0,2,5).data==0,"replay lead clamps to session start");
    const auto camera=CameraMessage("007",8); Require(camera.command==1 && camera.arg==3007 && camera.data==8,"camera targets car number and current group");
    WNDCLASSW cls{}; cls.lpfnWndProc=MessageReceiver; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"RaceMate.ProtocolTest";
    RegisterClassW(&cls); HWND receiver=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
    Require(receiver!=nullptr,"create private protocol test receiver");
    Require(SendIRacingMessage(seek,receiver) && LOWORD(receivedCommand)==12 && HIWORD(receivedCommand)==3 && static_cast<unsigned>(receivedData)==3713500,"Windows broadcast encoding preserves complete replay timestamp");
    Require(SendIRacingMessage(camera,receiver) && HIWORD(receivedCommand)==3007 && receivedData==8,"Windows camera encoding preserves padded car number");
    DestroyWindow(receiver);

}
