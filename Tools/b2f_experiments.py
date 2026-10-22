"""BORN 2 FLAP — headless flight-experiment driver.

Runs a standard battery against the math DLL and prints/records a compact
comparison table. Reusable for every future physics fine-tuning pass:

  run        -- presets + custom servos: hover/cruise thrust, flap amplitude,
                free-flight climb + roll stability (does it fly? does it flip?)
  deviations -- small-deviation servo sweep around a reference: does thrust
                respond monotonically, or does a weaker/slower servo overshoot
                into flutter and *gain* thrust (the known bug)?

Usage:
  python Tools/b2f_experiments.py run [--json out.json]
  python Tools/b2f_experiments.py deviations [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Tools"))

from b2f_harness import (  # noqa: E402
    B2F, PRESETS, Servo, free_flight, thrust_curve,
)

GRAVITY = 9.80665
KESTREL_REF = PRESETS[1]  # VOTIK PTK 7465 / 7465W MG

# Nonexistent/custom servos for extrapolation (name, speed, torque, backdrive, V).
CUSTOM = (
    Servo("CUSTOM ultra-fast ultra-weak", 2000.0, 0.20, 20.0, 7.4),
    Servo("CUSTOM fast weak", 1200.0, 0.40, 20.0, 7.4),
    Servo("CUSTOM mid mid", 750.0, 1.00, 20.0, 7.4),
    Servo("CUSTOM slow strong", 400.0, 2.50, 20.0, 7.4),
    Servo("CUSTOM ultra-slow ultra-strong", 300.0, 6.17, 20.0, 7.4),
    Servo("CUSTOM speed-monster weak", 2500.0, 0.50, 20.0, 7.4),
    Servo("CUSTOM torque-monster slow", 300.0, 8.00, 20.0, 7.4),
)


def _weight(servo: Servo, mass_kg: float) -> float:
    return mass_kg * GRAVITY


def sweep_battery(b2f: B2F, *, mass_kg: float = 0.050) -> list[dict]:
    """Thrust/lift scaling probes.

    speed axis   -- strong torque (stroke held) at increasing servo speed:
                    reveals the thrust ~ f^2 scaling and the flutter onset.
    airspeed axis -- kestrel reference at increasing forward speed: reveals
                    whether the bird can sustain cruise (thrust > drag).
    """
    rows = []
    weight = _weight(None, mass_kg)
    for spd in (400, 600, 800, 1000, 1200, 1500, 2000, 3000):
        s = Servo(f"strong {spd}", spd, 3.0)
        th = thrust_curve(b2f, s, airspeed=0.0, seconds=2.0, body_mass_kg=mass_kg)
        rows.append(dict(axis="speed", servo=f"strong@{spd}", speed=spd,
                         airspeed=0.0, thrust_n=round(th.mean_force[0], 4),
                         lift_n=round(th.mean_force[2], 4),
                         flap_amp_deg=round(th.flap_amp_deg, 1),
                         t_over_w=round(th.mean_force[0] / weight, 3)))
    for v in (0, 2, 4, 6, 8, 10, 12, 15):
        th = thrust_curve(b2f, KESTREL_REF, airspeed=float(v), seconds=2.0,
                          body_mass_kg=mass_kg)
        rows.append(dict(axis="airspeed", servo="kestrel", speed=KESTREL_REF.speed_deg_s,
                         airspeed=v, thrust_n=round(th.mean_force[0], 4),
                         lift_n=round(th.mean_force[2], 4),
                         flap_amp_deg=round(th.flap_amp_deg, 1),
                         t_over_w=round(th.mean_force[0] / weight, 3)))
    return rows


def run_battery(b2f: B2F, servos, *, mass_kg: float = 0.050,
                hover_s: float = 2.0, flight_s: float = 6.0) -> list[dict]:
    rows = []
    weight = _weight(None, mass_kg)
    for s in servos:
        th0 = thrust_curve(b2f, s, airspeed=0.0, seconds=hover_s,
                           body_mass_kg=mass_kg)
        th8 = thrust_curve(b2f, s, airspeed=8.0, seconds=hover_s,
                           body_mass_kg=mass_kg)
        fl = free_flight(b2f, s, seconds=flight_s, body_mass_kg=mass_kg)
        rows.append(dict(
            servo=s.name,
            speed=s.speed_deg_s, torque=s.stall_torque_nm,
            hover_thrust_n=round(th0.mean_force[0], 4),
            cruise_thrust_n=round(th8.mean_force[0], 4),
            hover_lift_n=round(th0.mean_force[2], 4),
            flap_amp_deg=round(th0.flap_amp_deg, 1),
            rms_rate_deg_s=round(th0.rms_rate_deg_s, 0),
            power_w=round(th0.mean_power, 3),
            flight_max_alt_m=round(fl.max_altitude_m, 3),
            flight_climb_ms=round(fl.climb_rate_m_s, 3),
            flight_max_roll_deg=round(fl.max_roll_deg, 1),
            flight_crashed=fl.crashed,
            weight_n=round(weight, 3),
            thrust_over_weight=round(th0.mean_force[0] / weight, 3),
        ))
    return rows


def deviation_battery(b2f: B2F, *, mass_kg: float = 0.050) -> list[dict]:
    """Small deviations around the reference servo: torque and speed axes.

    A healthy model responds monotonically: raising stall torque (holding the
    commanded stroke) must NOT reduce thrust below a weaker servo, and raising
    speed must not cause the wing to overshoot into flutter. We flag both.
    """
    base_speed = KESTREL_REF.speed_deg_s
    base_torque = KESTREL_REF.stall_torque_nm
    rows = []
    # torque axis (fixed speed)
    for t in (0.81, 0.86, 0.91, 0.96, 1.01, 1.06, 1.11, 1.21, 1.33):
        s = Servo(f"torque {t:.2f}Nm@{base_speed:.0f}", base_speed, t)
        th = thrust_curve(b2f, s, airspeed=0.0, seconds=2.0, body_mass_kg=mass_kg)
        rows.append(dict(axis="torque", speed=base_speed, torque=t,
                         thrust_n=round(th.mean_force[0], 4),
                         flap_amp_deg=round(th.flap_amp_deg, 1)))
    # speed axis (fixed torque)
    for spd in (600, 634, 667, 700, 734, 767, 800, 850, 1000):
        s = Servo(f"speed {spd:.0f}@{base_torque:.2f}Nm", spd, base_torque)
        th = thrust_curve(b2f, s, airspeed=0.0, seconds=2.0, body_mass_kg=mass_kg)
        rows.append(dict(axis="speed", speed=spd, torque=base_torque,
                         thrust_n=round(th.mean_force[0], 4),
                         flap_amp_deg=round(th.flap_amp_deg, 1)))
    return rows


def _table(rows: list[dict], keys: list[str]) -> str:
    if not rows:
        return "(empty)"
    header = "  ".join(f"{k:>18}" for k in keys)
    lines = [header, "-" * len(header)]
    for r in rows:
        cells = []
        for k in keys:
            v = r.get(k)
            if isinstance(v, float):
                cells.append(f"{v:>18.4g}")
            else:
                cells.append(f"{str(v):>18}")
        lines.append("  ".join(cells))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("run", "deviations", "sweep"))
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--mass", type=float, default=0.050)
    ap.add_argument("--dll", type=Path, default=None)
    args = ap.parse_args()

    b2f = B2F(args.dll) if args.dll else B2F()

    if args.mode == "run":
        servos = list(PRESETS) + list(CUSTOM)
        rows = run_battery(b2f, servos, mass_kg=args.mass)
        print("=== SERVO BATTERY (hover thrust vs flap amp vs free-flight) ===")
        print(_table(rows, [
            "servo", "speed", "torque", "flap_amp_deg", "hover_thrust_n",
            "thrust_over_weight", "flight_max_alt_m", "flight_max_roll_deg",
            "flight_crashed"]))
        print("\n(kestrel reference = VOTIK PTK 7465; weight = %.3f N)" %
              _weight(None, args.mass))
    elif args.mode == "sweep":
        rows = sweep_battery(b2f, mass_kg=args.mass)
        print("=== THRUST/LIFT SCALING SWEEP ===")
        print(_table(rows, ["axis", "servo", "speed", "airspeed",
                            "flap_amp_deg", "thrust_n", "lift_n", "t_over_w"]))
        print(f"(weight = {_weight(None, args.mass):.3f} N)")
    else:
        rows = deviation_battery(b2f, mass_kg=args.mass)
        print("=== SMALL-DEVIATION SERVO SWEEP (reference 667 deg/s, 1.01 Nm) ===")
        print(_table(rows, ["axis", "speed", "torque", "flap_amp_deg", "thrust_n"]))
        # monotonicity flags
        print("\n--- non-monotonicity scan ---")
        torque_rows = [r for r in rows if r["axis"] == "torque"]
        speed_rows = [r for r in rows if r["axis"] == "speed"]
        for label, rr in (("torque", torque_rows), ("speed", speed_rows)):
            for a, b in zip(rr, rr[1:]):
                if b["thrust_n"] < a["thrust_n"]:
                    print(f"  NON-MONOTONIC {label}: {a[('torque' if label=='torque' else 'speed')]:.4g} -> "
                          f"{b[('torque' if label=='torque' else 'speed')]:.4g}: "
                          f"thrust {a['thrust_n']:.4f} -> {b['thrust_n']:.4f} N")

    if args.json:
        args.json.write_text(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()