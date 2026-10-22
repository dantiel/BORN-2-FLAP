"""Headless servo-sweep: measure flapping thrust/flap response vs servo spec.

Loads the real born2flap_math.dll, reconfigures the firmware vehicle for a
grid of servo (speed, stall-torque) combinations -- including the 7 in-game
presets and custom/nonexistent extremes -- and steps it in a fixed-airspeed
wind-tunnel fixture. Reports the resulting net thrust, flap amplitude, RMS
servo rate, and power so we can detect NON-MONOTONIC behaviour (a weaker
servo producing more thrust than a stronger one, or a faster servo cancelling
its own stroke).

Read-only: no game or saved settings are touched.
"""
import argparse
import ctypes as c
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT / 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'


class Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('throttle', 'roll', 'pitch', 'yaw', 'speed', 'coupled')]


class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('v', c.c_double * 3), ('w', c.c_double * 3),
                ('height', c.c_double), ('inertia', c.c_double)]


class Output(c.Structure):
    _fields_ = [('force', c.c_double * 3), ('moment', c.c_double * 3),
                ('power', c.c_double), ('separation', c.c_double),
                ('left', c.c_double), ('right', c.c_double), ('soc', c.c_double),
                ('envelope', c.c_double), ('coverage', c.c_double),
                ('phase', c.c_double), ('gain', c.c_double), ('flags', c.c_uint32)]


class Tuning(c.Structure):
    _fields_ = [(n, c.c_double) for n in (
        'ServoSpeed', 'StallTorque', 'Backdrive', 'BatteryVoltage',
        'BatteryResistance', 'BatteryCapacity', 'BatteryCRating', 'BatteryBurstCRating', 'FlapBaseFreq',
        'TailElevatorAngle', 'GlideAngle', 'StrokeFerocity', 'AileronScale',
        'ElevatorScale', 'MountAngle', 'BodyMassKg', 'VerticalMountAngle')]


# In-game servo presets (name, speed deg/s, stall Nm, backdrive, voltage).
PRESETS = [
    ("Blue Arrow AF D43S-6.0-MG", 1500.0, 0.31, 20.0, 6.0),
    ("VOTIK PTK 7465 / 7465W MG", 667.0, 1.01, 20.0, 8.4),
    ("VOTIK PTK 7465 Plus", 750.0, 1.33, 20.0, 7.4),
    ("Blue Arrow D0576HT-MG-HV", 1000.0, 0.74, 20.0, 7.4),
    ("Inservos D0474HT-MG-HV", 750.0, 0.63, 20.0, 7.4),
    ("KST MR320", 750.0, 0.97, 20.0, 7.4),
    ("SAVOX SV-1270TG", 545.0, 6.17, 20.0, 7.4),
]

# Custom / nonexistent servos for extrapolation.
CUSTOM = [
    ("CUSTOM ultra-fast ultra-weak", 2000.0, 0.20, 20.0, 7.4),
    ("CUSTOM fast weak", 1200.0, 0.40, 20.0, 7.4),
    ("CUSTOM mid mid", 750.0, 1.00, 20.0, 7.4),
    ("CUSTOM slow strong", 400.0, 2.50, 20.0, 7.4),
    ("CUSTOM ultra-slow ultra-strong", 300.0, 6.17, 20.0, 7.4),
    ("CUSTOM speed-monster weak", 2500.0, 0.50, 20.0, 7.4),
    ("CUSTOM torque-monster slow", 300.0, 8.00, 20.0, 7.4),
]


def make_tuning(speed, torque, backdrive=20.0, voltage=7.4):
    return Tuning(
        speed, torque, backdrive, voltage,   # servo + battery voltage
        0.05, 0.35, 30.0, 60.0,              # resistance, capacity, C, burstC
        50.0,                                # flap base freq (deci-Hz -> 5 Hz)
        0.0, 0.0,                            # tail elevator, glide angle
        50.0,                                # stroke ferocity
        100.0, 100.0,                        # aileron scale, elevator scale
        0.0,                                 # mount/flap-centre angle
        0.050,                               # body mass kg (kestrel reference)
        0.0,                                 # vertical mount incidence
    )


def run_case(lib, create, destroy, step, reconfigure, speed, torque, voltage,
             airspeed, hz=480, seconds=3.0, warmup=0.5):
    ctx = create(None)
    assert ctx, "create_firmware_vehicle returned null"
    try:
        tuning = make_tuning(speed, torque, voltage=voltage)
        assert reconfigure(ctx, c.byref(tuning)) == 1, (speed, torque, voltage, "reconfigure failed")
        body = Body(1 / hz, (airspeed, 0, 0), (0, 0, 0), 1000, 0.008)
        # Full throttle, neutral speed modifier, coupled.
        pilot = Pilot(1.0, 0, 0, 0, 0.5, 1)
        out = Output()
        total = int(seconds * hz)
        warm = int(warmup * hz)
        fx = fy = fz = power = 0.0
        left_min = right_min = float('inf')
        left_max = right_max = float('-inf')
        prev_left = prev_right = 0.0
        rate_sq = 0.0
        n = 0
        soc_start = None
        soc_end = 0.0
        peak_amp = 0.0
        for i in range(total):
            ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
            assert ok == 1, (speed, torque, voltage, airspeed, i, "solver rejected")
            if i == 0:
                soc_start = out.soc
            if i >= warm:
                fx += out.force[0]; fy += out.force[1]; fz += out.force[2]
                power += out.power
                left_min = min(left_min, out.left)
                left_max = max(left_max, out.left)
                right_min = min(right_min, out.right)
                right_max = max(right_max, out.right)
                dl = out.left - prev_left
                dr = out.right - prev_right
                rate_sq += dl * dl + dr * dr
                n += 1
            prev_left = out.left
            prev_right = out.right
            soc_end = out.soc
        amp = 0.5 * ((left_max - left_min) + (right_max - right_min))
        rms_rate = math.sqrt(rate_sq / (2 * n)) * hz if n else 0.0  # deg/s
        return dict(
            speed=speed, torque=torque, voltage=voltage, airspeed=airspeed,
            mean_fx=fx / n, mean_fy=fy / n, mean_fz=fz / n,
            mean_power=power / n,
            flap_amp_deg=amp,
            peak_left_deg=max(abs(left_min), abs(left_max)),
            rms_rate_deg_s=rms_rate,
            soc_drop=(soc_start - soc_end),
        )
    finally:
        destroy(ctx)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--json', type=Path, default=None)
    args = parser.parse_args()

    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 14, "ABI mismatch"
    assert lib.b2f_math_runtime_init() == 1

    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    step.restype = c.c_int
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    reconfigure.restype = c.c_int

    configs = [(n, s, t, v) for (n, s, t, b, v) in PRESETS + CUSTOM]
    # Plus a 1D speed sweep (fixed torque 1.0 Nm) and torque sweep (fixed 750 deg/s).
    for s in (300, 400, 545, 667, 750, 1000, 1200, 1500, 2000, 2500):
        configs.append((f"SPEED-SWEEP {s}deg/s @1.0Nm", s, 1.0, 7.4))
    for t in (0.2, 0.31, 0.5, 0.63, 0.74, 0.97, 1.01, 1.33, 2.0, 3.0, 6.17, 8.0):
        configs.append((f"TORQUE-SWEEP {t}Nm @750deg/s", 750.0, t, 7.4))

    results = []
    for name, speed, torque, voltage in configs:
        row = dict(name=name)
        for airspeed in (0.0, 8.0):
            try:
                case = run_case(lib, create, destroy, step, reconfigure,
                                speed, torque, voltage, airspeed)
                row[f"v{int(airspeed)}"] = case
            except Exception as exc:
                row[f"v{int(airspeed)}"] = dict(error=str(exc))
        results.append(row)

    report = dict(abi=14, results=results)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
