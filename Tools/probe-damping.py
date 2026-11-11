"""Aerodynamic damping + weathervane diagnostic.

Measures the two derivatives the flight model must have:
  A) ROLL DAMPING L_p: body roll rate p -> wing roll moment (the "parachute"
     resistance of the wings sweeping through the air).
  B) WEATHERVANE Cn_beta / YAW DAMPING Cn_r: sideslip beta (velocity not
     aligned with the nose) and yaw rate s -> yaw moment (tail/body side
     force aligning the nose with the relative wind).

State is held FIXED (no body integration) so out.moment is the pure net
moment the engine would apply to the rigid body.
"""
import ctypes as c
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'


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


def run_case(lib, ctx, step, hz, pilot, body, out, settle, collect, label):
    for _ in range(round(hz * settle)):
        assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
    ms, mn, mx = [], [1e9] * 3, [-1e9] * 3
    for _ in range(round(hz * collect)):
        assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
        m = [out.moment[0], out.moment[1], out.moment[2]]
        ms.append(m)
        for j in range(3):
            mn[j] = min(mn[j], m[j])
            mx[j] = max(mx[j], m[j])
    n = len(ms)
    mean = [sum(m[j] for m in ms) / n for j in range(3)]
    print(f'{label}: meanM={[round(v, 6) for v in mean]} minM={[round(v, 6) for v in mn]} maxM={[round(v, 6) for v in mx]}')
    print(f'   servoL={out.left:.4f} servoR={out.right:.4f} flapHz={out.flap_hz:.4f}')
    return mean


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--speed', type=float, default=8.0)
    parser.add_argument('--legacy', action='store_true', help='skip set_frame: pre-recoil branch')
    parser.add_argument('--flap', action='store_true', help='flapping branch (throttle 1)')
    parser.add_argument('--model', type=int, default=0, help='wing model id (0=raven, 2=kestrel)')
    parser.add_argument('--hz', type=int, default=960)
    args = parser.parse_args()

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
    set_model = lib.b2f_math_set_wing_model
    set_model.argtypes, set_model.restype = [c.c_void_p, c.c_uint32], c.c_int32
    set_frame = lib.b2f_math_set_mass_frame
    set_frame.argtypes = [c.c_void_p, c.c_double, c.c_double, c.c_double]
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]

    tuning = Tuning(857, 0.735, 20, 7.4, 0.08, 1.3, 30, 60, 32,
                    -2.3, -4, 50, 40, 55, 0, 0.05, 0, 50, 100, 0, 0)
    ctx = create(None)
    try:
        assert set_model(ctx, args.model) == 1
        assert reconfigure(ctx, c.byref(tuning)) == 1
        props = (c.c_double * 7)()
        assert get_mass(ctx, 0, props) == 1
        dry_roll, dry_pitch, dry_yaw = props[4], props[5], props[6]
        print('MASS_PROPS', [round(v, 6) for v in props])
        if not args.legacy:
            assert set_frame(ctx, 0.0, dry_pitch, dry_yaw) == 1
        mode = 'FLAP' if args.flap else 'GLIDE'
        print(f'--- {mode} branch, hz={args.hz}, speed={args.speed} ---')
        pilot = Pilot(1.0 if args.flap else 0.0, 0, 0, 0, .5, 1)
        out = Output()
        settle, collect = 3.0, 1.0

        # A) Roll damping: held roll rate p, straight & level.
        print('== A) roll damping L_p: held body roll rate p, neutral stick ==')
        for p in [0.0, 0.1, 0.3, 0.6, 1.0]:
            body = Body(1 / args.hz, (args.speed, 0, 0), (p, 0, 0), 1000, dry_roll, (0, 0, 1))
            run_case(lib, ctx, step, args.hz, pilot, body, out, settle, collect, f'A p={p}')

        # B) Weathervane Cn_beta: sideslip of the velocity vector.
        print('== B) weathervane Cn_beta: held sideslip beta, straight & level ==')
        for bdeg in [0.0, 2.0, 5.0, 10.0, 20.0]:
            b = math.radians(bdeg)
            v = (args.speed * math.cos(b), args.speed * math.sin(b), 0)
            body = Body(1 / args.hz, v, (0, 0, 0), 1000, dry_roll, (0, 0, 1))
            run_case(lib, ctx, step, args.hz, pilot, body, out, settle, collect, f'B beta={bdeg}')

        # C) Yaw damping Cn_r: held yaw rate s.
        print('== C) yaw damping Cn_r: held body yaw rate s ==')
        for s in [0.0, 0.2, 0.5, 1.0]:
            body = Body(1 / args.hz, (args.speed, 0, 0), (0, 0, s), 1000, dry_roll, (0, 0, 1))
            run_case(lib, ctx, step, args.hz, pilot, body, out, settle, collect, f'C s={s}')

        # D) Pitch damping Cm_q: held pitch rate q (reference).
        print('== D) pitch damping Cm_q: held body pitch rate q ==')
        for q in [0.0, 0.2, 0.5]:
            body = Body(1 / args.hz, (args.speed, 0, 0), (0, q, 0), 1000, dry_roll, (0, 0, 1))
            run_case(lib, ctx, step, args.hz, pilot, body, out, settle, collect, f'D q={q}')
    finally:
        destroy(ctx)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())