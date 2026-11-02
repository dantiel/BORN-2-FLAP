"""Yaw/roll coupling audit against the installed physics DLL.

A curved tow maintains forward speed and prescribed yaw rate; heave, lateral
motion, body roll and elastic wings are free. This is not a full-flight trim test.
Positive body yaw advances the left wing. Compare neutral and both yaw signs,
including cycle averages while flapping, with no pilot roll/rudder command.
"""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fields', ROOT/'Tools/test-strip-fields.py')
fields = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fields)


def mul(a, b):
    w, x, y, z = a
    v, i, j, k = b
    return (w*v-x*i-y*j-z*k, w*i+x*v+y*k-z*j,
            w*j-x*k+y*v+z*i, w*k+x*j-y*i+z*v)


def inverse(q):
    return (q[0], -q[1], -q[2], -q[3])


def rotate(q, v):
    return mul(mul(q, (0, *v)), inverse(q))[1:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=fields.fixture.DLL)
    parser.add_argument('--preferences', type=Path, default=ROOT/'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    parser.add_argument('--hz', type=int, default=240)
    parser.add_argument('--speed', type=float, default=8)
    parser.add_argument('--yaw-rate', type=float, default=.6)
    parser.add_argument('--seconds', type=float, default=1.5)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--expect-outside-bank', action='store_true',
                        help='Optional whole-airframe response gate; inverted-V tail and sideslip can oppose wing lift')
    args = parser.parse_args()
    if args.hz < 120 or args.speed <= 0 or args.yaw_rate <= 0 or args.seconds <= 0:
        parser.error('use positive speed, yaw rate and duration, and at least 120 Hz')
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1 and lib.b2f_math_abi_version() == 17
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(fields.Tuning)]
    mass = lib.b2f_math_get_mass_properties
    mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(fields.Pilot), c.POINTER(fields.Body), c.POINTER(fields.Output)]
    read = lib.b2f_math_get_wing_fields
    read.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(fields.Field), c.POINTER(fields.Field)]
    cfg = configparser.ConfigParser()
    cfg.read(args.preferences)
    tuning = fields.Tuning(*(cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                            for n, _ in fields.Tuning._fields_))

    def run(throttle, yaw):
        ctx = create(None)
        assert ctx
        try:
            assert reconfigure(ctx, c.byref(tuning)) == 1
            props = (c.c_double*7)()
            assert mass(ctx, 0, props) == 1
            dt = 1/args.hz
            body = fields.Body(dt, (args.speed, 0, 0), (0, 0, 0), 1000, props[4], (0, 0, 1))
            pilot = fields.Pilot(throttle, 0, 0, 0, .5, 1)
            out = fields.Output()
            left, right = (fields.Field*16)(), (fields.Field*16)()
            q = (1, 0, 0, 0)
            velocity = (args.speed, 0, 0)
            samples = []
            for i in range(round((2+args.seconds)*args.hz)):
                t = i*dt-2
                body.w[2] = yaw if t >= 0 else 0
                vb = rotate(inverse(q), velocity)
                body.v[:] = (args.speed, vb[1], vb[2])
                velocity = rotate(q, body.v)
                body.up[:] = rotate(inverse(q), (0, 0, 1))
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1, (throttle, yaw, i)
                assert read(ctx, 16, left, right) == 16
                assert all(math.isfinite(v) for v in (*out.force, *out.moment))
                if t >= 0:
                    def mean(wing, name, index):
                        return sum(getattr(f, name)[index] for f in wing)/16
                    lift_l = sum(rotate(q, f.lift)[2] for f in left)
                    lift_r = sum(rotate(q, f.lift)[2] for f in right)
                    lift_moment = sum(f.position[1]*f.lift[2]-f.position[2]*f.lift[1]
                                      for f in [*left, *right])
                    normal = tuple(sum(f.normal[k] for f in [*left, *right])/32 for k in range(3))
                    normal_world = rotate(q, normal)
                    forward = rotate(q, (1, 0, 0))
                    horizontal = math.hypot(forward[0], forward[1])
                    heading_right = (-forward[1]/horizontal, forward[0]/horizontal, 0)
                    surface_bank = math.degrees(math.atan2(-sum(a*b for a, b in zip(normal_world, heading_right)), normal_world[2]))
                    up = rotate(q, (0, 0, 1))
                    bank = math.degrees(math.atan2(-sum(a*b for a, b in zip(up, heading_right)), up[2]))
                    span = rotate(q, tuple(mean(right, 'position', k)-mean(left, 'position', k) for k in range(3)))
                    span_bank = math.degrees(math.atan2(span[2], sum(a*b for a, b in zip(span, heading_right))))
                    samples.append(dict(t=t, forward_delta=mean(left, 'velocity', 0)-mean(right, 'velocity', 0),
                                        expected_yaw_delta=body.w[2]*(mean(right, 'position', 1)-mean(left, 'position', 1)),
                                        lift_delta=lift_l-lift_r, lift_moment=lift_moment,
                                        roll_moment=out.moment[0], body_bank=bank, surface_bank=surface_bank,
                                        span_bank=span_bank, side_speed=body.v[1]))
                force_world = rotate(q, out.force)
                velocity = tuple(velocity[k]+dt*(force_world[k]/props[0]-(9.81 if k == 2 else 0)) for k in range(3))
                body.w[0] += out.moment[0]/props[4]*dt
                w = tuple(body.w)
                rate = math.sqrt(sum(a*a for a in w))
                dq = (math.cos(rate*dt/2), *(a*math.sin(rate*dt/2)/rate if rate else 0 for a in w))
                q = mul(q, dq)
                norm = math.sqrt(sum(a*a for a in q))
                q = tuple(a/norm for a in q)
            averages = {k: sum(s[k] for s in samples)/len(samples) for k in samples[0] if k != 't'}
            return dict(throttle=throttle, yaw_rate=yaw, mean=averages, first=samples[0], final=samples[-1])
        finally:
            destroy(ctx)

    results = [run(throttle, yaw) for throttle in (0, .72) for yaw in (0, args.yaw_rate, -args.yaw_rate)]
    checks = {}
    for start, mode in ((0, 'glide'), (3, 'flap')):
        neutral, positive, negative = results[start:start+3]
        checks[mode+'_neutral_symmetry'] = max(abs(neutral['final'][k]) for k in ('body_bank', 'span_bank', 'lift_delta')) < 1e-4
        checks[mode+'_mirror'] = max(abs(positive[when][k]+negative[when][k])
                                    for when in ('first', 'mean', 'final')
                                    for k in ('forward_delta', 'lift_delta', 'body_bank', 'span_bank')) < 1e-4
        for turn, sample in (('positive', positive), ('negative', negative)):
            sign = math.copysign(1, sample['yaw_rate'])
            first, mean, final = (sample[k] for k in ('first', 'mean', 'final'))
            prefix = mode+'_'+turn
            checks[prefix+'_outside_faster'] = sign*mean['forward_delta'] > .01
            checks[prefix+'_local_speed_geometry'] = abs(mean['forward_delta']-mean['expected_yaw_delta']) < .005*abs(mean['expected_yaw_delta'])
            # Check lift before the later sideslip, pressure reaction and
            # asymmetric twist have changed the two wings' operating points.
            # The MathCore property also covers a complete frozen-geometry
            # flap cycle; instantaneous upstroke lift need not point upwards.
            checks[prefix+'_initial_lift_gain'] = sign*first['lift_delta'] > 1e-3
            checks[prefix+'_initial_lift_roll'] = sign*first['lift_moment'] < -1e-5
            # These are whole-airframe outcomes, not consequences of wing lift
            # alone: the inverted-V tail, hinge recoil and later sideslip act too.
            if args.expect_outside_bank:
                checks[prefix+'_outside_rises'] = sign*final['span_bank'] < -.1
                checks[prefix+'_body_banks'] = sign*final['body_bank'] < -.1
    report = dict(hz=args.hz, speed=args.speed, seconds=args.seconds, checks=checks, results=results)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())