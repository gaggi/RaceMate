# RaceMate 0.4.36

Lightweight native Windows overlays for iRacing. C++20, Win32 and GDI, with no browser runtime or additional runtime packages. All app labels, tray entries, messages and documentation are in English.

## Getting started

The portable executable is built into **dist/x64/RaceMate-0.4.36.exe**. Requires Windows 10/11 and a monitor or triple-screen setup. Run iRacing in **borderless windowed mode**.

1. Start RaceMate.

2. Enable **Preview with sample data** to set up your overlays.

3. Enable **Move overlays**, then drag each window to the desired monitor.

4. Disable Move and Preview; RaceMate connects to iRacing automatically.

The main menu contains overlay toggles, scale, opacity, a **Settings** button for each overlay, refresh rate and startup options. Preview, Move, Reset positions, Hide overlays and Exit stay in the main menu. Each overlay has its own settings window; changes are saved automatically in **%LOCALAPPDATA%/RaceMate/settings.ini**. Old placement, scale, opacity, Relative row count and Standings class filter settings migrate automatically. Preview and Move mode are not saved as enabled. Closing or minimizing Settings hides it in the tray; **Exit** quits RaceMate. Overlay settings, header/footer options, column ordering and field management scroll on smaller screens. Keyboard focus automatically reveals controls; changing monitor DPI keeps unsaved input in place. If the tray icon is unavailable, minimizing keeps the taskbar entry and closing Settings exits RaceMate.

**Start with Windows** registers only RaceMate for the current user in the Windows Run registry key and removes that entry when unchecked. **Start Minimized** starts with Settings hidden in the tray while enabled overlays continue to work. These options are off by default; no administrator access is required. Launching a new executable refreshes the startup path when Start with Windows was already enabled. Keep that executable in a stable location.

Relative, Standings and Fuel Calculator remain visible throughout a connected iRacing session, including spotting, replays and paused playback. Relative and Standings follow the camera car when spotting or watching a replay, then switch back to your car when driving. The pit limiter message appears while driving your own car on pit approach, in pit lane, or whenever the limiter is active. Overlays hide when iRacing disconnects, unless Preview is enabled.

## Overlays

| Overlay | Contents |

| --- | --- |

| Relative | Nearby cars, position, car number, driver, estimated gap, pit marker, lapping colors and class color badges; independent numeric row count |

| Standings | Separate class sections or combined overall standings, class filter, configurable leaders and neighbours; GAP, BEST and LAST |

| Fuel Calculator | Selectable Last lap, Average 10, Average 5, Maximum, Minimum and Race average rows, with consumption and estimated fuel to add for each strategy |

| Pit Limiter | Green **PIT LIMITER ACTIVE**, red **PIT LIMITER NOT ACTIVE**, gray if telemetry status is unavailable |

The dark overlay layout is an initial design based on iRacing blackboxes. Exact matching to the current original overlays still needs reference screenshots.

Relative and Standings use the simulator's **CarClassColor** from [DriverInfo](https://sajax.github.io/irsdkdocs/yaml/driverinfo.html) for car-number badges and class strips. Relative additionally marks lapping differences in red/blue and highlights the reference car.

## Overlay settings

**Relative:** Rows is an odd numeric input from 1 to 127, including the reference car; even entries round up. The default is 7 (three ahead, your car, three behind). It affects only Relative and always includes all classes.

**Standings:** Class view switches between **Separate classes** and **Combined overall standings**. **Show only driver's class** filters to the own or currently watched car's class. Top drivers, Drivers ahead and Drivers behind are independent numeric inputs from 0 to 128. The reference car remains visible even when all counts are zero. There is no shared Rows setting.

In Separate classes, each class gets its selected top drivers, plus neighbours around the reference car within that car's class. Other classes show their leaders. Positions and gaps refer to the class leader. In Combined overall standings, the top count applies once to the complete field and neighbours follow overall position. Overlapping selections appear only once; gaps between selected positions are marked by a dotted divider. The overlay height follows the selected rows, so large counts may extend beyond a monitor.

**Fuel Calculator:** Each of the six consumption rows can be shown or hidden independently. The previous maximum-consumption toggle, reserve and manual lap controls are removed. **Pit Limiter** Settings explains its display states; enable, scale and opacity remain in the main menu.

## Standings and late joins

Standings combines the current session's **ResultsPositions/FastestTime/LastTime** values with live telemetry. Valid best laps set before you joined are therefore available as soon as iRacing supplies the session results. A slower lap observed after joining does not replace a faster server best; a newer faster live lap can improve it. Missing last laps fall back to the session's LastTime. Outside races, F2 best-lap telemetry provides an additional fallback if the other sources have no valid best.

Practice and qualifying are ranked using the merged best laps, and GAP is the difference to the fastest listed car. Race gaps continue to use race telemetry, while BEST and LAST are separate columns. Historical positions use the SDK's YAML position conventions. Results from another session are never used as lap-time fallbacks.

Updates to session results are read even when the replay's telemetry tick has stopped. If iRacing supplies no valid time through any supported source, the cell remains a dash. RaceMate cannot reconstruct a lap that the simulator has not shared.

## Controls

| Shortcut | Action |

| --- | --- |

| Ctrl + Shift + E | Toggle Move mode |

| Ctrl + Shift + H | Hide/show all overlays |

| Ctrl + Shift + S | Open Settings |

In normal operation overlays pass mouse clicks through and do not take keyboard focus. All actions are available through Settings and the tray menu if another app occupies a shortcut. Negative monitor coordinates are supported, and windows remain reachable after monitor changes.

## Fuel calculation

RaceMate learns fuel usage between start/finish crossings. The first partial lap, pit laps, refueling and replays do not count as consumption samples. Laps with yellow flags are included by default. Enable **Exclude laps with yellow flags** in Fuel Calculator settings to omit those laps from future consumption samples; the option is saved across restarts. Changing the option does not recalculate already recorded history. The history resets when the session or player car changes.

Last lap uses the latest valid lap. Average 10 / Average 5 use the most recent ten / five available clean laps; while learning, the row shows the actual sample count in parentheses. Maximum / Minimum use all clean laps recorded in the current session, including laps older than the rolling averages. Each row calculates **ADD TO FINISH** from its own consumption: max(0, remaining laps x liters per lap − current tank). No reserve is added.

The compact forecast shows only **Leader distance** (fractional remaining laps, assuming no further stops at current pace) and **Time left at final lap start** (seconds remaining on the race clock when the leader is predicted to begin the final lap). The driver decides whether to allow for an additional lap. Timed races project the leading car�s crossing after time expires, then your first crossing at or after the predicted race finish to calculate fuel. Current position and observed clean lap pace are used, with available lap times as a fallback. A lap limit can end the race earlier. Future pit stops are not automatically applied. If the finish target is unavailable, consumption stays visible and fuel to add remains a dash.

The Fuel Calculator remains visible while spotting or watching a replay, but its fuel data belongs to your own car. Replays do not learn fuel consumption or calculate a finish target. Fuel data for other cars is not inferred.

Fuel and Relative values are estimates. Lapping, changing pace, multiclass racing and cautions can change the actual remaining distance. Relative uses SDK time positions for the same class when possible, otherwise track progress and a reference lap time. No seconds gap is invented when a usable lap time is unavailable. Automatic transfer of the fuel amount to iRacing is not included.

## Resources

- One executable with a statically linked C++ runtime; only Windows system DLLs.

- No network requests, process scanning or game injection.

- Read-only access to local iRacing shared memory.

- One connection attempt per second when disconnected.

- Connected refresh options: 2, 5, 10 or 20 Hz; default 10 Hz.

- Redraw only when displayed values change, with reusable drawing surfaces.

- Parse session information only when its update counter or session changes.

A short idle test of the earlier development build used about 11.5 MiB Working Set and 1.9 MiB Private Memory. This is not a race-load benchmark or a measured comparison with Slipstream.

## Build and verification

Requires Visual Studio 2022 Build Tools with Desktop development with C++, Windows SDK and CMake, or CMake on PATH with installed MSVC build tools.

    .\build.ps1

    # Optional 32-bit build:

    .\build.ps1 -Architecture Win32

The script creates a Release build, runs the core/telemetry tests and copies a versioned executable into dist. It also updates RaceMate.exe when that file is not running. If the previous app is open, close it and launch the versioned executable. The default build is x64. The icon is included; resources/generate-icon.ps1 regenerates it from drawing operations.

    # Build and run the separate diagnostic executable; shipping builds contain no image export:

    .\build.ps1 -Diagnostics

    # Run from the project directory to export BMP previews and check window states:

    .\build\x64\Release\RaceMateDiagnostics.exe --diagnostics

    # Ten-second idle run without visible UI or tray:

    .\dist\x64\RaceMate-0.4.36.exe --benchmark

    # Read-only summary of lap-time coverage in a connected iRacing session:

    .\build\x64\Release\RaceMateTests.exe --live-summary

Diagnostics and benchmarks use only build/qa/settings.ini, leaving personal settings untouched.

Tests cover YAML scopes and names, historical results, late joins, invalid times, session isolation, best-time merging, result updates without new telemetry ticks, relative gaps at start/finish, one-row reference visibility, separate/combined class selection, deduplication, camera following, partial and wrapped rolling fuel averages, session min/max, individual refuel estimates, refueling, cautions, replay pause, disconnection and reconnection. Telemetry tests provide their own shared-memory mapping and check variable types, arrays, unsigned bitfields and memory bounds. GUI diagnostics check visibility, per-overlay settings, class view changes, fuel row toggles and persistent startup options without changing Windows startup registration.

**Status:** Release, synthetic telemetry tests and native window diagnostics verified. Historical best-lap merging was also checked against a live Practice session: 49 best laps were recovered from session results while the live best-lap array supplied none. Full visual testing in iRacing, mixed monitor DPI setups and exact visual matching still require user testing. VR and layout profiles are outside the current scope.

## References

- [Slipstream](https://github.com/antonholovko-cloud/Slipstream) as a functional reference; no program files were copied.

- [iRacing shared-memory protocol](https://github.com/vipoo/irsdk/blob/master/irsdk_defines.h).

- [Session results schema](https://sajax.github.io/irsdkdocs/yaml/sessioninfo.html).

- RaceMate is an independent project and is not provided or endorsed by iRacing.

Relative and Standings each have independent optional columns for license / safety rating, iRating, tires (Dry / Wet), last pit entry lap and pit lane duration. All default to off. Pit lane duration measures fully observed entry-to-exit visits, not stationary service time. Stops before joining and incomplete visits are unavailable. History resets on session changes, disconnects and replay rewinds. The tire column displays Dry / Wet for GT3 and GTP. Dallara IR18 / DW12 add Primary or Alternate; Dallara iR-01, McLaren MP4-30 and Mercedes W12 / W13 add Soft, Medium or Hard. The column widens only for a session with a supported multi-compound car. Unrecognized values remain unavailable. Missing data appears as a dash.

Tire names are compact: Dry / Wet, Primary / Alternate or Soft / Medium / Hard. Wet text is blue, regular Dry and Hard white, Soft red and Medium yellow. Primary uses a black badge with readable white text; IR18 Alternate is green and legacy DW12 Alternate red.

The main menu option Show Title Bar controls the header of every overlay. It defaults to off and is saved in General / ShowTitleBar. Hidden headers remove their height without hiding column headings or data. Move overlays still allows dragging anywhere in an overlay.

Relative Settings offers Gap decimal places (0–3), default 1. This applies to all Relative gaps including the reference car and is persisted separately from Standings. Display precision does not increase telemetry accuracy.

## Race Control

Open Race Control from the main menu button or tray menu. It is a normal resizable Windows window with complete standings and an event list; no lap graph is included. Class mode, driver's-class filter and optional standings columns follow Standings Settings. Double-click a standings driver to request that car's spectator camera using its car number, preserving leading zeros. The Camera dropdown at the bottom selects the camera for both driver switches and replay jumps. It lists non-scenic groups supplied by iRacing, defaults to Far Chase when available and remembers your choice by name across sessions. Both lists use the light settings-window style.

Click the -5s or -10s button directly beside an event to seek the iRacing replay to that session timestamp, select the affected car and start playback. Go Live requests the end of the replay buffer. Camera and replay controls require being out of the driving car. A successful Windows command dispatch does not guarantee that the footage is still in the simulator's replay buffer. Preview actions are simulated and never send commands to iRacing.

The feed captures offtrack transitions, incident point increases where published, black/disqualification flags, pit entries and completed pit lane visits. Pit stop replay targets the pit entry time. Possible crashes are inferred from new repair flags or sharp speed loss off track and are marked as estimated. Overtakes are estimated from nearby same-class cars reversing adjacent race positions and retaining the change for two seconds; pit-lane and caution position changes are excluded. It does not infer incident point totals for other drivers when iRacing withholds their counters.

Recording runs while the window is closed or minimized. The feed retains up to 2,000 events in memory for the current session and is cleared on session change, sample-data mode switch or app restart. Earlier events from before RaceMate connected cannot be reconstructed. Reviewing old replay footage does not duplicate the live feed. Event time is the observation timestamp and may lag the underlying event. Type filters affect display only.

Core event tests cover deduplication, offtracks, incidents, possible crashes, completed / partial pit visits, race passes, pit-related position swaps and replay rewinds. Protocol tests use a private Windows receiver to verify camera identities and complete 32-bit replay timestamps; no simulator commands are issued by these tests. GUI diagnostics cover selection, filtering, reopening and simulated replay actions. Live standings were also checked against a connected iRacing session. The actual camera / replay result still needs validation in the simulator.

Standings Settings includes **Show Interval (INT)**, off by default. INT appears immediately after GAP and measures the gap to the actual preceding car, including drivers hidden by row selection. Separate-class mode uses the preceding car in that class; combined mode uses overall order. Practice and qualifying use best-lap differences. Missing intervals remain a dash. Race Control always shows INT and a STATUS column (On track, Off track, Pit approach, Pit lane, Pit stall, Outlap or Inactive). Inactive drivers are gray; unranked entrants remain available for camera selection.

Relative and Standings mark an observed outlap with **OUT**. It begins at an observed pit exit and ends at the next start/finish crossing. Race Control shows Outlap in STATUS. Before an exit has been observed, or after a telemetry gap/disconnect/replay rewind, no outlap is assumed.

**Show Pit Stop Time** adds a separate PIT STOP column to Relative, Standings and the corresponding Race Control columns. PIT TIME continues to measure total pit-lane time. PIT STOP estimates only stationary intervals inside the pit stall: the driver's own speed is used while driving; other cars use track-position changes and track length. Transit, movements above the detection threshold and stationary time outside the stall are excluded. Stop time is shown live during a fully observed pit visit, then retained after pit exit, and included in the completed pit event. Values are marked ≈ because detection is sampled (0.1 m/s threshold for direct speed, 0.5 m/s for positional estimates, at least 0.5 s stillness). Creeping below the threshold can count. Stops require continuous observations with at most one second between samples; incomplete visits, missing movement data, rewinds and disconnects cannot reconstruct earlier standstill. Missing values are a dash.

Each overlay's Settings window has **Fixed Corner**: Top Left (default), Top Right, Bottom Left or Bottom Right. Added rows, optional columns, title-bar changes and scale adjustments keep that corner fixed. Shrinking follows the same rule. Changing the corner keeps the window in place; dragging repositions its fixed corner. Corner and screen coordinates persist across restarts, including negative coordinates on multi-monitor setups. Reset positions and recovery after a monitor change can reposition the overlay to keep it reachable.

Relative and Standings Settings each include **Column Order...**. Select a column and use **Move Up / Move Down** to change its left-to-right position. All columns, including position, number, name and gaps, can move. Hidden optional columns retain their place and visibility is still controlled by the existing checkboxes. Each overlay saves its order independently and applies changes immediately; fixed-corner placement remains unchanged. Race Control keeps its existing column order.

The Fuel Calculator includes a compact **PIT** timeline: yellow marks your current lap; green marks the next-stop window for complete-race Average consumption. The **PIT ≈** column shows the window for every enabled consumption strategy. Tank capacity is DriverCarFuelMaxLtr × DriverCarMaxFuelPct, including series restrictions. **Time Empty (Average)** is current fuel / average consumption × predicted own lap pace, with last/best lap pace as a fallback. **Fuel at End (Average)** is current fuel minus predicted remaining distance × average consumption, without future refueling. Surplus is green; missing fuel is red with a minus sign.

**Stops Remaining (Average)** is the minimum feasible number of future refueling visits in the approximate stop model. The next stop can occur during the current lap; later stops use lap-ending crossings as anchors and each intermediate tank must reach its next anchor with positive fuel; the final tank may finish empty. The first window opens only when filling the tank then still permits finishing with that stop count. The same calculation runs again after fuel use or refueling, so subsequent windows preserve the remaining minimum count. Very early top-ups can leave the count unchanged and thereby add an unnecessary stop. The text status uses whether entering now can preserve the count, while lap ranges and green blocks give approximate lap guidance.

Future stop locations use approximate lap anchors; exact pit entry position, service consumption, fuel saving and future pit loss are not modeled. No separate reserve is added. The finish prediction still assumes no future pit losses; the driver decides whether to allow an additional lap. No window is shown when no stop is needed or race, capacity, consumption or finish data is missing. While refueling or replaying, the window and count are suppressed. A tank too small to reach any modeled next stop has no valid window. The timeline shows one full-tank stint, with ceil(allowed tank capacity / complete-race average) blocks. The range starts at the last observed refueling lap, or the first observed lap if the start of the stint is unknown. Narrow, square-cornered blocks are taller than wide. A current-lap block inside the pit window is green with a yellow top strip of approximately ten percent; otherwise it is yellow.

The additional **Average** consumption row uses the total fuel consumed across all valid laps observed in this session, divided by their count, not the last five or ten laps. Race Average drives Time Empty, Fuel at End, Stops Remaining and the PIT graphic regardless of whether its row is enabled. The optional Average 5 / 10 strategies keep their rolling histories. First partial laps, pit/refueling laps, caution laps and replay data remain excluded. Earlier laps before RaceMate connected cannot be reconstructed. The race average and stint anchor reset with a new session/car or rewind, using the existing fuel-history rules.

The Fuel Calculator tank gauge displays the current liters and the legal maximum tank capacity. The two-part pit-window panel is gray when CLOSED and green when OPEN. Known windows include their lap range even while closed; unknown windows show no range and a dash for the amount. The right-hand value is the planned fuel addition for the next stop: a final stop adds the finish shortfall; an intermediate stop plans a full legal tank. For a closed intermediate window the amount is projected at the earliest displayed stop lap; once open it uses the current tank contents. Average changes update the window, range and planned addition. The separate ADD TO FINISH column continues to show total missing fuel across the remaining race.

Fuel Calculator display elements can be toggled individually in its Settings window. Hidden elements collapse without leaving gaps; all are enabled by default. The current fuel amount is centered beneath the full tank gauge.

Fuel Settings > Element Order moves each display block and the consumption table with Move Up / Move Down; the order is saved. Leader Distance is the predicted total leader race progress at timer expiry, displayed with two decimal places. For a lap-limited race it uses the finish lap limit. Remaining Leader Distance remains the leader distance still to travel to the checkered crossing. Current Lap X of Y uses your completed laps, current lap progress and predicted remaining distance. The timeline uses absolute lap numbers from the current stint start and is capped at the predicted finish lap. Its yellow marker and L-number indicate the same current race lap as the pit panel.

Multiclass/endurance fuel forecasts use the overall winner's predicted flag time and each driver's own observed pace and track position. Existing lap deficits and further laps lost to faster classes are included through the first own finish crossing after the flag; leader remaining distance is never used as own fuel distance. Regression tests cover one and three hours, a separate class leader, changing own pace, lap-limited races, refuel amounts, finish fuel and remaining stops. Future own pit losses remain subject to the configured prediction assumptions.

Refuel to End (Average) displays the total remaining refuel requirement using the whole-race average and your own finish distance, less current fuel. It may exceed tank capacity, covers all remaining stops and defaults directly below Stops Remaining. Both new fields are independently optional and sortable; existing custom orders are preserved when adding them.

Version 0.4.17: fractional Leader Distance at timer expiry is separate from the checkered crossing used for fuel planning. Fuel overlay repaint tracking also includes own pace, own finish distance, stint start and pit-stall state, so independently changing those inputs updates Time Empty and pit displays.

Standings Settings > Live Positions / GAP / INT (estimated) uses live track progress and SDK estimated lap phases instead of start/finish snapshots. It also applies to Race Control standings. The default is off, keeping official SDK snapshot gaps. Before race positions exist, standings use the starting grid when available, otherwise active track progress. Relative can use CarClassEstLapTime before a first completed lap; SDK phases are normalized per class for multiclass. Missing timing inputs stay unknown, while available driver rows remain visible.

Practice standings include drivers without recorded laps, even while their cars are inactive. Valid best laps rank first by time; drivers without a lap rank by numeric car number (2 before 11 before 100). Equal times use car number as a tie-break. Unknown gaps remain blank/dashes until a valid time exists. Class filters and configured row selection continue to apply.

Relative race positions update from full race progress. With the standings live mode enabled, overall and class positions and row order update before the next start/finish crossing; lapped traffic is ranked by completed distance, not nearest track position. Missing progress keeps its previous SDK slot; practice/qualifying still rank by lap times. Pit Limiter displays on SDK pit approach, in the lane/stall, or whenever the limiter is active. After pit exit it stays green until the limiter is switched off. Disconnecting, replay, changing sessions or leaving the driving view clears the reminder.

Fuel learning recognizes a continuously observed transition from grid/formation to racing. A clean first race lap then immediately populates Last Lap and Average; pre-start fuel use is excluded. Joining mid-lap still discards the partial sample. Race-start evidence expires on telemetry gaps and is cleared on disconnect, replay, leaving the driving view, rewinds and session/driver changes. Existing pit/refuel/caution filters remain active.

Version 0.4.18: car numbers are centered within their class-color badges. Relative and Standings each offer Footer Settings: Show Footer and independent Left / Center / Right metrics (track, incidents/limit, weather, clocks, session, lap forecasts and class SOF). Defaults are Track / None / Incidents. Lap and incident values follow the watched car; predictions are marked ~, missing data —.

Version 0.4.19: Wind overlay uses an inward arrow at the continuous wind source bearing relative to your live car heading, with wind speed in km/h centered in the circle. Includes enable, scale, opacity, placement and fixed corner controls. Calm wind shows no arrow; missing heading, spotting and replay show Direction — instead of reusing a stale heading. Uses the SDK WindDir / YawNorth relative-direction conversion documented at https://sajax.github.io/irsdkdocs/telemetry/yawnorth.html.

Version 0.4.20: Weather replaces Wind with individually selectable Track Temperature, Air Temperature, Wind Speed, Track Wetness and Track Usage cards, plus an independent wind-direction switch. The wind ring has a rotating inward chevron and centered km/h speed. Track Usage comes from the active session's SessionTrackRubberState. Old Wind placement, scale, opacity and visibility settings migrate automatically.

Version 0.4.21: Weather cards use text only (Track Temp, Air Temp, Wind, wetness and usage). Wetness is a compact status row without a scale. The wind ring and chevron use anti-aliased GDI+ circular geometry at every DPI and overlay scale. Element Order saves a custom card order. Show Wind Direction is nested below Show Wind and disabled while the Wind card is hidden.

Version 0.4.22: Weather usage and wetness are centered. Standings offers Show Completed Laps, an optional LAPS column included in Column Order. It shows completed laps (not the lap in progress), using live completed laps, then current lap minus one, then active session result LapsComplete. Unknown counts show —. Existing column orders and hidden defaults are preserved.

Version 0.4.23: Weather cards have square corners. Drawing reuses the Windows DC brush and caches the large weather font per overlay buffer. Standings content and rendering reuse each selected group list, including footer placement. Weather repaint keys match the displayed integer wind speed. Diagnostics measure repeated Content + Render calls and report GDI handle stability.

Version 0.4.24: Relative lap colors apply only in Race sessions. Relative selects balanced neighbours, leaves unused slots empty and keeps the reference car in the exact center, including when hiding other pit-lane drivers. Row counts are odd (1–127), rounding even entries up. Pit limiter reminder hides immediately at lane exit even when the limiter is still engaged; pit approach remains enabled.

The footer shows the incident limit only during Race sessions; Practice and qualifying show the incident count alone.

Version 0.4.25: Relative defaults to seven rows, three cars ahead and three behind the centered reference car. The old nine-row default migrates once; other custom values and subsequent nine-row selections remain saved.

Standings includes optional, sortable Positions Gained (GAIN): starting grid minus current position, using class positions in separate-class view and overall positions in combined view. Green upward arrow indicates gains; red downward arrow indicates losses. Practice/qualifying or unknown starting positions show —.

TOW takes priority over PIT/OUT. Own-car towing uses PlayerCarIsTowing, with PlayerCarTowTime as a fallback. Other-car towing is inferred in active races from an observed direct track-to-stall return or an impossible position jump to the stall; it persists until the car leaves the stall. It cannot distinguish the end of towing from subsequent repairs for another driver. Unobserved returns and joining while already in the stall do not invent a tow status. Race Control also shows Towing.

Version 0.4.26: Pit Limiter remains visible and green after pit exit for as long as the limiter is active. Disabling it outside the pit lane hides the reminder immediately. With the limiter off, the reminder still hides immediately at pit exit.

Version 0.4.27: All session types show the standings roster before a first lap, including qualifying and races with no position telemetry. Untimed driver order defaults to ascending car number; descending iRating is optional in Standings settings. Timed drivers stay ahead, sorted by lap time; races prefer the qualifying grid before the start. Relative retains the reference driver at gap zero when track progress is unavailable; unknown positions display —.

Version 0.4.28: All overlay scale choices use 10% steps from 50% to 150%, with 100% as the default. Saved values migrate to the nearest available step and clamp to that range.

Version 0.4.29: PIT, OUT and TOW status labels use the same regular font weight in Relative and Standings.

Version 0.4.30: Relative and Standings have independent Short View switches nested below License and iRating. Short license shows just the safety rating (3,45) in the SDK-provided license color; short iRating shows thousands with one decimal (2,4K). Full view remains the default. The switches are disabled when their parent column is hidden, preserving the selected preference.

Version 0.4.31: Relative and Standings have independently selectable Black Flag, Slowdown and Required Repairs signals in a sortable FLAGS column (BLACK / SLOW / REPAIR, DQ for disqualification). Signals use CarIdxSessionFlags; own-car SessionFlags is a fallback if the per-car array is absent. These flags do not identify penalty duration, drive-through versus stop-and-go, or optional damage. Required repairs are not inferred from speed or incidents. Flag bit definitions: https://github.com/meltingice/node-iracing/blob/master/lib/irsdk_defines.h.

Race Control now includes FLAGS, optional LAPS and GAIN, and independent column visibility for both tables. Right-click either table header and select Manage Fields; column clicks toggle ascending/descending sorting with numeric comparisons for positions, numbers, laps, iRating and times. Missing numeric values sort last. Restore race order returns to automatic standings. Visibility and sort preferences persist separately from overlay settings. Events can be filtered by both type and car, including recorded cars no longer in the roster. Replay buttons always act on the event in the clicked row after filtering/sorting; either replay-button column can be hidden.

Version 0.4.32: Relative and Standings merge PIT / OUT / TOW and selected BLACK / SLOW / REPAIR flags into a single sortable STATUS column, independently of DRIVER. Display Settings provides Show Driver, Show Status, Name Format (full, F. Last name, F Last name), Show Header (title bar), Show Column Headings and Show Footer per overlay. Hidden sections shrink the overlay around the fixed corner. The main Show Title Bar switch remains a master switch; per-overlay Header options can override it independently. Existing FLAGS migrates to STATUS directly after DRIVER once; subsequent custom status order and footer metric selections are preserved.

Version 0.4.33: Driver visibility, Name Format, Status visibility and Column Headings are directly in the Relative / Standings settings. STATUS shows one value only, prioritized PIT > TOW > BLACK/DQ > REPAIR > SLOW > OUT, using enabled flag categories. The status column is compact (70 px). Header Settings and Footer Settings each have independent visibility and left / center / right metric choices. Overlay Name is available in both. The former Display Settings submenu is removed; existing selections are preserved.

Column headings default to hidden: existing settings migrate once to this default, then future selections are retained. With headings hidden, numeric/status columns use compact widths; short license, short iRating and abbreviated names shrink further. Enabling headings restores the wider label-friendly widths.

Version 0.4.34: Settings dialogs scroll when space is limited, reveal focused controls and retain edits across DPI changes. Related options have clearer headings and an automatic-save hint. Missing tray icons no longer strand the app invisibly. Invalid session IDs and car metadata keep their missing-value defaults; unavailable fuel, race-clock and incident telemetry no longer becomes fabricated valid data. Race sorting remains deterministic when progress is missing, and waving-yellow laps are excluded from clean fuel samples. Diagnostic code lives in src/Diagnostics.inc; build.ps1 -Diagnostics runs the GUI checks as well as core tests. Absolute build-directory paths are supported when packaging. See REVIEW.md for scope and verification limits.

Version 0.4.35: Yellow-flag fuel laps are included by default, with a saved opt-in filter in Fuel Calculator settings. Header Settings and Footer Settings share one row in Relative and Standings, followed by Column Order below. The automatic-save hint is removed and the dialogs are shorter.

Version 0.4.36: The executable, app windows and tray use the supplied resources/rm64.ico icon.
