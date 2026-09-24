# Multiplayer — Regeln im Ruby-Brain

Status: Entwurf · Zukunft · baut auf `docs/game-design.md` (Multiplayer-Reihenfolge)

## Grundsatz

Alle Multiplayer-Regeln leben im Ruby-Brain. Unreal/C++ liefert nur
Transport, deterministische Eventqueues und Telemetrie. Die Physik bleibt
lokal-deterministisch (fester Schritt, Haskell-Kern); das Netzwerk überträgt
**Eingaben und Ereignisse**, nicht Zustandsdifferenzen.

## Reihenfolge (aus game-design.md, erweitert)

1. lokale Time Trials
2. Ghosts und teilbare Replays
3. asynchrone Leaderboards
4. kleine synchrone Rennen (2–8 Flieger)
5. (später) Saisons und Ligen

Netzwerkphysik wird erst begonnen, wenn der lokale Fixed-Step-Kern stabil ist.

## Ruby-Module

| Modul | Verantwortung |
| --- | --- |
| `SessionRules` | Lebenszyklus: Lobby, Start, Abbruch, Rematch |
| `RaceStateMachine` | Zustände: waiting → countdown → racing → finished → results |
| `GateRules` | Durchfliegen, Reihenfolge, verpasste Tore, Strafzeit |
| `ScoringRules` | Punkte, Splits, Strafen, Wertungsklassen |
| `GhostValidation` | Validität eines Replays/Geists (Telemetrie-Fingerprint) |
| `LeaderboardRules` | Aggregation, Ränge, Saisons, Anti-Spam |
| `FairPlayPolicy` | Anti-Cheat: Fingerprints, Plausibilität, Mod-Flags |

## Netcode-Rollenverteilung

```text
Inputgerät → Unreal C++ → Haskell Kommando/Zustand
Ruby RaceRules → validierte Kommandos → Unreal (Transport)
Telemetrie → begrenzte Queue → Ruby (Regeln + Fairness)
```

- **Autorität der Regeln:** Ruby entscheidet Tore, Splits, Sieg — nicht der
  Client.
- **Determinismus:** gleiche Eingaben + gleicher Kern = gleicher Flug.
- **Ghosts/Replays:** Telemetrie-Fingerprint (Hash über Eingabe- und
  Kernversion), damit Replays reproduzierbar und fälschungssicher sind.

## Anti-Cheat / Fairness

- Replays validieren gegen den eigenen Fingerprint.
- Physikverändernde Plugins markieren Session und Leaderboard als modifiziert
  (siehe ADR 0001 Plugin-Capabilities).
- Ruby prüft Plausibilität aggregierter Telemetrie (keine Energie aus dem
  Nichts, keine unmöglichen G-Kräfte).

## Synchrone Race-Modi

- Sprint (Punkt-zu-Punkt)
- Runde (Circuit)
- Gate-Slalom
- Energieausdauer (gleicher Tank, wer fliegt weiter)
- Zeitangriff mit Ghosts

Details siehe `docs/game-modes.md`.
