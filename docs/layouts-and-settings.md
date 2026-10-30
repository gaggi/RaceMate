# Layouts and settings

[Documentation](README.md) · [Overlay reference](overlays.md)

## Position and appearance

Open **Overlays** and expand the row for an overlay. Its placement settings belong to that overlay in its own layout.

| Setting | Available choices | Effect |
| --- | --- | --- |
| Size | 50–150%, in 10% steps | Scales the overlay |
| Opacity | 40–100%, in 10% steps | Controls the overlay's opacity; **Background only** keeps text, badges and foreground at full opacity |
| Fixed corner | Top left, top right, bottom left, bottom right | Keeps that corner in place when content changes size |
| Updates | 1, 2, 5, 10, 15 or 20 Hz | Controls that overlay's refresh rate |

The fixed corner is useful for standings near the right edge or a panel above the taskbar: adding rows, columns, a header or a footer grows the panel away from that corner. Changing the corner does not itself jump the overlay to a screen corner.

Choose **Settings** in the row for content options. **Rounded corners** is a separate setting on that page; square corners are the default. **Reset positions** returns overlays to their default places on the main monitor.

## Automatic layouts

![Layout list with independent arrangements and activation conditions](images/layouts.png)

*Each layout is an expandable group with its own overlays and activation condition.*

A layout owns its overlays, their names, placement, size, opacity, refresh rates and content settings. Two Relatives in different layouts can look completely different. Click its header or arrow to open or close its settings and overlay group. The header settings contain its name and **Active when** controls. The layout you edit does not determine which layouts run.

### Creating a layout

1. Select **Add layout** and choose a copy of the layout being edited or default overlays.
2. Click its header to expand the group and set its name and activation condition.
3. Configure the overlays inside that group. Dragging layout rows changes their display order, not their activation priority.

| Active when | Applies |
| --- | --- |
| In car | While you drive your car |
| Out of car | While you spot other cars or watch a replay |
| Garage | While the garage screen is open |
| Always | Alongside every other matching layout |

### Matching layouts

**Every layout whose condition currently matches is active at the same time.** An Always layout can therefore supply persistent OBS overlays while separate In car and Out of car layouts supply the driver's screen overlays. There is no first-match priority.

| Layouts | Driving | Spotting or replay | Garage |
| --- | --- | --- | --- |
| Always | Always | Always | Always |
| In car | In car | None | None |
| Garage, In car, Always | In car + Always | Always | Garage + Always |
| In car, Out of car | In car | Out of car | None |

When **no condition matches**, no layout is activated as a fallback. Overlay windows, rendering buffers and OBS channels for inactive layouts are released; their saved configuration remains available for editing.

Opening an overlay's **Settings**, or expanding its inline placement controls, automatically shows that overlay on screen with a red editing frame. If it would otherwise be inactive or hidden, it uses isolated sample data, without activating its layout or publishing those sample images to OBS. Closing the editor removes that temporary preview. Live overlays continue to use the live data. A map preview uses a circle when no official map geometry is available.

Each layout has its own **Preview** / **Stop preview** button in its title row. Several layouts can be previewed independently, including layouts whose conditions do not currently match. Sample previews use a yellow **Preview** badge; the green **Active** badge is reserved for matching layouts using real SIM data. Previews remain on screen and never publish sample images to OBS. Live telemetry continues throughout. If a previewed layout becomes live, its live data takes priority; stopping its preview does not hide its live overlays.

**Move overlays** lets you arrange the currently displayed overlays. Any layout can be removed, including Default and the last remaining layout. An empty layout list remains empty after restarting; no fallback layout is recreated. The original Default layout initially retains older settings, but its name gives it no special activation behavior.

## Multiple instances

**Add overlay** creates another **Relative**, **Standings**, **Fuel Calculator**, **Weather** or **Track Map**. Each has independent settings and placement, and an initial numbered name such as **Weather 2**. Expand the overlay row and edit **Name** to rename it. An empty name restores the default. Renaming preserves its settings but updates its Spout sender label; select the new sender name in OBS afterward. Pit Assist, Side Warning, Radar, Traffic and Adjustments are not offered as additional instances.

Useful arrangements include a second Weather overlay showing only the wind rose, a compact Relative beside a fuller Standings table, or an official Map alongside a wide Line map.

The bin button removes an added instance and its settings after confirmation. For original instances, use the visibility switch.

## Headers and footers

Every overlay supports a header and footer with up to **four metrics each**. The first slot is left aligned; the remaining slots are right aligned. Choose **None** for unused slots. All overlays offer the same metric list.

Footers are off by default. Pit Assist also starts without a header. **Settings → Headers on all overlays** switches headers globally; each overlay page also has its own header controls. A transparent Track Map hides its frame, captions, header and footer while retaining their settings.

See the [metric reference](overlays.md#header-and-footer-metrics) for which values describe the camera car, your own car or the overall leader.

## Global settings

![General Settings page with startup, rates and strategy settings](images/settings.png)

*This sample configuration shows a +1-lap SAVE allowance. The factory default is +3 laps.*

| Setting | Behavior |
| --- | --- |
| Start with Windows | Adds RaceMate to the current user's Windows Run key; administrator rights are not needed |
| Start minimized | Starts in the tray when the tray icon is available |
| Headers on all overlays | Changes header visibility across overlays |
| Additional laps with fuel saving | 0–10, default +3; shared opponent SAVE allowance |
| Pit lane loss | Learned or manually fixed per track |
| Service times | Four-tire time and refuel rate per car and rule set |
| Check for updates on startup | Checks GitHub and reports availability in the interface |

RaceMate reads every frame iRacing publishes, 60 a second, so event recording, pit histories, consumption learning and finish forecasts see every frame. Each overlay is drawn only at its own rate, always on whole iRacing frames: 15 Hz is exactly every fourth frame, 66.7 ms apart. Rates of 1, 2, 5, 10, 15 and 20 Hz all divide 60, so every overlay refreshes at even intervals.

## Keyboard shortcuts

| Default | Action |
| --- | --- |
| Ctrl + Alt + Shift + E | Move overlays |
| Ctrl + Alt + Shift + H | Hide/show overlays |
| Ctrl + Alt + Shift + S | Open settings |
| Ctrl + Alt + Shift + Left | Previous Race Control event |
| Ctrl + Alt + Shift + Right | Next Race Control event |

To change a shortcut, click its button and press a combination containing **Ctrl or Alt**. **Esc** cancels; **Backspace** disables it. The reset arrow restores a changed shortcut's default. A conflict appears as **In use**, and the main banner identifies unavailable shortcuts.

These shortcuts work while another application has focus. Race Control's own **F**, **Enter** and divider keys are local to that window; see [Race Control](race-control.md).

## Settings backup and reset

RaceMate writes settings as you change them and remembers the main window's size and position. Its normal configuration is:

```text
%LOCALAPPDATA%\RaceMate\settings.ini
```

To back up, quit through the tray menu and copy `settings.ini` somewhere safe. To restore or migrate, quit RaceMate, put the saved file in the same local-app-data path on the target machine, then launch it. Overlay coordinates may need adjustment on another monitor arrangement; use **Reset positions** if necessary.

To reset all preferences, quit and **rename** `settings.ini` to a backup name. RaceMate creates a fresh configuration on its next launch. The official map cache is separate, under `trackmaps/v1`, so resetting preferences does not remove downloaded maps.

The INI is an implementation format. Prefer the interface for edits; added overlays and layouts use separate numbered sections. GUI diagnostics and benchmarks use `build/qa/settings.ini` instead of personal settings.

## Removing RaceMate

Turn off **Start with Windows** before deleting or moving the executable, then quit using **Exit**. Delete the executable or extracted folder. If you also want to remove preferences, learned pit settings and cached maps, remove `%LOCALAPPDATA%\RaceMate` after making any backup you need.

The startup entry is the `RaceMate` value under `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. The application manages it through **Start with Windows**.
