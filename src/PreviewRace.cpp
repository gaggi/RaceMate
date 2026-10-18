#include "PreviewRace.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace rm {
namespace {
constexpr double TrackMeters=2470, PitSpeed=16.7, FuelPerSecond=3, TireChange=20;
constexpr double LaneIn=.94, Stall=.985, LaneOut=.05; // Lap fractions of the pit lane at Lime Rock's start/finish.
constexpr double Tau=6.283185307179586;
constexpr const char* Names[]={"Luca Weber","Daniel Fischer","Alex Martin","Sofia Laurent","Ben Clarke","Marco Rossi","Thomas Meyer","Emma Wilson",
    "Nico Berg","Jonas Keller","Lena Hoffmann","Mateo Garcia","Olivia Brown","Lucas Martin","Chloe Dubois","Finn Larsen","Mia Novak",
    "Hugo Laurent","Noah Schmidt","Ella Virtanen","Leo Rossi","Ava Kowalski","Max Bauer","Zoe Andersen","Felix Wagner","Ivy Chen",
    "Oscar Lindqvist","Nina Petrova","Theo Moreau","Sara Jensen","Paul Richter","Elena Costa","Jan de Vries","Lara Fischer","Kai Nakamura",
    "Ruby Taylor","Tim Schulz","Anna Novak","Liam O'Connor","Maya Patel"};
constexpr const char* Gt3[]={"porsche992rgt3","bmwm4gt3","ferrari296gt3","mclaren720sgt3","mercedesamgevogt3","audir8lmsevo2gt3",
    "fordmustanggt3","lamborghinievogt3","astonmartinvantagegt3"};
constexpr const char* Gt4[]={"bmwm4gt4","porsche718gt4","mclaren570sgt4","mercedesamggt4","astonmartinvantagegt4","toyotagrsupragt4"};
constexpr const char* Usage[]={"clean","slightly used","moderately low usage","moderately used","well used"};
}
// The same value for the same car, lap and purpose in every run of this race: lap times do not jump between frames.
double PreviewRace::Noise(int a,int b,int c) const {
    uint32_t h=seed_^static_cast<uint32_t>(a*73856093)^static_cast<uint32_t>(b*19349663)^static_cast<uint32_t>(c*83492791);
    h^=h>>16; h*=0x7feb352dU; h^=h>>15; h*=0x846ca68bU; h^=h>>16;
    return h/2147483647.5-1;
}
void PreviewRace::Start(uint32_t seed) {
    seed_=seed; random_.seed(seed);
    auto uniform=[&](double low,double high){ return std::uniform_real_distribution<double>(low,high)(random_); };
    auto whole=[&](int low,int high){ return std::uniform_int_distribution<int>(low,high)(random_); };
    session_={}; frame_={}; runners_.clear();
    auto& s=session_;
    s.track="Lime Rock Park"; s.trackName="limerock 2019 gp"; s.trackConfig="Grand Prix"; s.trackId=353; s.trackMeters=TrackMeters;
    s.type="Race"; s.key="preview "+std::to_string(seed); s.number=0; s.fuelCapacity=capacity_; s.ruleSet="None";
    s.trackUsage=Usage[whole(0,4)]; s.incidentLimit=25; s.incidentWarning=17;
    s.cameras={{1,"Scenic",true},{7,"Cockpit"},{11,"Far Chase"},{12,"TV1"}};
    // The field: names, numbers and cars drawn without repeats, ratings and licenses that go together.
    std::vector<int> names(std::size(Names)); std::iota(names.begin(),names.end(),0); std::shuffle(names.begin(),names.end(),random_);
    std::vector<int> numbers(98); std::iota(numbers.begin(),numbers.end(),2); std::shuffle(numbers.begin(),numbers.end(),random_);
    const int gt3=whole(9,12), gt4=whole(6,8), count=gt3+gt4;
    std::normal_distribution<double> rating(2300,850), spread(0,1);
    for(int i=0;i<count;++i) {
        const bool fast=i<gt3; Driver d; d.index=i; d.classId=fast?1:2; d.className=fast?"GT3":"GT4"; d.classColor=fast?0xe7c451:0x67b7ff;
        d.name=Names[names[i]]; d.number=std::to_string(numbers[i]);
        d.carPath=fast?Gt3[whole(0,static_cast<int>(std::size(Gt3))-1)]:Gt4[whole(0,static_cast<int>(std::size(Gt4))-1)]; d.brand=CarBrand(d.carPath,"");
        d.rating=static_cast<int>(std::clamp(rating(random_),650.0,6500.0));
        const char letter=d.rating>3600?'A':(d.rating>2200?(whole(0,3)?'A':'B'):(d.rating>1400?(whole(0,2)?'B':'C'):(whole(0,1)?'C':'D')));
        char license[16]; std::snprintf(license,sizeof(license),"%c %.2f",letter,uniform(letter=='D'?1.0:2.0,4.99)); d.license=license;
        d.incidentCount=0; d.estimatedLapTime=fast?53.6:57.8; s.drivers.push_back(d); // The class reference lap, as in CarClassEstLapTime.
        Runner r; r.index=i;
        // Better drivers are a little faster; GT4 laps Lime Rock about four seconds slower.
        r.pace=(fast?53.6:57.8)*(1+(2300.0-d.rating)/2300*.012+spread(random_)*.004);
        r.burn=(fast?2.42:2.02)*(1+spread(random_)*.03); r.fuel=capacity_;
        // One stop for fuel and tires somewhere a full tank allows.
        const double laps=Length/r.pace+1, range=capacity_/r.burn;
        r.stopLap=std::clamp(whole(static_cast<int>(laps-range)+3,static_cast<int>(range)-2),2,static_cast<int>(laps)-2);
        runners_.push_back(r);
    }
    // You drive a GT3 from the middle of its class.
    std::vector<int> byRating(gt3); std::iota(byRating.begin(),byRating.end(),0);
    std::sort(byRating.begin(),byRating.end(),[&](int a,int b){ return s.drivers[a].rating>s.drivers[b].rating; });
    player_=byRating[whole(2,gt3-3)]; s.player=player_;
    // The grid: GT3 by rating, then GT4, a car length and a half apart behind the line.
    std::vector<int> grid(count); std::iota(grid.begin(),grid.end(),0);
    std::stable_sort(grid.begin(),grid.end(),[&](int a,int b){ const auto& x=s.drivers[a]; const auto& y=s.drivers[b]; return x.classId!=y.classId?x.classId<y.classId:x.rating>y.rating; });
    for(int i=0;i<count;++i) { auto& r=runners_[grid[i]]; r.progress=-.004-i*.0035; r.lapStart=0; s.gridPositions[grid[i]]=i+1; }
    // Weather: mostly dry, sometimes light rain; it drifts a little over the race.
    wet_=uniform(0,1)<.3;
    if(wet_) { air_=uniform(12,19); track_=air_+uniform(1,5); rain_=uniform(.15,.45); wetness_=whole(3,4); skies_=3; humidity_=uniform(.85,.97); }
    else { air_=uniform(16,29); track_=air_+uniform(6,16); rain_=0; wetness_=1; skies_=whole(0,2); humidity_=uniform(.35,.7); }
    windSpeed_=uniform(.5,6); windDirection_=uniform(0,Tau);
    time_=-3; showFrom_=uniform(14*60,30*60);
    Publish();
}
void PreviewRace::Advance(double seconds) {
    while(seconds>1e-9) { const double dt=std::min(.5,seconds); Step(dt); seconds-=dt; }
    Publish();
}
void PreviewRace::Step(double dt) {
    time_+=dt;
    if(time_<0) return; // On the grid until the green flag.
    auto uniform=[&](double low,double high){ return std::uniform_real_distribution<double>(low,high)(random_); };
    for(auto& r:runners_) {
        auto& driver=session_.drivers[r.index];
        const double before=r.progress; const int completed=static_cast<int>(std::floor(r.progress)); const double pct=r.progress-completed;
        if(r.phase==Runner::Track) {
            const double lapPace=r.pace*(1+.006*Noise(r.index,completed,1));
            const bool off=time_<r.offUntil;
            r.progress+=dt/lapPace*(off?.3:1);
            // Now and then a wheel too far: a second and a half off the track and an incident point.
            if(!off && r.progress>0 && uniform(0,1)<dt/lapPace*.025) { r.offUntil=time_+1.5; ++driver.incidentCount; }
            if(!r.stopped && completed==r.stopLap-1 && pct>=LaneIn) {
                // How much to take on: the rest of the race at its consumption, plus a margin that may also fall short.
                const double lapsLeft=(Length-time_)/r.pace+1.3+(1-pct);
                const double margin=r.index==player_?uniform(-1.2,4):uniform(.5,4);
                r.refuelTo=std::clamp(lapsLeft*r.burn+margin,r.fuel,capacity_); r.phase=Runner::Lane;
            }
        } else if(r.phase==Runner::Lane) {
            r.progress+=dt*PitSpeed/TrackMeters;
            if(r.progress>=r.stopLap-1+Stall) { r.progress=r.stopLap-1+Stall; r.phase=Runner::Stall; r.stall=(r.refuelTo-r.fuel)/FuelPerSecond+TireChange; r.stopped=true; }
        } else if(r.phase==Runner::Stall) {
            r.stall-=dt; r.fuel=std::min(r.refuelTo,r.fuel+FuelPerSecond*dt);
            if(r.stall<=0) r.phase=Runner::Exit;
        } else {
            r.progress+=dt*PitSpeed/TrackMeters;
            if(r.progress>=r.stopLap+LaneOut) { r.phase=Runner::Track; r.outLap=true; r.outCompleted=r.stopLap; }
        }
        r.fuel=std::max(0.0,r.fuel-(r.progress-before)*r.burn);
        // A lap completed: its time, the best of the clean ones, and the out lap ends.
        if(r.progress>=1 && std::floor(r.progress)>std::floor(before)) {
            // The moment of the crossing within the step, so lap times keep their milliseconds.
            const double line=std::floor(r.progress), crossed=time_-dt*(r.progress-line)/std::max(1e-9,r.progress-before);
            r.last=crossed-r.lapStart; r.lapStart=crossed;
            if(r.last<r.pace*1.1 && (r.best<0 || r.last<r.best)) r.best=r.last;
            if(r.outLap && std::floor(r.progress)>r.outCompleted) r.outLap=false;
        }
    }
}
void PreviewRace::Publish() {
    Frame f; auto& s=session_;
    f.connected=f.onTrack=true; f.player=player_; f.camera=player_; f.session=0; f.state=time_<0?3:4;
    f.time=time_; f.timeLeft=std::max(0.0,Length-time_); f.timeTotal=Length; f.timeLeftKnown=true; f.lapsLeft=32767; f.lapsTotal=32767;
    std::vector<int> order(runners_.size()); std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){ return runners_[a].progress>runners_[b].progress; });
    const auto& leader=runners_[order.front()];
    int classPositions[3]{};
    for(int place=0;place<static_cast<int>(order.size());++place) {
        const auto& r=runners_[order[place]]; auto& c=f.cars[r.index]; const auto& d=s.drivers[r.index];
        c.position=place+1; c.classPosition=++classPositions[std::clamp(d.classId,0,2)];
        c.completed=static_cast<int>(std::floor(r.progress)); c.lap=c.completed+1; c.pct=r.progress-c.completed;
        c.pit=r.phase!=Runner::Track; c.outLap=r.outLap; c.surface=r.phase==Runner::Stall?1:(time_<r.offUntil?0:3);
        // CarIdxEstTime: the class reference time to this point of the lap.
        c.estTime=c.pct*d.estimatedLapTime; c.f2Time=(leader.progress-r.progress)*leader.pace; c.best=r.best; c.last=r.last;
        c.speed=r.phase==Runner::Stall?0:(c.pit?PitSpeed:(c.surface==0?12:TrackMeters/r.pace));
        c.tireCompound=wet_ && rain_>.3?1:0;
    }
    const auto& me=runners_[player_]; const auto& car=f.cars[player_];
    f.lapCompleted=car.completed; f.lapPct=car.pct; f.fuel=me.fuel; f.fuelKnown=true; f.lastLap=me.last; f.bestLap=me.best;
    f.pit=car.pit; f.inStall=me.phase==Runner::Stall; f.limiterKnown=true; f.limiter=car.pit;
    f.incidents=s.drivers[player_].incidentCount; f.incidentsKnown=true;
    // Weather drifts slowly; the car's heading turns once a lap around the circuit.
    f.airTemperature=air_+.6*std::sin(time_/900); f.trackTemperature=track_+(wet_?-.8:1.2)*std::sin(time_/1300);
    f.precipitation=rain_*(1+.3*std::sin(time_/600)); f.trackWetness=wetness_; f.skies=skies_; f.humidity=humidity_; f.fog=0; f.declaredWet=wet_;
    f.windSpeed=windSpeed_*(1+.2*std::sin(time_/120)); f.windDirection=std::fmod(windDirection_+.15*std::sin(time_/400)+Tau,Tau);
    f.yawNorth=std::fmod(car.pct*Tau,Tau); f.timeOfDay=14*3600+time_;
    frame_=f;
}
}
