# ADR 0002: ONDAS-Stabilisierung — Pfadwahl (Citrinitas)

Status: angenommen

## Kontext

Der Implementationsplan `docs/ondas-stabilization-plan.md` (Albedo) beschreibt drei
Lücken (A/B/C) zur Einbringung des audit-verifizierten ONDAS-Wissens. Dieses ADR
vergleicht für jede Lücke die realen Implementierungsalternativen und wählt den
Pfad, der Eleganz, Effizienz und Codebase-Konformität (ADR 0001) optimal ausbalanciert.
Alle Pfade bauen ausschließlich auf der **korrigierten** Mathematik auf:
Nah-Kommensurabilität schlägt (nicht exakte Ganzzahligkeit), Phyllotaxis in den
**Sampler** (nicht ins Spektrum), Ferocity-Preis `1/(1−d)³`, Dwell-Parkbrunnen
`sin(2δ)` im **Fehler**-Frame.

---

## Lücke A — Golden-Angle-Strobe / Phase-Envelope-Diagnostic

### A.1 Schicht (wo lebt das Diagnostic?)

| Option | Bewertung |
|---|---|
| **A. Haskell `PhaseEnvelope.hs` (IO-frei)** | ✅ gewählt |
| B. C++ OrniCore | OrniCore nutzt Sinus-Kinematik (nicht Firmware-Pfad), ist nur Referenz/Fallback; die Flap-Phase lebt im Haskell-Oszillator |
| C. Python-nur-Diagnose | Diagnostik gehört nach ADR 0001 in den Math-Core, nicht in Testskripte |

**Entscheidung: A.** Rein-mathematisch, `cabal test`-fähig ohne Unreal, faithful
Port-Quelle `ondas_metrics.c` vorhanden. Die Phase (`mixPhase`) und der Reversal-Zeitpunkt
werden von `advanceOscillator` erzeugt — das Modul gehört daneben, nicht ins C++-Fallback.

### A.2 Sampler-Mechanik (wie wird der Bin gewählt?)

| Option | Bewertung |
|---|---|
| **A. Golden-Angle-Akkumulator** `acc += 648095; bin=(acc>>16)&0xF` | ✅ gewählt |
| B. Phasen-indizierter Bin (`bin = gemessene Phase`) | „fester Raster“, aliast systematisch auf Harmonische — Negativkontrolle in Test A.4.2 |
| C. Fester-Zeit-Strobe | NAH-kommensurabel (2π/0.0048=1308.997) — schlägt |

**Entscheidung: A.** `648095 = ⌊φ⁻¹·16·2¹⁶⌋`, `gcd(648095, 2²⁰)=1` ⇒ der Akkumulator
durchläuft alle 2²⁰ Zustände und die 16 Bins exakt gleichverteilt (je 2¹⁶ Treffer).
Irrationale Rotation fällt nie systematisch auf eine Harmonische — immun gegen die
gesamte Obertonreihe. Option B/C werden **nicht** gebaut, sondern nur als Test-Gegenmodell.

### A.3 Reversal-Detection + Strobe-Größe (Präzisierung des Plans)

**Entscheidung:** Reversal = **Sinusoid-Nulldurchgang** `sign(sin φ)` wechselt
(`prevSin·sin ≤ 0 ∧ prevSin ≠ sin`), `downstrokeEnded = prevSin > 0` — faithful zu
`pid.c:1846`. Gestrobt wird **`|pitchErrorRate|`** (Born2Flap-Äquivalent von
`itermErrorRate`, **vor** dem RESONANCE-Filter), **nicht** die Flap-Phase selbst —
faithful zu `pid.c:1954` `ondasMetricsPhaseStrobe(fabsf(itermErrorRate), …)`.
Der Plan-Schritt A.2 („Reversal aus Phase-Sprung π“) wird hiermit präzisiert:
der Nulldurchgang der Sinusoid (≡ `sin mixPhase`) ist der korrekte Trigger; die
gestrobte Größe ist die Fehlerrate, nicht die Phase.

---

## Lücke B — Ferocity-Preis-Validierung (1/(1−d)³)

### B.1 Validierungs-Schicht

| Option | Bewertung |
|---|---|
| **A. Haskell-Unit (Derivativ-Skalierung) + Python-DLL (End-to-End-Leistung)** | ✅ gewählt |
| B. nur Python | verliert den reinen, schnell-iterierbaren Kern-Test |
| C. nur Haskell | die mechanische Leistung `mechanical_power_w` lebt im DLL-Fahrzeugpfad |

**Entscheidung: A.** Zwei komplementäre Behauptungen: (1) die Wellenform-Derivative
skaliert mit `1/(1−d)` — rein, cabal-Test (`ferocityPriceWave`, shapeMix=0); (2) die
mechanische Leistung folgt `1/(1−d)³` — End-to-End, Python-DLL (`ferocity_price.py`).

### B.2 Assertions-Richtung

**Entscheidung: einseitig „von unten“** (`P_gemessen ≤ P_analytisch` für alle f).
Rationale: Servo-Sag, Backdrive und Stall können die gelieferte Leistung nur
**reduzieren**, nie erhöhen. Eine Überschreitung der analytischen Kurve ist daher ein
Modellfehler (rotes Flag → Test stoppt), eine Unterschreitung wird physikalisch
ausgebucht (Sag/Backdrive/Stall-Anteil). Absolute Watt-Werte bleiben Kalibrierung.

---

## Lücke C — RESONANCE / Phasen-Lock-Stabilisierung

### C.1 Tracker-Algorithmus

| Option | Bewertung |
|---|---|
| **A. Vold–Kalman 1. Generation** (2 Zustände, w=ω, bw=2ζω, ζ=0.06) | ✅ gewählt |
| B. fester Bandpass | verliert die Flap-Band beim Chirp (Test C.4.1 zeigt es) |
| C. PLL / erweitertes Kalman | Overkill für eine einzelne Ordnung; komplexere Loop-Auslegung |

**Entscheidung: A.** Faithful Port von `ondas_tracker.c`, sim-validiert (2.5× Extraktion,
ρ −0.504 Tracker vs 0.001 fester BP). Zwei Zustände, expliziter Euler, ω<0.1 rad/s → Reset.

### C.2 Kopplungs-Frame (Parkbrunnen)

| Option | Bewertung |
|---|---|
| **A. `sin(2δ)` Fehler-Frame** (rotierend) | ✅ gewählt |
| B. `sin(2θ)` Lab-Frame | pinnt Phase auf festen Winkel, destabilisiert (validiert: invertiert) |

**Entscheidung: A.** Note 94: `dδ/dt = η − 2κ·d·sin(2δ)`. Option B wird **nicht** gebaut,
aber als permanenter Regressionstest C.4.3 als „verboten“ ausgeschlossen.

### C.3 Injektionspunkt in den Regelpfad

| Option | Bewertung |
|---|---|
| **A. `kGainMod` (Phasen-Advance-Bedarf)** | ✅ gewählt |
| B. direkter Phasen-Offset | umgeht das Washboard, Pin-Risiko |
| C. `cadenceTarget`-Modulation | falsche Zeitskala (verschiebt das Beat-Gitter) |

**Entscheidung: A.** `advanceOscillator` übersetzt `kGainMod` via Washboard in eine
quantisierte Beat-Landung; `Firmware.hs:418` setzt es heute hart auf `1`. Der
RESONANCE-Ausgang ersetzt genau diese `1`. Kein zweiter Phasen-Integrator entsteht.

### C.4 Washboard ↔ Dwell-Kopplung

**Entscheidung: geschichtet, kein Ersatz.** Washboard (`−ω₀²·sin(φ_offset)…`) bleibt der
**ganzzahlige Beat-Lock** (ganzer Schlag über die π-Barriere). Dwell-Kopplung
(`−2κ·d·sin(2δ)`) ist der **kontinuierliche Fein-Lock darüber** im rotierenden
Fehler-Frame. Beide wirken auf `advanceOscillator`, aber auf verschiedenen Zeitskalen
und in verschiedenen Frames.

---

## Konsequenzen

- Alle drei Lücken sind rein-mathematisch/IO-frei im Haskell-Kern testbar (`cabal test`);
  nur Lücke A erzwingt eine ABI-Erweiterung (v2→v3, append-only +2 Doubles).
- Die Negativ-Kontrollen (fester Raster, fester BP, `sin(2θ)`-Lab-Frame) werden als
  dauerhafte Regressionstests eingefroren, um Rückfall auf die invertierte Prämisse
  auszuschließen.
- Kein Schritt bricht einen früheren; Reihenfolge: B.1→B.2→C.1→C.2→A.1→A.2→A.3→B.3→A.4.3→C.4.4.
- Kalibrierung (κ, σ, absolute Watt) bleibt außerhalb dieses Tasks; die Pfade liefern
  nur relativ/numerisch validierte Größen.
