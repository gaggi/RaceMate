# Track Map validation

[Documentation](README.md) · [Track Map guide](track-map.md) · [Development](development.md)

This checklist is for validating the experimental official-Map projection in the simulator. For normal settings and usage, read the Track Map guide. Automated geometry tests do not replace these spatial checks.

## Prepare the test

1. Build the version you want to test and quit the existing RaceMate instance through the tray menu. Only one normal instance runs at a time.
2. Start the intended executable, enable **Overlays → Track Map**, and choose **Map**. Configure size, position, opacity and refresh rate as usual.
3. Allow the first download for that layout, or use a valid existing cache under `%LOCALAPPDATA%\RaceMate\trackmaps\v1\<TrackID>`. Preview uses Lime Rock GP with sample cars.
4. Join the exact layout to be checked. Enable **Projection details** to record track ID, cache source and projection status. **Track and layout** is also available as a header metric.

Useful identity cases:

| Track/layout | TrackID |
| --- | --- |
| Lime Rock GP | 353 |
| Lime Rock Chicanes | 354 |
| Okayama Full | 166 |
| Okayama Short | 167 |
| Suzuka GP | 168 |

## On-track checklist

- Cross start/finish and confirm that the car marker crosses the visible line at the same time.
- Check driving direction, then compare the marker in several separated corners and straights.
- Inspect the wrap from just below 100% to 0% lap progress.
- Change layouts and tracks without restarting RaceMate. The previous geometry must disappear, and the new map must match the new TrackID.
- Observe a pit visit. Toggle **Pit lane** and **Cars in the pits** independently: hollow markers should use the drawn pit lane when enabled and the circuit when disabled.
- On a bridge layout, follow both branches of the crossing. Cars should continue through a valid prepared gap rather than reverse along the road outline.
- Compare driving, live spectating, replay and paused playback. The focused ring should follow the appropriate reference car.
- Disconnect and reconnect. Confirm expected hiding and map identity after reconnection.

Use **Retry** after a loading error. Unknown or ambiguous official layouts should report the limitation rather than display a guessed replacement.

## Presentation checklist

Turn **Background and frame** off. Only map content should remain over the simulator; captions, frame and header/footer disappear while their settings are retained. The checkerboard in diagnostic screenshots is only a transparency preview.

Toggle **Pit lane** and **Start/finish marking** independently. Hiding these drawings must not alter the prepared position mapping. Toggle **Background** surroundings and **All configurations** in the Map layout; the view should expand to include other configurations without changing lap alignment.

Enable **Corner numbers** and **Corner names** separately. Check **Label size** from 75% to 200%, the contrast outline, and transparent presentation. Suzuka GP provides numbers; Lime Rock GP and Okayama Full can also supply names. Missing official names must not be invented.

Corner labels request `turns.svg` only when enabled and can extend an existing cache. Missing labels should leave the map usable. Failed label requests wait six hours before another automatic attempt; **Retry** attempts them immediately.

Switch **Text in markers** between car numbers, class positions, overall positions and off. Check crowded groups for acceptable readability. Then test **Circle** and **Line**: these use lap percentage directly and require no SVG download.

## How projection works

The runtime selects the source by exact ID and validates the internal name. It prepares SVG geometry on a worker, extracts start/finish and driving direction from the official marking, and maps `CarIdxLapDistPct` along a length-based path. No manual direction table, learned map or per-track calibration is used.

Bridge gaps can be closed in the position path when their endpoints and directions are compatible; the original SVG drawing remains unchanged. Ambiguous disconnected branches or incompatible open ends are rejected. Missing direction can leave the drawing visible without car markers.

A schematic SVG need not preserve distance uniformly. Alignment at the line with a repeatable error at a hairpin is therefore different from a constant start offset. Pit-lane marker placement is approximate, beside the car's lap position; it is not a precise telemetry position along the pit road.

## Automated checks

`RaceMateTrackMapTests` covers identity, SVG curves/transforms, start/finish, direction, lap wrap, invalid/oversized files and projection edge cases. A synthetic figure-eight checks the bridge gap, crossing traversal, route length and absence of reversal.

To inspect existing official assets:

```powershell
.\build\x64\Release\RaceMateTrackMapTests.exe --assets C:\path\to\map-assets
```

The folder contains `active.svg`, `start-finish.svg`, and optionally `pitroad.svg` and `turns.svg`. GUI diagnostics additionally check filters, saved settings, bitmap reuse, labels, transparency and clearing an unknown layout. Their real-map checks use `build/qa/trackmaps/v1`, separately from the normal application's cache, and report when skipped. See [cache preparation](development.md#documentation-screenshots); no official SVG files are committed for these tests.

The source index is regenerated with `tools/UpdateTrackMapIndex.ps1`, optionally using `-MetadataPath`. At runtime RaceMate does not retrieve that third-party metadata repository; it uses its built-in index and official asset URLs. See [third-party notices](third-party.md).

## Report a useful result

Include the RaceMate version/build, TrackID, internal layout name if available, source/cache state, display mode and screenshots at known points. Describe line alignment, direction and locations where the marker diverges. For example: “Okayama Full, marker matches start/finish but is already past the hairpin while the car is at the apex.”

Also note the reference car, live/replay state and whether the error repeats every lap. This separates geometry/projection errors from delayed telemetry, reference selection and wrong-layout identity.
