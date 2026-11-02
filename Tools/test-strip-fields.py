"""Measure body AND deformed wing motion with the actual physics DLL.

The tow fixture constrains pitch/yaw/forward speed. --free-translation frees
lateral/heave motion. Neither fixture establishes full-flight acceptance.
"""
import argparse
import ctypes as c
import importlib.util
import json
import math
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('roll_fixture', ROOT/'Tools/test-roll-coupling.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
Pilot, Body, Output, Tuning = fixture.Pilot, fixture.Body, fixture.Output, fixture.Tuning


class Field(c.Structure):
    _fields_ = [(name, c.c_double*3) for name in
                ('position', 'normal', 'lift', 'pressure', 'velocity', 'displacement', 'direction')]


def main():
    import configparser
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dll', type=Path, default=fixture.DLL)
    p.add_argument('--preferences', type=Path, default=ROOT/'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    p.add_argument('--hz', type=int, default=240)
    p.add_argument('--speed', type=float, default=8)
    p.add_argument('--sink', type=float, default=0)
    p.add_argument('--throttle', type=float, default=0)
    p.add_argument('--roll', type=float, default=.25)
    p.add_argument('--ramp', type=float, default=8)
    p.add_argument('--hold', type=float, default=2)
    p.add_argument('--perturb-bank', type=float, default=0,
                   help='Abrupt body/wing bank after settling; measures passive recovery')
    p.add_argument('--free-translation', action='store_true')
    p.add_argument('--output', type=Path)
    p.add_argument('--progress', action='store_true')
    p.add_argument('--expect-level-support', action='store_true',
                   help='Legacy support-spring acceptance gate; NOT a physical requirement')
    args = p.parse_args()
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 17
    assert lib.b2f_math_runtime_init() == 1
    lib.b2f_math_create_firmware_vehicle.argtypes = [c.c_void_p]
    lib.b2f_math_create_firmware_vehicle.restype = c.c_void_p
    lib.b2f_math_destroy_firmware_vehicle.argtypes = [c.c_void_p]
    lib.b2f_math_reconfigure_firmware_vehicle.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    lib.b2f_math_get_mass_properties.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]
    lib.b2f_math_step_firmware_vehicle.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    lib.b2f_math_transport_airfields.argtypes = [c.c_void_p, c.POINTER(c.c_double)]
    fields = getattr(lib, 'b2f_math_get_wing_fields', None)
    if fields:
        fields.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(Field), c.POINTER(Field)]
    cfg = configparser.ConfigParser()
    cfg.read(args.preferences)
    tuning = Tuning(*(cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n) for n, _ in Tuning._fields_))

    def run(direction):
        ctx = lib.b2f_math_create_firmware_vehicle(None)
        assert ctx
        try:
            assert lib.b2f_math_reconfigure_firmware_vehicle(ctx, c.byref(tuning)) == 1
            props = (c.c_double*7)()
            assert lib.b2f_math_get_mass_properties(ctx, 0, props) == 1
            body = Body(1/args.hz, (args.speed, 0, args.sink), (0, 0, 0), 1000, props[4], (0, 0, 1))
            pilot, out = Pilot(args.throttle, 0, 0, 0, .5, 1), Output()
            left, right = (Field*16)(), (Field*16)()
            angle = vy = 0.0
            vz = args.sink
            samples, final = [], []
            peak_rate = orthogonality = normal_error = 0.0
            settle = 2.0
            initial = None
            start=time.perf_counter()
            for i in range(round(args.hz*(settle+args.ramp+args.hold))):
                t = i/args.hz-settle
                if i == round(settle*args.hz) and args.perturb_bank:
                    delta = math.radians(args.perturb_bank)
                    angle += delta
                    body.w[0] = 0
                    assert lib.b2f_math_transport_airfields(ctx, (c.c_double*3)(-delta, 0, 0)) == 1
                pilot.roll = direction*args.roll*max(0, min(1, t/args.ramp))
                co, si = math.cos(angle), math.sin(angle)
                if args.free_translation:
                    body.v[1], body.v[2] = co*vy+si*vz, -si*vy+co*vz
                assert lib.b2f_math_step_firmware_vehicle(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1, (i, 'solver rejected')
                assert all(math.isfinite(x) for x in (*out.force, *out.moment, out.left, out.right))
                # The force and surface telemetry are evaluated before this
                # host attitude increment, so transform them with this angle.
                wing_bank = math.degrees(angle)+(out.right-out.left)/2
                normal_bank = wing_bank
                if fields:
                    assert fields(ctx, 16, left, right) == 16
                    banks = []
                    for f in [*left, *right]:
                        orthogonality = max(orthogonality, abs(sum(a*b for a,b in zip(f.lift, f.velocity))))
                        normal_error = max(normal_error, abs(sum(a*a for a in f.normal)-1))
                        banks.append(math.degrees(angle+math.atan2(-f.normal[1], f.normal[2])))
                    normal_bank = sum(banks)/len(banks)
                world_fz = si*out.force[1]+co*out.force[2]
                if i == round(settle*args.hz)-1:
                    initial = dict(body=math.degrees(angle), wing=wing_bank, normal=normal_bank)
                if t >= args.ramp:
                    final.append((math.degrees(angle), wing_bank, normal_bank, world_fz/(props[0]*9.81)))
                if i % max(1, args.hz//10) == 0:
                    samples.append(dict(t=round(t,3), body=math.degrees(angle), wing=wing_bank,
                                        normal=normal_bank, left=out.left, right=out.right,
                                        lift_ratio=world_fz/(props[0]*9.81)))
                if args.progress and i % args.hz == 0:
                    print(json.dumps(samples[-1]), flush=True)
                if args.free_translation:
                    vy += (co*out.force[1]-si*out.force[2])/props[0]/args.hz
                    vz += (world_fz/props[0]-9.81)/args.hz
                body.w[0] += out.moment[0]/props[4]/args.hz
                angle += body.w[0]/args.hz
                body.up[1], body.up[2] = math.sin(angle), math.cos(angle)
                if t >= 0:
                    peak_rate = max(peak_rate, abs(body.w[0]))
            means = [sum(x[j] for x in final)/len(final) for j in range(4)]
            return dict(wall_seconds=time.perf_counter()-start, simulated_seconds=settle+args.ramp+args.hold, body_deg=means[0]-initial['body'], wing_deg=means[1]-initial['wing'],
                        surface_deg=means[2]-initial['normal'], lift_weight_ratio=means[3],
                        peak_roll_rate=peak_rate, lift_dot_velocity=orthogonality,
                        normal_error=normal_error, final_vertical_velocity=vz, samples=samples)
        finally:
            lib.b2f_math_destroy_firmware_vehicle(ctx)

    result = dict(speed=args.speed, throttle=args.throttle, hz=args.hz, roll=args.roll,
                  ramp=args.ramp, free_translation=args.free_translation, sample=run(1))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2))
    printable = dict(result, sample={k:v for k,v in result['sample'].items() if k != 'samples'})
    print(json.dumps(printable, indent=2))
    s = result['sample']
    # A differential hinge command counter-rotates body relative to wings.
    # Neither absolute bank nor passive return to the horizon is prescribed.
    relative=s['body_deg']-s['wing_deg']
    checks = dict(relative_direction=relative*args.roll <= 1e-4,
                  relative_response=abs(relative) >= abs(args.roll)*15,
                  finite_motion=all(math.isfinite(s[k]) for k in ('body_deg','wing_deg','surface_deg','peak_roll_rate')),
                  lift_direction=s['lift_dot_velocity'] < 1e-8,
                  normals=s['normal_error'] < 1e-10)
    if args.expect_level_support:
        checks.update(direction=s['body_deg']*args.roll < 0,
                      wing_stability=abs(s['surface_deg']) < 3,
                      smooth_roll=s['peak_roll_rate'] < 1)
    if args.perturb_bank:
        checks.pop('relative_direction')
        checks.pop('relative_response')
        if not args.free_translation and args.roll == 0:
            checks['no_horizon_anchor']=abs(s['body_deg']-args.perturb_bank)<.1
    print(json.dumps(checks))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())