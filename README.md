# RaceMate 0.1.0

Lightweight native overlays for iRacing on Windows 10/11. C++20, Win32 and GDI: one executable with a statically linked runtime, no browser engine, no network access, read-only access to iRacing's shared memory.

## Getting started

1. Start `dist/x64/RaceMate-0.1.0-windows-x64.exe`. Run iRacing in borderless windowed mode.
2. Click **Preview** in the banner at the top to see every overlay with sample data.
3. Turn on **Move overlays** (Overlays page) and drag each overlay where you want it, then click **Done**.
4. Stop the preview. RaceMate connects to iRacing on its own; the banner shows the track and session.

The window has three pages in its sidebar: **Overlays** (switch each overlay on or off, its size, opacity and fixed corner, and a page per overlay for what it shows), **Race Control** and **Settings**. Changes are saved immediately to `%LOCALAPPDATA%\RaceMate\settings.ini`; the window remembers its size and position. Closing it keeps RaceMate in the tray; **Exit** in the tray menu quits. Without a tray icon, closing the window exits.

**Start with Windows** adds RaceMate to the current user's Run key (no administrator rights needed). **Start minimized** starts in the tray. Keep the executable in a fixed location when using Start with Windows.

| Default shortcut | Action |
| --- | --- |
| Ctrl + Alt + Shift + E | Toggle Move mode |
| Ctrl + Alt + Shift + H | Hide/show all overlays |
| Ctrl + Alt + Shift + S | Open Settings |

Shortcuts are global and configurable on the **Settings** page: expand a shortcut, click the field and press a combination with Ctrl or Alt; **Reset** restores the default. The banner names shortcuts another program already owns.

Overlays stay visible during spotting, replays and paused playback, and follow the camera car when you are not driving. They hide when iRacing disconnects, unless Preview is on. Outside Move mode they pass mouse clicks through and never take focus.

## Overlays

On the Overlays page, each overlay's row expands to its size, opacity and **fixed corner**. The fixed corner stays in place when the overlay grows or shrinks (more rows, extra columns, header or footer). **Settings** in the row opens the overlay's own page.

### Relative

Nearby cars centered on the reference car, with position, class-colored number badge, driver, status and estimated gap. In races, cars a lap ahead are red and a lap behind blue.

- **Rows**: odd number from 1 to 127 (default 7: three ahead, the reference car, three behind).
- **Gap decimals**: 0–3 (default 1). Gaps use same-class SDK time estimates where possible, otherwise track position and a reference lap time.
- Optional columns, column order, header/footer metrics and name format: see below.

### Standings

Separate class sections or one combined order, optionally only the reference car's class. **Top drivers**, **Drivers ahead** and **Drivers behind** (0–128 each) select the rows; a dotted line marks skipped positions. In separate-class view each class header shows the number of entrants and the class strength of field (SOF).

- Best and last laps merge live telemetry with the session results, so laps set before you joined are shown. Practice and qualifying rank by best lap; drivers without a time follow by car number or, optionally, iRating.
- **Show Interval (INT)**: gap to the car directly ahead, including hidden rows.
- **Live Positions / GAP / INT (estimated)**: ranks races by live track progress instead of the SDK's start/finish snapshots.
- Optional **LAPS** (completed laps) and **GAIN** (positions gained against the starting grid).

### Columns, header and footer (Relative and Standings)

Both overlays offer the same options, configured separately.


- **STATUS** shows one value, by priority: PIT > TOW > BLACK/DQ > REPAIR > SLOW > OUT. OUT lasts from an observed pit exit to the next line crossing. TOW uses the SDK for your car and is inferred for others from a direct return to the pit stall.
- Optional: license/safety rating, iRating (both with a short view), tires, last pit lap, pit lane time and **PIT STOP** (estimated stationary time in the stall, marked ≈).
- Every column is a row on the page: optional columns have a switch, all can move up and down, and the order is saved per overlay.
- Header and footer each show up to three metrics: track, incidents, temperatures, session clocks, lap and finish forecasts, session type, wetness, wind, class SOF or overlay name. Forecasts are marked `~`, missing data `—`.

### Fuel Calculator

RaceMate learns consumption between line crossings. The first partial lap, pit and refuel laps and replays are never counted. Yellow-flag laps count unless **Exclude yellow / red flag laps** is on. History resets when the session or car changes.

- **Tank / Range / At Finish**: current fuel, laps the tank lasts and fuel left (green) or missing (red) at the finish.
- **Pit window timeline**: one block per lap of a full-tank stint, current lap yellow, pit window green, with the window and fuel to add at the next stop.
- Consumption rows: Last lap, Average (all clean laps), Average 10, Average 5, Maximum, Minimum, each with L/lap, **ADD TO FINISH** and an optional pit window (**PIT ≈**).
- The finish forecast projects the overall leader to the flag and then your own first crossing after it, using observed pace and track position. Lapping by faster classes is included; future pit stops are not.
- Further elements: leader distance, time left at final lap start, Time Empty, Fuel at End, Stops Remaining, Refuel to End, tank gauge, stint timeline and pit-window panel. Every element can be hidden and reordered; all forecasts use the race Average.

### Pit Limiter

Green when the limiter is on, red when it is off, gray when unknown. Shown while you drive on pit approach or in the pit lane, and as long as the limiter stays on after pit exit.

### Weather

Track and air temperature, wind speed with a direction ring relative to your car, track wetness and track usage. Each card can be hidden and reordered.

## Race Control

A normal resizable window (Race Control page or tray menu) with full standings and an event feed: offtracks, incidents, possible crashes, pit entries and stops, black flags and estimated overtakes. Up to 2,000 events are kept for the current session.

- The banner at the top shows whether you are watching live or a replay (with the replay clock); **Go live** returns to live.
- **All / Incidents / Pit / Passes** filters the event feed. **Events of this car** (or **F**) shows only the selected driver's events; the pill next to the filter clears it.
- **Replay from** under the event feed sets what a double-click (or **Enter**) does: **Live** puts the camera on that car live; **5 s** (default) or **10 s** replays from that long before the event. On a driver in the standings (or **Watch**) it replays the last 5 / 10 s of that car from the moment on screen.
- **Only this car** above the event feed limits it to the selected event's car. The camera button chooses the camera group used for all of this.
- The standings show a class stripe, a status pill (Pit, Out, Tow, Repair, Black, DQ, Inactive …) and your incident count.
- **Keep on top** keeps the window above iRacing while you use the sim. This works only when iRacing runs in borderless windowed mode, not in exclusive fullscreen.
- Camera and replay controls work only when you are out of the car. Choose the columns on the Race Control page (right-click a table heading gets you there); click a heading to sort.

## Resource use

- RaceMate runs in Windows efficiency mode (low priority, EcoQoS), so the simulator always gets CPU time first.
- Refresh rate 2, 5, 10 or 20 Hz (default 10). One connection attempt per second while iRacing is not running.
- Each overlay keeps a list of draw commands and repaints only the parts that changed.
- Session YAML is parsed only when iRacing updates it.

Measured on the development machine (`build.ps1 -Diagnostics`, then the benchmarks below): with 60 cars in three classes and every overlay visible, a refresh costs about 0.6 ms of CPU time; reading a telemetry frame about 4 µs.

## Build

Requires Visual Studio 2022 Build Tools (Desktop development with C++, Windows SDK, CMake).

```powershell
.\build.ps1                      # Release build, tests, copy to dist\x64
.\build.ps1 -Architecture Win32  # 32-bit build
.\build.ps1 -Diagnostics         # also build and run the GUI diagnostics
```

Diagnostics and benchmarks use `build\qa\settings.ini` and leave personal settings untouched.

```powershell
.\build\x64\Release\RaceMateDiagnostics.exe --diagnostics       # GUI checks, BMP previews in build\qa
.\build\x64\Release\RaceMateDiagnostics.exe --benchmark-tick    # refresh cost, written to build\qa\benchmark.txt
.\build\x64\Release\RaceMateTests.exe --benchmark-telemetry
.\build\x64\Release\RaceMateTests.exe --benchmark-prediction
.\build\x64\Release\RaceMateTests.exe --live-summary            # lap-time coverage of a running session
```

## Releases

Raise the version in `project(RaceMate VERSION ...)` in `CMakeLists.txt`, commit, then push a matching tag:

```powershell
git tag v0.2.0
git push origin v0.2.0
```

The release workflow (`.github/workflows/release.yml`) checks that the tag matches the version, builds x64 and x86, runs the tests and publishes a GitHub release with the exe, a zip and its SHA-256 for each. RaceMate's update check offers that release in Settings.

## References

- [iRacing shared-memory protocol](https://github.com/vipoo/irsdk/blob/master/irsdk_defines.h)
- [Session YAML](https://sajax.github.io/irsdkdocs/yaml/sessioninfo.html) and [YawNorth / wind direction](https://sajax.github.io/irsdkdocs/telemetry/yawnorth.html)
- [Slipstream](https://github.com/antonholovko-cloud/Slipstream) served as a functional reference; no code was copied.

RaceMate is an independent project and is not provided or endorsed by iRacing.
