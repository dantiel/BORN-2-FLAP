"""Glue-limit contract: slow held differential sweep at 0 m/s.

The user's law: when the wings rotate slowly by a large angle relative to
the fuselage, the wings must stay HORIZONTAL IN THE WORLD (glued to the
compressed-air field) and only the tail+fuselage must counter-rotate by
the commanded angle.

Sequence: settle level at 0 m/s, ramp the roll stick 0->1 over ~3 s, hold
2 s. Contracts:
  glue      max wing world-roll change < 30% of the body roll
  counter   |body roll| >= 70% of the commanded differential
  direction stick-right -> bank right (body roll negative)
  no-runaway body roll settles after the sweep ends (no phantom couple)
"""
import ctypes as c
import json
import math
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'
PREFS = ROOT / 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini'


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw', 'speed', 'coupled')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('v', c.c_double * 3), ('w', c.c_double * 3),
                ('height', c.c_double), ('inertia', c.c_double)]


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
    parser.add_argument('--ramp-seconds', type=float, default=3.0)
    parser.add_argument('--hold-seconds', type=float, default=2.0)
    parser.add_argument('--scales', type=float, nargs='+', default=[40, 200])
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
    results = []
    for scale in args.scales:
        tuning = Tuning(*[getattr(base, n) for n, _ in Tuning._fields_])
        tuning.AileronScale = scale
        ctx = create(None)
        assert ctx
        try:
            assert reconfigure(ctx, c.byref(tuning)) == 1
            body = Body(1 / args.hz, (0, 0, 0), (0, 0, 0), 1000, inertia)
            pilot, out = Pilot(0, 0, 0, 0, .5, 1), Output()
            settle = 0.5
            sweep = args.ramp_seconds
            hold = args.hold_seconds
            count = round(args.hz * (settle + sweep + hold))
            angle = 0.0
            initial = None
            max_wing_world = 0.0
            end_body = None
            max_body_rate = 0.0
            for i in range(count):
                t = i / args.hz - settle
                if t < 0:
                    frac = 0.0
                elif t < sweep:
                    frac = t / sweep
                else:
                    frac = 1.0
                pilot.roll = frac
                ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
                assert ok == 1, (scale, i, 'solver rejected')
                assert all(math.isfinite(v) for v in (*out.force, *out.moment, out.left, out.right))
                body.w[0] += out.moment[0] / inertia / args.hz
                angle += body.w[0] / args.hz
                max_body_rate = max(max_body_rate, abs(body.w[0]))
                if i == round(settle * args.hz) - 1:
                    initial = (angle, out.left, out.right)
                if initial is not None:
                    # wing world roll [deg] = body roll [deg] +/- relative sweep
                    wing_l = math.degrees(angle - initial[0]) - (out.left - initial[1])
                    wing_r = math.degrees(angle - initial[0]) + (out.right - initial[2])
                    max_wing_world = max(max_wing_world, abs(wing_l), abs(wing_r))
                    end_body = angle - initial[0]
                    end_wing_l = wing_l
                    end_wing_r = wing_r
            commanded = 2 * 1.0 * scale * 0.01 * 175
            result = dict(scale=scale, commanded_differential_deg=commanded,
                          body_deg=math.degrees(end_body),
                          right_wing_world_deg=end_wing_r,
                          left_wing_world_deg=end_wing_l,
                          max_body_rate_rads=max_body_rate,
                          final_left=out.left, final_right=out.right)
            results.append(result)
            checks = {}
            achieved = abs(out.right - out.left)
            # BOTH wings stay glued to the air: their world roll stays near
            # zero while the body takes the angle.
            checks['glue'] = max(abs(end_wing_l), abs(end_wing_r)) < 0.20 * achieved
            # The fuselage counter-rotates the wing movement: the glued fixed
            # point is body roll = half the achieved differential.
            checks['counter'] = abs(end_body) >= 0.40 * math.radians(achieved)
            checks['direction'] = end_body < 0  # stick right -> bank right
            # no runaway spin: the body rate stays bounded at the sweep rate
            checks['no_runaway'] = max_body_rate < 5.0
            for name, okk in checks.items():
                if not okk:
                    failures.append(dict(check=name, scale=scale, result=result))
            print(json.dumps({k: round(v, 3) if isinstance(v, float) else v
                              for k, v in result.items()}, indent=1))
        finally:
            destroy(ctx)
    print('INERTIA', inertia)
    print('FAILURES', json.dumps(failures, indent=1)[:1500])
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())