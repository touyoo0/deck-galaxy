# Versionshinweise (Deutsch)

Die letzten drei Versionen aus `CHANGELOG.md`, übersetzt: Der Startbildschirm
zeigt sie in dieser Sprache. Die Überschriften (`## Version - Datum`,
`### Added`) bleiben wie in `CHANGELOG.md`: Über sie wird eine Version ihrer
Übersetzung zugeordnet. Eine Version, die in dieser Datei fehlt, erscheint auf
Englisch.

## 0.12.0 - 2026-11-22

### Changed

- Keine Tasten- oder Tastenkombination bleibt übrig: Ansicht + Menü (Zurück + Start) öffnet das Menü nicht mehr, Alt+Eingabe und Umschalt+F12 entfallen. L4 des Decks (F1) öffnet es, und der Hinweis beim Start sagt das. Die Option „Zurück + Start öffnet das Menü“ entfällt. Die im Spiel verbliebenen Entwicklerwerkzeuge (die Option „Entwicklerwerkzeuge“, `RG_DEV_TOOLS`) sind samt ihren Kombinationen ebenfalls weg. Der Hinweis „Verbunden: Controller“ erscheint nicht mehr, ebenso wenig die Tastenhinweise unter den Neuigkeiten des Startbildschirms.

## 0.11.0 - 2026-11-19

### Added

- Fortsetzen: Der Startbildschirm bietet an, dort weiterzumachen, wo das Spiel verlassen wurde. Das Spiel behält einen eigenen Zustand, wenn sein Fenster in den Hintergrund geht (Steam-Menü) und beim Beenden; Spielen beginnt weiterhin am Titel. Ein Zustand lässt sich jetzt auch aus jedem Spielbereich laden.
- Die Fortschrittsseite des Installers zeigt beim Kompilieren bebilderte Tipps in der Sprache des Fensters, alle 9 Sekunden einen.
- Ein Ladeübergang nach Spielen: Der Startbildschirm löst sich in einem Sternenstrom auf, dann bleibt ein Ladebildschirm, bis das Spiel zeichnet.
- Spielstände zum Schnellspeichern: fünf Plätze, die das ganze Spiel in einem Moment festhalten und sofort laden, im selben Lauf oder nach dem Schließen des Spiels. F2 öffnet die Plätze zum Speichern, F3 die zum Laden; jeder hat ein Bild und die Uhrzeit, und ein belegter Platz fragt vor dem Überschreiben nach. Die neue Seite Spielstände im Menü bietet dasselbe für den Controller. Etwa 10 MB pro Zustand. Nicht während eines Videos.
- Beim Hinzufügen zu Steam werden auch die Rücktasten des Decks belegt: L4 öffnet das Menü (F1), L5 pausiert (F5), R4 speichert einen Zustand (F2) und R5 lädt einen (F3). Eine bereits angelegte Belegung bleibt unangetastet.

### Changed

- Im Menü überdeckt die Zeile, die sagt, was eine Option braucht (eine Disc, ein Neustart), die Beschreibung nicht mehr: Sie steht darunter.
- Die Hinweise zu den japanischen und europäischen Discs und zum Aktualisieren nennen die `.bat`-Dateien der Windows-Version nicht mehr: Sie sagen, die Disc im Installer hinzuzufügen und auf dem Startbildschirm Aktualisieren zu nutzen.
- Das Spiel pausiert standardmäßig, wenn sein Fenster nicht mehr vorn ist (Steam-Menü, anderes Fenster): Oberfläche > Im Hintergrund pausieren, zuvor aus. Eine vorhandene Einstellungsdatei behält ihre Wahl.
- Die Hilfezeile unter einer Option läuft über die ganze Fensterbreite statt in einer schmalen Spalte neben dem Namen.
- Das Menü hat keine Leiste am oberen Rand mehr: Alles liegt im Fenster in der Mitte, mit einer neuen Seite Spielstände, einer Seite Aktionen (Pause, Vorspulen, Stumm, Screenshots, Vollbild, Renderer-Statistik) und einer Seite Hilfe (Tastenkürzel, Projektseite, Problem melden, Info), alle mit dem Controller erreichbar. F1 öffnet dieses Fenster, und Y führt nicht mehr zu einer Leiste.
- Die Pause liegt jetzt auf F5 (vorher F2), um Platz für die Tasten der Spielstände zu schaffen.
- Startbildschirm und Einstellungen sind neu gestaltet: flache dunkle Panels mit feiner Kante, eine weiße Pille unter dem gewählten Eintrag, dünne Regler, weiche Schalter und ein einziger Bernstein-Akzent statt des weißen Doppelrahmens, der goldenen Platten und des Dreiecks-Cursors. Der Startbildschirm verliert seine bewegten Funken und Schlieren und behält Bild, Logo und Notizen.
- Das Update des Startbildschirms ist kein Knopf neben Spielen, Optionen und Beenden mehr: Es steht oben im Panel „Neuigkeiten“, eine Karte mit der neuen Version, dem Fortschritt und den Fehlern, eine ruhige Zeile, wenn nichts zu tun ist, und ein goldener Punkt am Titel, solange ein Update wartet. Am Controller öffnet Rechts das Panel und A startet das Update.

### Removed

- Der Profiler (`RG_PROFILE`) und die Hardware-Überwachung (`RG_HWWATCH`): Sie liefen nur unter Windows und taten auf dem Deck nichts.
- Der Bildraten-Zähler des Menüs (Ansicht > Bildrate, F3, samt Einstellung): Das Steam Deck hat einen eigenen.

## 0.10.0 - 2026-11-18

### Added

- Installer, Startbildschirm, Einstellungen, Beenden-Fenster und Update-Meldungen gibt es auf Englisch, Französisch, Deutsch, Spanisch und Italienisch. Die Sprache ist die von Steam (sonst die des Systems), bis du eine wählst: oben rechts im Installer oder im Spiel unter Einstellungen > Oberfläche > Menüsprache. Auch die Hinweise der letzten drei Versionen sind übersetzt (`docs/changelog/`).
