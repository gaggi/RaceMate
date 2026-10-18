#include "PreviewRace.h"
#include <algorithm>
#include <cmath>
#include <set>

using namespace rm;
void Require(bool, const char*);

// The preview's race is one consistent session: positions follow progress, gaps follow positions, fuel and lap
// times are plausible, every car stops once for fuel, and a seed always gives the same race.
void PreviewRaceTests() {
    for(uint32_t seed:{1u,42u,20261018u}) {
        PreviewRace race; race.Start(seed);
        const auto& s=race.CurrentSession();
        Require(s.type=="Race" && s.trackId==353 && s.drivers.size()>=15 && s.drivers.size()<=20 && s.player>=0,"a race at Lime Rock with a full field");
        Require(race.ShowFrom()>=14*60 && race.ShowFrom()<=30*60,"shown from the middle of the race");
        std::set<std::string> names,numbers; for(const auto& d:s.drivers) { names.insert(d.name); numbers.insert(d.number); }
        Require(names.size()==s.drivers.size() && numbers.size()==s.drivers.size(),"names and numbers are unique");
        Require(s.drivers[s.player].classId==1,"you drive a GT3");
        double lowestFuel=1e9, highestFuel=-1; std::set<int> stalled;
        while(race.Time()<race.ShowFrom()) {
            race.Advance(.5); const auto& f=race.CurrentFrame();
            lowestFuel=std::min(lowestFuel,f.fuel); highestFuel=std::max(highestFuel,f.fuel);
            for(int i=0;i<static_cast<int>(s.drivers.size());++i) if(f.cars[i].surface==1) stalled.insert(i);
        }
        const auto& f=race.CurrentFrame(); const int n=static_cast<int>(s.drivers.size());
        std::vector<const Car*> byPosition(n);
        for(int i=0;i<n;++i) { const auto& c=f.cars[i]; Require(c.position>=1 && c.position<=n && !byPosition[c.position-1],"positions 1 to n, once each"); byPosition[c.position-1]=&c; }
        for(int p=1;p<n;++p) {
            const auto& ahead=*byPosition[p-1]; const auto& car=*byPosition[p];
            Require(ahead.completed+ahead.pct>=car.completed+car.pct-1e-9,"positions follow the distance covered");
            Require(car.f2Time>=ahead.f2Time-1e-9,"gaps grow down the order");
        }
        Require(byPosition[0]->f2Time==0,"the leader has no gap");
        for(int classId:{1,2}) {
            std::vector<int> positions;
            for(int i=0;i<n;++i) if(s.drivers[i].classId==classId) positions.push_back(f.cars[i].classPosition);
            std::sort(positions.begin(),positions.end());
            for(int i=0;i<static_cast<int>(positions.size());++i) Require(positions[i]==i+1,"class positions 1 to n within each class");
        }
        for(int i=0;i<n;++i) {
            const auto& c=f.cars[i]; const double base=s.drivers[i].classId==1?53.6:57.8;
            Require(c.best>base*.95 && c.best<base*1.06 && c.last>base*.9,"lap times fit the class at Lime Rock");
        }
        Require(lowestFuel>=0 && highestFuel<=s.fuelCapacity+1e-9 && f.fuel>0,"fuel stays within the tank and never runs dry");
        Require(f.timeLeft>0 && std::abs(f.timeLeft+f.time-PreviewRace::Length)<1e-6,"the clock and time left add up");
        // To the flag: everyone has stopped once, and the same seed runs the same race.
        PreviewRace again; again.Start(seed); again.Advance(race.Time()+3);
        Require(again.CurrentFrame().cars[s.player].pct==f.cars[s.player].pct || std::abs(again.CurrentFrame().cars[s.player].pct-f.cars[s.player].pct)<1e-9,"a seed gives the same race");

        while(race.Time()<PreviewRace::Length) {
            race.Advance(.5); const auto& now=race.CurrentFrame();
            for(int i=0;i<n;++i) if(now.cars[i].surface==1) stalled.insert(i);
            Require(now.fuel>=0 && now.fuel<=s.fuelCapacity+1e-9,"fuel within the tank to the end");
        }
        Require(static_cast<int>(stalled.size())==n,"every car stops in its stall once during the race");
    }
    PreviewRace a,b; a.Start(1); b.Start(2);
    Require(a.CurrentSession().drivers.front().name!=b.CurrentSession().drivers.front().name || a.CurrentSession().drivers.size()!=b.CurrentSession().drivers.size(),"another seed gives another race");
}
