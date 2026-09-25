# ONDAS-Stabilisierung — Implementationsplan (Born2Flap)

> Albedo-Dokument: die „geweißte“ Lösung. Konkret, geordnet, testbar.
> Jede Lücke (A/B/C) benennt Module, Typ-Signaturen, ABI-Änderungen,
> Testfälle mit erwarteten Zahlen, Reihenfolge und Definition of Done.
> Gültig für Haskell-MathCore (`MathCore/src/Born2Flap/Math/`), Native-ABI
> (`Native/include/born2flap_math.h`), Python-Tests (`Native/tests/`).

---

## 0. Verbindliche mathematische Grundlage (audit-verifiziert)

Alle folgenden Arbeiten bauen ausschließlich auf der **korrigierten** Mathematik auf.
Die frühere, invertierte Prämisse („ganzzahlige Obertöne schlagen“, „Phyllotaxis im
Spektrum“) ist **falsch** und darf nirgends übernommen werden.

1. **Kommensurabilität (korrigiert).** Exakt-kommensurable ganzzahlige Obertöne
   (1, 1/3, 1/5, 1/7, …) **schlagen nicht** — ihre Summe ist strikt periodisch.
   Was schlägt, ist **Nah-Kommensurabilität** (z. B. 2π/0.0048 = 1308.997, fast ganzzahlig).
   Schwebung braucht eine kleine Verstimmung, nicht exakte Ganzzahligkeit.

2. **Golden-Angle-Strobe / Phyllotaxis (korrigiert).** Phyllotaxis gehört in den
   **Sampler** (wie die Phase abgetastet wird), nicht ins Spektrum. Ein Strobe, der
   die Flap-Phase an golden-angle-versetzten Punkten abtastet (Schritt
   `648095 = ⌊φ⁻¹·16·2¹⁶⌋` für 16 Bins), fällt **nie** systematisch auf eine
   Harmonische der Obertonreihe und ist daher gegen die gesamte Obertonreihe immun.
   Ein Strobe mit fester Rate oder bei Stroke-Reversal mit **fester** Phase ist es nicht.

3. **Ferocity-Preis (9.5, verifiziert).** Bei Dwell-Fraktion `d`:

   | Größe | Skalierung | d=0.05 | d=0.3 | d=0.8 |
   |---|---|---|---|---|
   | Kraft / Drehmoment | `1/(1−d)²` | 1.108 | 2.04 | 25 |
   | Schub | `1/(1−d)`   | 1.053 | 1.43 | 5 |
   | Leistung | `1/(1−d)³` | 1.166 | 2.92 | 125 |

4. **Phasen-Lock-Dwell (Note 94).** Dwell ist Phasen-Lock-Kopplung im **rotierenden**
   Frame. Modell `dδ/dt = η(t) − 2κ·d·sin(2δ)`: δ = Phasenfehler, η = Wind-Phasenrauschen,
   d = Dwell-Fraktion, κ = Kopplung. **Kritisch:** der Parkbrunnen muss `sin(2δ)`
   (Fehler-Frame) nutzen, **nicht** `sin(2θ)` (Lab-Frame) — Lab-Frame pinnt die Phase auf
   einen festen Winkel und destabilisiert. Referenzwerte (`sim_ferocity.rb phaselock`,
   ω=4.8, κ=8.0, σ=0.6, DT=0.001, srand(1), 20 s):
   d=0.05 → **10.8×** Phasen-Jitter-Reduktion, d=0.8 → **43×**.

---

## 1. Architektur-Einordnung

```mermaid
graph TD
    RC[RC-Kanäle] --> MIX[Firmware.hs computeServoMixer]
    MIX --> OSC[Waveform.hs advanceOscillator + shapeWave]
    OSC --> SRV[Servo.hs stepServo]
    SRV --> WING[Wing.hs/Vehicle.hs Aerodynamik]
    WING --> HT[hingeTorque] --> SRV
    SRV --> ABI[FFI.hs / born2flap_math.h] --> UE[Unreal 240 Hz]
    A[Lücke A: PhaseEnvelope.hs Golden-Angle-Strobe] -. Diagnose bei Reversal .-> OSC
    A -. phase_envelope/coverage .-> ABI
    B[Lücke B: Ferocity-Preis 1/(1-d)^3] -. Dwell-Sweep .-> OSC
    C[Lücke C: OrderTracker.hs + PhaseLockDwell sin(2d)] -. RESONANCE-Schicht .-> OSC
```

**Rein-mathematisch / IO-frei** (Haskell-Kern, sofort `cabal test`-testbar):
`PhaseEnvelope.hs` (A), `OrderTracker.hs` (C1), `Resonance.hs` (C2, pure ODE),
Ferocity-Preis-Helfer (B, Waveform-Ebene).

**ABI/Unreal** (braucht `born2flap_math.h` + `FFI.hs` + Python-`Output`-Update + C++-Bridge):
Durchreichen von `phase_envelope`, `phase_coverage` (A) und optional `resonance_*` (C).

**Kalibrierung** (braucht physische Messdaten, NICHT in diesem Task):
absolute Kopplungsstärke κ, Wind-σ, absolute Leistungswerte — werden hier nur als
relativ/numerisch validierte Größen behandelt.

---

## 2. Lücke A — Golden-Angle-Strobe / Phase-Envelope-Diagnostic

**Ziel:** ein reines, IO-freies Poincaré-Phasen-Hüllen-Diagnostic nach dem
korrigierten 9.7 (Strobe immun gegen Obertonreihe), als faithful Port von
`src/main/flight/ondas_metrics.c` (OrniFlight).

### A.1 Modul `MathCore/src/Born2Flap/Math/PhaseEnvelope.hs` (neu)

```haskell
{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}
module Born2Flap.Math.PhaseEnvelope
  ( goldenAngleStep, phaseBins, phaseEma
  , PhaseEnvelopeState(..), defaultPhaseEnvelope
  , PhaseEnvelopeMetric(..)
  , phaseStrobe, phaseEnvelope, phaseCoverage
  , detectReversal
  ) where

import Data.Bits
import Data.Word

-- ⌊φ⁻¹ · 16 · 2^16⌋ = 648095 (0x9E39F). gcd(648095, 2^20) = 1 → der Akkumulator
-- durchläuft alle 2^20 Zustände; die oberen 4 Bit (Bin-Index) besuchen alle 16 Bins
-- exakt gleichverteilt (je 2^16 Treffer über 2^20 Strobes).
goldenAngleStep :: Word32
goldenAngleStep = 648095

phaseBins :: Int
phaseBins = 16

phaseEma :: Double
phaseEma = 0.0625  -- 1/16 pro Strobe

data PhaseEnvelopeState = PhaseEnvelopeState
  { peAccum :: !Word32       -- 32-Bit-Golden-Angle-Akkumulator
  , peMeans :: ![Double]     -- 16 EMA-Bin-Mittelwerte
  , peMask  :: !Word16       -- Coverage-Bitmaske
  } deriving stock (Eq, Show)

defaultPhaseEnvelope :: PhaseEnvelopeState
defaultPhaseEnvelope = PhaseEnvelopeState 0 (replicate phaseBins 0) 0

data PhaseEnvelopeMetric = PhaseEnvelopeMetric
  { peSupMean  :: !Double  -- sup |bin mean| — Phasen-Lock-Diskriminator
  , peCoverage :: !Double  -- abgedeckter Anteil 0..1
  } deriving stock (Eq, Show)

-- | Ein Strobe-Event: trägt |errorAbs| in den golden-angle-gewählten Bin ein.
-- Port von ondasMetricsPhaseStrobe (Leak aller Bins, dann EMA-Add auf den Bin).
phaseStrobe :: Double -> PhaseEnvelopeState -> PhaseEnvelopeState
phaseStrobe errorAbs state =
  let acc'   = peAccum state + goldenAngleStep
      bin    = fromIntegral ((acc' `shiftR` 16) .&. 0xF)
      means' = [ m * (1 - phaseEma) | m <- peMeans state ]
      means'' = addAt bin (errorAbs * phaseEma) means'
  in PhaseEnvelopeState acc' means'' (peMask state .|. (1 `shiftL` bin))
  where
    addAt :: Int -> Double -> [Double] -> [Double]
    addAt i v xs = take i xs ++ [xs !! i + v] ++ drop (i + 1) xs

phaseEnvelope :: PhaseEnvelopeState -> Double
phaseEnvelope = maximum . map abs . peMeans

phaseCoverage :: PhaseEnvelopeState -> Double
phaseCoverage state =
  fromIntegral (popCount (peMask state)) / fromIntegral phaseBins

-- | Stroke-Reversal-Detektion aus zwei aufeinanderfolgenden Phasen [0,2π):
-- Überqueren der Downstroke-Grenze (limiar) ODER Wrap 2π→0 (Upstroke→Downstroke).
detectReversal :: Double -> Double -> Double -> Bool
detectReversal prev curr limiar =
  (prev < limiar && curr >= limiar) || (curr < prev)
```

### A.2 Integration in FirmwareVehicle / OscillatorState

1. `FirmwareVehicleState` bekommt ein Feld
   `fvPhaseEnvelope :: !PhaseEnvelopeState` (Default `defaultPhaseEnvelope`).
2. In `advanceFirmwareVehicle` (nach `stepServo`, wo `mixPhase`/`servoAngleDeg`
   bekannt sind) wird pro Schritt gerechnet:

   ```haskell
   prevPhase = oscPhase (fwOscillator (fvFirmware state))  -- vor advanceOscillator
   limiar    = limiarFromFerocities (mixStrokeFerL mix) (mixReturnFerL mix)
   reversal  = detectReversal prevPhase (mixPhase mix) limiar
   trackErr  = abs (mixLeftFlapDevDeg mix - servoAngleDeg nextServoL)  -- deg
   peNext    = if reversal then phaseStrobe trackErr (fvPhaseEnvelope state)
                           else fvPhaseEnvelope state
   ```

   Der **gestrobte Wert** ist der Servo-Tracking-Fehler (links) in Grad — ein
   physisches, phasen-kohärentes Signal, dessen Phasen-Lock die Hülle misst.
3. Das Feld `fvPhaseEnvelope` wird in `nextState` gesetzt.

> Hinweis: `limiarFromFerocities` ist in `Waveform.hs` bereits exportiert; die
> Reversal-Logik ist identisch zur Down/Upstroke-Grenze der Wave-Shaping.

### A.3 ABI-Erweiterung (ABI-Version 2 → 3)

`Native/include/born2flap_math.h` — `B2F_FirmwareOutput` **append-only** erweitern
(bestehende Offsets 0..10 bleiben unverändert, nur `flags` verschiebt sich):

```c
#define B2F_MATH_ABI_VERSION 3u

typedef struct B2F_FirmwareOutput {
    double force_n[3];             /* 0..2  unverändert */
    double moment_n_m[3];          /* 3..5  unverändert */
    double mechanical_power_w;     /* 6 */
    double maximum_separation;     /* 7 */
    double left_flap_deg;          /* 8 */
    double right_flap_deg;         /* 9 */
    double battery_soc;            /* 10 */
    double phase_envelope;         /* 11  NEU: sup|mean| (deg) */
    double phase_coverage;         /* 12  NEU: 0..1 */
    uint32_t flags;                /* Byte-Offset 104 (bit 0 = is_flapping) */
} B2F_FirmwareOutput;
```

`FFI.hs` `pokeFwOutput`: von 11 auf **13** Doubles erweitern
(`phaseEnvelope (fvPhaseEnvelope state)`, `phaseCoverage (fvPhaseEnvelope state)`
vor `flags` schreiben), `flags` auf Byte-Offset 104.

`Native/src/born2flap_math_bridge.cpp` (C++-Fallback): die beiden neuen Felder mit
`0.0` füllen (Fallback besitzt kein Phase-Envelope-Diagnostic).

### A.4 Tests (mit erwarteten Zahlen)

**A.4.1 Golden-Angle-Uniformität (unit, `cabal test`).**
2¹⁶ = 65536 identische Strobe-Events (`phaseStrobe 1.0` wiederholt):
- `phaseCoverage` == 1.0 (alle 16 Bins besucht);
- jeder Bin exakt **4096** Treffer (deterministisch, weil `gcd(648095, 2^20)=1`).
- Assertion auf die Bin-Zählung über ein abgeleitetes `peAccum`/`peMask`-Histogramm.

**A.4.2 Phasen-Aliasing-Vermeidung (unit, der korrigierte 9.7-Kern).**
Signal `e(θ) = |sin(3θ)|`, zwei Sampler:
- **Fester-Rate-Sampler** bei θ₀ = π/6: jeder Strobe liefert `|sin(3·π/6)| = 1`
  → `phaseEnvelope` (sup|mean|) **= 1.0** (spurious Phasen-Lock).
- **Golden-Angle-Strobe** über 65536 Reversals: `phaseEnvelope` → Zyklusmittel
  `2/π ≈ 0.6366`.
- Assertion: `phaseEnvelope(golden) < 0.65` **und** `phaseEnvelope(golden) <
  phaseEnvelope(fixed)` — der Strobe aliast nicht auf die Harmonische.

**A.4.3 Nah-Kommensurabilität (DLL-Test, `Native/tests/phase_envelope.py` neu).**
Treibe den echten Firmware-Vehicle-Loop bei konstanter Flap-Cadence, verstimme die
Abtast-Rate der Telemetrie leicht (z. B. Abtastung alle `1/239.9` s statt 1/240 s =
Nah-Kommensurabilität 1308.997-artig) und zeige: `phase_envelope` bleibt beim
golden-angle-Strobe klein (quasi-uniform, ≈ Zyklusmittel), während ein fester
Abtast-Raster ein deutlich größeres `phase_envelope` liefert. Erwartungswerte werden
beim ersten Lauf aufgezeichnet und als Regression eingefroren (relativer Test, keine
absolute Kalibrierung).

### A.5 Verknüpfung zur Frame-Raten-Inkonsistenz

`docs/rc-controls.md` dokumentiert: „der native Solver läuft bei 240 Hz, aber Kräfte
werden pro Game-Frame gemittelt, daher sind Trajektorien über 30/60/144 FPS nicht
identisch.“ Der golden-angle-Strobe macht die **Phase-Envelope-Telemetrie**
frame-raten-**robust**: weil der Strobe quasi-uniform über die Flap-Phase streut,
driftet sein Mittel nicht mit der Game-Frame-Abtastung. Die neue Metrik
`phase_coverage` dient explizit als Kanal, um diese Robustheit im 30/60/144-FPS-Test
zu belegen (Coverage → 1.0 bei allen drei Raten, `phase_envelope` konvergent).

### A.6 Definition of Done (Lücke A)

- [ ] `PhaseEnvelope.hs` kompiliert, rein/IO-frei, `cabal test` grün (A.4.1, A.4.2).
- [ ] `fvPhaseEnvelope` durch `FirmwareVehicle` gefädelt; Reversal-Detektion nutzt
      `detectReversal` mit demselben `limiar` wie die Wave-Shaping.
- [ ] ABI v3: `phase_envelope`/`phase_coverage` in `born2flap_math.h` + `FFI.hs` +
      C++-Fallback + Python-`Output` (13 Doubles) konsistent; bestehende Offsets 0..10 unverändert.
- [ ] `phase_envelope.py` reproduziert „Strobe schlägt nicht, fester Raster schlägt“.
- [ ] Dokumentierte Annahme + Einheit: gestrobter Wert = |Servo-Tracking-Fehler| [deg],
      EMA = 1/16, 16 Bins, Schritt 648095.
- [ ] Ohne Unreal testbar (`cabal test` + `Native/tests/phase_envelope.py`).
- [ ] Kein Replay-Bruch: Append-only-ABI, ABI-Version gebumpt, alte Felder stabil.

---

## 3. Lücke B — Ferocity-Preis-Validierung (1/(1−d)³)

**Ziel:** den Energie-Test (`Native/tests/aerodynamic_flight.py`) gegen die analytische
Leistungs-Quittung `1/(1−d)³` legen und Abweichungen buchführen.

### B.1 Analytischer Preis (Helfer, IO-frei)

```haskell
-- Born2Flap-Dwell-Mapping: d = ferocity01 · kWaveMaxDwell, ferocity01 = f · 0.125,
-- kWaveMaxDwell = 0.98 (Waveform.hs). f ∈ [0,8] → d ∈ [0, 0.98].
dwellFromFerocity :: Double -> Double
dwellFromFerocity f = clamp 0 8 f * 0.125 * 0.98

-- Audit-verifizierte Quittung (9.5). Nur die Skalierung, keine Absolutwerte.
ferocityPowerPrice :: Double -> Double  -- d → Leistungs-Verhältnis
ferocityPowerPrice d = 1 / (1 - d) ** 3

ferocityForcePrice :: Double -> Double  -- d → Kraft/Drehmoment-Verhältnis
ferocityForcePrice d = 1 / (1 - d) ** 2

ferocityThrustPrice :: Double -> Double  -- d → Schub-Verhältnis
ferocityThrustPrice d = 1 / (1 - d)
```

Kanonische Stützwerte (als Unit-Test-Referenz): d=0.05 → 1.166/1.108/1.053;
d=0.3 → 2.92/2.04/1.43; d=0.8 → 125/25/5 (jeweils Leistung/Kraft/Schub).

### B.2 Dwell-Sweep auf Waveform-Ebene (unit)

`shapeWaveWithDerivative` komprimiert die cosHalf-Rampe um `1/(1−d)`. Für symmetrische
Ferocity (fD=fS ⇒ `limiar = π`) und `shapeMixPercent = 0` (Default) skaliert das
Maximum der Ableitung `d(pulse)/dθ` exakt mit `1/(1−d)`.

**Test `ferocityPriceWave`:** für f ∈ {0.4, 1.0, 2.0, 4.0, 6.53, 8.0}
(→ d ∈ {0.049, 0.1225, 0.245, 0.49, 0.8, 0.98}) das Maximum von
`|d(pulse)/dθ|` über die Downstroke-Rampe bestimmen und gegen `1/(1−d)` (normiert auf
f=0.4) legen. Toleranz ±1 % (die cosHalf-Rampe trägt den Faktor `1/(1−d)` exakt;
die pointed-Transmute `asin(k·cos)/asin(k)` trägt ihn **nicht** — daher wird
`shapeMixPercent = 0` gesetzt).

> **Korrekturbedarf vermerkt:** die pointed-Form (shapeMix > 0) respektiert den Dwell
> **nicht** (sie nutzt das rohe `t`, nicht `u = (t−frontDwell)/(1−d)`). Das ist eine
> potenzielle Preis-Abweichungsquelle bei shapeMix > 0 und wird als offenes Ticket
> festgehalten, nicht stillschweigend „korrigiert“.

### B.3 Energie-Test-Erweiterung (`aerodynamic_flight.py`)

Der bestehende Test (30 s, Level-Body, Masse 0.45 kg, config `1200/8/20/11.1/.08/1.3`)
misst: Gleit −279.62 J, 72 % → +91.58 J, 100 % → +132.95 J. Das ist **Amplituden-**
getrieben (im FFI-Default ist `profThrottleThrustShapeMix = 0` ⇒ `thrustDwellBoost = 0`
⇒ der Dwell bleibt bei **d = 4.0·0.125·0.98 = 0.49** konstant über 72 %/100 %).

**Neuer Dwell-Sweep (eigener Test `Native/tests/ferocity_price.py`):** bei fester
Amplitude die Profil-Ferocity (fD=fS) durchfahren {0.4, 1, 2, 4, 6.53, 8} und
`mechanical_power_w` (Index 6) sowie `left_flap_deg`-Travel (Index 8) mitschneiden.
Assertion: das **Leistungs-Verhältnis** `P(f)/P(0.4)` folgt `1/(1−d)³` **von unten**
(Leistung darf die analytische Kurve nicht überschreiten), Abweichung nach unten wird
quantifiziert und klassifiziert (B.4).

### B.4 Abweichungs-Buchhaltung (was erklärt was)

| Abweichung | Ursache | Erwartung |
|---|---|---|
| `P_gemessen < P_analytisch` bei hohem d | Servo-Sag (`voltFactor`), Backdrive (Stall-Überschreitung → „Wing wins“), reduzierte Amplitude unter Last | **physikalisch erwartet**, kein Modellfehler |
| `P_gemessen > P_analytisch` | Modellfehler / falsche Preis-Skalierung | **rotes Flag**, stoppt den Test |
| Travel-Kollaps bei d → 0.98 | `kWaveMaxDwell = 0.98` + `ampMaxDeg`/`clampRate`-Begrenzung | dokumentieren, nicht „fixen“ |

Der Test bucht die Differenz `P_analytisch − P_gemessen` pro Sweep-Punkt aus und
weist sie mindestens qualitativ (Sag/Backdrive/Stall-Anteil) aus. Absolute Watt-Werte
bleiben **Kalibrierung** und sind nicht Teil der Regression.

### B.5 Definition of Done (Lücke B)

- [ ] `ferocityPowerPrice`/`ferocityForcePrice`/`ferocityThrustPrice` + `dwellFromFerocity`
      im Kern, Stützwerte 9.5 als Unit-Test eingefroren.
- [ ] `ferocityPriceWave` belegt `1/(1−d)`-Skalierung der Ableitung (shapeMix=0, ±1 %).
- [ ] `ferocity_price.py` belegt `1/(1−d)³` von unten; `P_gemessen ≤ P_analytisch` für alle f.
- [ ] Abweichung pro Sweep-Punkt ausgebucht (Sag/Backdrive/Stall) und dokumentiert.
- [ ] Korrekturbedarf „pointed-Form respektiert Dwell nicht“ als Ticket vermerkt.
- [ ] Ohne Unreal testbar; kein bestehender Replay-/ABI-Bruch (nur neue Test-Dateien).


---

## 4. Lücke C — RESONANCE / Phasen-Lock-Stabilisierung

**Ziel:** eine RESONANCE-Schicht oberhalb des Basis-PID (Control.hs), bestehend aus
(a) Vold-Kalman-Ordnungstracker als phasen-gelockter Fehlerfilter und
(b) Dwell-Parkbrunnen-Kopplung `sin(2δ)` im Fehler-Frame als Stabilisierungs-Mode.

### C.1 Modul `MathCore/src/Born2Flap/Math/OrderTracker.hs` (neu)

Faithful Port von `src/main/flight/ondas_tracker.c` (OrniFlight).

```haskell
{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}
module Born2Flap.Math.OrderTracker
  ( OrderTrackerState(..), defaultOrderTracker
  , orderTrackerZeta, orderTrackerOmegaMin
  , stepOrderTracker
  ) where

orderTrackerZeta :: Double
orderTrackerZeta = 0.06        -- Q ≈ 8.3 (sim-validiert)

orderTrackerOmegaMin :: Double
orderTrackerOmegaMin = 0.1     -- rad/s: darunter kein Flapping → Zustände idlen

data OrderTrackerState = OrderTrackerState
  { otX :: !Double  -- Bandpass-Ausgang (flap-kohärente Schätzung)
  , otV :: !Double  -- Ableitungs-Zustand
  } deriving stock (Eq, Show)

defaultOrderTracker :: OrderTrackerState
defaultOrderTracker = OrderTrackerState 0 0

-- Vold–Kalman, 1. Generation: Resonator 2. Ordnung, dessen Zentrumsfrequenz der
-- momentanen Flap-Rate ω folgt. order=1 ⇒ w = ω. Expliziter Euler.
stepOrderTracker :: Double -> Double -> Double -> OrderTrackerState
                 -> (Double, OrderTrackerState)
stepOrderTracker sample omega dt state
  | omega < orderTrackerOmegaMin || dt <= 0 = (0, defaultOrderTracker)
  | otherwise =
      let w  = omega
          bw = 2 * orderTrackerZeta * w
          v' = otV state + (-w*w*otX state - bw*otV state + bw*sample) * dt
          x' = otX state + v' * dt
      in (x', OrderTrackerState x' v')
```

### C.2 Modul `MathCore/src/Born2Flap/Math/Resonance.hs` (neu)

```haskell
{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}
module Born2Flap.Math.Resonance
  ( ResonanceState(..), defaultResonance
  , stepPhaseLockDwell     -- pure ODE, Test-/Validierungs-Kern
  , stepResonance          -- Kopplung in den Regelpfad
  ) where

import Born2Flap.Math.OrderTracker

data ResonanceState = ResonanceState
  { rsTracker    :: !OrderTrackerState  -- C.1 Vold-Kalman-Tracker
  , rsPhaseError :: !Double             -- δ, Fehler-Frame [rad]
  , rsDwell      :: !Double             -- effektive Dwell-Fraktion d (0..1)
  , rsCoupling   :: !Double             -- κ (Dwell→Kopplungsstärke)
  } deriving stock (Eq, Show)

defaultResonance :: ResonanceState
defaultResonance = ResonanceState defaultOrderTracker 0 0 8.0

-- | Der Parkbrunnen im ROTIERENDEN Frame:
--     dδ/dt = η − 2κ·d·sin(2δ)
-- δ = Phasenfehler (Pendelphase vs. freilaufendes Reversal-Gitter), η = Windrauschen.
-- KRITISCH: sin(2δ) im FEHLER-Frame, niemals sin(2θ) im Lab-Frame.
stepPhaseLockDwell :: Double  -- η, Wind-Phasenrauschen [rad/s]
                   -> Double  -- κ, Kopplung
                   -> Double  -- d, Dwell-Fraktion 0..1
                   -> Double  -- dt [s]
                   -> Double  -- δ aktuell [rad]
                   -> Double  -- δ' [rad]
stepPhaseLockDwell eta kappa d dt delta =
  delta + (eta - 2 * kappa * d * sin (2 * delta)) * dt

-- | Ein RESONANCE-Schritt: (1) Tracker auf das gemessene Fehlersignal bei ω laufen
-- lassen, (2) Phasenfehler δ aus Tracker-Phase vs. Oszillator-Phase ableiten,
-- (3) Dwell-Kopplungs-Korrektur −2κ·d·sin(2δ) als Phasen-Advance-Bedarf zurückgeben.
-- Die Rückgabe ist der ONDAS-Phasen-Advance-Bedarf (kGainMod), der in
-- advanceOscillator einfließt — NICHT ein direkter Stellwinkel.
stepResonance
  :: Double  -- measured, flap-kohärentes Fehlersignal (z. B. Servo-Tracking-Fehler)
  -> Double  -- omega, momentane Flap-Rate [rad/s]
  -> Double  -- eta, Wind-Phasenrauschen [rad/s]
  -> Double  -- dt [s]
  -> ResonanceState
  -> (Double, ResonanceState)  -- (kGainMod-Korrektur, next)
stepResonance measured omega eta dt st = ...
```

### C.3 Zusammenspiel Washboard vs. Dwell-Kopplung (Kern-Klärung)

Beide Mechanismen wirken auf `advanceOscillator` (Waveform.hs), aber auf **verschiedenen**
Zeitskalen/Bedeutungen:

| Mechanismus | Gleichung | Was es leistet | Frame |
|---|---|---|---|
| Josephson-**Washboard** (vorhanden) | `φ_offset'' = −ω₀²·sin(φ_offset) − 2ζω₀(φ_offset'−extraTarget)` | **ganzzahliger Beat-Lock**: quantisierter Phasen-Slip um genau einen ganzen Schlag über die π-Barriere; landet immer auf einem Beat | Lab/Beat-Gitter |
| **Dwell-Parkbrunnen** (neu, C.2) | `dδ/dt = η − 2κ·d·sin(2δ)` | **kontinuierlicher Phasen-Lock**: konfiniert δ nahe einem Reversal gegen Wind-Detuning | **rotierender Fehler-Frame** |

**Regel:** das Washboard bleibt der Beat-Gitter-Anker (ganze Schläge); die
Dwell-Kopplung ist die **kontinuierliche** Fein-Stabilisierung **darüber**. Der
Ausgang von `stepResonance` (Phasen-Advance-Bedarf) speist `kGainMod` in
`advanceOscillator` — das Washboard übersetzt ihn dann in die quantisierte
Beat-Landung. Es wird **kein** `sin(2θ)`-Term in den Lab-Frame eingeführt
(das würde die Phase auf einen festen Winkel pinnen und destabilisieren).

### C.4 Tests (mit erwarteten Zahlen)

**C.4.1 OrderTracker (unit).**
- Konstantes ω, Sample `sin(ωt)`: Tracker-Ausgang erreicht **Amplitude 1** (Unity-Gain)
  und **Phasenversatz ≈ 0** am Zentrum (kein Inphase-Verlust wie bei ×sin(θ)-Lock-in).
- Chirp 2.4 → 7.2 rad/s: Tracker hält Unity-Gain, wohingegen ein fester Bandpass
  die Flap-Band verliert. Referenz (`ondas_tracker.h`, sim-validiert): **2.5× Extraktion**,
  ρ −0.504 (Tracker) vs 0.001 (fester Bandpass). Assertion: ρ(Tracker) < −0.4 bei der Chirp.
- `ω < 0.1` ⇒ Ausgang == 0, Zustand resettet (kein linearer Drift auf veraltetem v).

**C.4.2 Dwell-Parkbrunnen (pure ODE) — Note-94-Reproduktion.**
Port von `sim_ferocity.rb phaselock` mit identischen Parametern
(ω=4.8, κ=8.0, σ=0.6, DT=0.001, 20 s, deterministisches η): std(δ) bei
d ∈ {0, 0.05, 0.15, 0.3, 0.5, 0.8} messen und die Reduktion `std0/std` bilden.
Assertion: d=0.05 → **10.8×** (±10 %), d=0.8 → **43×** (±10 %), und die Reduktion ist
streng monoton in d. (Exakte PRNG-Gleichheit zu Ruby ist nicht gefordert; die
**Verhältnisse** sind robuster Steady-State.)

**C.4.3 Lab-vs-Fehler-Frame (Regression gegen die invertierte Prämisse).**
Dieselbe ODE, aber mit **`sin(2θ)`** (Lab-Frame) statt `sin(2δ)`: die Phase pinnt auf
einen festen Winkel und die Jitter-Reduktion **kollabiert/invertiert** (validiert:
invertiertes Ergebnis). Assertion: die Lab-Frame-Variante liefert **keine** monotone
Verbesserung und wird als „verboten“ dokumentiert — schützt dauerhaft vor Rückfall.

**C.4.4 DLL-Integration (optional, nach C.1–C.3).**
`Native/tests/resonance.py`: Wind-Phasenrauschen auf den Firmware-Loop geben und
`phase_envelope` (aus Lücke A) mit/ohne RESONANCE-Kopplung vergleichen; die Kopplung
muss `phase_envelope` (Phasen-Jitter-Proxy) senken. Relativer Regressionstest.

### C.5 Definition of Done (Lücke C)

- [ ] `OrderTracker.hs` kompiliert, rein/IO-frei, Unity-Gain + Chirp + Idle-Test grün (C.4.1).
- [ ] `Resonance.hs`: `stepPhaseLockDwell` reproduziert 10.8×/43× (C.4.2, ±10 %).
- [ ] Lab-Frame-Variante als „verboten“ dokumentiert und per Test ausgeschlossen (C.4.3).
- [ ] Klärung Washboard ↔ Dwell-Kopplung schriftlich fixiert (C.3) und im Modul-Kommentar
      von `Resonance.hs` wiederholt.
- [ ] `stepResonance`-Ausgang ist **Phasen-Advance-Bedarf (kGainMod)**, kein Stellwinkel;
      `advanceOscillator` bleibt der einzige Phasen-Integrator.
- [ ] Ohne Unreal testbar (`cabal test`); kein Replay-/ABI-Bruch (kein ABI-Zwang für C).

---

## 5. Reihenfolge der Implementierung (geordnet, testbar)

Jeder Schritt ist für sich `cabal test`- bzw. DLL-testbar; kein Schritt bricht einen
früheren. Die Reihenfolge folgt „rein-mathematisch zuerst, ABI/Unreal zuletzt“.

1. **B.1** — Ferocity-Preis-Helfer + Stützwerte-Unit-Test (reiner Kern, kein Risiko).
2. **B.2** — `ferocityPriceWave` (Waveform-Ebene, 1/(1−d)-Skalierung, shapeMix=0).
3. **C.1** — `OrderTracker.hs` + Unit-Tests (Unity-Gain/Chirp/Idle).
4. **C.2** — `Resonance.hs` `stepPhaseLockDwell` + C.4.2/C.4.3 (10.8×/43×, Fehler-Frame).
5. **A.1** — `PhaseEnvelope.hs` + A.4.1/A.4.2 (Uniformität, Aliasing-Vermeidung).
6. **A.2** — `fvPhaseEnvelope` in `FirmwareVehicle` fädeln (reine Haskell-Integration).
7. **A.3** — ABI v3 (Header + FFI.hs + C++-Fallback + Python-`Output` 13 Doubles).
8. **B.3/B.4** — `ferocity_price.py` (DLL-Dwell-Sweep, 1/(1−d)³ von unten, Abweichungs-Buch).
9. **A.4.3/A.5** — `phase_envelope.py` (Nah-Kommensurabilität + 30/60/144-FPS-Robustheit).
10. **C.4.4** — optional `resonance.py` (RESONANCE-Kopplung senkt `phase_envelope`).

`MathCore/born2flap-math.cabal`: neue Module `PhaseEnvelope`, `OrderTracker`,
`Resonance` in `exposed-modules` aufnehmen; `MathCore/test/Main.hs` um die neuen
Unit-Tests erweitern.

---

## 6. Korrekturbedarf (Reste der invertierten Prämisse)

Beim Sichten von Code/Docs gefundene Stellen, die auf der **falschen** Prämisse
(„ganzzahlige Obertöne schlagen“ / „Phyllotaxis im Spektrum“) aufbauen — sie werden
**nicht** übernommen, nur markiert:

1. `docs/en/wiki/ondas_development.md`, Abschnitt **9.7** (OrniFlight-Wiki): beschreibt
   die Obertonreihe/phyllotaktisches Spektrum mit **invertierter** Prämisse. Der Audit
   (Note 95) hat dies bereits als fehlerhaft verifiziert. **Korrekturbedarf:** 9.7 auf
   die korrigierte Aussage umschreiben (Nah-Kommensurabilität schlägt; golden-angle-
   Strobe im **Sampler** ist immun). Der neue Plan (Lücke A) ist die korrigierte
   Fassung und hat Vorrang.
2. `Born2Flap` selbst enthält **keine** invertierte Prämisse im Code (kein „Oberton-
   Spektrum“-Pfad vorhanden) — die Lücken A/B/C sind **Neubau**, kein Umbau eines
   falschen Spektrums. Wo `shapeWaveWithDerivative` den Dwell nur auf die cosHalf-Rampe
   (nicht auf die pointed-Form) anwendet, ist das ein **Preis-Konsistenz-**Ticket (B.2),
   keine Kommensurabilitäts-Prämisse.

---

## 7. DoD-Zusammenfassung (eine Zeile je Lücke)

| Lücke | Kern-Deliverable | Test-Kriterium (Zahl) | ABI | Kalibrierung |
|---|---|---|---|---|
| A | `PhaseEnvelope.hs`, golden-angle-Strobe | Coverage 1.0, 4096/Bin; Strobe `phaseEnvelope` < 0.65 und < fester Raster | v3: +2 Doubles (append-only) | nein (relativ) |
| B | Ferocity-Preis + `ferocity_price.py` | `1/(1−d)³` von unten, `P_gemessen ≤ P_analytisch` ∀f | keine Änderung | nein (relativ) |
| C | `OrderTracker.hs` + `Resonance.hs` | 10.8× (d=0.05), 43× (d=0.8), ±10 %; sin(2δ) Fehler-Frame | optional | κ, σ (numerisch fixiert, nicht physisch) |

---

*Das Dokument ist die Albedo-Stufe: die Lösung ist geweißt, die Schnittstellen sind
definiert, die erwarteten Zahlen sind eingefroren. Die citrinitas (Implementierung)
folgt exakt dieser Reihenfolge.*

---

## 8. Citrinitas — Pfadwahl & Rationale (vergleichend, verbindlich)

Die Alternativen je Entscheidungspunkt sind vollständig verglichen und im ADR
[`docs/decisions/0002-ondas-stabilization-paths.md`](decisions/0002-ondas-stabilization-paths.md)
festgehalten (Status: angenommen). Hier die verbindliche Zusammenfassung der
gewählten goldenen Pfade plus zwei Präzisierungen gegenüber der Albedo-Fassung.

### 8.1 Gewählte Pfade (eine Zeile je Entscheidung)

| Lücke | Entscheidung | Gewählt | Verworfen (als Regressionstest eingefroren) |
|---|---|---|---|
| A.1 | Schicht | Haskell `PhaseEnvelope.hs` (IO-frei) | C++ OrniCore / Python-nur |
| A.2 | Sampler | Golden-Angle-Akkumulator `648095`, `bin=(acc>>16)&0xF` | Phasen-indizierter Raster, fester Zeit-Strobe |
| A.3 | Reversal+Strobe-Größe | Sinusoid-Nulldurchgang; `\|pitchErrorRate\|` | Phase-Sprung-π-Heuristik; Flap-Phase als Strobe |
| B.1 | Validierung | Haskell-Unit + Python-DLL (komplementär) | nur Haskell / nur Python |
| B.2 | Assertion | einseitig „von unten“ `P_gemessen ≤ P_analytisch` | symmetrisches ±X % |
| C.1 | Tracker | Vold–Kalman 1. Gen (ζ=0.06, w=ω) | fester BP / PLL / EKF |
| C.2 | Kopplungs-Frame | `sin(2δ)` Fehler-Frame | `sin(2θ)` Lab-Frame (verboten) |
| C.3 | Injektion | `kGainMod` (ersetzt hartes `1` in Firmware.hs:418) | direkter Phasen-Offset / cadenceTarget |
| C.4 | Washboard↔Dwell | geschichtet (Beat-Lock + Fein-Lock) | Ersatz des Washboard |

### 8.2 Präzisierung A.3 (Reversal-Detection + Strobe-Größe)

Der Albedo-Schritt A.2 formulierte „Reversal aus Phase-Sprung π“. Die Referenz
(`pid.c:1846`, `pid.c:1954`) ist präziser und wird hiermit verbindlich übernommen:

- **Trigger:** Sinusoid-Nulldurchgang — `prevSin · sin ≤ 0 ∧ prevSin ≠ sin`,
  `downstrokeEnded = prevSin > 0`. In Born2Flap ist das der Vorzeichenwechsel von
  `sin mixPhase` (nicht ein fester Phasenwert π, da Dwell/Skew den Nulldurchgang
  der *geformten* Welle verschieben — die Sinusoid ist die stabile Referenz).
- **Strobe-Größe:** `abs (pitchErrorRate)` — das Born2Flap-Äquivalent von
  `itermErrorRate` **vor** dem RESONANCE-Filter (offene Schleife), **nicht** die
  Flap-Phase selbst. Die Phasen-Hülle misst also die Verteilung der *Tracking-Fehlerrate*
  über die phyllotaktisch abgetasteten Reversal-Ereignisse, nicht die Phase.

Konsequenz für das Modul: `phaseStrobe` trägt das übergebene `errorAbs` ein und ist
bewusst **ohne** `strokeReversal`-Parameter — der Aufrufer gated über `detectReversal`
(nur bei Reversal aufrufen), faithful zu `ondasMetricsPhaseStrobe` (das den
`strokeReversal`-Guard intern führt).

### 8.3 Vorbereitung auf Rubedo (Implementierung)

Die Reihenfolge §5 bleibt unverändert gültig und wird durch die Pfadwahl nicht
verschoben. Rubedo setzt exakt diese Pfade um; Abweichungen davon erfordern ein
neues/revidiertes ADR. Die Negativ-Kontrollen (fester Raster in A.4.2, fester BP in
C.4.1, `sin(2θ)` in C.4.3) sind **Teil des Lieferumfangs**, nicht optional.
