# RaceMate 0.1.1

Lightweight native overlays for iRacing on Windows 10/11. C++20, Win32 and GDI: one executable with a statically linked runtime, no browser engine, no network access, read-only access to iRacing's shared memory.

## Getting started

1. Start `dist/x64/RaceMate-0.1.1-windows-x64.exe`. Run iRacing in borderless windowed mode.
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

The Overlays page starts with **Preview**, **Move overlays** and **Hide all overlays**, so they stay in view with many overlays. In all lists, Space switches a row and Enter opens its settings or expands it. Each overlay's row expands to its size, opacity, **fixed corner** and **update rate** (1–20 Hz; default 10 Hz for Relative and Pit Limiter, 1 Hz for Standings, Fuel Calculator and Weather). The fixed corner stays in place when the overlay grows or shrinks (more rows, extra columns, header or footer). **Settings** in the row opens the overlay's own page; its **Layout** section switches **rounded corners** (default square).

**Add overlay** adds another Relative, Standings, Fuel Calculator or Weather with its own settings and place, for example a second Weather that shows only the wind rose in the middle of the screen. The copy button in an overlay's row adds one with the same settings; the bin removes an added overlay with its settings. Added overlays are numbered (Weather 2) and saved in their own section of `settings.ini`. While an overlay's own page is open, it has a red border; while overlays can be moved, all have a dashed yellow border.

### Relative

Nearby cars centered on the reference car, with position, class-colored number badge, driver with status badge and estimated gap. In races, cars a lap ahead are red and a lap behind blue (switch **Lap colors**).

- **Rows**: odd number from 1 to 127 (default 7: three ahead, the reference car, three behind).
- **Gap decimals**: 0–3 (default 1). Gaps use same-class SDK time estimates where possible, otherwise track position and a reference lap time.
- Optional columns, column order, header/footer metrics and name format: see below.

### Standings

Separate class sections or one combined order, optionally only the reference car's class. **Top drivers**, **Drivers ahead** and **Drivers behind** (0–128 each) select the rows; a dotted line marks skipped positions. In separate-class view each class header shows the number of entrants and the class strength of field (SOF).

- Best and last laps merge live telemetry with the session results, so laps set before you joined are shown. Practice and qualifying rank by best lap; drivers without a time follow by car number or, optionally, iRating.
- **Show Interval (INT)**: gap to the car directly ahead, including hidden rows.
- **Live Positions / GAP / INT (estimated)**: ranks races by live track progress instead of the SDK's start/finish snapshots.
- Optional **LAPS** (completed laps), **GAIN** (▲/▼ positions against the starting grid) and **STOPS** (pit stops seen in this race, `?` after joining late). The fastest lap of each class is purple.
- **Session progress**: a bar under the header with the percentage done, by laps or by time.

### Header and footer

Relative, Standings, Fuel Calculator and Weather each have a header and a footer with up to four metrics, switched and chosen on the overlay's page. Footers are off by default. Fuel Calculator's header shows its name, current and expected final lap and the stops needed as a colored pill; Weather's shows its name and **DECLARED WET** while rain tires are allowed. The **Title bar** setting switches all headers at once.

### Columns (Relative and Standings)

Both overlays offer the same options, configured separately.


- The **status badge** after the driver's name shows one value, by priority: PIT > TOW > BLACK/DQ > REPAIR > SLOW > OUT. OUT lasts from an observed pit exit to the next line crossing. TOW uses the SDK for your car and is inferred for others from a direct return to the pit stall.
- Optional: car brand as logo and/or name (the initial for brands without a logo), tires as a ring with D (dry), W (wet) or the compound's initial, iRating (both with a short view), **iRating change** (estimated as if the race ended in the current class order), **laps since stop** (L12; L- until a stop or the start was seen), last pit lap, pit lane time and **PIT STOP** (estimated stationary time in the stall, marked ≈). A last lap equal to the car's best is green.
- **Class colors** puts the class color on the number box and a stripe on the left; switched off, numbers sit in a neutral box.
- Every column is a row on the page: optional columns have a switch, all can move up and down, and the order is saved per overlay.
- Header and footer each show up to four metrics, the first on the left and the others right-aligned: track, incidents (with the race's warning and disqualification limits), temperatures, session clocks, lap and finish forecasts, session type, wetness, wind, class SOF, position in class, brake bias, stops needed, declared wet or overlay name. Forecasts are marked `~`, missing data `—`.

### Fuel Calculator

RaceMate learns consumption between line crossings. Only whole laps at racing speed count: caution laps, pit and out laps, refuels, the first partial lap and replays never do. History resets when the session or car changes. The average of the last 10 counted laps drives range, pit window, refuel and targets.

- **Tank**: fuel, range in laps and a bar of the tank with the fuel missing to the finish (hatched) and the finish mark.
- **Consumption**: last lap, averages of 3, 10 and 5 laps, race average, minimum and maximum, each with its own switch.
- **Last 10 laps**: every whole lap's fuel against the 10-lap average; caution laps can be marked.
- **Refuel to finish** (without a safety margin; green when it fits in the tank) and **pit window** (earliest and latest lap); side by side while they follow each other in the module order.
- **Pit window timeline**: one block per lap of a full-tank stint, current lap yellow, window green, with the fuel to add at the next stop.
- **Targets**: the most fuel per lap that still reaches the latest stop (or the finish without a stop), and the same for one lap longer, two laps longer or one lap shorter.
- The finish forecast projects the overall leader to the flag and then your own first crossing after it, using observed pace and track position. Lapping by faster classes is included; future pit stops are not.
- The arrows on the overlay's page order the modules; adjacent target rows share one band.
- Layout **Panel** shows the modules one under the other; **Strip** shows tank, range, average, refuel, latest stop and targets in one line, in the same order. Modules the strip leaves out are marked **Panel only** on the page.
- An average over fewer laps than its name shows how many it has so far, e.g. **AVG 10 (3)**, in gray.

### Pit Limiter

Green when the limiter is on, red when it is off, gray when unknown. Shown while you drive on pit approach or in the pit lane, and as long as the limiter stays on after pit exit.

### Weather

Modules, each with its own switch and ordered with the arrows on the overlay's page (the strip follows the same order): the track condition in iRacing's seven levels; precipitation; track and air temperature; the track temperature of the last 20 minutes; wind speed, compass direction and the direction ring relative to your car (with "Crosswind from the left" and similar); humidity, sky and fog; track usage.

- **Trend arrows** compare with ten minutes ago (from five minutes of history on): rising, falling or steady. iRacing publishes no forecast, so RaceMate samples the weather every 30 seconds itself.
- The wind module shows the **wind rose** (the ring with the speed inside), the **wind text** or both. With only the wind rose switched on, the overlay shrinks to the ring.
- Layout **Panel** or **Strip** (condition, rain, temperatures and wind in one line; the others are marked **Panel only**).
- Trends compare with 10 minutes ago and pause while old footage plays; the temperature history is recorded while you drive or watch live and says how many minutes it holds so far. Precipitation is the current rain, not a forecast.

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
- Telemetry rate 2, 5, 10, 15 or 20 Hz (default 10, Settings page); an overlay set to update faster raises it. One connection attempt per second while iRacing is not running.
- Each overlay updates at its own rate, keeps a list of draw commands and repaints only the parts that changed.
- Session YAML is parsed only when iRacing updates it.

Measured on the development machine (`build.ps1 -Diagnostics`, then the benchmarks below): with 60 cars in three classes and every overlay visible, a refresh of all overlays costs about 0.6 ms of CPU time, about 2.5 ms per second at the default rates; reading a telemetry frame about 4 µs.

## Build

Requires Visual Studio 2022 Build Tools (Desktop development with C++, Windows SDK, CMake).

```powershell
.\build.ps1                      # Release build, tests, copy to dist\x64
.\build.ps1 -Architecture Win32  # 32-bit build
.\build.ps1 -Diagnostics         # also build and run the GUI diagnostics
```

Diagnostics and benchmarks use `build\qa\settings.ini` and leave personal settings untouched.

Car brand logos are white PNGs in `resources/brand-logos/`, named after the brand; `resources/generate-brand-logos.ps1` packs them into `resources/brand-logos.png` and `src/BrandLogos.inc`.

```powershell
.\build\x64\Release\RaceMateDiagnostics.exe --diagnostics       # GUI checks, BMP previews in build\qa
.\build\x64\Release\RaceMateDiagnostics.exe --benchmark-tick    # refresh cost, written to build\qa\benchmark.txt
.\build\x64\Release\RaceMateTests.exe --benchmark-telemetry
.\build\x64\Release\RaceMateTests.exe --benchmark-prediction
.\build\x64\Release\RaceMateTests.exe --live-summary            # lap-time coverage and live/replay state of a running session
.\build\x64\Release\RaceMateTests.exe --dump-vars               # every telemetry variable and the session YAML of a running session, to irsdk-dump.txt
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
