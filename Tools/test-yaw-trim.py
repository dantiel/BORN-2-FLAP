"""Cycle-averaged yaw and sideslip derivatives of the actual compliant wings.

The wind tunnel constrains the body (including its implicit roll inertia), but
leaves the real servos, bending, torsion and membrane history active. It samples
whole measured flap cycles after settling. This is a trim regression, not a
universal requirement for anhedral, inverted or stalled flight.
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


class Loads(c.Structure):
    _fields_ = [(n, c.c_double*3) for n in ('force', 'drag', 'moment')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', type=Path, default=fields.fixture.DLL)
    ap.add_argument('--preferences', type=Path, default=ROOT/'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    ap.add_argument('--hz', type=int, default=240)
    ap.add_argument('--speed', type=float, default=8)
    ap.add_argument('--alpha', type=float, default=0, help='Body angle to the airstream, degrees')
    ap.add_argument('--throttles', type=float, nargs='+', default=[0, .72, 1])
    ap.add_argument('--kinds', choices=['speed', 'yaw', 'sideslip'], nargs='+', default=['speed', 'yaw', 'sideslip'])
    ap.add_argument('--strip-details', action='store_true', help='Include the final strip snapshot (not a cycle average)')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if args.hz < 120 or args.speed <= 0:
        ap.error('require positive speed and at least 120 Hz')
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1 and lib.b2f_math_abi_version() in (17, 18)

    def bind(name, arguments, result=c.c_int):
        fn = getattr(lib, 'b2f_math_'+name)
        fn.argtypes, fn.restype = arguments, result
        return fn

    create = bind('create_firmware_vehicle', [c.c_void_p], c.c_void_p)
    destroy = bind('destroy_firmware_vehicle', [c.c_void_p], None)
    configure = bind('reconfigure_firmware_vehicle', [c.c_void_p, c.POINTER(fields.Tuning)])
    step = bind('step_firmware_vehicle', [c.c_void_p, c.POINTER(fields.Pilot), c.POINTER(fields.Body), c.POINTER(fields.Output)])
    read = bind('get_wing_fields', [c.c_void_p, c.c_uint32, c.POINTER(fields.Field), c.POINTER(fields.Field)])
    loads = bind('get_wing_loads', [c.c_void_p, c.c_uint32, c.POINTER(Loads), c.POINTER(Loads)])
    external = bind('get_external_force', [c.c_void_p, c.POINTER(c.c_double)])
    wind = bind('set_strip_wind', [c.c_void_p, c.c_uint32, c.POINTER(c.c_double), c.POINTER(c.c_double)])
    cfg = configparser.ConfigParser()
    assert cfg.read(args.preferences), args.preferences
    tuning = fields.Tuning(*[
        cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n, **({'fallback': 60} if n == 'YawScale' else {}))
        for n, _ in fields.Tuning._fields_])

    def run(throttle, kind, sign):
        ctx = create(None)
        assert ctx
        try:
            assert configure(ctx, c.byref(tuning)) == 1
            yaw = .6*sign if kind == 'yaw' else 0
            slip = -.5*sign if kind == 'sideslip' else 0
            # Large host inertia represents the tunnel clamp in the coupled
            # solve; merely discarding its free-body impulse is inconsistent.
            alpha=math.radians(args.alpha)
            body = fields.Body(1/args.hz, (args.speed*math.cos(alpha), slip, -args.speed*math.sin(alpha)),
                               (0, 0, yaw), 1000, 1e3, (math.sin(alpha), 0, math.cos(alpha)))
            pilot, out = fields.Pilot(throttle, 0, 0, 0, .5, 1), fields.Output()
            left, right = (fields.Field*16)(), (fields.Field*16)()
            ll, rr = (Loads*16)(), (Loads*16)()
            total_force = (c.c_double*3)()
            # Isolate streamwise speed from the sweep/anhedral crossflow of
            # yaw: a 0.3 m/s difference, applied as independent local wind.
            wl, wr = (c.c_double*48)(), (c.c_double*48)()
            if kind == 'speed':
                for j in range(16):
                    wl[j*3], wr[j*3] = -.15*sign, .15*sign
                assert wind(ctx, 16, wl, wr) == 1
            samples, crests = [], []
            last_angle, rising = 0, False
            for i in range(5*args.hz):
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1, (throttle, kind, sign, i)
                assert read(ctx, 16, left, right) == 16 and loads(ctx, 16, ll, rr) == 16
                assert external(ctx, total_force) == 1
                angle = (out.left+out.right)/2
                delta = angle-last_angle
                if i >= 2*args.hz:
                    if rising and delta < -1e-5:
                        crests.append(len(samples))
                    sample = dict(
                        lift_left=sum(f.lift[2] for f in left),
                        lift_right=sum(f.lift[2] for f in right),
                        forward_left=sum(f.velocity[0] for f in left)/16,
                        forward_right=sum(f.velocity[0] for f in right)/16,
                        lift_roll=sum(f.position[1]*f.lift[2]-f.position[2]*f.lift[1] for f in [*left, *right]),
                        lift_roll_vertical=sum(f.position[1]*f.lift[2] for f in [*left, *right]),
                        lift_roll_lateral=sum(-f.position[2]*f.lift[1] for f in [*left, *right]),
                        lift_roll_inner=sum(f.position[1]*f.lift[2]-f.position[2]*f.lift[1] for f in [*left[:8], *right[:8]]),
                        lift_roll_outer=sum(f.position[1]*f.lift[2]-f.position[2]*f.lift[1] for f in [*left[8:], *right[8:]]),
                        pressure_roll=sum(f.position[1]*f.pressure[2]-f.position[2]*f.pressure[1] for f in [*left, *right]),
                        wing_roll=sum(f.moment[0] for f in [*ll, *rr]),
                        host_roll=out.moment[0],
                        external_side_force=total_force[1],
                        yaw_moment=out.moment[2],
                        centre_deg=angle)
                    samples.append(sample)
                if abs(delta) > 1e-5:
                    rising = delta > 0
                last_angle = angle
            cycles = 0
            if throttle:
                assert len(crests) >= 3, ('not enough complete flap cycles', crests)
                samples = samples[crests[0]:crests[-1]]
                cycles = len(crests)-1
            mean = {k: sum(s[k] for s in samples)/len(samples) for k in samples[0]}
            mean['lift_delta'] = mean['lift_left']-mean['lift_right']
            mean['forward_delta'] = mean['forward_left']-mean['forward_right']
            result = dict(throttle=throttle, kind=kind, sign=sign, cycles=cycles, mean=mean)
            if args.strip_details:
                result['final_strips'] = {
                    name: [dict(position=list(f.position), normal=list(f.normal),
                                lift=list(f.lift), velocity=list(f.velocity)) for f in strip_fields]
                    for name, strip_fields in [('left', left), ('right', right)]}
            return result
        finally:
            destroy(ctx)

    results, checks = [], {}
    for throttle in args.throttles:
        for kind in args.kinds:
            pair = [run(throttle, kind, sign) for sign in (1, -1)]
            results.extend(pair)
            prefix = f'{throttle:g}_{kind}'
            # Discrete crest selection and implicit solver tolerances can shift
            # the sampled endpoint by a step. Allow 1% relative mismatch, while
            # requiring both signed response gates independently below.
            checks[prefix+'_mirror'] = all(
                abs(pair[0]['mean'][k]+pair[1]['mean'][k]) <
                1e-4+.01*max(abs(pair[0]['mean'][k]), abs(pair[1]['mean'][k]))
                for k in ('lift_delta', 'lift_roll', 'wing_roll', 'forward_delta'))
            for row in pair:
                m, sign = row['mean'], row['sign']
                key = prefix+('_right' if sign == 1 else '_left')
                checks[key+'_outside_lift'] = sign*m['lift_delta'] > 1e-4
                checks[key+'_outside_lift_roll'] = sign*m['lift_roll'] < -1e-5
                if kind != 'sideslip':
                    checks[key+'_outside_speed'] = sign*m['forward_delta'] > .01
    report = dict(hz=args.hz, speed=args.speed, alpha_deg=args.alpha, preferences=str(args.preferences), checks=checks, results=results)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    failures = [k for k, v in checks.items() if not v]
    print(json.dumps(dict(checks=len(checks), failures=failures, results=results), indent=2))
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
