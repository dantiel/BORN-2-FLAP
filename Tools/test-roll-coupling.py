"""Exercise the actual Haskell DLL in a roll-only wind-tunnel fixture.

Translation, pitch and yaw are constrained; roll and both wing joints are free.
This supplements, not replaces, full Unreal flight/contact checks.
"""
import argparse
import ctypes as c
import configparser
import json
import math
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'


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
                ('phase', c.c_double), ('gain', c.c_double), ('flags', c.c_uint32)]


class Tuning(c.Structure):
    _fields_ = [(n, c.c_double) for n in (
        'ServoSpeed', 'StallTorque', 'Backdrive', 'BatteryVoltage',
        'BatteryResistance', 'BatteryCapacity', 'BatteryCRating', 'BatteryBurstCRating', 'FlapBaseFreq',
        'TailElevatorAngle', 'GlideAngle', 'StrokeFerocity', 'AileronScale',
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--preferences', type=Path, help='Read-only saved aircraft tuning reproduction')
    args = parser.parse_args()
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 14
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    tuning = None
    if args.preferences:
        config = configparser.ConfigParser()
        config.read(args.preferences)
        tuning = Tuning(*(config.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                          for n, _ in Tuning._fields_))
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    samples = []
    failures = []
    elapsed = 0.0
    steps = 0

    def run(speed, inertia, hz, direction):
        nonlocal elapsed, steps
        ctx = create(None)
        assert ctx
        if tuning is not None:
            assert reconfigure(ctx, c.byref(tuning)) == 1
        body = Body(1/hz, (speed, 0, 0), (0, 0, 0), 1000, inertia)
        pilot, out = Pilot(0, 0, 0, 0, .5, 1), Output()
        angle = 0.0
        try:
            # Invalid inertial data must fail transactionally, not poison state.
            body.inertia = float('nan')
            assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 0
            body.inertia = inertia
            start = time.perf_counter()
            for i in range(hz):
                pilot.roll = direction * .5 if i >= hz // 2 else 0
                ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
                assert ok == 1, (speed, inertia, hz, direction, i, 'solver rejected')
                assert all(math.isfinite(v) for v in (*out.force, *out.moment, out.left, out.right))
                assert max(abs(out.left), abs(out.right)) <= 80.0001
                body.w[0] += out.moment[0] / inertia / hz
                angle += body.w[0] / hz
                if i == hz // 2 - 1:
                    initial_left, initial_right = out.left, out.right
            elapsed += time.perf_counter() - start
            steps += hz
            degrees = math.degrees(angle)
            return dict(speed=speed, inertia=inertia, hz=hz, direction=direction,
                        body_degrees=degrees,
                        left_world_change=degrees-(out.left-initial_left),
                        right_world_change=degrees+(out.right-initial_right))
        finally:
            destroy(ctx)

    reference_inertia = .008
    if tuning is not None:
        props = (c.c_double * 7)()
        get_mass = lib.b2f_math_get_mass_properties
        get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]
        ctx = create(None)
        try:
            assert reconfigure(ctx, c.byref(tuning)) == 1
            assert get_mass(ctx, 0, props) == 1
            assert abs(props[1] + 2*props[2] + props[3] - props[0]) < 1e-10
            reference_inertia = props[4]
        finally:
            destroy(ctx)
    for speed in (0, 8, 15):
        for inertia in ((.001, .008) if tuning is None else (reference_inertia,)):
            right = run(speed, inertia, 240, 1)
            left = run(speed, inertia, 240, -1)
            if abs(right['body_degrees'] + left['body_degrees']) >= (.1 if speed == 0 else .01):
                failures.append(dict(check='mirror', right=right, left=left))
            assert abs(right['body_degrees']) > .1, right
            samples.extend((right, left))
    for hz in (120, 480):
        samples.append(run(8, reference_inertia, hz, 1))
    reference = next(s for s in samples if s['speed'] == 8 and s['inertia'] == reference_inertia and s['hz'] == 240 and s['direction'] == 1)
    for sample in samples[-2:]:
        assert abs(sample['body_degrees'] - reference['body_degrees']) < 2, (sample, reference)
    print(json.dumps(dict(status='FAIL' if failures else 'PASS', mean_step_ms=1000*elapsed/steps, samples=samples, failures=failures), indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()