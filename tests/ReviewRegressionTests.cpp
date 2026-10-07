#include "Core.h"
#include "RaceControl.h"
#include <algorithm>
#include <array>
#include <limits>

using namespace rm;
// Pin the named flags to the published SDK bit values (irsdk_Flags).
static_assert(SessionFlag::Yellow==0x8u && SessionFlag::Red==0x10u && SessionFlag::YellowWaving==0x100u);
static_assert(SessionFlag::Caution==0x4000u && SessionFlag::CautionWaving==0x8000u && SessionFlag::Furled==0x80000u);
void Require(bool, const char*);

void ReviewRegressionTests() {
    const auto malformed = ParseSession(
        "SessionInfo:\n Sessions:\n - SessionNum: invalid\n   SessionType: Race\n"
        "DriverInfo:\n DriverCarIdx: invalid\n Drivers:\n"
        " - CarIdx: invalid\n   UserName: Invalid\n"
        " - CarIdx: 1.5\n   UserName: Fractional\n"
        " - CarIdx: 2\n   UserName: Valid\n   IRating: nan\n"
        "   CurDriverIncidentCount: invalid\n", 0);
    Require(malformed.player == -1 && malformed.type.empty(), "invalid metadata must not select session/car zero");
    Require(malformed.drivers.size() == 1 && malformed.drivers[0].index == 2,
        "invalid and fractional car indices must not become real drivers");
    Require(malformed.drivers[0].rating == 0 && malformed.drivers[0].incidentCount == -1,
        "invalid numeric metadata retains missing-value defaults");

    Session session; session.type = "Race"; session.number = 0; session.key = "review";
    Frame frame; frame.connected = frame.onTrack = true; frame.session = 0; frame.state = 4;
    for (int i = 0; i < 3; ++i) {
        Driver driver; driver.index = i; driver.number = std::to_string(3 - i);
        session.drivers.push_back(driver);
        auto& car = frame.cars[i]; car.surface = 3; car.completed = 2; car.lap = 3; car.pct = .8 - i * .3;
    }
    frame.cars[1].pct = -1;
    const auto drivers = session.drivers;
    std::array<int, 3> order{0, 1, 2};
    do {
        session.drivers.clear();
        for (int i : order) session.drivers.push_back(drivers[i]);
        const auto rows = Standings(session, frame, MaxCars, false);
        Require(rows.size() == 3 && rows[0].driver.index == 0 && rows[1].driver.index == 1 && rows[2].driver.index == 2,
            "missing race progress has deterministic ordering for every input permutation");
    } while (std::next_permutation(order.begin(), order.end()));

    session.drivers = drivers; frame.player = 0; frame.time = 100;
    EventRecorder recorder; recorder.Update(session, frame);
    frame.time += 1; frame.incidentsKnown = true; frame.incidents = 5; recorder.Update(session, frame);
    Require(recorder.Events().empty(), "first available incident counter is a baseline, not five new incidents");
    frame.time += 1; frame.incidents = 6; recorder.Update(session, frame);
    Require(recorder.Events().size() == 1 && recorder.Events().back().detail == "+1 incident point(s)",
        "known incident counter deltas are still recorded");
    frame.time += 1; frame.incidentsKnown = false; frame.incidents = 0; recorder.Update(session, frame);
    frame.time += 1; frame.incidentsKnown = true; frame.incidents = 6; recorder.Update(session, frame);
    Require(recorder.Events().size() == 1, "incident telemetry recovery must not duplicate old incidents");

    for(unsigned yellow : {SessionFlag::Yellow, SessionFlag::YellowWaving, SessionFlag::Caution, SessionFlag::CautionWaving, SessionFlag::Red}) for(int mode : {-1, 0, 1}) {
        FuelCalculator fuel;
        auto sample=frame; sample.fuelKnown=true; sample.fuel=50; sample.lapCompleted=0; sample.flags=0;
        auto update=[&] {
            if(mode<0) fuel.Update(session,sample); // Default behavior also includes yellow laps.
            else fuel.Update(session,sample,mode==1);
        };
        update();
        sample.time+=100; sample.lapCompleted=1; sample.fuel=47; update();
        sample.time+=50; sample.flags=yellow; update();
        sample.time+=50; sample.flags=0; sample.lapCompleted=2; sample.fuel=44; update();
        Require(fuel.Plan(sample,0,false).samples==(mode==1?0:1),
            "yellow laps are included by default and excluded only when the option is enabled");
        sample.time+=100; sample.lapCompleted=3; sample.fuel=41; update();
        const auto plan=fuel.Plan(sample,0,false);
        Require(plan.samples==(mode==1?1:2) && plan.average==3,
            "green laps resume normal learning after an optional yellow exclusion");
        sample.time+=50; sample.pit=true; update();
        sample.time+=50; sample.pit=false; sample.lapCompleted=4; sample.fuel=38; update();
        Require(fuel.Plan(sample,0,false).samples==plan.samples,
            "pit laps remain excluded regardless of the yellow flag option");
    }
}
