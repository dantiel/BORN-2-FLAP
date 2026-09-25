"""ONDAS stabilized mode (ABI v4): the master switch gates the phase-lock.

Enables the stabilized mode via b2f_math_set_stabilization and injects wind
phase noise via b2f_math_set_wind_phase_noise. With the switch ON the
phase-advance demand k_gain_mod must leave nominal 1 (the oscillator advances
to track the wind) and the phase error δ must stay pinned in its dwell well.
With the switch OFF the same wind must leave k_gain_mod exactly 1.
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
    assert lib.b2f_math_abi_version() == 4
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes, destroy.restype = [c.c_void_p], None
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    step.restype = c.c_int32
    set_stab = lib.b2f_math_set_stabilization
    set_stab.argtypes, set_stab.restype = [c.c_void_p, c.c_uint32], c.c_int32
    set_wind = lib.b2f_math_set_wind_phase_noise
    set_wind.argtypes, set_wind.restype = [c.c_void_p, c.c_double], c.c_int32
    return create, destroy, step, set_stab, set_wind


def run(path):
    create, destroy, step, set_stab, set_wind = load(path)
    body = Body(1 / 240, (c.c_double * 3)(5, 0, 0), (c.c_double * 3)(0, 0, 0))
    pilot = Pilot(0.8, 0, 0, 0)

    def settle(enabled):
        ctx = create(None)
        try:
            assert set_stab(ctx, 1 if enabled else 0) == 1
            assert set_wind(ctx, 1.0) == 1
            peak_delta, k_last = 0.0, 1.0
            for _ in range(240 * 8):
                out = Output()
                assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)), 'step rejected'
                assert all(math.isfinite(v) for v in out.values), 'non-finite output'
                peak_delta = max(peak_delta, abs(out.values[13]))
                k_last = out.values[14]
            return peak_delta, k_last
        finally:
            destroy(ctx)

    peak_off, k_off = settle(False)
    peak_on, k_on = settle(True)

    assert k_off == 1.0, f'off-mode must hold k_gain_mod at nominal 1 (got {k_off})'
    assert abs(k_on - 1.0) > 1e-7, f'on-mode must leave nominal phase-advance (got {k_on})'
    assert peak_on < 0.7, f'phase-lock let δ escape its well (peak |δ|={peak_on})'
    print(f'stabilized mode: off k_gain_mod={k_off:.6f} | on k_gain_mod={k_on:.6f} peak|δ|={peak_on:.4f} rad', flush=True)


if __name__ == '__main__':
    run(sys.argv[1])
