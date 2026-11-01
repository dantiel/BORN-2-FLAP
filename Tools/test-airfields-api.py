"""Exercise the actual DLL's strip-field ABI and transactional input handling."""
import argparse
import ctypes as c
import importlib.util
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fields', ROOT/'Tools/test-strip-fields.py')
fields = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fields)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=fields.fixture.DLL)
    args = parser.parse_args()
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1 and lib.b2f_math_abi_version() == 16
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(fields.Pilot), c.POINTER(fields.Body), c.POINTER(fields.Output)]
    read = lib.b2f_math_get_wing_fields
    read.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(fields.Field), c.POINTER(fields.Field)]
    wind = lib.b2f_math_set_strip_wind
    wind.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_double), c.POINTER(c.c_double)]
    rotate = lib.b2f_math_transport_airfields
    rotate.argtypes = [c.c_void_p, c.POINTER(c.c_double)]
    tail = lib.b2f_math_get_tail_incidence
    tail.argtypes = [c.c_void_p, c.POINTER(c.c_double)]
    contexts = [create(None), create(None)]
    assert all(contexts)
    try:
        pilot = fields.Pilot(.72, 0, 0, 0, .5, 1)
        body = fields.Body(1/240, (8, 0, 0), (0, 0, 0), 1000, .00090819, (0, 0, 1))
        outputs = [fields.Output(), fields.Output()]
        for _ in range(120):
            for ctx, out in zip(contexts, outputs):
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
        left, right = (fields.Field*16)(), (fields.Field*16)()

        def snapshot(ctx):
            assert read(ctx, 16, left, right) == 16
            return [v for wing in (left, right) for strip in wing
                    for name, _ in fields.Field._fields_ for v in getattr(strip, name)]

        original = snapshot(contexts[0])
        before = bytes(left), bytes(right)
        assert read(contexts[0], 15, left, right) == 0
        assert (bytes(left), bytes(right)) == before
        assert read(contexts[0], 16, left, None) == 0 and bytes(left) == before[0]
        zero, invalid = (c.c_double*48)(), (c.c_double*48)()
        invalid[47] = math.nan
        assert wind(contexts[0], 15, zero, zero) == 0
        # A valid left array followed by an invalid right must not partially commit.
        gust = (c.c_double*48)()
        gust[2] = 3
        assert wind(contexts[0], 16, gust, invalid) == 0
        assert rotate(contexts[0], (c.c_double*3)(0, math.nan, 0)) == 0
        assert snapshot(contexts[0]) == original
        assert rotate(contexts[0], (c.c_double*3)()) == 1
        assert snapshot(contexts[0]) == original
        turn = (.2, -.1, .05)
        assert rotate(contexts[0], (c.c_double*3)(*turn)) == 1
        assert rotate(contexts[0], (c.c_double*3)(*(-a for a in turn))) == 1
        assert max(abs(a-b) for a, b in zip(snapshot(contexts[0]), original)) < 1e-12
        for ctx, out in zip(contexts, outputs):
            assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
        assert max(abs(a-b) for a, b in zip(outputs[0].force, outputs[1].force)) < 1e-8
        assert max(abs(a-b) for a, b in zip(outputs[0].moment, outputs[1].moment)) < 1e-8
        assert wind(contexts[0], 16, gust, zero) == 1
        assert step(contexts[0], c.byref(pilot), c.byref(body), c.byref(outputs[0])) == 1
        assert step(contexts[1], c.byref(pilot), c.byref(body), c.byref(outputs[1])) == 1
        assert max(abs(a-b) for a, b in zip(outputs[0].force, outputs[1].force)) > 1e-5
        assert wind(contexts[0], 16, zero, zero) == 1
        angle = c.c_double()
        assert tail(contexts[0], c.byref(angle)) == 1
        assert -3.4 < angle.value < -2.3
        neutral_tail = angle.value
        assert tail(contexts[0], None) == 0
        pilot.pitch = .25
        assert step(contexts[0], c.byref(pilot), c.byref(body), c.byref(outputs[0])) == 1
        assert tail(contexts[0], c.byref(angle)) == 1
        assert abs(angle.value - (neutral_tail-18*.25)) < 1e-10
        print('Airfield ABI PASS: bounded reads, atomic inputs, frame transport, local wind, actual tail incidence')
    finally:
        for ctx in contexts:
            if ctx:
                destroy(ctx)


if __name__ == '__main__':
    main()
