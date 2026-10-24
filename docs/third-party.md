# Third-party sources

[Documentation](README.md) · [Track Map](track-map.md) · [Architecture](architecture.md)

- **NanoSVG** by Mikko Mononen and contributors: SVG parsing in `src/third_party/nanosvg/nanosvg.h`, retrieved from [the upstream project](https://github.com/memononen/nanosvg) on 2026-10-16. The unmodified source retains its license notice; the [full zlib-style license](../src/third_party/nanosvg/LICENSE.txt) is included. RaceMate uses the path parser and its own native rendering and position projection.
- **Track map index:** factual layout IDs, internal path names and official CDN addresses extracted from the 2026-05-05 [metadata snapshot](https://github.com/meowmachine/racing-track-maps-vector/blob/main/from-iracing/iracing-tracks-metadata.json). Regenerate using `tools/UpdateTrackMapIndex.ps1`. No SVG files, descriptions, or third-party derived track geometries are copied into RaceMate.
- **Official iRacing SVGs:** fetched directly from `https://members-assets.iracing.com/public/track-maps/` and cached locally at runtime. These assets remain the property of iRacing; they are not bundled in this repository or executable.
