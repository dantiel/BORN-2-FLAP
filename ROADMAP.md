# BORN-2-FLAP — Roadmap

> Buy once. Fly everything.
> Every machine from the channel becomes flyable — delivered as free updates
> included in your one-time purchase. No subscriptions, no surprise gates.

BORN-2-FLAP ships as **Alpha 1** with the honest, reduced physics core and the
first three flyable worlds. This document lists what is already here and what
is coming as **free updates** — the machines and mechanics that make the game
grow without asking for more money.

## Already in Alpha 1

- **Reduced physics core** — 2 × 16 spanwise wing strips, partial dynamic stall,
  spanwise crossflow, aeroelastic bending, fixed 240 Hz timestep (Haskell MathCore).
- **Firmware emulation** — PteronautOS-derived mixer, servo kernel, PID and
  ONDAS phase-lock stabilisation, closed-loop against aerodynamic hinge torque.
- **Three worlds** — Ravenstonefield (nature), Shiomori Bay (coast), Training
  (gate-race course).
- **Points of interest** — named, selectable reset/launch points per level.
- **Gate racing** — timed gate course with ghost replay and best-lap records.
- **RC controllers** — CRSF/real transmitter input plus keyboard fallback.
- **Live tuning hangar** — servo, battery and controller knobs edited in flight.
- **13 languages** — en, de, ar, es, fr, hi, it, ja, ko, no, pt, ru, zh.

## Coming as free updates (included in the one-time purchase)

The core promise: **the workshop becomes flyable**. Each machine from the
channel is a new flight model, not a cosmetic skin — its drivetrain, its
articulation and its aerodynamics are simulated.

| Feature | Description |
|---------|-------------|
| **Gearbox ornithopters** | Crank-driven flapping machines — the full drivetrain from crank to wing hinge, simulated. |
| **Wrist articulation** | Refined wrist joints shaping every downstroke and upstroke — a new axis of flight control. |
| **Servo ornithopters** | Direct-drive servo wings with articulation, fighting the aero hinge torque like the real prototypes. |
| **Wing slots** | Modular wing geometry — slot in span, chord, sweep and camber to build your own planform. |
| **Everything on the channel** | The whole workshop becomes flyable — each prototype, each upgrade, each wild idea. |

Planned features — scope and order may evolve. The flying never stops.

## Platforms

| Platform | Status |
|----------|--------|
| Windows (Win64) | 🟢 shipped |
| macOS (Apple Silicon) | 🟢 shipped |
| iOS (tablet + Bluetooth controller) | 🟡 in progress |
| Android | 🟡 planned, follows the iOS math-bridge work |

## Strategic notes (internal)

The public roadmap above is what a buyer gets for their one-time purchase.
Longer-term expansion surfaces — additional machine models via app purchases,
the Born2Flap Studio marketplace, and the Orniflight line — are **strategic**
and deliberately not part of the public roadmap. Buyers should never have to
read about monetisation to know what they are buying today.
