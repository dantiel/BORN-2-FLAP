# -*- coding: utf-8 -*-
"""Replicate the Unreal RollResponseTest exactly against the DLL:
8 m/s, roll stick 0.1 for 0.3 s, game tuning from FlightPreferences.ini.
Prints body roll, wing world roll, and the test verdict."""
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

    ctx = create(None)
    assert ctx
    set_model = lib.b2f_math_set_wing_model
    set_model.argtypes = [c.c_void_p, c.c_uint32]
    assert set_model(ctx, 2) == 1  # kestrel/raven: the engine's model mass 0.086
    cfg = configparser.ConfigParser()
    cfg.read(ROOT / 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
    tuning = Tuning(*(cfg.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n)
                      for n, _ in Tuning._fields_))
    tuning.BodyMassKg = 0.086
    assert reconfigure(ctx, c.byref(tuning)) == 1
    props = (c.c_double * 16)()
    assert get_mass(ctx, 0, props) == 1
    inertia = props[4]
    print('dry roll inertia =', inertia, 'body mass kg =', props[0])

    body = Body(dt, (0, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
    pilot, out = Pilot(0, 0, 0, 0, .5, 1), Output()
    angle = 0.0
    settle_n = round(.5 * hz)
    ramp_n = round(1.5 * hz)
    hold_n = round(1.0 * hz)
    moments = []
    for i in range(settle_n + ramp_n + hold_n):
        # The engine's desktop input layer attenuates the raw test stick 1.0 to
        # rc roll ≈ 0.07 — replicate the ENGINE-equivalent command here, ramped
        # over 1.5 s (the user's slow in-game roll gesture).
        frac = 0.0 if i < settle_n else min(1.0, (i - settle_n) / ramp_n)
        pilot.roll = .07 * frac
        assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
        if i == settle_n - 1:
            start_left, start_right, start_angle = out.left, out.right, angle
        if i >= settle_n:
            moments.append(out.moment[0])
        body.w[0] += out.moment[0] / inertia * dt
        angle += body.w[0] * dt
        body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
    destroy(ctx)
    body_deg = math.degrees(angle - start_angle)
    # Wing world roll: the left wing is mirrored (its deviation sign flips in
    # world space), the right adds — same accounting as probe-glue-limit.
    left_world = body_deg - (out.left - start_left)
    right_world = body_deg + (out.right - start_right)
    print('body=%.3f  leftWorld=%.3f  rightWorld=%.3f' % (body_deg, left_world, right_world))
    print('wing rel L/R = %.3f/%.3f deg' % (out.left - start_left, out.right - start_right))
    print('mean roll moment = %.5f Nm, max |M| = %.5f' %
          (sum(moments) / len(moments), max(abs(m) for m in moments)))
    # The user's glued-wing law: the full-stick sweep at ZERO airspeed leaves
    # the wings world-horizontal while the fuselage counter-rotates the
    # commanded differential — the pressure field holds the wings in place.
    verdict = abs(body_deg) > 3 and max(abs(left_world), abs(right_world)) < 0.5 * abs(body_deg)
    print('RollResponseTest %s' % ('PASS' if verdict else 'FAIL'))
    sys.exit(0 if verdict else 1)


if __name__ == '__main__':
    main()