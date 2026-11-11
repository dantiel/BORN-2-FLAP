"""Static-differential strip diagnostic: why is the held aileron roll torque 0?

Game test conditions (model 0, default tuning, 8 m/s), glide. Ramp the roll
stick to a HELD differential, then dump per-strip loads and the total moment.
"""
import ctypes as c
import json
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


class WingLoads(c.Structure):
    _fields_ = [('force', c.c_double * 3), ('drag', c.c_double * 3), ('moment', c.c_double * 3)]


class WingField(c.Structure):
    _fields_ = [('pos', c.c_double * 3), ('normal', c.c_double * 3), ('lift', c.c_double * 3),
                ('pressure', c.c_double * 3), ('vel', c.c_double * 3),
                ('disp', c.c_double * 3), ('dir', c.c_double * 3)]


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--speed', type=float, default=8.0)
    parser.add_argument('--fraction', type=float, default=0.098)
    parser.add_argument('--vz', type=float, default=0.0, help='vertical body velocity m/s (engine climbs ~1.1 in the tow test)')
    parser.add_argument('--flap', action='store_true', help='flapping branch (throttle 1)')
    parser.add_argument('--model', type=int, default=0, help='wing model id (0=raven, 2=kestrel)')
    parser.add_argument('--legacy', action='store_true', help='skip set_frame: run the pre-recoil (non-spatial) branch')
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
    get_loads = lib.b2f_math_get_wing_loads
    get_loads.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(WingLoads), c.POINTER(WingLoads)]
    get_fields = lib.b2f_math_get_wing_fields
    get_fields.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(WingField), c.POINTER(WingField)]

    tuning = Tuning(857, 0.735, 20, 7.4, 0.08, 1.3, 30, 60, 32,
                    -2.3, -4, 50, 40, 55, 0, 0.05, 0, 50, 100, 0, 0)
    ctx = create(None)
    try:
        assert set_model(ctx, args.model) == 1
        assert reconfigure(ctx, c.byref(tuning)) == 1
        props = (c.c_double * 7)()
        assert get_mass(ctx, 0, props) == 1
        dry_roll, dry_pitch, dry_yaw = props[4], props[5], props[6]
        if not args.legacy:
            assert set_frame(ctx, 0.0, dry_pitch, dry_yaw) == 1
        body = Body(1 / args.hz, (args.speed, 0, args.vz), (0, 0, 0), 1000, dry_roll, (0, 0, 1))
        pilot, out = Pilot(1.0 if args.flap else 0.0, 0, 0, 0, .5, 1), Output()
        settle, sweep, hold = 1.0, 8.0, 20.0
        count = round(args.hz * (settle + sweep + hold))
        angle = 0.0
        trace = []
        for i in range(count):
            t = i / args.hz - settle
            frac = 0.0 if t < 0 else (t / sweep if t < sweep else 1.0)
            pilot.roll = args.fraction * frac
            assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
            body.w[0] += out.moment[0] / dry_roll / args.hz
            angle += body.w[0] / args.hz
            body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
            if t >= sweep - 0.5 or i == count - 1:
                trace.append((round(t, 2), round(out.moment[0], 6), round(body.w[0], 6)))
            if 7.95 <= t <= 8.05:
                print('AT8 angle_deg', round(math.degrees(angle), 4), 'rate', round(body.w[0], 6), 'outMx', round(out.moment[0], 8))
            if 12.9 <= t <= 13.05:
                print('AT13 angle_deg', round(math.degrees(angle), 4), 'rate', round(body.w[0], 6), 'outMx', round(out.moment[0], 8))
        print('DRY_ROLL', dry_roll)
        print('SERVO_ANGLES', out.left, out.right)
        print('TRACE t,outMx,rollrate:', trace)
        print('FINAL roll_rate', round(body.w[0], 6), 'body_angle_deg', round(math.degrees(angle), 4))
        n = 16
        loads_l = (WingLoads * n)()
        loads_r = (WingLoads * n)()
        fields_l = (WingField * n)()
        fields_r = (WingField * n)()
        r_loads = get_loads(ctx, n, loads_l, loads_r)
        r_fields = get_fields(ctx, n, fields_l, fields_r)
        if r_loads < n or r_fields < n:
            print('GET_LOADS', r_loads, 'GET_FIELDS', r_fields)
            return 1
        for side, fields in (('L', fields_l), ('R', fields_r)):
            p0, p15 = fields[0].pos, fields[15].pos
            print(f'STRIPPOS {side} root=({p0[0]:.4f},{p0[1]:.4f},{p0[2]:.4f}) tip=({p15[0]:.4f},{p15[1]:.4f},{p15[2]:.4f})')
        total = dict(total_moment=out.moment[0],
                     total_force=tuple(round(v, 5) for v in out.force),
                     moment_x_y_z=tuple(round(v, 8) for v in out.moment))
        for side, loads, fields in (('L', loads_l, fields_l), ('R', loads_r, fields_r)):
            sum_force = [sum(loads[i].force[j] for i in range(n)) for j in range(3)]
            sum_lift = [sum(fields[i].lift[j] for i in range(n)) for j in range(3)]
            sum_moment = [sum(loads[i].moment[j] for i in range(n)) for j in range(3)]
            rx = sum(fields[i].pos[1] * loads[i].force[2] - fields[i].pos[2] * loads[i].force[1] for i in range(n))
            total[side] = dict(sum_force=tuple(round(v, 5) for v in sum_force),
                               sum_lift=tuple(round(v, 5) for v in sum_lift),
                               sum_moment=tuple(round(v, 6) for v in sum_moment),
                               sum_rxF_x=round(rx, 6))
        print(json.dumps(total, indent=1))
    finally:
        destroy(ctx)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())