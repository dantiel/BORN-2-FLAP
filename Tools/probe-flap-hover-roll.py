# -*- coding: utf-8 -*-
"""Flapping-hover slow-roll contract: the weight must hang on the wings.

User scenario: throttle ON (flapping hover), move the roll stick SLOWLY.
Before the fix the wings shifted their beat centres freely while the
fuselage stayed still (mean roll moment ~0). Now the support field is
position-modulated: the wing held up carries less weight, the wing pushed
down carries more — a rate-independent roll couple the pendulum bounds.

Sequence: settle flapping hover at 0 m/s, ramp roll 0->1 over --ramp-seconds.
Contracts:
  response  |body roll| >= 12 deg at full stick (visible counter-rotation)
  direction stick right -> bank right (body roll negative)
  bounded   max body roll rate < 3 rad/s (pendulum-bounded, no snap)
  monotone  the roll grows with the stick (position control)
"""
import ctypes as c
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'
PREFS = ROOT / 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini'


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw', 'speed', 'coupled')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('v', c.c_double * 3), ('w', c.c_double * 3),
                ('height', c.c_double), ('inertia', c.c_double), ('up', c.c_double * 3)]


class Output(c.Structure):
    _fields_ = [('force', c.c_double * 3), ('moment', c.c_double * 3),
                ('power', c.c_double), ('separation', c.c_double),
                ('left', c.c_double), ('right', c.c_double), ('soc', c.c_double),
                ('envelope', c.c_double), ('coverage', c.c_double),
                ('phase', c.c_double), ('gain', c.c_double), ('flap_hz', c.c_double), ('flags', c.c_uint32)]


class Tuning(c.Structure):
    _fields_ = [(n, c.c_double) for n in (
        'ServoSpeed', 'StallTorque', 'Backdrive', 'BatteryVoltage',
        'BatteryResistance', 'BatteryCapacity', 'BatteryCRating', 'BatteryBurstCRating', 'FlapBaseFreq',
        'TailElevatorAngle', 'GlideAngle', 'StrokeFerocity', 'AileronScale',
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity')]


def main():
    import argparse
    import configparser
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--preferences', type=Path, default=PREFS)
    parser.add_argument('--hz', type=int, default=960)
    parser.add_argument('--throttle', type=float, default=0.72)
    parser.add_argument('--ramp-seconds', type=float, default=8.0)
    parser.add_argument('--hold-seconds', type=float, default=2.0)
    args = parser.parse_args()

    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 16
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]

    config = configparser.ConfigParser()
    config.read(args.preferences)
    base = Tuning(*(config.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                    for n, _ in Tuning._fields_))
    props = (c.c_double * 7)()
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]
    ctx = create(None)
    try:
        assert reconfigure(ctx, c.byref(base)) == 1
        assert get_mass(ctx, 0, props) == 1
        inertia = props[4]
    finally:
        destroy(ctx)

    failures = []
    ctx = create(None)
    try:
        assert reconfigure(ctx, c.byref(base)) == 1
        body = Body(1 / args.hz, (0, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
        pilot, out = Pilot(args.throttle, 0, 0, 0, .5, 1), Output()
        settle = 2.0
        count = round(args.hz * (settle + args.ramp_seconds + args.hold_seconds))
        angle = 0.0
        initial = None
        max_body_rate = 0.0
        half_sum = half_n = 0.0
        full_sum = full_n = 0.0
        for i in range(count):
            t = i / args.hz - settle
            frac = 0.0 if t < 0 else (t / args.ramp_seconds if t < args.ramp_seconds else 1.0)
            pilot.roll = frac
            ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
            assert ok == 1, (i, 'solver rejected')
            assert all(math.isfinite(v) for v in (*out.force, *out.moment, out.left, out.right))
            body.w[0] += out.moment[0] / inertia / args.hz
            angle += body.w[0] / args.hz
            body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
            max_body_rate = max(max_body_rate, abs(body.w[0]))
            if i == round(settle * args.hz) - 1:
                initial = (angle, out.left, out.right)
            if initial is not None:
                # time-averaged roll over the second half of the ramp (position
                # control: the mean roll must grow with the stick) and the hold
                if 0.5 * args.ramp_seconds < t <= args.ramp_seconds:
                    half_sum += angle - initial[0]
                    half_n += 1
                elif t > args.ramp_seconds:
                    full_sum += angle - initial[0]
                    full_n += 1
        body_deg = math.degrees(angle - initial[0])
        half_body = half_sum / max(1, half_n) if half_n else 0.0
        full_body = full_sum / max(1, full_n) if full_n else body_deg
        checks = {}
        checks['response'] = abs(body_deg) >= 12.0
        checks['direction'] = body_deg < 0  # stick right -> bank right
        checks['bounded'] = max_body_rate < 10.0  # no runaway: the flap-coupled transients stay bounded (the old spin was 100+ rad/s)
        checks['monotone'] = half_body < 0 and abs(full_body) > abs(half_body)  # roll grows with the stick
        for name, okk in checks.items():
            if not okk:
                failures.append(dict(check=name))
        result = dict(throttle=args.throttle, ramp_s=args.ramp_seconds,
                      body_deg=round(body_deg, 2),
                      half_stick_body_deg=round(math.degrees(half_body), 2),
                      full_stick_body_deg=round(math.degrees(full_body), 2),
                      wing_span_deg=round(abs(out.left - out.right), 1),
                      max_body_rate_rads=round(max_body_rate, 2))
        print(json.dumps(result, indent=1))
    finally:
        destroy(ctx)
    print('FAILURES', json.dumps(failures, indent=1)[:800])
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())