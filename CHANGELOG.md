# Changelog

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
