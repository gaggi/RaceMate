# RaceMate documentation

[Project home](../README.md) · [Download releases](https://github.com/gaggi/RaceMate/releases)

These guides describe the implementation in **RaceMate 0.2.1**. Screenshots show the real Windows interface with diagnostic sample data. Some screenshots deliberately enable optional modules and columns; they do not represent the factory defaults or a live race.

## User guides

| Guide | What you will learn |
| --- | --- |
| [Getting started](getting-started.md) | Download, first launch, Preview, positioning, connecting and daily use |
| [Overlay reference](overlays.md) | Relative, Standings, Fuel Calculator, Pit Assist, Weather, columns and shared metrics |
| [Layouts and settings](layouts-and-settings.md) | Automatic layouts, extra instances, appearance, shortcuts, startup and backups |
| [Fuel and pit strategy](fuel-and-pit-strategy.md) | Consumption learning, finish forecasts, opponent estimates and pit exit predictions |
| [Track Map](track-map.md) | Map, Circle and Line, transparency, markers, labels and cached assets |
| [Race Control](race-control.md) | Events, standings, filtering, replay navigation and camera controls |
| [Troubleshooting](troubleshooting.md) | Visibility, connection, missing values, maps, updates and useful bug reports |

For a first session, follow **Getting started**, then adjust the overlays you use most. Read **Fuel and pit strategy** before relying on a finish or rejoin forecast.

## Developer guides

| Guide | Contents |
| --- | --- |
| [Development](development.md) | Build prerequisites, tests, diagnostics, benchmarks, screenshots and releases |
| [Architecture](architecture.md) | Source map, data flow, rendering, persistence and network access |
| [Track Map validation](track-map-validation.md) | Simulator checks for the experimental SVG projection |
| [Leader-stop scenarios](leader-stop-scenarios.md) | Design proposal for future stop assumptions; not a shipped feature |
| [Third-party notices](third-party.md) | NanoSVG, map index provenance and official iRacing assets |
| [Screenshot provenance](images/README.md) | How the documentation images were captured and how to refresh them |

## Reading conventions

| Display | Meaning |
| --- | --- |
| **Bold text** | An interface label or action |
| `~` or `≈` | An estimate rather than a directly measured result |
| `—` | A value is unavailable or does not apply |
| Blank opponent **FUEL** cell | No supported forecast; it does not prove that the car needs fuel |
| **Your car** | The player's car, even while watching somebody else |
| **Reference car** | Your car while driving; usually the camera car while spectating or replaying |

RaceMate is an independent project and is not provided or endorsed by iRacing.
