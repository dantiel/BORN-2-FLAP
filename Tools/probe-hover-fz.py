# -*- coding: utf-8 -*-
"""Quick hover vertical-force check: throttle ON, no roll, mean Fz vs weight."""
import ctypes as c
import configparser
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
    lib.b2f_math_runtime_init()
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
    ctx = create(None)
    reconfigure(ctx, c.byref(tuning))
    props = (c.c_double * 16)()
    get_mass(ctx, 0, props)
    inertia = props[4]
    mass = props[0]
    body = Body(dt, (0, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
    pilot, out = Pilot(0.72, 0, 0, 0, .5, 1), Output()
    n = round(3 * hz)
    fz_sum = fz_n = 0.0
    for i in range(n):
        step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
        if i >= round(1.5 * hz):
            fz_sum += out.force[2]
            fz_n += 1
    fz_mean = fz_sum / fz_n
    weight = mass * 9.81
    print('mass kg =', mass, 'weight N =', round(weight, 3))
    print('hover Fz mean =', round(fz_mean, 3), 'N  (ratio', round(fz_mean / weight, 2), ')')
    destroy(ctx)


if __name__ == '__main__':
    main()
