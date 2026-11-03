"""Whole-cycle force ledger with the body held in a wind tunnel.

Keeps the real servos and elastic wings. Separates wing lift, pressure, drag,
resultant and the effective force returned to the host. This is not free flight.
Compare --dll builds using identical preferences, speed, throttle and pitch.
"""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('yaw', ROOT/'Tools/test-yaw-trim.py')
yaw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(yaw)
f = yaw.fields

class Tail(c.Structure):
    _fields_ = [(n, c.c_double*3) for n in ('position', 'normal', 'velocity', 'lift', 'pressure', 'drag', 'moment')]

class Shape(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('fraction','chord','bend','geometric_twist','elastic_twist','camber')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', type=Path, default=f.fixture.DLL)
    ap.add_argument('--preferences', type=Path, default=ROOT/'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    ap.add_argument('--speed', type=float, default=5)
    ap.add_argument('--throttle', type=float, default=.72)
    ap.add_argument('--wheel', type=float, nargs='+', default=[.5],
                    help='Mouse-wheel speed modifier, 0..1')
    ap.add_argument('--yaw', type=float, default=0)
    ap.add_argument('--pitch', type=float, nargs='+', default=[0, .5, 1])
    ap.add_argument('--centre', type=float, nargs='+',
                    help='Replace flap trim with these physical centres (-15..15 deg); negative is anhedral')
    ap.add_argument('--hz', type=int, default=240)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if args.hz < 120 or not 0 <= args.throttle <= 1 or args.speed < 0:
        ap.error('require hz >= 120, nonnegative speed and throttle in [0,1]')
    if any(abs(p) > 1 for p in args.pitch) or any(abs(a) > 15 for a in (args.centre or [])):
        ap.error('pitch must be in [-1,1] and centre in [-15,15]')
    if any(not 0 <= w <= 1 for w in args.wheel) or abs(args.yaw) > 1:
        ap.error('wheel must be in [0,1] and yaw in [-1,1]')
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1 and lib.b2f_math_abi_version() == 17

    def bind(name, arguments, result=c.c_int):
        fn = getattr(lib, 'b2f_math_'+name)
        fn.argtypes, fn.restype = arguments, result
        return fn

    create = bind('create_firmware_vehicle', [c.c_void_p], c.c_void_p)
    destroy = bind('destroy_firmware_vehicle', [c.c_void_p], None)
    configure = bind('reconfigure_firmware_vehicle', [c.c_void_p, c.POINTER(f.Tuning)])
    step = bind('step_firmware_vehicle', [c.c_void_p, c.POINTER(f.Pilot), c.POINTER(f.Body), c.POINTER(f.Output)])
    fields = bind('get_wing_fields', [c.c_void_p, c.c_uint32, c.POINTER(f.Field), c.POINTER(f.Field)])
    loads = bind('get_wing_loads', [c.c_void_p, c.c_uint32, c.POINTER(yaw.Loads), c.POINTER(yaw.Loads)])
    tails = bind('get_tail_fields', [c.c_void_p, c.c_uint32, c.POINTER(Tail)])
    shapes = bind('get_wing_shape', [c.c_void_p, c.c_uint32, c.POINTER(Shape), c.POINTER(Shape)])
    cfg = configparser.ConfigParser()
    assert cfg.read(args.preferences), args.preferences
    base = f.Tuning(*[cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n,
                                  **({'fallback': 35} if n == 'AmpMaxDeg' else {}))
                      for n, _ in f.Tuning._fields_])

    def run(centre, pitch, wheel):
        tuning = f.Tuning.from_buffer_copy(base)
        if centre is not None:
            # Tail incidence is independent of wing position.
            tuning.FlapCentre = max(-15, min(15, -centre))
            if not args.throttle:
                assert abs(centre) <= 15, 'glide trim is limited to 15 degrees'
                tuning.GlideAngle = -centre
        ctx = create(None)
        assert ctx
        try:
            assert configure(ctx, c.byref(tuning)) == 1
            body = f.Body(1/args.hz, (args.speed, 0, 0), (0, 0, 0), 1000, 1e3, (0, 0, 1))
            pilot, out = f.Pilot(args.throttle, 0, pitch, args.yaw, wheel, 1), f.Output()
            left, right = (f.Field*16)(), (f.Field*16)()
            ll, rr = (yaw.Loads*16)(), (yaw.Loads*16)()
            tail = (Tail*18)()
            sl, sr = (Shape*16)(), (Shape*16)()
            samples, crests = [], []
            last, rising, closure = 0, False, 0
            for i in range(6*args.hz):
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1, (centre, pitch, i)
                assert fields(ctx, 16, left, right) == 16 and loads(ctx, 16, ll, rr) == 16
                assert tails(ctx, 18, tail) == 18
                assert shapes(ctx, 16, sl, sr) == 16
                angle = (out.left+out.right)/2
                delta = angle-last
                if i >= 3*args.hz:
                    if rising and delta < -1e-5:
                        crests.append(len(samples))
                    fs, ls = [*left, *right], [*ll, *rr]
                    sample = dict(angle_deg=angle, frequency_hz=out.flap_hz,
                                  tip_bend_abs_mm=500*(abs(sl[15].bend)+abs(sr[15].bend)),
                                  tip_twist_abs_deg=math.degrees((abs(sl[15].elastic_twist)+abs(sr[15].elastic_twist))/2),
                                  power_w=out.power, separation=out.separation,
                                  host_x_n=out.force[0], host_z_n=out.force[2],
                                  stroke_rate_deg_s=delta*args.hz,
                                  wing_pitch_nm=sum(s.moment[1] for s in ls),
                                  tail_pitch_nm=sum(t.moment[1] for t in tail),
                                  host_pitch_nm=out.moment[1])
                    for side, strips in [('left', ll), ('right', rr)]:
                        for axis, j in [('x', 0), ('y', 1), ('z', 2)]:
                            sample[side+'_force_'+axis+'_n'] = sum(s.force[j] for s in strips)
                            sample[side+'_moment_'+axis+'_nm'] = sum(s.moment[j] for s in strips)
                    sample['host_roll_nm'] = out.moment[0]
                    sample['host_yaw_nm'] = out.moment[2]
                    for axis, j in [('x', 0), ('z', 2)]:
                        sample['tail_'+axis+'_n'] = sum(t.lift[j]+t.pressure[j]+t.drag[j] for t in tail)
                    for name, arr, attr in [('lift', fs, 'lift'), ('pressure', fs, 'pressure'),
                                            ('drag', ls, 'drag'), ('wing', ls, 'force')]:
                        for axis, j in [('x', 0), ('z', 2)]:
                            sample[name+'_'+axis+'_n'] = sum(getattr(s, attr)[j] for s in arr)
                    closure = max(closure, max(abs(s.force[j]-v.lift[j]-v.pressure[j]-s.drag[j])
                                               for v, s in zip(fs, ls) for j in range(3)))
                    assert all(math.isfinite(v) for v in sample.values())
                    samples.append(sample)
                if abs(delta) > 1e-5:
                    rising = delta > 0
                last = angle
            if args.throttle:
                assert len(crests) >= 3, ('insufficient complete cycles', crests)
                samples = samples[crests[0]:crests[-1]]
            assert closure < 1e-10, closure
            mean = {k: sum(s[k] for s in samples)/len(samples) for k in samples[0]}
            by_phase = {}
            for phase, sign in [('downstroke', -1), ('upstroke', 1)]:
                selected = [s for s in samples if sign*s['stroke_rate_deg_s']>1e-5]
                by_phase[phase] = {k: sum(s[k] for s in selected)/len(selected) for k in mean} if selected else {}
            return dict(centre_deg=centre, pitch=pitch, wheel=wheel, mean=mean,
                        by_phase=by_phase,
                        travel_deg=max(s['angle_deg'] for s in samples)-min(s['angle_deg'] for s in samples),
                        cycles=max(0, len(crests)-1), force_closure_error_n=closure)
        finally:
            destroy(ctx)

    results = [run(centre, pitch, wheel) for centre in (args.centre or [None])
               for pitch in args.pitch for wheel in args.wheel]
    report = dict(dll=str(args.dll), preferences=str(args.preferences), hz=args.hz,
                  speed=args.speed, throttle=args.throttle, dry_weight_n=base.BodyMassKg*9.81, results=results)
    output = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(output, encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
