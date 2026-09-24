# Spielmodi — Ruby-Rulesets

Status: Entwurf · Zukunft · erweitert `docs/game-design.md`

## Grundsatz

Jeder Modus ist ein Ruby-Ruleset mit schmalem Vertrag:

```ruby
# schematisch, nicht final
module Ruleset
  def setup(session); end
  def tick(session, telemetry); end
  def on_event(session, event); end
  def score(session); end
  def finished?(session); end
end
```

Unreal/C++ liefert Telemetrie und rendert; Ruby liefert die Regeln und den
View-Model-Zustand für UMG/Slate (siehe ADR 0001).

## Praxis (Practice Mode)

Der freie Übungsmodus, in dem Fliegen gelernt wird — als Ruby-Ruleset:

- konfigurierbare Assists (Selbststabilisierung, Start-/Landehilfe, Limits)
- Telemetrie-Overlays und Kraftvektoren
- Wind, Böen, Wetter als Übungsbedingungen
- Zeitlupe, Replay-Scrubbing, schneller Restart
- Windkanal- und Testbench-Ansicht
- Ghost-Sticks und Sollkorridore
- Erklärung, warum ein Manöver scheiterte

## Rennmodi (mehr davon)

Jeder Rennmodus ist ein eigener `RaceRules`-Ableger:

| Modus | Beschreibung |
| --- | --- |
| Sprint | Punkt-zu-Punkt, möglichst schnell |
| Circuit | Runden mit Gates, Splits und Klassen |
| Gate-Slalom | Tore in Folge, verpasste Tore = Strafzeit |
| Zeitangriff | Bestzeit gegen eigene/globale Ghosts |
| Energieausdauer | gleiche Energie, maximale Distanz |
| Staffel | Abschnitte, Übergabe per Gate |
| Liga/Saison | Punkte über mehrere Rennen, Ränge |

## Weitere Modi

- Freestyle & Tricks (Punkte für Manöver)
- Precision Landing (Punktlandung, Wind)
- Thermik & Gleitflug (Effizienz, Energie)
- Delivery & Rescue (Transport, Zielpunkte)
- Konstrukteurs-Challenges (baue, fliege, optimiere)
- Test Bench & Windkanal

## Regeln, die Ruby verdrahtet

- Klassen- und Tuning-Grenzen pro Rennmodus
- Strafen, Splits, Bonus-/Malus-Punkte
- Freischaltung und Progression (Hangar, Teile)
- modusübergreifende Saisons und Leaderboards

## Warum Ruby

Weil Regeln der Inbegriff von Redstone sind: klein, schaltbar, verdrahtbar —
und headless mit `minitest` testbar, bevor sie die Engine sehen. Siehe
`docs/ruby-as-redstone.md`.
