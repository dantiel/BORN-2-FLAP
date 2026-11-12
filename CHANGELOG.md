# Changelog

## 0.2.0-alpha — "Wing Grip" (2026-11-12)

### Flight physics
- **Chronological Chaos load handoff** — StepMath no longer collapses a rendered
  frame into one mean force/torque for Chaos. The calculated loads keep their
  durations and order and go through a Chaos integration callback; the predictor
  reads the authoritative physics pose and math sample boundaries align with Chaos
  substeps. Accelerating/braking shoulder reactions no longer cancel in the
  average: measured body roll now tracks actual wing travel (e.g. 10.41° shoulder
  travel → 10.37° body roll, previously 4.52°; error below 1e-6° mid-move).
- **Host roll-moment fix** — removed an erroneous `mass*shoulderHeight²` term from
  the host roll-moment conversion (~10.7% over-amplification).
- **Fixed-tail correction** — removed the phantom vertical-fin lateral force on the
  horizontal tail fan (`fanLatCl`/`fanLatCd`/`fanLatDragCd`); 3D skin friction
  (`C_skin = 0.012`, complete local velocity) replaces it. Banked-tail yaw no
  longer reverses near horizontal/anhedral wings (Unreal ramp: heading 0.859° →
  3.672°, total tail yaw moment −0.000355 → +0.000488 N·m).
- New audits: `TailCouplingAudit`, `BankedTurnAudit`, `WakeReference`
  (MathCore) + knife-edge and tail-coupling tooling
  (`run-knife-edge-audit.ps1`, `run-tail-coupling-audit.ps1`,
  `audit-tail-coupling.py`, `summarize-tail-coupling.py`, …).
- Installed DLL: ABI 21.

### Game feel & HUD
- **Aerobatic trick toasts** — Handcatch!, Looping!, Barrel Roll!, Inverted!, Skim!
  detected from the Chaos pose + body-axis angular velocity (stall/reversal resets,
  shared cooldown).
- Reward banner pops in and fades out; **TRICK TOASTS** toggle across the Brain
  tuning desk, native menus and the in-flight panel (all 13 locales).

### Worlds
- **Yunsha aeolian field** — 301×301 wind-potential solve baked into terrain UV1
  (shelter/transport mask, shared gameplay wind direction) plus three deterministic
  sand-ripple beds (5/11/20 cm wavelengths) blended in wind coordinates.
  See [docs/yunsha.md](docs/yunsha.md) and [docs/weather.md](docs/weather.md).
- Shiomori material refresh; weather surf/audio/materials refresh.
- Avatar animation imports: SK_Scout, SK_Soldier, SK_Xbot (SourceAssets GLBs +
  Content actors).
- Woshi/WoshiSM content removed (superseded).

### Notes
- Development log reports are no longer committed; their durable content lives in
  this changelog and the standing docs.

## 0.1.0-alpha — "First Flap" (2026-09-25)

First public alpha. Free, MIT-licensed ornithopter flight simulator on Unreal Engine 5.8.

### Flight physics
- Real-time reduced wing model: **2 wings × 16 spanwise strips**
- Local angle of attack, Reynolds number and per-strip **dynamic partial stall**
- **Spanwise crossflow** (sweep-driven transport between neighbouring elements)
- **Aeroelastic bending** — per-station bending stiffness `EI` and torsional stiffness `GJ`, relaxed into strip state; load-induced washout
- **Passive-adaptive airfoil** — rest camber, reflex and membrane cupping per station, coupled to lift and sectional pitching moment
- PteronautOS **waveform oscillator** (asymmetric dwell/pointed stroke, beat-locked phase)
- Fixed **240 Hz** timestep, NaN/Inf guarded, deterministic

### Architecture
- **Haskell MathCore** as canonical physics (aerodynamics, kinematics, control, stabilisation)
- **C / C++ bridge** into Unreal + tested OrniCore research fallback
- **Unreal Engine 5.8** rendering, Chaos rigid bodies, world interaction
- **Ruby brain** for app state, menus, rules and orchestration

### World
- Natural forested valley: winding river, firs, grass and moss rocks (Poly Haven, CC0)
- Modelled terrain and trunk collision; animated water surface

### Control
- **Real RC transmitter** over Windows USB joystick/HID (`F3` calibration, per-device profiles, failsafe gate)
- Keyboard virtual sticks (W / Shift+W / A / D / arrows)
- Hand launch (Space) and reset (R)

### Documentation
- Public website (`web/`): HAML + Sass + Markdown, 8 languages (EN/DE/ES/FR/ZH/JA/KO/RU)
- Physics conventions, RC controls and Windows build docs

### Known limits
- Coefficients and mass distribution are **provisional, not experimentally calibrated**
- Windows-only build (UE 5.8.2)
- RC transmitter support covers Windows joystick/HID devices only
- Yaw and roll remain coupled (same wings produce thrust, lift and drag)