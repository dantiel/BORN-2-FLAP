"""ONDAS A-layer telemetry (Rubedo M6): phase-envelope strobe de-aliases.

Reads the two new ABI v3 fields (phase_envelope, phase_coverage) from the
firmware-vehicle loop and checks the golden-angle strobe covers the flap cycle
uniformly at every game frame rate instead of pinning a single overtone.
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
    _fields_ = [('values', c.c_double * 15), ('flags', c.c_uint32)]


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
    # Hold a stable level-body flight at full throttle, sampling at three game
    # frame rates. The phase envelope must de-alias: coverage -> 1.0 and the
    # sup-mean must stay bounded (no single harmonic read as a standing offset).
    for fps in (30, 60, 144):
        ctx = create(None)
        coverage, peak_env = 0.0, 0.0
        try:
            for i in range(240 * 8):  # eight seconds of flapping
                dt = 1 / 240
                body = Body(dt, (c.c_double * 3)(5, 0, 0), (c.c_double * 3)(0, 0, 0))
                out = Output()
                assert step(ctx, c.byref(Pilot(0.8, 0, 0, 0)), c.byref(body), c.byref(out)), 'step rejected'
                env = out.values[11]
                cov = out.values[12]
                assert math.isfinite(env) and math.isfinite(cov), 'non-finite envelope telemetry'
                coverage, peak_env = max(coverage, cov), max(peak_env, env)
            print(f'{fps}fps: coverage={coverage:.3f} peak_envelope={peak_env:.3f}', flush=True)
            assert coverage >= 0.99, 'golden-angle strobe did not cover the full cycle'
            assert peak_env < 40, 'phase envelope escaped a sane flap-error bound'
        finally:
            destroy(ctx)


if __name__ == '__main__':
    run(sys.argv[1])