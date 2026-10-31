# -*- coding: utf-8 -*-
"""Diagnose wing WORLD roll (mean stroke centre) vs body roll during a slow
flapping-hover roll.  Compares flap-averaged centres, not instantaneous angle.
"""
import ctypes as c
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'
PREFS = ROOT / 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini'


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


def run(args):
    import configparser
    lib = c.CDLL(str(args.dll.resolve()))
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
    set_model = lib.b2f_math_set_wing_model
    set_model.argtypes = [c.c_void_p, c.c_uint32]
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]

    config = configparser.ConfigParser()
    config.read(args.preferences)
    base = Tuning(*(config.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                    for n, _ in Tuning._fields_))

    ctx = create(None)
    try:
        assert set_model(ctx, 2) == 1
        assert reconfigure(ctx, c.byref(base)) == 1
        props = (c.c_double * 16)()
        assert get_mass(ctx, 0, props) == 1
        inertia = props[4]

        body = Body(1 / args.hz, (0, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
        pilot, out = Pilot(args.throttle, 0, 0, 0, .5, 1), Output()
        settle = 2.0
        count = round(args.hz * (settle + args.ramp_seconds + args.hold_seconds))
        angle = 0.0
        initial = None
        # flap-averaged centres
        sL = sR = sN = 0.0
        hL = hR = hN = 0.0
        for i in range(count):
            t = i / args.hz - settle
            frac = 0.0 if t < 0 else (t / args.ramp_seconds if t < args.ramp_seconds else 1.0)
            pilot.roll = frac
            ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
            assert ok == 1, (i, 'solver rejected')
            body.w[0] += out.moment[0] / inertia / args.hz
            angle += body.w[0] / args.hz
            body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
            if t < 0:
                sL += out.left; sR += out.right; sN += 1
                if initial is None and i == round(settle * args.hz) - 1:
                    initial = angle
            elif t > args.ramp_seconds:
                hL += out.left; hR += out.right; hN += 1
        cL0 = sL / sN; cR0 = sR / sN
        cL1 = hL / hN; cR1 = hR / hN
        body_deg = math.degrees(angle - initial)
        wing_L_world = body_deg - (cL1 - cL0)
        wing_R_world = body_deg + (cR1 - cR0)
        print(json.dumps(dict(
            throttle=args.throttle, ramp_s=args.ramp_seconds,
            body_deg=round(body_deg, 2),
            wing_L_world_deg=round(wing_L_world, 2),
            wing_R_world_deg=round(wing_R_world, 2),
            centre_L_shift=round(cL1 - cL0, 2),
            centre_R_shift=round(cR1 - cR0, 2),
            wing_span_deg=round(abs(cL1 - cR1), 1))))
    finally:
        destroy(ctx)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--preferences', type=Path, default=PREFS)
    parser.add_argument('--hz', type=int, default=960)
    parser.add_argument('--throttle', type=float, default=0.72)
    parser.add_argument('--ramp-seconds', type=float, default=8.0)
    parser.add_argument('--hold-seconds', type=float, default=2.0)
    args = parser.parse_args()
    run(args)
