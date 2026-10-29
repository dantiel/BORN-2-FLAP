# -*- coding: utf-8 -*-
"""Diagnose: hover (throttle 0.72) + slow full roll — dump per-100ms state."""
import ctypes as c
import configparser
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw', 'speed', 'coupled')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('v', c.c_double * 3), ('w', c.c_double * 3),
                ('height', c.c_double), ('inertia', c.c_double), ('up', c.c_double * 3)]


class Output(c.Structure):
    _fields_ = [('force', c.c_double * 3), ('moment', c.c_double * 3),
                ('power', c.c_double), ('separation', c.c_double),
                ('left', c.c_double), ('right', c.c_double), ('soc', c.c_double),
                ('envelope', c.c_double), ('coverage', c.c_double),
                ('phase', c.c_double), ('gain', c.c_double), ('flap_hz', c.c_double), ('flags', c.c_uint32)]


class Tuning(c.Structure):
    _fields_ = [(n, c.c_double) for n in (
        'ServoSpeed', 'StallTorque', 'Backdrive', 'BatteryVoltage',
        'BatteryResistance', 'BatteryCapacity', 'BatteryCRating', 'BatteryBurstCRating', 'FlapBaseFreq',
        'TailElevatorAngle', 'GlideAngle', 'StrokeFerocity', 'AileronScale',
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity')]


def main():
    hz = 960
    dt = 1.0 / hz
    lib = c.CDLL(str(DLL.resolve()))
    assert lib.b2f_math_abi_version() == 16
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]

    cfg = configparser.ConfigParser()
    cfg.read(ROOT / 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    tuning = Tuning(*(cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                      for n, _ in Tuning._fields_))
    tuning.BodyMassKg = 0.086

    ctx = create(None)
    assert reconfigure(ctx, c.byref(tuning)) == 1
    props = (c.c_double * 16)()
    assert get_mass(ctx, 0, props) == 1
    inertia = props[4]

    body = Body(dt, (0, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
    pilot, out = Pilot(0.72, 0.0, 0, 0, .5, 1), Output()
    angle = 0.0
    # settle 2 s, then ramp roll 0->1 over 8 s; print every 0.5 s
    total = round((2 + 8 + 1) * hz)
    for i in range(total):
        t = i / hz - 2.0
        frac = 0.0 if t < 0 else (t / 8.0 if t < 8.0 else 1.0)
        pilot.roll = frac
        ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
        body.w[0] += out.moment[0] / inertia * dt
        angle += body.w[0] * dt
        body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
        if i % 480 == 0:
            print('t=%6.2f stick=%.2f L=%8.2f R=%8.2f w=%8.3f ang=%8.2f Mx=%9.5f'
                  % (t, frac, out.left, out.right, body.w[0], math.degrees(angle), out.moment[0]))
    destroy(ctx)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
