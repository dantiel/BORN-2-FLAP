"""Free-flight damping probe: roll-damping decay, yaw-damping decay, weathervane.

Correct body-frame conventions (unlike b2f_harness.free_flight, which never
rotates the up vector):
  - firmware Body gets velocity and up in BODY frame
  - integrator keeps velocity/omega in body frame, quaternion attitude
  - gravity is rotated into the body frame each step

Measurements (all glide, throttle 0):
  A) ROLL DAMPING: launch level at cruise, initial roll rate p0, neutral
     stick -> log p(t). Decay rate = L_p / I_xx.
  B) WEATHERVANE: launch with 20 deg sideslip (velocity not aligned with
     nose), neutral stick -> log yaw rate s(t) and beta(t).
  C) YAW DAMPING: launch straight, initial yaw rate s0 -> log s(t).
"""
import ctypes as c
import math
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
        'ElevatorScale', 'FlapCentre', 'BodyMassKg', 'VerticalMountAngle', 'ReturnFerocity',
        'YawScale', 'StrokeSkew', 'ReturnSkew')]


class Quat:
    __slots__ = ('w', 'x', 'y', 'z')

    def __init__(self, w=1.0, x=0.0, y=0.0, z=0.0):
        self.w, self.x, self.y, self.z = w, x, y, z

    def rotate(self, v):
        """body -> world"""
        w, x, y, z = self.w, self.x, self.y, self.z
        vx, vy, vz = v
        tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
        return (vx + w * tx + (y * tz - z * ty),
                vy + w * ty + (z * tx - x * tz),
                vz + w * tz + (x * ty - y * tx))

    def unrotate(self, v):
        """world -> body"""
        w, x, y, z = self.w, self.x, self.y, self.z
        vx, vy, vz = v
        tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
        return (vx + w * tx + (z * ty - y * tz),
                vy + w * ty + (x * tz - z * tx),
                vz + w * tz + (y * tx - x * ty))

    def integrate(self, w, dt):
        wx, wy, wz = w
        q = self
        dqx = 0.5 * (wx * q.w + wy * q.z - wz * q.y)
        dqy = 0.5 * (wy * q.w + wz * q.x - wx * q.z)
        dqz = 0.5 * (wz * q.w + wx * q.y - wy * q.x)
        dqw = -0.5 * (wx * q.x + wy * q.y + wz * q.z)
        qw, qx, qy, qz = q.w + dqw * dt, q.x + dqx * dt, q.y + dqy * dt, q.z + dqz * dt
        n = math.sqrt(qw * qw + qx * qx + qy * qy + qz * qz)
        return Quat(qw / n, qx / n, qy / n, qz / n)


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=DLL)
    parser.add_argument('--speed', type=float, default=8.0)
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--case', default='A', help='A=roll decay, B=weathervane, C=yaw decay')
    parser.add_argument('--p0', type=float, default=0.6)
    parser.add_argument('--s0', type=float, default=0.6)
    parser.add_argument('--beta0', type=float, default=20.0)
    parser.add_argument('--flap', action='store_true', help='flapping branch (throttle 1)')
    parser.add_argument('--model', type=int, default=0, help='wing model id (0=raven, 2=kestrel)')
    parser.add_argument('--seconds', type=float, default=3.0)
    parser.add_argument('--hz', type=int, default=960)
    args = parser.parse_args()

    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version() == 20
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
    set_model.argtypes, set_model.restype = [c.c_void_p, c.c_uint32], c.c_int32
    set_frame = lib.b2f_math_set_mass_frame
    set_frame.argtypes = [c.c_void_p, c.c_double, c.c_double, c.c_double]
    get_mass = lib.b2f_math_get_mass_properties
    get_mass.argtypes = [c.c_void_p, c.c_double, c.POINTER(c.c_double)]

    tuning = Tuning(857, 0.735, 20, 7.4, 0.08, 1.3, 30, 60, 32,
                    -2.3, -4, 50, 40, 55, 0, 0.05, 0, 50, 100, 0, 0)
    ctx = create(None)
    try:
        assert set_model(ctx, args.model) == 1
        assert reconfigure(ctx, c.byref(tuning)) == 1
        props = (c.c_double * 7)()
        assert get_mass(ctx, 0, props) == 1
        m = props[0]
        I = (props[4], props[5], props[6])
        print('MASS', round(m, 5), 'I', [round(v, 6) for v in I], 'legacy' if args.legacy else 'spatial')
        if not args.legacy:
            assert set_frame(ctx, 0.0, I[1], I[2]) == 1
        dt = 1.0 / args.hz
        case = args.case.upper()
        q = Quat()
        if case == 'A':
            vel, omega = (args.speed, 0.0, 0.0), (args.p0, 0.0, 0.0)
        elif case == 'B':
            b = math.radians(args.beta0)
            vel, omega = (args.speed * math.cos(b), args.speed * math.sin(b), 0.0), (0.0, 0.0, 0.0)
        else:
            vel, omega = (args.speed, 0.0, 0.0), (0.0, 0.0, args.s0)
        pilot = Pilot(1.0 if args.flap else 0.0, 0, 0, 0, .5, 1)
        out = Output()
        steps = int(args.seconds * args.hz)
        print(f'CASE {case} t,rollrate,yawrate,pitchrate,beta_deg,bank_deg,My,Mz')
        for i in range(steps):
            up = q.unrotate((0.0, 0.0, 1.0))
            body = Body(dt, vel, omega, 1000, I[0], up)
            assert step(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
            F = (out.force[0], out.force[1], out.force[2])
            M = (out.moment[0], out.moment[1], out.moment[2])
            gb = q.unrotate((0.0, 0.0, -9.80665))
            vx, vy, vz = vel
            wx, wy, wz = omega
            vel = (vx + (F[0] / m + gb[0] - (wy * vz - wz * vy)) * dt,
                   vy + (F[1] / m + gb[1] - (wz * vx - wx * vz)) * dt,
                   vz + (F[2] / m + gb[2] - (wx * vy - wy * vx)) * dt)
            omega = (wx + (M[0] - (I[2] - I[1]) * wy * wz) / I[0] * dt,
                     wy + (M[1] - (I[0] - I[2]) * wz * wx) / I[1] * dt,
                     wz + (M[2] - (I[1] - I[0]) * wx * wy) / I[2] * dt)
            q = q.integrate(omega, dt)
            if i % 24 == 0:
                t = i * dt
                beta = math.degrees(math.atan2(-vel[1], vel[0]))
                bank = math.degrees(math.asin(max(-1, min(1, up[1]))))
                print(f'{t:.3f} {omega[0]:.5f} {omega[2]:.5f} {omega[1]:.5f} {beta:.3f} {bank:.3f} {M[1]:.6f} {M[2]:.6f}')
    finally:
        destroy(ctx)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())