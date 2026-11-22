# Versionshinweise (Deutsch)

Die letzten drei Versionen aus `CHANGELOG.md`, übersetzt: Der Startbildschirm
zeigt sie in dieser Sprache. Die Überschriften (`## Version - Datum`) bleiben
wie in `CHANGELOG.md`: Über sie wird eine Version ihrer Übersetzung
zugeordnet. Eine Version, die in dieser Datei fehlt, erscheint auf Englisch.

## 0.12.3 - 2026-11-22

- Die RetroAchievements-Meldung erscheint nicht mehr bei jedem Spielstart.
- Die Meldungen zu freigeschalteten Erfolgen, freier Kamera, voller Tasche und Schnellspeichern sind auf Französisch, Deutsch, Spanisch und Italienisch übersetzt.
- Das Leuchten um eine Figur (die Aura des Blitzschwerts) ist wieder eine dünne weiße Kontur und kein breiter Lichtschleier mehr auf dem Boden.
- Der Name des Gebiets am unteren Bildrand erscheint auf Französisch, Deutsch, Spanisch und Italienisch vollständig (Akzente werden als einfache Buchstaben gezeichnet); er wurde beim ersten Akzent oder Bindestrich abgeschnitten oder fehlte.

## 0.12.2 - 2026-11-22

- Der Rückwärtssprung im Kampf ist bei 60 fps nicht mehr viel zu hoch und zu weit.

## 0.12.1 - 2026-11-22

- Der Import eines PCSX2-Spielstands hinterlässt bei manchen Memory Cards keinen leeren Spielstand mehr (das Spiel meldete ihn als beschädigt); ein erneuter Import über den Installer repariert ihn.
- Mit einer europäischen Disc startet der Text des Spiels beim ersten Start in der Sprache des Decks (die von Steam, sonst die des Systems), wie der Installer und das Menü; Englisch, wenn diese Sprache nicht zu den fünf gehört.
- Die Steinhund-Monster haben ihre Texturen (sie waren grau und durchsichtig).
- Das Laden eines Savestates lässt das Spiel mit den europäischen Texten nicht mehr einfrieren oder abstürzen und bringt das Pausenmenü nicht mehr durcheinander.

## 0.12.0 - 2026-11-22

- Der Installer kann deinen PCSX2-Spielstand des Spiels importieren: Er sucht deine Memory Card selbst (PCSX2, EmuDeck, RetroDECK, SD-Karte); findet er keine, kannst du eine Datei oder einen Ordner von Hand wählen.
- Die Logos beim Start lassen sich mit einer Taste überspringen (A, B, X, Y oder Start), auch beim ersten Start.
- Das „A drücken“ des Ladebildschirms erscheint, sobald das Spiel geladen ist, nicht erst nach dem ganzen Geschichtstext.
- Der Startbildschirm macht leise Geräusche: ein leichtes Ticken beim Bewegen der Auswahl, ein Tippen beim Drücken einer Taste, ein warmer Akkord, wenn Spielen das Spiel startet.
- Ein neues Spiel startet mit der Kameradrehung auf „Invertiert“; bestehende Spielstände behalten ihre Wahl.
- Keine Tastenkombinationen mehr: L4 (F1) öffnet das Menü. Entwicklerwerkzeuge entfernt.
- Nativer Renderer, schonender auf dem Deck: stabile 60 fps, etwa 4 W weniger. Startet mit 2x. Texturpakete werden vorerst nicht gezeichnet.
- Keine Option „Vollbild“ und kein F11 mehr: Das Spiel ist auf dem Deck immer im Vollbild.
- Schnellladen behält Musik, Kämpfe und Texturen, auch nach einem Neustart des Spiels.
- Schnellladen während der Kampfbelohnungen verliert die Texturen der Kulisse nicht mehr (neu schnellspeichern: ältere behalten das Problem).
- Fortsetzen lädt deinen letzten Schnellspeicherstand, nicht den Ort, an dem du das Spiel verlassen hast.
- Schnellspeicherstand überschreiben: Die Frage beginnt bei Überschreiben, nicht bei Abbrechen.

## 0.11.0 - 2026-11-19

- Fortsetzen: Der Startbildschirm setzt dort fort, wo du das Spiel verlassen hast.
- Speicherstände: 5 Plätze, F2 zum Speichern, F3 zum Laden (siehe `docs/SAVESTATES.md`).
- Rückseitentasten beim Hinzufügen zu Steam belegt: L4 Menü, L5 Pause, R4 Speichern, R5 Laden.
- Bebilderte Tipps im Installer, während das Spiel gebaut wird.
- Ladeübergang nach Spielen.
- Startbildschirm und Einstellungen neu gestaltet; das Update steht jetzt in „Neuigkeiten“.
- Das Menü ist ein einziges zentriertes Fenster mit den neuen Seiten Speicherstände, Aktionen und Hilfe.
- Die Pause liegt auf F5 (vorher F2).
- Das Spiel pausiert, wenn sein Fenster den Fokus verliert (neuer Standard).
