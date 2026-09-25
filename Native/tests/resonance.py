"""ONDAS C-layer smoke (Rubedo M6): the RESONANCE fine-lock must not destabilize.

The Vold–Kalman engagement → phase-lock → kGainMod loop runs every step inside
the firmware vehicle. This checks that under sustained flapping the loop keeps
the phase envelope bounded and the aircraft finite — the coupling holds, never
blows up. The "coupling lowers phase_envelope" assertion is the M2 Haskell
unit test's domain (lab-frame vs error-frame).
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


def run(path):
    create, destroy, step = load(path)
    ctx = create(None)
    peak_env, peak_power = 0.0, 0.0
    try:
        body = Body(1 / 240, (c.c_double * 3)(6, 0, -0.5), (c.c_double * 3)(0, 0, 0))
        for _ in range(240 * 8):
            out = Output()
            assert step(ctx, c.byref(Pilot(0.85, 0, 0, 0)), c.byref(body), c.byref(out)), 'step rejected'
            assert all(math.isfinite(v) for v in out.values), 'non-finite output'
            peak_env = max(peak_env, out.values[11])
            peak_power = max(peak_power, out.values[6])
        print(f'resonance-coupled: peak_envelope={peak_env:.3f} peak_power={peak_power:.3f}', flush=True)
        assert peak_env < 40, 'resonance loop let the phase envelope diverge'
    finally:
        destroy(ctx)


if __name__ == '__main__':
    run(sys.argv[1])
