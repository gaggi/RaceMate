# RaceMate 0.1.2

Lightweight native overlays for iRacing on Windows 10/11. C++20, Win32 and GDI: one executable with a statically linked runtime, no browser engine and read-only access to iRacing's shared memory. Optional update checks and the experimental Track Map use HTTPS.

## Getting started

1. Start `dist/x64/RaceMate-0.1.2-windows-x64.exe`. Run iRacing in borderless windowed mode.
2. Click **Preview** in the banner at the top to see every overlay with sample data.
3. Turn on **Move overlays** (Overlays page) and drag each overlay where you want it, then click **Done**.
4. Stop the preview. RaceMate connects to iRacing on its own; the banner shows the track and session.

The window has three pages in its sidebar: **Overlays** (switch each overlay on or off, its size, opacity and fixed corner, and a page per overlay for what it shows), **Race Control** and **Settings**. Changes are saved immediately to `%LOCALAPPDATA%\RaceMate\settings.ini`; the window remembers its size and position. Closing it keeps RaceMate in the tray; **Exit** in the tray menu quits. Without a tray icon, closing the window exits.

**Start with Windows** adds RaceMate to the current user's Run key (no administrator rights needed). **Start minimized** starts in the tray. Keep the executable in a fixed location when using Start with Windows.

| Default shortcut | Action |
| --- | --- |
| Ctrl + Alt + Shift + E | Toggle Move mode |
| Ctrl + Alt + Shift + H | Hide/show all overlays |
| Ctrl + Alt + Shift + S | Open settings |
| Ctrl + Alt + Shift + ← | Previous event in Race Control (opens it if closed) |
| Ctrl + Alt + Shift + → | Next event in Race Control |

Shortcuts are global and configurable on the **Settings** page: click a shortcut and press a combination with Ctrl or Alt; Esc cancels, Backspace turns it off, and the reset arrow beside a changed one restores the default. The banner names shortcuts another program already owns.

Overlays stay visible during spotting, replays and paused playback, and follow the camera car when you are not driving. They hide when iRacing disconnects, unless Preview is on. Outside Move mode they pass mouse clicks through and never take focus.

## Overlays

The Overlays page starts with **Preview**, **Move overlays** and **Hide all overlays**, so they stay in view with many overlays. In all lists, Space switches a row and Enter opens its settings or expands it. Each overlay's row expands to its size, opacity, **fixed corner** and **update rate** (1–20 Hz; default 10 Hz for Relative and Pit Limiter, 1 Hz for Standings, Fuel Calculator and Weather, 20 Hz for Track Map). The fixed corner stays in place when the overlay grows or shrinks (more rows, extra columns, header or footer). **Settings** in the row opens the overlay's own page; its **Layout** section switches **rounded corners** (default square).

**Layouts** are complete sets of overlays: which are on, their places, sizes and every setting, each kept apart. **Add layout** starts one as a copy of the active layout or with default overlays; **Use** makes one active, and the overlays and their pages then belong to it. Each layout becomes active by its condition: **Always**, **In car** (while you drive), **Out of car** (spotting and replays) or **Garage** (the garage screen open). When the situation changes and holds for a second, the first layout in the list whose condition applies takes over, so the arrows set the priority and an **Always** layout at the bottom catches the rest. A layout chosen by hand stays until the situation changes again. The tray menu lists the layouts too. Settings from before layouts are the **Default** layout.

**Add overlay** adds another Relative, Standings, Fuel Calculator or Weather with its own settings and place, for example a second Weather that shows only the wind rose in the middle of the screen. The bin in an added overlay's row removes an added overlay with its settings. Added overlays are numbered (Weather 2) and saved in their own section of `settings.ini`. While an overlay's own page is open, it has a red border; while overlays can be moved, all have a dashed yellow border.

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

Every overlay, including Pit Limiter, has an optional header and footer with up to four metrics, switched and chosen on the overlay's page. **All headers and footers offer the same metric list.** Footers are off by default; Pit Limiter also starts without a header. Fuel Calculator's header shows its name, current and expected final lap and the stops needed as a colored pill; Weather's shows its name and **DECLARED WET** while rain tires are allowed. The **Headers on all overlays** setting switches all headers at once.

Two overall-leader forecasts are available everywhere: **Leader projected race distance**, for example `~19.08 laps` at time expiry (at the finish for lap-limited races), and **Time left at leader's final lap**, for example `~13 s` or `~1:13` on the race clock when that leader is projected to start the final lap. They always refer to the overall leader, even when watching another car, and work before fuel consumption is learned. They are forecasts at the observed pace, not a report of an actual white flag. Missing data shows `—`. The existing **Current / Expected Final Lap** and **Estimated Laps Remaining** still refer to your car or the watched car. **Stops Needed (your car)** and **Fuel at finish (your car)** use your fuel data.

**Current lap / Leader projected distance** combines your current lap (or the watched car's lap) with the overall leader's fractional projection, for example `Lap 8 / ~19.12`. It is a separate choice in every header and footer; **Current / Expected Final Lap** keeps the rounded finishing lap for your own or the watched car.

### Columns (Relative and Standings)

Both overlays offer the same options, configured separately.

- **Fuel to finish (FUEL)** is an optional, reorderable column. Green **END** means your normal full-tank range is sufficient; yellow **SAVE** means finishing requires up to the **Additional laps with fuel saving** set in Settings (shared by both overlays, 0–10, default **+3 laps**). The extra laps are converted to time using your pace and apply only to SAVE. Blank means insufficient range or unavailable data, including other classes and your own car.
- Opponent fuel is a forecast, not measured fuel: it assumes a full tank at an observed race start or the exit of a completely observed service stop (at least two stationary seconds). It uses your restricted tank capacity, the average of at least three clean consumption samples and your pace to estimate a full-tank duration, subtracts elapsed stint time and compares with that opponent's projected finish time after the overall leader's flag. Drive-throughs, tows, telemetry gaps, replays and unobserved starts cannot establish a refill. No separate yellow-phase adjustment is applied. Different tanks, consumption or partial fills within the same class can still make the estimate optimistic.

- The **status badge** after the driver's name shows one value, by priority: PIT > TOW > BLACK/DQ > REPAIR > SLOW > OUT. OUT lasts from an observed pit exit to the next line crossing. TOW uses the SDK for your car and is inferred for others from a direct return to the pit stall.
- Optional: car brand as logo and/or name (the initial for brands without a logo), tires as a ring with D (dry), W (wet) or the compound's initial, iRating (both with a short view), **Safety and iRating** (the iRating on the license color in one badge), **iRating change** (estimated as if the race ended in the current class order), **laps since stop** (L12; L- until a stop or the start was seen), last pit lap, pit lane time and **PIT STOP** (estimated stationary time in the stall, marked ≈). A last lap equal to the car's best is green.
- **Class colors** puts the class color on the number box and a stripe on the left; switched off, numbers sit in a neutral box.
- Every column is a row on the page: optional columns have a switch, all can move up and down, and the order is saved per overlay.
- Header and footer each show up to four metrics, the first on the left and the others right-aligned: track, incidents (with the race's warning and disqualification limits), temperatures, session clocks, lap and finish forecasts, session type, wetness, wind, class SOF, position in class, brake bias, stops needed, declared wet or overlay name. Forecasts are marked `~`, missing data `—`.

### Fuel Calculator

RaceMate learns consumption between line crossings. Only whole laps at racing speed count: caution laps, pit and out laps, refuels, the first partial lap and replays never do. History resets when the session or car changes. The average of the last 10 counted laps drives range, pit window, refuel and targets.

- **Tank**: fuel, range in laps and a bar of the tank with the fuel missing to the finish (hatched) and the finish mark.
- **Consumption**: last lap, averages of 3, 10 and 5 laps, race average, minimum and maximum, each with its own switch.
- **Last 10 laps**: every whole lap's fuel against the 10-lap average; caution laps can be marked in yellow (switch in the module's expanded row, off by default).
- **Leader projected race distance** and **Time left at leader's final lap**: separate, reorderable modules in both Panel and Strip, off by default, also selectable in every overlay's header/footer. Fractional total leader laps (`~19.08`) are distinct from your rounded finishing lap (`~20`).
- **Refuel to finish** (without a safety margin; orange when it fits in the tank, red beyond it) and **pit window** (earliest and latest lap); side by side while they follow each other in the module order. Once no stop is needed, the card shows the fuel left **at the finish** at the current consumption (`+10.8 L`, about 3.5 laps spare), the tank bar marks that spare part in green, and the **Stops Needed** pill reads `NO STOP · +10.8 L`. A shortfall of up to a tenth of the average adds `or save 0.09 L/lap`.
- **Pit window timeline**: one block per lap of a full-tank stint, current lap yellow, window green, with the fuel to add at the next stop.
- **Targets**: the most fuel per lap that still reaches the latest stop (or the finish without a stop), and the same for one lap longer, two laps longer or one lap shorter.
- The finish forecast projects the overall leader to the flag and then your own first crossing after it, using observed pace and track position. Lapping by faster classes is included; future pit stops are not.

RaceMate cannot determine another car's remaining fuel from the telemetry it reads. A leader's current pit visit already contributes an estimated remaining pit loss, but a future stop must be treated as an assumption. A stop longer than a 13-second final-lap margin can remove the extra lap for that leader; another contender can still keep the race at the longer distance. See [the proposed leader-stop scenarios](docs/leader-stop-scenarios.md).
- The arrows on the overlay's page order the modules; adjacent target rows share one band.
- Layout **Panel** shows the modules one under the other; **Strip** shows tank, range, average, refuel, latest stop and targets in one line, in the same order. Modules the strip leaves out are marked **Panel only** on the page.
- An average over fewer laps than its name shows how many it has so far, e.g. **AVG 10 (3)**, in gray.

### Pit Limiter

Green when the limiter is on, red when it is off, gray when unknown. Shown while you drive on pit approach or in the pit lane. Past pit exit with the limiter still on it flashes red, **TURN OFF PIT LIMITER**, until you switch it off.

**Setup changes** (on by default): in the pit lane it lists in-car adjustments, such as brake bias, ABS, TC, throttle map, fuel mixture and engine braking, that no longer match the loaded setup, each with its value now and in the setup. It compares the dials your car has with the values its setup lists; a dial that differs already when you get in the car is taken as not comparable on that car and left out.

### Weather

Modules, each with its own switch and ordered with the arrows on the overlay's page (the strip follows the same order): the track condition in iRacing's seven levels; precipitation; track and air temperature; the track temperature of the last 20 minutes; wind speed, compass direction and the direction ring relative to your car, or to the car you watch live or in a replay while iRacing reports its heading (with "Crosswind from the left" and similar); humidity, sky and fog; track usage.

- **Trend arrows** compare with ten minutes ago (from five minutes of history on): rising, falling or steady. iRacing publishes no forecast, so RaceMate samples the weather every 30 seconds itself.
- The wind module shows the **wind rose** (the ring with the speed inside), the **wind text** or both. With only the wind rose switched on, the overlay shrinks to the ring.
- Layout **Panel** or **Strip** (condition, rain, temperatures and wind in one line; the others are marked **Panel only**).
- Trends compare with 10 minutes ago and pause while old footage plays; the temperature history is recorded while you drive or watch live and says how many minutes it holds so far. Precipitation is the current rain, not a forecast.

### Pit exit estimate

Where you rejoin if you box now: your class position after the stop, the car ahead at that point and the time lost. It appears as **Pit exit (your car)** in every header and footer, as the **Pit exit** module of the Fuel Calculator (off by default), as a faded row of your own in the Standings (**Pit exit row**, off by default: right behind the car that would then be just ahead, with the gaps to the cars ahead and behind; its expanded row sets how many cars around it are shown, two ahead and two behind by default, even where the usual selection leaves them out) and as a blue **PIT** ring on the Track Map. It needs a race, your own car on track and outside the pit lane.

- **Pit lane loss**: what driving through the pit lane costs against the same stretch on track. RaceMate learns it from every car's observed pit visit, drive-throughs too: time on pit road, minus the time standing still, minus iRacing's reference time (`CarIdxEstTime`) for the bypassed stretch. Slow entries after a corner and exits onto a straight therefore count correctly, which a constant lap speed would not. The median of the last visits is kept per track for the next session; **Settings → Pit stop estimate** can fix a value instead.
- **Service**: from the pit menu (`PitSvFlags`, `PitSvFuel`, `PitRepairLeft`), with fuel limited to what fits in the tank. The server's car rule set (`DCRuleSet`) decides the order, as in iRacing's 2026 Season 4 notes: with the default rules tires and repairs follow the fueling, with the IMSA rules everything runs at once. Two wheel guns change any pair of tires at once, so one or three tires take as long as two or four. The four-tire time is iRacing's value for GT3 (20 s, DTM rules 7 s), GT4 (35 s), TCR (40 s), GTE, GT1 and LMP1 (20 s); for other classes, or to override, set it per car under **Service times**. The refuel rate is measured at your own stop, or typed in; until then 3 l/s is assumed (1.5 l/s under NEC rules). Both are kept per car and rule set. Parts without a known time count as zero and mark the time lost with `?`.
- Everyone is assumed to keep their pace; positions count cars whose race progress stays ahead of yours after the time lost.

### Track Map (experimental)

Enable **Track Map** on the Overlays page; it starts disabled. The exact layout is selected by iRacing's `TrackID` and checked against its internal `TrackName`. The built-in index currently covers 424 layouts. Unknown layouts stay empty.

The official `active.svg`, `start-finish.svg` and optional `pitroad.svg` download from iRacing's public asset server on a worker, then stay in `%LOCALAPPDATA%\RaceMate\trackmaps\v1\<TrackID>`. There are no bundled track SVGs or learned fallback maps. Preview uses the official Lime Rock GP SVG with sample cars. After the first successful download the cache works offline; **Retry** in its settings retries a failure.

Start/finish and driving direction are extracted from the SVG. Car lap percentages follow a prepared path between the circuit boundaries; this is an approximation until checked in the simulator. If start/finish cannot be resolved the map reports the error; if direction cannot be resolved, it shows the map without car markers. The focused car has a white ring and yellow marker; other cars use class colors. The header shows track and layout (**Track and layout**, also available in every other header and footer). Numbers, own-class filtering, pit cars and the usual header/footer metrics are optional; **Projection details** (off by default) adds one line under the map with track ID, cache source, rate and the projection notice. Pit markers are hollow; with **Pit lane** shown they sit on the drawn pit lane beside their lap position, otherwise on the circuit. **Text in markers** shows the car number, the position in class or the overall position. Cars are drawn from the back of the race to the front, each marker with its number, so the leader covers the car behind; your own car comes last. The static map bitmap is cached per overlay and size; car markers are blended in software into one copy of the map area per refresh. The default update rate is 20 Hz for smooth movement; it raises the telemetry rate to 20 Hz while the map is on.

The track is white with a black edge by default (**White track**; off: grey). **Background** adds iRacing's drawing around the track and **All configurations** the track's other layouts in grey, both off by default and for the Map layout; the map makes room for the other layouts. Both layers download once per layout, also for maps cached before. **Layout** chooses the official **Map**, a **Circle** or a **Line**. Circle and line place cars by lap position alone (0 % at the top, clockwise, or at the left), need no download and work on every track; the line suits 21:9 and 32:9 monitors.

Switch **Background and frame** off for just the map and car markers on a transparent background; captions, border and header/footer are hidden while their settings are kept. **Pit lane** and **Start/finish marking** independently switch those SVG layers without changing position mapping. **Cars in the pits** remains a separate marker filter. All three new switches default to on, preserving existing layouts.

For bridge layouts such as Suzuka, a single filled road outline is converted to a centerline during preparation. Small, directionally consistent SVG gaps are bridged only in the position path; the drawing retains the bridge gap. Cars traverse both branches of the crossing rather than following the road outline back the way they came. Ambiguous branches or incompatible open ends are rejected.

See [the prototype test guide](docs/trackmap-prototype.md) and [third-party notices](docs/third-party.md).

## Race Control

A normal resizable window (Race Control page or tray menu) with full standings and an event feed: offtracks, incidents, possible crashes, pit entries and stops, black flags and estimated overtakes. Up to 2,000 events are kept for the current session. While you watch a replay, cars move as recorded, so only incident points keep counting: they come from the live session info and are stamped with the live session time.

Events stay on the left and standings on the right. Drag the divider between the tables to change their width ratio; it is saved across restarts. Both tables retain a minimum width. Double-click the divider (or focus it and press Home) to restore the original 56/44 split; Left/Right also adjusts it. Filters and replay controls remain reachable independently of the split.

The Race Control page independently switches **Fuel to finish** (the same green END / yellow SAVE as the overlays, including the shared default +3 saving laps), **laps since stop**, **stint time**, **pit stops**, **iRating change** and **car brand**. Stint time runs from an observed race start or pit exit; it is unavailable in the pits, replay or after a telemetry gap. Other unavailable values show a dash; an unavailable fuel forecast stays blank. Every new column starts disabled and supports sorting, with unknown values last.

**Event lap** adds the recorded lap beside each event's timestamp and sorts numerically. Under **Race information above the tables**, the leader's projected fractional race distance and the clock at the start of the projected final lap can each be switched on. These use the same forecast as the overlays and work in Preview. The extra row only takes space when at least one value is enabled. All choices survive closing and restarting Race Control. Optional fuel and iRating calculations share the overlays' cached results and are only requested when shown.

- The banner at the top shows whether you are watching live or a replay (with the replay clock); **Go live** returns to live.
- **All / Incidents / Pit / Passes** filters the event feed. **Events of this car** (or **F**) shows only the selected driver's events; the pill next to the filter clears it.
- **Replay from** under the event feed sets what a double-click (or **Enter**) does: **Live** puts the camera on that car live; **5 s** (default) or **10 s** replays from that long before the event. On a driver in the standings (or **Watch**) it replays the last 5 / 10 s of that car from the moment on screen.
- **‹ Previous** and **Next ›** step through the events shown, in time order whatever the sort, and replay each the same way. A replay jump waits until iRacing has reached that moment before it switches the camera and plays; if it does not get there within 12 s, it plays from where it is and says that the footage may be older than iRacing's replay buffer keeps.
- **Only this car** above the event feed limits it to the selected event's car. The camera button chooses the camera group used for all of this.
- The standings show a class stripe, a status pill (Pit, Out, Tow, Repair, Black, DQ, Inactive …) and your incident count.
- **Keep on top** keeps the window above iRacing while you use the sim. This works only when iRacing runs in borderless windowed mode, not in exclusive fullscreen.
- Camera and replay controls work only when you are out of the car. Choose the columns on the Race Control page (right-click a table heading gets you there); click a heading to sort.

## Resource use

- RaceMate runs in Windows efficiency mode (low priority, EcoQoS), so the simulator always gets CPU time first.
- **Background rate** 5, 10, 15 or 20 Hz (default 10, Settings page): how often RaceMate reads iRacing for Race Control events, pit stops, fuel learning and the finish forecast, even while no overlay redraws. Overlays have their own rate; one set faster raises it. One connection attempt per second while iRacing is not running.
- Each overlay updates at its own rate, keeps a list of draw commands and repaints only the parts that changed.
- Session YAML is parsed only when iRacing updates it.
- `RaceMate.exe --profile` measures where the time goes while it runs: telemetry and session YAML, each analysis, each overlay's update, presentation and screen copy, against the process CPU. Every 10 s it overwrites `%LOCALAPPDATA%\RaceMate\profile.txt` with the last 10 s. Without the switch each measured spot costs one check of a flag.

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
