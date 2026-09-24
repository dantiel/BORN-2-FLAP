"""Coupled gravity/aerodynamics regression using the real Haskell DLL (SI).

Unlike a fixed-airspeed smoke test, body velocity feeds back into the next step.
This is a level-body numerical harness; Unreal's Chaos/contact tests are separate.
"""
import ctypes as c
import math
import itertools
import sys
from pathlib import Path


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('velocity', c.c_double * 3), ('rates', c.c_double * 3)]


class Output(c.Structure):
    _fields_ = [('values', c.c_double * 11), ('flags', c.c_uint32)]


class Backend:
    def __init__(self, path):
        path = Path(path)
        if path.is_dir():
            matches = [p for p in path.rglob('*') if p.name in
                       ('libborn2flap_math.so', 'libborn2flap_math.dylib', 'born2flap_math.dll')]
            assert len(matches) == 1, f'Expected one library, found {matches}'
            path = matches[0]
        self.lib = c.CDLL(str(path.resolve()))
        assert self.lib.b2f_math_runtime_init() == 1
        self.create = self.lib.b2f_math_create_firmware_vehicle
        self.create.argtypes, self.create.restype = [c.c_void_p], c.c_void_p
        self.destroy = self.lib.b2f_math_destroy_firmware_vehicle
        self.destroy.argtypes = [c.c_void_p]
        self.destroy.restype = None
        self.step = self.lib.b2f_math_step_firmware_vehicle
        self.step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]

    def tick(self, context, throttle, velocity, dt=1/240):
        output = Output()
        body = Body(dt, (c.c_double * 3)(*velocity), (c.c_double * 3)(0, 0, 0))
        assert self.step(context, c.byref(Pilot(throttle, 0, 0, 0)), c.byref(body), c.byref(output)), 'backend rejected a valid flight step'
        assert all(math.isfinite(v) for v in output.values), 'non-finite output'
        assert max(abs(output.values[8]), abs(output.values[9])) <= 85.001, 'servo escaped mechanical travel'
        return list(output.values)


def run(path):
    backend = Backend(path)
    # Passive held wings must oppose vertical motion, including reverse flow.
    for velocity in itertools.product((-5, 0, 5), repeat=3):
        ctx = backend.create(None)
        try:
            out = backend.tick(ctx, 0, velocity)
            power = sum(a*b for a, b in zip(out[:3], velocity))
            print(f'passivity v={velocity} force={out[:3]} work={power:.4f} W', flush=True)
            assert power <= 1e-8, 'unpowered aerodynamic force adds energy to the body'
        finally:
            backend.destroy(ctx)
    for fps in (30, 60, 144):
        ctx = backend.create(None)
        dt, z, velocity = 1/240, 2.0, [0.0, 0.0, 0.0]
        peak_z, peak_speed = z, 0.0
        try:
            # Six seconds falling/settling, 20 flapping, then coast to rest.
            for i in range(240*60):
                throttle = float(6 <= i*dt < 26)
                # Mirror a game-frame-held body measurement at the math boundary.
                if i == 0 or int(i*fps/240) != int((i-1)*fps/240):
                    measured = velocity[:]
                out = backend.tick(ctx, throttle, measured)
                for axis in range(3):
                    velocity[axis] += (out[axis]/0.45 - (9.81 if axis == 2 else 0))*dt
                z += velocity[2]*dt
                if z < 0:
                    z = 0.0
                    velocity[2] = max(0, velocity[2])  # inelastic contact
                    velocity[0] *= math.exp(-8*dt)
                    velocity[1] *= math.exp(-8*dt)
                peak_z = max(peak_z, z)
                peak_speed = max(peak_speed, math.dist(velocity, [0, 0, 0]))
                assert z < 100 and peak_speed < 60, f'runaway at t={i*dt:.3f}s z={z} v={velocity}'
                if i == 5*240:
                    assert z < 0.1 and math.dist(velocity, [0, 0, 0]) < 0.2, 'idle aircraft does not settle'
            print(f'coupled {fps}fps/60s: peak_alt={peak_z:.3f}m peak_speed={peak_speed:.3f}m/s final_alt={z:.3f}m', flush=True)
            assert z < 0.1, 'aircraft did not return to ground after throttle release'
        finally:
            backend.destroy(ctx)


if __name__ == '__main__':
    run(sys.argv[1])
