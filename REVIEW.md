# Code and settings review — 2026-10-06

Follow-up in 0.4.35: At the user's request, yellow-flag laps now remain in fuel consumption samples by default. Their exclusion is a saved, optional Fuel Calculator setting. The original review's mandatory exclusion below is superseded. Header and Footer buttons now share a row, Column Order sits below, and the automatic-save hint is removed.

Reviewed the native application, settings and rendering includes, shared-memory reader, session parser, standings, prediction and fuel logic, event recording, tests, build script and resources. The checkout has no `.git` directory, so changes could not be recorded as a Git commit. Pre-edit source and test copies are in `build/review-before`.

## Assessment

Code alone does not establish whether an AI wrote it. The project has substantial working functionality and useful synthetic telemetry tests. The maintenance problems are concrete: long functions, densely packed statements, numeric control IDs, global application state, textual `.inc` dependencies, duplicated settings patterns and a README with accumulated release notes. The settings look like a conventional native Windows utility; the main usability problem was reaching controls on small or scaled displays.

## Fixed

| Area | Problem | Change |
| --- | --- | --- |
| Race standings | Comparing progress for some pairs and car numbers for others can form a comparison cycle when progress is missing. This violates the sorting algorithm's requirements. | Establish a consistent fallback order, then order the known progress slots independently. Test all six input permutations of a mixed three-car field. |
| Session metadata | Invalid strings became zero; fractional car IDs were truncated and nonfinite numbers reached integer conversions. | Validate integer syntax, bounds and integrality; preserve unknown-value defaults. Recognize textual YAML booleans for spectators and pace cars. |
| Telemetry availability | A present but invalid fuel or clock value was marked known, producing an empty tank or expired race. | Derive availability from the actual value. Real zero fuel and negative overtime remain valid. |
| Incident events | The player's default zero count became a baseline even while that telemetry was unavailable. Its later recovery could invent incidents. | Respect `incidentsKnown`, falling back to session metadata when available. |
| Fuel samples | Waving-yellow flag `0x10` was excluded by prediction logic but included in clean fuel averages. | Exclude it consistently. |
| Settings layout | Fixed-height dialogs could exceed the monitor work area. DPI changes destroyed/reopened the overlay settings, losing uncommitted text and moving the dialog. | Shared scrolling for overlay settings, header/footer settings, column order and field management; focus visibility; in-place DPI/font/layout updates. |
| Settings clarity | Long repetitive labels, weak grouping and no save explanation. | Shorter fuel/column labels, related-option headings and an automatic-save hint. |
| Tray fallback | Closing/minimizing hid the application even when tray icon creation failed. | Keep minimization accessible through the taskbar; close exits when no tray is available. Restore Settings if tray recreation fails. |
| Dialog lifetime | Column-order children depended on implicit destruction while borrowing the owner's font. | Explicit nested-dialog cleanup and owned, independently scaled fonts. |
| Diagnostics/build | About 600 lines of diagnostic code lived in the application entry point; `-Diagnostics` built but did not run the GUI checks. Absolute build paths were incorrectly joined during packaging. | Extract `Diagnostics.inc`, run diagnostics through the build script and resolve the build directory once. |

README whitespace and outdated consumption-row counts were cleaned up. Release metadata is consistently 0.4.34.

## Verification

The new malformed-metadata regression failed against the original implementation before the fix. Verification uses the existing Release build and core/telemetry suites plus new regression cases in `tests/ReviewRegressionTests.cpp` and `tests/CoreTests.cpp`.

Native GUI diagnostics additionally exercise compact dialogs, wheel scrolling, focused-control visibility, restoring a larger window, simulated DPI messages at 96/192/current DPI, retaining uncommitted input, nested-dialog cleanup and the missing-tray window procedures. The existing overlay rendering, settings persistence, event filtering and GDI resource checks remain in place. `build/qa/controls.txt` records the results; native control renders are exported to `build/qa` for visual inspection.

Run `./build.ps1 -Diagnostics` from PowerShell to build, test, export the GUI previews and package the executable. Diagnostics use `build/qa/settings.ini`; personal settings and Windows startup registration are not changed.

## Limits and remaining maintenance work

This review does not prove the absence of every bug. Verification used synthetic telemetry and native diagnostic windows, not an active iRacing race or physical dragging between monitors of different DPI. The 32-bit configuration was not rerun in this review.

The application still uses global state, many numeric IDs and textual includes. A future structural change should move settings persistence and rendering behind explicit interfaces and split the larger functions. Those changes deserve a separate, focused refactor rather than a wholesale rewrite mixed into bug fixes. The simple YAML parser intentionally supports the simulator's known layout rather than the full YAML language.
