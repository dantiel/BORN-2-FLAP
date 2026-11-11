"""Spatial-vs-legacy slow-roll comparison: reproduce the GAME path exactly.

The game (Born2FlapFlightPawn::StepMath) always calls b2f_math_set_mass_frame
after ApplyTuning -> the firmware step runs the coupled SPATIAL solve. The
existing glue probes never set a mass frame, so they exercise the LEGACY
roll-only path. This probe runs the SAME slow-roll ramp in both modes and
measures the body-roll grip to find where the in-game grip vanishes.

Game fixture (RollResponseTest): v=(8,0,0) m/s, roll stick 0->0.25 over 8 s.
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
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity',
        'YawScale', 'StrokeSkew', 'ReturnSkew')]


def run(args, spatial, hz=960):
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 20
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    set_frame = lib.b2f_math_set_mass_frame
    set_frame.argtypes = [c.c_void_p, c.c_double, c.c_double, c.c_double]
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]

    import configparser
    config = configparser.ConfigParser()
    config.read(args.preferences)
    base = Tuning(*(config.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                    for n, _ in Tuning._fields_))
    tuning = Tuning(*[getattr(base, n) for n, _ in Tuning._fields_])
    if args.defaults:
        # DefaultTuningForModel(0) as the RollResponseTest sees it (prefs skipped).
        tuning = Tuning(750, 1.8, 20, 11.1, 0.08, 1.3, 30, 60, 32,
                        -2.3, -4, 50, 40, 65, 0, 0.05, 0, 50, 100, 0, 0)
    tuning.AileronScale = args.scale
    set_model = lib.b2f_math_set_wing_model
    set_model.argtypes = [c.c_void_p, c.c_uint32]
    set_model.restype = c.c_int32

    ctx = create(None)
    try:
        if args.model >= 0:
            assert set_model(ctx, args.model) == 1
        assert reconfigure(ctx, c.byref(tuning)) == 1
        props = (c.c_double * 7)()
        assert get_mass(ctx, args.cg, props) == 1
        dry_roll, dry_pitch, dry_yaw = props[4], props[5], props[6]
        print('PROPS', list(round(p, 5) for p in props))
        if spatial:
            assert set_frame(ctx, args.cg, dry_pitch, dry_yaw) == 1
        body = Body(1 / hz, (args.speed, 0, args.climb), (0, 0, 0), 1000, dry_roll, (0, 0, 1))
        pilot, out = Pilot(0, 0, 0, 0, .5, 1), Output()
        settle, sweep, hold = 1.0, args.ramp, 3.0
        count = round(hz * (settle + sweep + hold))
        angle = 0.0
        max_rate = 0.0
        samples = []
        for i in range(count):
            t = i / hz - settle
            frac = 0.0 if t < 0 else (t / sweep if t < sweep else 1.0)
            pilot.roll = args.fraction * frac
            if args.expo:
                pilot.roll = args.fraction * ((1 - args.expo) * frac + args.expo * frac * frac * frac)
            ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
            assert ok == 1, (spatial, i, 'solver rejected')
            body.w[0] += out.moment[0] / dry_roll / hz
            angle += body.w[0] / hz
            body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
            max_rate = max(max_rate, abs(body.w[0]))
            if i % 20 == 0:
                samples.append(dict(t=round(i / hz - settle, 2), frac=round(frac, 3),
                                   moment=round(out.moment[0], 4),
                                   roll_deg=round(math.degrees(angle), 3),
                                   left=round(out.left, 2), right=round(out.right, 2)))
        return dict(spatial=spatial,
                    body_deg=round(math.degrees(angle), 3),
                    commanded_differential_deg=round(2 * args.fraction * args.scale * 0.01 * 175, 2),
                    achieved_differential_deg=round(out.right - out.left, 2),
                    max_rate_rads=round(max_rate, 4),
                    final_left=round(out.left, 2), final_right=round(out.right, 2),
                    final_moment=round(out.moment[0], 8),
                    final_force_z=round(out.force[2], 6),
                    trajectory=samples if args.trajectory else None)
    finally:
        destroy(ctx)


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--preferences', type=Path, default=PREFS)
    parser.add_argument('--scale', type=float, default=100)
    parser.add_argument('--model', type=int, default=2, help='wing model 0=raven 1=proto 2=kestrel; -1 = default')
    parser.add_argument('--cg', type=float, default=0.0, help='CG x [m] for mass properties/mass frame (game: -CgOffsetMm/1000)')
    parser.add_argument('--defaults', action='store_true', help='use the game factory defaults (test mode) instead of the prefs')
    parser.add_argument('--climb', type=float, default=0.0, help='vertical velocity component m/s (game test climbs ~1.03)')
    parser.add_argument('--expo', type=float, default=0.0, help='apply cubic expo to the ramp (game default ControlExpo=0.65)')
    parser.add_argument('--fraction', type=float, default=0.25,
                        help='peak roll stick deflection (0.25 = in-engine test, 1.0 = full)')
    parser.add_argument('--ramp', type=float, default=8.0, help='sweep seconds')
    parser.add_argument('--speed', type=float, default=8.0, help='forward speed m/s')
    parser.add_argument('--trajectory', action='store_true')
    parser.add_argument('--hz', type=int, default=960)
    args = parser.parse_args()
    for spatial in (False, True):
        r = run(args, spatial, hz=args.hz)
        print(json.dumps(r, indent=1))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())