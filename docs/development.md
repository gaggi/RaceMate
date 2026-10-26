# Development

[Documentation](README.md) · [Architecture](architecture.md)

## Toolchain

RaceMate is a Windows desktop application using **C++20**, **Win32**, **GDI/GDI+** and resource compilation. Install **Visual Studio 2022 Build Tools** with Desktop development with C++, a Windows SDK and CMake support. CMake 3.21 or later is required by the project.

The build script selects the **Visual Studio 17 2022** generator. If CMake is not on PATH, it locates the Visual Studio installation with `vswhere` and uses its bundled CMake. MSVC builds link the C++ runtime statically.

## Build and test

Run commands from the repository root:

```powershell
.\build.ps1                         # x64 Release, tests, package in dist/x64
.\build.ps1 -Architecture Win32     # x86 Release, tests, package in dist/Win32
.\build.ps1 -Diagnostics            # Also build and run the GUI diagnostics
.\build.ps1 -NoPackage              # Build/test without copying release artifacts
```

Choose another build directory when needed:

```powershell
.\build.ps1 -Architecture x64 -BuildDirectory build/docs-x64 -NoPackage
```

The script stops on a failed configure, build, test or diagnostic command. A test failure prevents packaging by that invocation. For the normal x64 configuration, the production executable is `build/x64/Release/RaceMate.exe` and packages go to `dist/x64`.

| Target | Purpose |
| --- | --- |
| `racemate_core` | Telemetry-independent analysis/model library |
| `RaceMate` | Production GUI executable |
| `RaceMateDiagnostics` | GUI checks, render captures and tick benchmarks; excluded from the default build target set |
| `RaceMateTests` | Core, telemetry, prediction, pit and event regression tests |
| `RaceMateUpdateTests` | Update metadata/parser, file replacement and updater regression tests |
| `RaceMateTrackMapTests` | Track identity, SVG preparation, projection and asset validation tests |

CTest registers **CoreAndTelemetry**, **Updater** and **TrackMap**. To rerun the suite after an existing build:

```powershell
ctest --test-dir build/x64 -C Release --output-on-failure
```

If CTest is not on PATH, use `ctest.exe` beside the CMake executable selected by `build.ps1`, or rerun the build script. Individual test executables can also be run directly:

```powershell
.\build\x64\Release\RaceMateTests.exe
.\build\x64\Release\RaceMateUpdateTests.exe
.\build\x64\Release\RaceMateTrackMapTests.exe
```

## GUI diagnostics

```powershell
.\build.ps1 -Diagnostics -NoPackage
```

After the diagnostic target has been built, it can be run separately:

```powershell
& .\build\x64\Release\RaceMateDiagnostics.exe --diagnostics | Out-Host
```

Piping to `Out-Host` makes PowerShell wait for this GUI executable, as the build script does. Check its exit code as well as `build/qa/controls.txt`.

Diagnostics exercise the actual native windows with controlled sample data. Checks cover navigation, DPI, persistence, layout switching, columns, forecasts, weather, pit states, replay controls, splitters and overlay visibility. BMP captures are written under `build/qa`.

Diagnostics and GUI benchmarks use **`build/qa/settings.ini`**, not personal preferences. Diagnostic startup does not register Windows startup or perform an update check. Official-map checks use the separate **`build/qa/trackmaps/v1`** cache and skip layouts without a `source.txt` marker; the report identifies skipped real-map checks. An existing incomplete/older map cache can still trigger optional-layer retrieval through the normal loader, so use a complete current cache for offline reproduction.

Keep the command's working directory at the repository root: QA output locations are relative to it.

## Documentation screenshots

```powershell
.\tools\ExportDocumentationScreenshots.ps1
```

Run this after successful GUI diagnostics. It converts a curated set of native BMP captures into lossless PNGs under `docs/images`, preserving their size and contents. The official-map captures additionally need cached **Lime Rock GP (TrackID 353)** assets and corner labels when diagnostics run.

To prepare that cache, use the normal application's Preview with Map enabled and turn on corner labels once with network access. Copy the complete `%LOCALAPPDATA%\RaceMate\trackmaps\v1\353` folder, including its metadata files, to **`build/qa/trackmaps/v1/353`**. Then run diagnostics again. If a required capture is missing, the exporter fails before writing any output. Existing captures may be stale, so confirm that the new diagnostic run exercised the cached maps and refreshed their files.

See [image provenance and the capture mapping](images/README.md). Do not use real driver/session data as sample screenshots without checking what is visible. Do not commit the entire QA directory, telemetry dumps or the runtime map cache.

## Profiling and benchmarks

To inspect real application work, start the normal executable with:

```powershell
.\build\x64\Release\RaceMate.exe --profile
```

Quit any existing normal instance first; the single-instance behavior otherwise focuses it. Every ten seconds RaceMate replaces `%LOCALAPPDATA%\RaceMate\profile.txt` with timings for the last interval: telemetry, YAML parsing, analysis, overlay update/presentation/screen copy and process CPU. Restart without the switch when finished.

For controlled comparisons:

```powershell
& .\build\x64\Release\RaceMateDiagnostics.exe --benchmark-tick | Out-Host
& .\build\x64\Release\RaceMateDiagnostics.exe --benchmark-live | Out-Host
.\build\x64\Release\RaceMateTests.exe --benchmark-telemetry
.\build\x64\Release\RaceMateTests.exe --benchmark-prediction
```

The tick benchmark writes `build/qa/benchmark.txt`. The live benchmark runs the real application for 30 s against `RaceMateFakeIRacing`, a stand-in for iRacing that publishes 60 cars at 60 Hz under shared-memory and event names of its own, and writes `build/qa/live-benchmark.txt`: CPU in cycles for the process and the main thread, frames processed, the main thread's time per part, and for every overlay how evenly it refreshes and how old its data is. Windows' own CPU times are sampled on its 15.6 ms clock and miss timer-driven work, so compare cycles. Treat results as measurements of that machine, build, scene and configuration. More visible overlays or a faster map rate changes the workload. GUI diagnostics also report some rendering timings and GDI handle checks in `controls.txt`.

In normal operation RaceMate requests low process priority and EcoQoS. It samples disconnected telemetry about once per second, parses session YAML only after an update, shares analysis results and refreshes overlays at their individual rates. A faster enabled overlay raises telemetry sampling as needed.

## Live telemetry inspection

These commands require a running iRacing session:

```powershell
.\build\x64\Release\RaceMateTests.exe --live-summary
.\build\x64\Release\RaceMateTests.exe --dump-vars
```

**`--live-summary`** reports lap-time coverage and live/replay state. **`--dump-vars`** writes available telemetry variables and session YAML to `irsdk-dump.txt` in the working directory. It can contain identifying session/driver information; inspect it before sharing. These files are debugging inputs, not documentation assets.

## Track assets and generated resources

Update the factual Track Map source index with:

```powershell
.\tools\UpdateTrackMapIndex.ps1
.\tools\UpdateTrackMapIndex.ps1 -MetadataPath C:\path\to\iracing-tracks-metadata.json
```

Without `-MetadataPath`, the script retrieves the metadata snapshot source online. It validates IDs and official CDN paths, then writes `src/TrackMapSources.inc`. This does not bundle the SVGs or make every indexed layout projectable; real geometry still needs validation.

Validate a folder containing official map assets with:

```powershell
.\build\x64\Release\RaceMateTrackMapTests.exe --assets C:\path\to\map-assets
```

The folder should contain `active.svg`, `start-finish.svg`, and optionally `pitroad.svg` and `turns.svg`. Follow [simulator validation](track-map-validation.md) for spatial correctness.

Brand logos are white PNGs under `resources/brand-logos`, named for the manufacturer. Regenerate their atlas and source include with:

```powershell
.\resources\generate-brand-logos.ps1
```

This produces `resources/brand-logos.png` and `src/BrandLogos.inc`. Icon-generation source is in `resources/generate-icon.ps1`. Review generated artifacts with their source changes.

## Release workflow

The version is declared in `project(RaceMate VERSION ...)` in [CMakeLists.txt](../CMakeLists.txt). Before publishing a release, update that version, validate the change and commit it. A matching pushed `v*` tag triggers the [release workflow](../.github/workflows/release.yml).

For a version that is ready to publish, the tag sequence is:

```powershell
# Example only: use the new version declared in CMakeLists.txt.
git tag v0.2.0
git push origin v0.2.0
```

The workflow checks tag/version agreement, builds and tests **x64** and **Win32/x86** on Windows, then publishes the executable, ZIP and SHA-256 file for each architecture. Each ZIP currently contains `RaceMate.exe` and the root `README.md`; use its absolute documentation links when reading the packaged README outside a checkout.

The `.sha256` file hashes the corresponding **ZIP**, not the standalone executable. The in-app updater separately verifies the executable's `sha256:` digest when GitHub release metadata provides one.

The updater selects the asset whose name ends in `windows-x64.exe` or `windows-x86.exe`. Preserve that suffix in release naming. Custom builds can configure `RACEMATE_GITHUB_OWNER` and `RACEMATE_GITHUB_REPOSITORY`; defaults point to `gaggi/RaceMate`.

## Keeping documentation current

When changing behavior, update the relevant guide and any option/range/default tables. Refresh screenshots when the visual workflow changes. Keep proposed features explicitly separate from implemented behavior, and include [third-party notices](third-party.md) when adding external code or assets.

Use repository-relative Markdown links between committed files and images so they work on GitHub. The root README also provides an absolute documentation link for release ZIP readers. Avoid local absolute image paths in committed Markdown.
