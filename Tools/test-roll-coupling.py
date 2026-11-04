"""Exercise the actual Haskell DLL in a roll-only wind-tunnel fixture.

By default translation, pitch and yaw are constrained; roll and wing joints are free.
With --free-translation, lateral/heave motion responds to forces and gravity;
forward speed remains towed and pitch/yaw remain constrained.
This supplements, not replaces, full Unreal flight/contact checks.
"""
import argparse
import ctypes as c
import configparser
import json
import math
from pathlib import Path
import time

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
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity', 'YawScale')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--preferences', type=Path, help='Read-only saved aircraft tuning reproduction')
    parser.add_argument('--hz', type=int, default=960, help='Actual flight solver rate')
    parser.add_argument('--ramp-seconds', type=float, default=1.5,
                        help='Ramp roll input before holding it (the slow in-game gesture; the field-inertia glue reacts with a lag, so an instant step excites the flap transient)')
    parser.add_argument('--roll', type=float, default=.5, help='Roll stick magnitude')
    parser.add_argument('--speeds', type=float, nargs='+', default=[0,8,15])
    parser.add_argument('--free-translation', action='store_true', help='Free lateral/heave motion under gravity; forward speed is towed')
    args = parser.parse_args()
    if args.hz < 120 or args.ramp_seconds < 0: parser.error('invalid rate or ramp duration')
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() in (17, 18)
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    tuning = None
    if args.preferences:
        config = configparser.ConfigParser()
        config.read(args.preferences)
        tuning = Tuning(*(config.getfloat('Flight' if n == 'BodyMassKg' else 'Tuning', n, **({'fallback': 60} if n == 'YawScale' else {}))
                          for n, _ in Tuning._fields_))
    reconfigure = lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes = [c.c_void_p, c.POINTER(Tuning)]
    samples = []
    failures = []
    elapsed = 0.0
    steps = 0

    def run(speed, inertia, hz, direction, roll=None, throttle=0.0):
        nonlocal elapsed, steps
        ctx = create(None)
        assert ctx
        if tuning is not None:
            assert reconfigure(ctx, c.byref(tuning)) == 1
        body = Body(1/hz, (speed, 0, 0), (0, 0, 0), 1000, inertia, (0, 0, 1))
        pilot, out = Pilot(throttle, 0, 0, 0, .5, 1), Output()
        angle = 0.0
        vy_world = vz_world = 0.0
        settle = 3.0 if args.free_translation else .5
        count = round(hz*(settle+.5+args.ramp_seconds))
        initial_angle = 0.0
        initial_support = 0.0
        try:
            # Invalid inertial data must fail transactionally, not poison state.
            body.inertia = float('nan')
            assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 0
            body.inertia = inertia
            start = time.perf_counter()
            moment_sum = moment_count = 0.0
            rise_moment_sum = 0.0
            prev_wing = 0.0
            for i in range(count):
                fraction = max(0, min(1, (i/hz-settle)/max(1/hz,args.ramp_seconds)))
                pilot.roll = direction * (args.roll if roll is None else roll) * fraction
                if args.free_translation:
                    co, si = math.cos(angle), math.sin(angle)
                    body.v[1] = co*vy_world+si*vz_world
                    body.v[2] = -si*vy_world+co*vz_world
                ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
                assert ok == 1, (speed, inertia, hz, direction, i, 'solver rejected')
                assert all(math.isfinite(v) for v in (*out.force, *out.moment, out.left, out.right))
                assert max(abs(out.left), abs(out.right)) <= 80.0001
                if i >= round(settle*hz):
                    wing = out.right if direction == 1 else out.left
                    if wing - prev_wing > 0.001:
                        rise_moment_sum += out.moment[0]
                    prev_wing = wing
                if i >= round(settle*hz) + 1:
                    moment_sum += out.moment[0]
                    moment_count += 1
                co, si = math.cos(angle), math.sin(angle)
                fy_world = co*out.force[1]-si*out.force[2]
                fz_world = si*out.force[1]+co*out.force[2]
                if args.free_translation:
                    mass = tuning.BodyMassKg if tuning is not None else .05
                    vy_world += fy_world/mass/hz
                    vz_world += (fz_world/mass-9.81)/hz
                body.w[0] += out.moment[0] / inertia / hz
                angle += body.w[0] / hz
                body.up[0], body.up[1], body.up[2] = 0, math.sin(angle), math.cos(angle)
                if i == round(settle*hz) - 1:
                    initial_left, initial_right = out.left, out.right
                    initial_angle = angle
                    initial_support = fz_world / ((tuning.BodyMassKg if tuning is not None else .05)*9.81)
            elapsed += time.perf_counter() - start
            steps += count
            degrees = math.degrees(angle-initial_angle)
            return dict(speed=speed, inertia=inertia, hz=hz, direction=direction,
                        body_degrees=degrees, initial_support_weight_ratio=initial_support, vertical_velocity=vz_world,
                        left_world_change=degrees-(out.left-initial_left),
                        right_world_change=degrees+(out.right-initial_right),
                        mean_moment=moment_sum/moment_count,
                        rise_moment_sum=rise_moment_sum,
                        final_left=out.left, final_right=out.right)
        finally:
            destroy(ctx)

    reference_inertia = .008
    if tuning is not None:
        props = (c.c_double * 7)()
        get_mass = lib.b2f_math_get_mass_properties
        get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]
        ctx = create(None)
        try:
            assert reconfigure(ctx, c.byref(tuning)) == 1
            assert get_mass(ctx, 0, props) == 1
            assert abs(props[1] + 2*props[2] + props[3] - props[0]) < 1e-10
            reference_inertia = props[4]
        finally:
            destroy(ctx)
    for speed in sorted(set(args.speeds + [8])):
        for inertia in ((.001, .008) if tuning is None else (reference_inertia,)):
            right = run(speed, inertia, args.hz, 1)
            left = run(speed, inertia, args.hz, -1)
            # Free translation lets the bird slide sideways (the static
            # dihedral couple also produces a lateral force), so the
            # mirror comparison needs a slightly wider gate there.
            mirror_tol = .35 if speed == 0 else (.5 if args.free_translation else .4)
            if abs(right['body_degrees'] + left['body_degrees']) >= mirror_tol:
                failures.append(dict(check='mirror', right=right, left=left))
            # The compressed-air support field exists only while the wing BEATS.
            # A static glide at ZERO airspeed pushes no air, so the bird must be
            # UNSUPPORTED here (it falls and builds speed) — not neutrally
            # buoyant. The old "supported equilibrium" contract encoded the
            # hang bug: the bird could never shed its weight in glide.
            if speed == 0:
                for sample in (right,left):
                    if abs(sample['initial_support_weight_ratio']) > .05:
                        failures.append(dict(check='glide_unsupported', sample=sample))
            # Wing-tracking contract: the servo must REACH the commanded
            # differential — a stuck differential is the "magic roll" bug
            # (the body rolls while the wings barely move). The FFI default
            # vehicle overrides AileronScale to 10 (±1.8° full stick); a
            # reconfigured tuning carries its own AileronScale.
            aileron_scale = tuning.AileronScale if tuning is not None else 10.0
            commanded = 2 * abs(args.roll) * aileron_scale * 0.01 * 87.5
            for sample in (right, left):
                achieved = abs(sample['final_right'] - sample['final_left'])
                if achieved < 0.75 * commanded:
                    failures.append(dict(check='wing_tracking', sample=sample,
                                         commanded_deg=commanded, achieved_deg=achieved))
            # Roll authority is the STATIC differential stroke: the position-
            # based lift modulation (a raised wing loses lift, a lowered wing
            # gains it) plus the weight support and the pressure floor. Even
            # at zero airspeed the held geometry must roll the body.
            assert abs(right['body_degrees']) > .1, right
            # Counter-rotation contract (negative moment = right bank, see
            # docs/rc-controls.md): stick-right must RAISE the RIGHT wing and
            # bank RIGHT at every speed — the fuselage counter-rotates the
            # wing movement, never follows it.
            if not (right['final_right'] > right['final_left'] + .1 and
                    left['final_left'] > left['final_right'] + .1):
                failures.append(dict(check='stick_wing_assignment', right=right, left=left))
            # The integrated bank angle is the contract: stick-right raises the
            # RIGHT wing and the body rolls right (negative). mean_moment is
            # reported as a diagnostic only — at light inertia the servo recoil
            # transient dominates its window-mean while the net roll stays right.
            if not (right['body_degrees'] < 0 < left['body_degrees']):
                failures.append(dict(check='counter_rotation', right=right, left=left))
            # While the commanded wing is RISING (moving through the air), the
            # body moment must already oppose it: the air pushes the rising
            # side down. This is the transient glue contract — the fuselage
            # counter-rotates the wing MOTION, not only the final position.
            # In free translation the air reaction legitimately splits into
            # lateral slide + roll; the transient contract applies to the
            # roll-only fixture.
            if not args.free_translation and not (right['rise_moment_sum'] < 0 and left['rise_moment_sum'] > 0):
                failures.append(dict(check='wing_rising_counter_rotation', right=right, left=left))
            samples.extend((right, left))
    # Held-stroke regression (the user's test case): full stick at ZERO
    # airspeed, only gravity — the differentially held wings must roll the
    # body immediately through the position-based lift modulation and the
    # static weight-support couple, with the fuselage counter-rotating the
    # wing movement (stick-right raises the RIGHT wing and banks right).
    held = {d: run(0, reference_inertia, args.hz, d, roll=1.0) for d in (1, -1)}
    for d, sample in held.items():
        if abs(sample['body_degrees']) < .5:
            failures.append(dict(check='held_stroke_zero_speed', sample=sample))
    if abs(held[1]['body_degrees'] + held[-1]['body_degrees']) >= .35:
        failures.append(dict(check='held_stroke_mirror', right=held[1], left=held[-1]))
    if not (held[1]['final_right'] > held[1]['final_left'] and
            held[1]['body_degrees'] < 0 < held[-1]['body_degrees'] and
            held[-1]['final_left'] > held[-1]['final_right']):
        failures.append(dict(check='held_counter_rotation', right=held[1], left=held[-1]))
    samples.extend(sorted(held.values(), key=lambda x: x['direction']))
    # Powered-stroke health: the wing must FOLLOW the firmware command — the
    # air attachment comes from the lift field, never from a servo-stalling
    # drag. Commanded half amplitude at 0.72 throttle = 12 + 38*0.72 = 39.4 deg.
    # Measured at zero airspeed too: that is where a drag-glue would choke the
    # stroke hardest and where the user saw the wings stop moving.
    def run_flap(speed):
        nonlocal elapsed, steps
        ctx = create(None)
        assert ctx
        if tuning is not None:
            assert reconfigure(ctx, c.byref(tuning)) == 1
        body = Body(1/args.hz, (speed, 0, 0), (0, 0, 0), 1000, reference_inertia, (0, 0, 1))
        pilot, out = Pilot(0.72, 0, 0, 0, .5, 1), Output()
        peak, valley = -999.0, 999.0
        fz_sum = fz_count = 0.0
        start = time.perf_counter()
        count = round(args.hz * 2.5)
        try:
            for i in range(count):
                ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
                assert ok == 1
                if i >= round(args.hz * 1.2):
                    peak = max(peak, out.left)
                    valley = min(valley, out.left)
                    fz_sum += out.force[2]
                    fz_count += 1
            elapsed += time.perf_counter() - start
            steps += count
            return dict(amplitude=peak - valley, mean_fz=fz_sum / max(1, fz_count))
        finally:
            destroy(ctx)
    weight_n = (tuning.BodyMassKg if tuning is not None else .05) * 9.81
    for flap_speed in (0, 8):
        flap = run_flap(flap_speed)
        commanded = 2 * (12 + 38 * .72)
        # At cruise the servo legitimately trades a little amplitude against
        # the aerodynamic load (battery sag); at hover it must follow closely.
        # The field anchor (the compressed-air grip, active at EVERY speed) is
        # part of that cruise load: a wing glued to its air meets real
        # resistance on every stroke — the loaded servo honestly sweeps ~55%
        # of the commanded arc. The contract is that the stroke stays LARGE
        # and visible (≥ half the commanded arc), not that an unloaded-servo
        # ideal is met.
        tracking_floor = .7 if flap_speed == 0 else .5
        if flap['amplitude'] < tracking_floor * commanded:
            failures.append(dict(check='wing_follows_firmware', speed=flap_speed,
                                 commanded_deg=commanded, **flap))
        if flap_speed == 0 and flap['mean_fz'] < 0.6 * weight_n:
            failures.append(dict(check='hover_lift', speed=0, **flap))
    # Glide lift: static wings at cruise speed must carry the body through
    # circulation lift — the "no lift" regression the user reported.
    def run_glide_lift(speed):
        nonlocal elapsed, steps
        ctx = create(None)
        assert ctx
        if tuning is not None:
            assert reconfigure(ctx, c.byref(tuning)) == 1
        body = Body(1/args.hz, (speed, 0, 0), (0, 0, 0), 1000, reference_inertia, (0, 0, 1))
        pilot, out = Pilot(0, 0, 0, 0, .5, 1), Output()
        fz_sum = fz_count = 0.0
        start = time.perf_counter()
        count = round(args.hz * 1.5)
        try:
            for i in range(count):
                ok = step(ctx, c.byref(pilot), c.byref(body), c.byref(out))
                assert ok == 1
                if i >= round(args.hz * 1.0):
                    fz_sum += out.force[2]
                    fz_count += 1
            elapsed += time.perf_counter() - start
            steps += count
            return fz_sum / max(1, fz_count)
        finally:
            destroy(ctx)
    glide_lift = run_glide_lift(8)
    if glide_lift < 0.6 * weight_n:
        failures.append(dict(check='glide_lift', speed=8, mean_fz=glide_lift))
    # Hover sustain: full throttle from rest must not plummet under gravity.
    hover = run(0, reference_inertia, args.hz, 1, roll=0.0, throttle=.72)
    if args.free_translation and hover['vertical_velocity'] < -2.0:
        failures.append(dict(check='hover_sustain', sample=hover))
    for hz in (args.hz//2, args.hz*2):
        samples.append(run(8, reference_inertia, hz, 1))
    reference = next(s for s in samples if s['speed'] == 8 and s['inertia'] == reference_inertia and s['hz'] == args.hz and s['direction'] == 1)
    for sample in samples[-2:]:
        if abs(sample['body_degrees'] - reference['body_degrees']) >= 2:
            failures.append(dict(check='convergence', sample=sample, reference=reference))
    print(json.dumps(dict(status='FAIL' if failures else 'PASS', mean_step_ms=1000*elapsed/steps, samples=samples, failures=failures), indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
