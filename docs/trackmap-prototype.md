# Trackmap-Prototyp testen

Branch: `codex/trackmap-prototype`.

1. Die bisherige RaceMate-Instanz über das Tray-Menü beenden. Es kann nur eine Instanz laufen.
2. Die Test-EXE starten und unter **Overlays → Track Map** einschalten. Größe, Position, Transparenz und Aktualisierungsrate funktionieren wie bei den anderen Overlays.
3. Für den ersten Aufruf eines Layouts ist Internet nötig. Danach liegen die offiziellen SVGs unter `%LOCALAPPDATA%\RaceMate\trackmaps\v1\<TrackID>`. Die Vorschau zeigt Lime Rock GP mit Beispieldaten.
4. In iRacing das gewünschte Layout laden. Im Overlay stehen Streckenname, Konfiguration und ID. Lime Rock GP ist 353, Chicanes 354; Okayama Full ist 166, Short 167.
5. Zunächst an Start/Ziel prüfen, ob der Marker beim Überqueren der Linie ebenfalls über die weiße Linie geht. Danach Fahrtrichtung und Position in mehreren Kurven prüfen. Auch den Übergang von 99,x % auf 0 % ansehen.
6. Ohne RaceMate-Neustart das Layout oder die Strecke wechseln. Die alte Karte muss verschwinden und die zum neuen TrackID passende Karte erscheinen. **Retry** in den Track-Map-Einstellungen lädt nach einem Fehler erneut.

**Background and frame** ausschalten zeigt nur die Karte mit Fahrzeugen auf transparentem Hintergrund. Rand, Überschriften, Legende sowie Header/Footer verschwinden dabei; ihre Einstellungen bleiben für den Modus mit Hintergrund erhalten. **Pit lane** und **Start/finish marking** schalten die gezeichnete Boxengasse beziehungsweise Linie mit Richtungspfeil unabhängig voneinander um. Das Ausblenden beeinflusst die Positionsberechnung nicht. **Cars in the pits** steuert separat die Fahrzeugmarker in der Box.

Für Rückmeldungen helfen TrackID/Layout und konkrete Stellen: zum Beispiel „Okayama Full, Marker an der Hairpin schon 30 m zu weit, an Start/Ziel passend“. So lässt sich ein Startversatz von einer Abweichung der gezeichneten Streckenlänge unterscheiden.

## Was der Prototyp macht

- Exakter ID-Abgleich mit zusätzlicher Prüfung des internen Streckennamens; keine Suche nach ähnlich klingenden Namen.
- Einmalige SVG-Aufbereitung auf einem Worker. Fahrbahnflächen werden als solche gezeichnet. Für Positionen wird aus den Randlinien ein Verlauf mit vorberechneter Länge erzeugt.
- Die statische Karte und die geglätteten Fahrzeugsymbole werden als Bitmaps wiederverwendet. Die laufenden Updates ändern Positionen und Beschriftungen.
- Startpunkt aus der Start/Ziel-Markierung und Richtung aus dem Richtungspfeil derselben Datei. Keine streckenspezifischen Richtungslisten oder Korrekturen.
- Brücken mit einer gezeichneten Unterbrechung, etwa in Suzuka: Bei einer einzelnen geschlossenen Fahrbahnfläche wird einmalig die Mittellinie bestimmt. Eine kleine Lücke mit zusammenpassenden Endrichtungen wird nur für die Positionsberechnung geschlossen. Das SVG bleibt unverändert; Fahrzeuge laufen über die Unterführung weiter, statt an der Straßenkante zurückzufahren.
- `CarIdxLapDistPct` wird entlang dieses Verlaufs abgebildet. Ungültige Positionen, Zuschauer und Pace Car werden ausgeblendet. Der weiße Ring folgt beim Zuschauen dem beobachteten Fahrzeug.
- Unbekannte oder nicht eindeutig aufbereitbare Layouts zeigen einen Hinweis. Es gibt kein Selbstlernen und keinen Ersatz durch eine andere Karte.

## Bewusste Grenzen

Die Länge einer schematischen SVG muss nicht an jeder Stelle proportional zur realen Fahrstrecke sein. Genau diese Abweichung soll jetzt im Simulator geprüft werden. Bisher sind keine manuellen Korrekturen eingebaut. Offene Strecken und mehrdeutige, getrennte Fahrbahnverläufe werden abgelehnt; bei fehlender Richtung bleiben die Fahrzeugmarker ausgeblendet.

Fahrzeuge in der Box werden hohl an ihrer Rundenposition auf der Hauptstrecke dargestellt. Eine genaue Zuordnung zur gezeichneten Boxengasse ist noch nicht enthalten. Dicht beieinander liegende Nummern können sich überlagern; **Car numbers** lässt sich für kleinere Punkte abschalten.

## Technische Prüfung

`RaceMateTrackMapTests` prüft Layout-Identität, SVG-Kurven und Transformationen, Start/Ziel, beide Richtungen, Rundenumbruch sowie fehlende, übergroße und unpassende Dateien. Eine eigene Achterstrecke prüft die Brückenlücke, beide Kreuzungsdurchfahrten, die Rundenlänge und das Ausbleiben einer Umkehr. Mit `--assets <Ordner>` lassen sich lokal vorhandene `active.svg`, `start-finish.svg` und optional `pitroad.svg` aufbereiten. Der Diagnosemodus prüft außerdem Filter, Einstellungsspeicherung, Bitmap-Wiederverwendung und das Leeren der alten Karte bei unbekanntem Layout. Echte SVG-Dateien für diese Sichtprüfung liegen ausschließlich im ignorierten Build-/Cache-Verzeichnis.

Den Quellenindex aktualisiert `tools/UpdateTrackMapIndex.ps1`; alternativ kann eine lokale Metadatendatei mit `-MetadataPath` übergeben werden. Die App selbst lädt weder den Index aus dem Drittanbieter-Repository noch benötigt sie einen iRacing-Login.
