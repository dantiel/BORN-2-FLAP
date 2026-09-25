"""ONDAS ferocity-price (Rubedo M6, complement to the M0 Haskell unit).

Two independent checks:
  1. The analytic price anchors 1/(1-d), 1/(1-d)^2, 1/(1-d)^3 re-derived in
     Python — mirroring the frozen Haskell unit test (cross-language guard).
  2. The simulator's mechanical power is finite and non-negative across the
     throttle sweep (the physical "no free lunch" bound).

The exact "measured <= analytic, from below" assertion is calibration-dependent
(Rubedo §9: no absolute watt calibration in this stage), so it is deliberately
left as the M0 Haskell anchor test's domain, not re-asserted here.
"""
import ctypes as c
import math
import sys
from pathlib import Path


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('velocity', c.c_double * 3), ('rates', c.c_double * 3)]


class Output(c.Structure):
    _fields_ = [('values', c.c_double * 13), ('flags', c.c_uint32)]


def load(path):
    path = Path(path)
    if path.is_dir():
        matches = [p for p in path.rglob('*') if p.name in
                   ('libborn2flap_math.so', 'libborn2flap_math.dylib', 'born2flap_math.dll')]
        assert len(matches) == 1, f'Expected one library, found {matches}'
        path = matches[0]
    lib = c.CDLL(str(path.resolve()))
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes, destroy.restype = [c.c_void_p], None
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    step.restype = c.c_int32
    return create, destroy, step


def dwell(f):
    return max(0.0, min(8.0, f)) * 0.125 * 0.98


def power_price(d):
    return 1 / (1 - d) ** 3


def force_price(d):
    return 1 / (1 - d) ** 2


def thrust_price(d):
    return 1 / (1 - d)


def run(path):
    # 1. Analytic anchors (Rubedo 3.1, audit-verified 9.5).
    anchors = [(0.05, 1.166, 1.108, 1.053), (0.3, 2.92, 2.04, 1.43), (0.8, 125, 25, 5)]
    for d, p, f, t in anchors:
        assert abs(power_price(d) - p) < 1e-2, 'power price anchor drifted'
        assert abs(force_price(d) - f) < 1e-2, 'force price anchor drifted'
        assert abs(thrust_price(d) - t) < 1e-2, 'thrust price anchor drifted'
    assert abs(dwell(8) - 0.98) < 1e-12 and dwell(0) == 0.0, 'dwell scaling drifted'

    # 2. Physical bound: power is finite and non-negative everywhere.
    create, destroy, step = load(path)
    ctx = create(None)
    peak = 0.0
    try:
        body = Body(1 / 240, (c.c_double * 3)(5, 0, 0), (c.c_double * 3)(0, 0, 0))
        for throttle in (0.0, 0.2, 0.4, 0.6, 0.8, 1.0):
            for _ in range(240 * 2):
                out = Output()
                assert step(ctx, c.byref(Pilot(throttle, 0, 0, 0)), c.byref(body), c.byref(out)), 'step rejected'
                power = out.values[6]
                assert math.isfinite(power) and power >= 0.0, 'power must be finite and non-negative'
                peak = max(peak, power)
    finally:
        destroy(ctx)
    print(f'analytic anchors + physical bound: peak_power={peak:.3f} W', flush=True)


if __name__ == '__main__':
    run(sys.argv[1])
