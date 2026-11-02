"""Exercise the installed fixed-tail force field and live incidence control."""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fields', ROOT/'Tools/test-strip-fields.py')
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)


class Tail(c.Structure):
    _fields_ = [(n, c.c_double*3) for n in ('position', 'normal', 'velocity', 'lift', 'pressure', 'drag', 'moment')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', type=Path, default=f.fixture.DLL)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1 and lib.b2f_math_abi_version() == 17

    def bind(name, types, result=c.c_int):
        fn = getattr(lib, 'b2f_math_'+name)
        fn.argtypes, fn.restype = types, result
        return fn

    create = bind('create_firmware_vehicle', [c.c_void_p], c.c_void_p)
    destroy = bind('destroy_firmware_vehicle', [c.c_void_p], None)
    tune = bind('reconfigure_firmware_vehicle', [c.c_void_p, c.POINTER(f.Tuning)])
    step = bind('step_firmware_vehicle', [c.c_void_p, c.POINTER(f.Pilot), c.POINTER(f.Body), c.POINTER(f.Output)])
    read = bind('get_tail_fields', [c.c_void_p, c.c_uint32, c.POINTER(Tail)])
    angle = bind('get_tail_incidence', [c.c_void_p, c.POINTER(c.c_double)])
    cfg = configparser.ConfigParser()
    cfg.read(ROOT/'Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini')
    tuning = f.Tuning(*[cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n) for n, _ in f.Tuning._fields_])
    cross = lambda a, b: (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    dot = lambda a, b: sum(x*y for x, y in zip(a, b))
    reports = []
    for incidence in (-8, -2.3, 0, 8):
        ctx = create(None)
        assert ctx
        try:
            tuning.TailElevatorAngle = incidence
            assert tune(ctx, c.byref(tuning)) == 1
            actual = c.c_double()
            # Live setting is visible immediately, even when physics is paused.
            assert angle(ctx, c.byref(actual)) == 1 and abs(actual.value-incidence) < 1e-12
            patches = (Tail*18)()
            out = f.Output()
            body = f.Body(1/240, (8, 0, 0), (0, 0, 0), 1000, 1e3, (0, 0, 1))
            for throttle, pitch, yaw in ((0, 0, 0), (.72, .7, -.6), (1, -.8, .6)):
                pilot = f.Pilot(throttle, 0, pitch, yaw, .5, 1)
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
                assert angle(ctx, c.byref(actual)) == 1 and abs(actual.value-incidence) < 1e-12
                assert read(ctx, 18, patches) == 18
                snapshot = bytes(patches)
                assert read(ctx, 17, patches) == 0 and bytes(patches) == snapshot
                assert read(ctx, 18, None) == 0
                assert read(ctx, 18, patches) == 18 and bytes(patches) == snapshot
                solved_rates = (out.moment[0]*body.dt/body.inertia, 0, 0)
                for p in patches:
                    normal = (-math.sin(math.radians(incidence)), 0, math.cos(math.radians(incidence)))
                    assert max(abs(p.normal[i]-normal[i]) for i in range(3)) < 1e-12
                    force = [p.lift[i]+p.pressure[i]+p.drag[i] for i in range(3)]
                    assert abs(dot(p.lift, p.velocity)) < 1e-10
                    assert dot(p.pressure, p.velocity) <= 1e-10 and dot(p.drag, p.velocity) <= 1e-10
                    expected = cross(p.position, force)
                    assert max(abs(p.moment[i]-expected[i]) for i in range(3)) < 1e-10
                    motion = cross(solved_rates, p.position)
                    assert max(abs(p.velocity[i]-body.v[i]-motion[i]) for i in range(3)) < 1e-6
            force = [sum(p.lift[i]+p.pressure[i]+p.drag[i] for p in patches) for i in range(3)]
            moment = [sum(p.moment[i] for p in patches) for i in range(3)]
            if incidence:
                assert incidence*force[2] > 0 and incidence*moment[1] > 0
            if incidence == -2.3:
                for bank in (-.6, .6):
                    world_fy = math.cos(bank)*force[1]-math.sin(bank)*force[2]
                    world_mz = math.sin(bank)*moment[1]+math.cos(bank)*moment[2]
                    assert bank*world_fy > 0 and bank*world_mz < 0
            reports.append(dict(incidence=incidence, force=force, moment=moment))
        finally:
            destroy(ctx)
    if args.output:
        args.output.write_text(json.dumps(reports, indent=2), encoding='utf-8')
    print('Tail PASS: live incidence, fixed mount under steering/throttle, bounded reads, local flow, passive forces, r x F and bank-to-yaw sign')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()