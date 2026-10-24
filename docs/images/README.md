# Documentation screenshots

[Documentation](../README.md) · [Development](../development.md#documentation-screenshots)

These PNGs were exported from the actual **RaceMate 0.2.0** Windows interface on **18 October 2026** using the diagnostic target built from the working source. They show controlled sample drivers, fuel, weather and events. Camera/replay actions in Preview are simulated. Optional columns/modules are enabled in several captures to explain their behavior.

The images are native render captures converted losslessly from BMP to PNG. They are not interface mockups. The transparent map's checkerboard is supplied by the diagnostic exporter to reveal transparency; it is not part of the live overlay.

Official circuit artwork visible within map screenshots belongs to iRacing and was loaded through RaceMate's runtime cache. The underlying SVG/cache files are not included with these documentation images. See [third-party notices](../third-party.md).

## Refresh procedure

From the repository root:

```powershell
.\build.ps1 -Diagnostics -NoPackage
.\tools\ExportDocumentationScreenshots.ps1
```

The first command builds, runs CTest and runs the native GUI diagnostics. If CTest fails, the script stops before diagnostics. After investigating/reporting that failure, the already built GUI target can also be run explicitly to obtain render captures:

```powershell
& .\build\x64\Release\RaceMateDiagnostics.exe --diagnostics | Out-Host
```

Confirm an exit code of zero and inspect `build/qa/controls.txt`. Official-map checks require Lime Rock GP assets and `turns.svg` in the separate **`build/qa/trackmaps/v1/353`** cache, and the report must show that cached SVG rendering was exercised. Use normal Preview to populate `%LOCALAPPDATA%\RaceMate\trackmaps\v1\353`, then copy that complete folder and its metadata files into the QA cache before diagnostics. Layouts without a cached source marker are skipped; an incomplete existing cache can cause the normal loader to retrieve optional layers. Verify timestamps so an old optional BMP is not mistaken for a newly rendered capture.

The exporter checks for all source files before writing. Overlay captures keep their size. Main-window captures for layouts, settings, the Relative page and the pit stop estimate are cut to the part the guide explains, without the sidebar and preview banner; the crop rectangles in the script assume 100 % display scaling. The Pit Assist's two states are placed side by side on a transparent background. Visually inspect refreshed PNGs for readable text, missing content, clipping and sample-data provenance before committing them. Commit only the curated images, not the ignored `build/qa` directory.

## Capture mapping

| Committed PNG | Diagnostic BMP basename |
| --- | --- |
| `main-overlays.png` | `main-overlays` |
| `layouts.png` | `main-layouts` |
| `settings.png` | `main-settings` |
| `relative-settings.png` | `main-0` |
| `relative.png` | `overlay-0` |
| `standings.png` | `standings-interval` |
| `standings-pit-exit.png` | `standings-pit-exit-around` |
| `fuel-calculator.png` | `fuel-open` |
| `fuel-save.png` | `fuel-save` |
| `fuel-surplus.png` | `fuel-surplus` |
| `fuel-strip.png` | `fuel-strip` |
| `pit-stop-settings.png` | `settings-pit-stop` |
| `pit-assist.png` | `pit-setup-changes` and `pit-turn-off-limiter`, side by side |
| `pit-lane-progress.png` | `pit-strip-approach`, `pit-strip-lane` and `pit-strip-exit`, side by side |
| `weather.png` | `weather-all` |
| `wind-rose.png` | `weather-wind-rose` |
| `track-map.png` | `trackmap-corner-labels-353` |
| `track-map-transparent.png` | `trackmap-transparent-353` |
| `track-map-circle.png` | `trackmap-circle` |
| `track-map-line.png` | `trackmap-line` |
| `race-control.png` | `race-control` |
| `race-control-fields.png` | `race-control-extra-fields` |
