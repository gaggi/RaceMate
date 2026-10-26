# Troubleshooting

[Documentation](README.md) · [Getting started](getting-started.md)

## Overlays are missing

1. Turn on **Preview**. If sample overlays appear, the application can render; check the simulator connection and the conditions below.
2. Make sure the overlay's switch is enabled in the **active layout**.
3. Check **Hide all overlays**, including its global shortcut and tray-menu state.
4. Check the layout conditions. If no layout applies to the current situation, all overlays are hidden; the Overlays page then says **None applies now: overlays hidden**. Add an **Always** layout at the end of the list, or click **Use** to show a layout anyway. See [How RaceMate chooses](layouts-and-settings.md#how-racemate-chooses).
5. Run iRacing in **borderless windowed** mode.
6. Enable **Move overlays** to find the frames. After a monitor change, use **Reset positions** on the Overlays page.
7. Check the overlay's content choices. A Weather panel with all modules hidden has nothing to display; Pit Assist appears only under its driving/pit conditions.

After Preview is off, disconnected overlays hide automatically. A transparent Track Map intentionally hides its panel, header and footer.

## RaceMate is not connected

Start iRacing on the same machine and join a session. RaceMate reads the local shared-memory interface and retries a disconnected connection about once per second. There is no account or server configuration to enter.

Confirm **Preview is off**: its banner explicitly states that live telemetry is not being read. A successful connection replaces that banner with session information. If necessary, quit RaceMate via the tray menu and launch it again after entering the session.

Starting the executable twice normally brings the existing settings window forward; it does not create a second live instance.

## I cannot click or drag an overlay

Overlays are click-through outside **Move overlays** mode. Enable it with the Overlays page, tray menu or Ctrl + Alt + Shift + E, drag the overlay, then finish Move mode.

A red border means that overlay's settings page is open. The dashed yellow border means Move mode. The red selection border alone does not enable dragging.

## A layout keeps changing or the wrong one is active

Check layout conditions and their order. The first matching layout wins after a situation is stable for one second. Put Garage/In car/Out of car layouts above an Always layout at the end. If no layout applies, every overlay is hidden. A manual choice lasts until the situation changes; then automatic selection can apply again.

Each layout owns its content settings. If a column or module seems to revert, confirm that you are editing the intended layout and instance.

## A shortcut is marked In use

Another application or Windows already owns that global key combination. Click the shortcut under **Settings** and choose another combination containing Ctrl or Alt. Esc cancels, Backspace disables the binding, and the reset arrow restores its default.

Check the status banner for the conflicting shortcut names. Local Race Control keys apply only in that window.

## Fuel and strategy values are missing

| Symptom | What to check |
| --- | --- |
| No consumption average | Complete whole green laps; partial, pit, out, caution and refueling laps are excluded |
| AVG 10 has a smaller count | Fewer than ten valid samples have been learned; the displayed count is intentional |
| Missing finish projection | Race state, usable leader/field progress and pace are required |
| Blank opponent FUEL | Same class, supported observed stint start/refill and at least three clean consumption samples are required |
| STOPS has a question mark | The session was joined late or the history is incomplete |
| Unknown stint age | A start/exit was not observed, telemetry was interrupted, or replay/pit state invalidates it |
| No pit exit estimate | Your car must be racing on track outside the pits, with enough timing information |
| Pit loss includes `?` | A requested service component has an unknown duration |

Blank opponent fuel does not mean the opponent certainly needs a stop. The estimate assumes a full tank and your car's range; it cannot read an opponent's actual fuel. See [Fuel and pit strategy](fuel-and-pit-strategy.md).

Fuel history resets for a session/car change. Watching historical replay does not train new live fuel or stint evidence.

## Weather trends or wind direction are missing

Weather history starts when RaceMate observes the session. Trend arrows require at least five minutes of history and compare against ten minutes earlier when possible. The temperature graph covers up to twenty minutes and labels its available duration.

Car-relative wind needs a heading for the car being driven or watched. If iRacing does not report one, that information stays unavailable. Current precipitation is not a forecast of rain elsewhere on the circuit or later in the session.

## Track Map is blank or misaligned

1. Enable Track Map, which starts disabled, and choose the intended **Map/Circle/Line** layout.
2. For the official Map, inspect its track/load status and try **Retry**. An uncached layout needs access to iRacing's public asset server.
3. Confirm the exact TrackID/layout is supported. The official Map intentionally leaves unknown layouts empty.
4. If the track draws without markers, its direction may be unresolved or car positions may be unavailable.
5. If labels are missing, enable Corner numbers/names. Some circuits have numbers only. Retry bypasses the six-hour wait after a failed label request.
6. Use Circle or Line when you want a lap-position display without SVG downloads.

Misalignment in particular corners can result from schematic SVG distance differing from simulator distance. Report the TrackID, layout, direction and locations of the error, including whether start/finish aligns. Follow the [validation checklist](track-map-validation.md).

**Background** is the official surroundings layer; **Background and frame** is the overlay panel. **Pit lane** toggles the drawing; **Cars in the pits** separately toggles markers.

## Race Control cannot replay an event

Get out of the car before using camera/replay controls. Check **Replay from** and the chosen camera group. In Preview these actions are simulated. In a live session, old footage may no longer exist in iRacing's replay buffer; RaceMate stops waiting after twelve seconds and explains the limitation.

Previous/Next follow filtered events chronologically, not the visible sort order. Clear the car/category filters if expected events seem absent. The feed contains only observed events from the current session, up to its 2,000-event limit.

## Update check or installation fails

Use **Settings → Updates → Check now** to see an explicit result. Startup failures are intentionally quiet. Check that the machine can reach GitHub; availability and network errors are shown in the settings row.

If RaceMate cannot replace its executable in the current folder, download the matching architecture from [releases](https://github.com/gaggi/RaceMate/releases), quit via **Exit**, then replace the executable manually in a user-writable location. Keep `settings.ini`; it lives separately. If no compatible direct asset exists, the button opens the release page instead.

After moving the executable, turn **Start with Windows** off and on from the new location to update the startup path.

## Closing the window did not stop RaceMate

That is normal when a tray icon is available. Choose **Exit** in the tray menu to stop it. Without a working tray icon, closing the main window exits. **Start minimized** also depends on a working tray icon.

## Unexpected CPU usage

Check per-overlay rates. Drawing costs far more than reading iRacing, which happens 60 times a second regardless, and more visible instances mean more rendering work. Try lower rates for slowly changing panels, and disable unused instances.

To record a short profile, quit RaceMate and start its executable with:

```powershell
.\RaceMate.exe --profile
```

Every ten seconds it overwrites `%LOCALAPPDATA%\RaceMate\profile.txt` with the preceding interval's timings. Capture the issue, then quit and restart normally to disable profiling. See [Development](development.md#profiling-and-benchmarks).

## Reporting a reproducible issue

Include the RaceMate version/architecture, Windows version, monitor arrangement/scaling, iRacing display mode, session type, track/layout and car. Describe the steps, expected result and actual result; mention driving, live spectating or replay and whether Preview reproduces it.

A screenshot and the relevant setting names are often enough. A profile is useful for performance reports. Developers can use `--live-summary` or `--dump-vars` as described in [Development](development.md#live-telemetry-inspection); inspect telemetry dumps before sharing because session information can contain driver and session identifiers.
