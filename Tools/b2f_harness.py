"""BORN 2 FLAP — reusable physics-experiment harness.

Loads the real ``born2flap_math.dll`` and exposes the full firmware-vehicle
ABI behind a single API so future experiments (and fine-tuning sweeps) don't
re-derive ctypes layouts. Provides:

  * ``B2F``            — DLL loader + ABI-checked ctypes bindings.
  * ``Servo``/``PRESETS`` — the 7 in-game servo presets + custom spec builder.
  * ``tuning(...)``    — B2F_TuningConfig builder (servo/battery/profile knobs).
  * ``thrust_curve()`` — fixed-airspeed wind-tunnel thrust/flap measurement.
  * ``free_flight()``  — 6-DOF rigid-body flight integrator (quaternion attitude,
                         Euler rigid-body equations, gravity, ground plane).

This module is READ-ONLY with respect to the game: no saved settings, no
Unreal objects — it only talks to the math DLL and integrates in pure Python.
"""

from __future__ import annotations

import ctypes as c
import math
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DLL_DEFAULT = ROOT / "Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll"
ABI = 14

# --------------------------------------------------------------------------- #
# ctypes FFI layouts (match Native/include/born2flap_math.h)
# --------------------------------------------------------------------------- #


class _Pilot(c.Structure):
    _fields_ = [(n, c.c_double) for n in
                ("throttle", "roll", "pitch", "yaw", "speed", "coupled")]


class _Body(c.Structure):
    _fields_ = [("dt", c.c_double), ("v", c.c_double * 3), ("w", c.c_double * 3),
                ("height", c.c_double), ("inertia", c.c_double)]


class _Output(c.Structure):
    _fields_ = [("force", c.c_double * 3), ("moment", c.c_double * 3),
                ("power", c.c_double), ("separation", c.c_double),
                ("left", c.c_double), ("right", c.c_double), ("soc", c.c_double),
                ("envelope", c.c_double), ("coverage", c.c_double),
                ("phase", c.c_double), ("gain", c.c_double), ("flags", c.c_uint32)]


class _Tuning(c.Structure):
    _fields_ = [(n, c.c_double) for n in (
        "ServoSpeed", "StallTorque", "Backdrive", "BatteryVoltage",
        "BatteryResistance", "BatteryCapacity", "BatteryCRating",
        "BatteryBurstCRating", "FlapBaseFreq", "TailElevatorAngle", "GlideAngle",
        "StrokeFerocity", "AileronScale", "ElevatorScale", "MountAngle",
        "BodyMassKg", "VerticalMountAngle", "ReturnFerocity")]


class B2F:
    """Thin ABI-checked wrapper around born2flap_math.dll."""

    def __init__(self, dll: Path = DLL_DEFAULT):
        self.lib = c.CDLL(str(Path(dll).resolve()))
        assert self.lib.b2f_math_abi_version() == ABI, "ABI mismatch"
        assert self.lib.b2f_math_runtime_init() == 1
        self.create = self._bind("b2f_math_create_firmware_vehicle",
                                 [c.c_void_p], c.c_void_p)
        self.destroy = self._bind("b2f_math_destroy_firmware_vehicle",
                                  [c.c_void_p], None)
        self._step_fn = self._bind("b2f_math_step_firmware_vehicle",
                                   [c.c_void_p, c.POINTER(_Pilot),
                                    c.POINTER(_Body), c.POINTER(_Output)], c.c_int)
        self.reconfigure = self._bind("b2f_math_reconfigure_firmware_vehicle",
                                      [c.c_void_p, c.POINTER(_Tuning)], c.c_int)
        self.get_mass = self._bind("b2f_math_get_mass_properties",
                                   [c.c_void_p, c.c_double,
                                    c.POINTER(c.c_double)], c.c_int)
        # Optional ABI v13 model-selection entry points. The C bridge
        # (cbits/bridge.c) does not yet re-export these even though FFI.hs and
        # the header declare them, so bind defensively.
        self.set_wing_model = self._bind_optional("b2f_math_set_wing_model",
                                                  [c.c_void_p, c.c_uint32], c.c_int)
        self.get_model_mass = self._bind_optional("b2f_math_get_model_mass_kg",
                                                  [c.c_uint32], c.c_double)

    def _bind(self, name, argtypes, restype):
        fn = getattr(self.lib, name)
        fn.argtypes, fn.restype = argtypes, restype
        return fn

    def _bind_optional(self, name, argtypes, restype):
        try:
            return self._bind(name, argtypes, restype)
        except AttributeError:
            return None

    def new(self, tuning: "_Tuning") -> int:
        ctx = self.create(None)
        assert ctx, "create_firmware_vehicle returned null"
        assert self.reconfigure(ctx, c.byref(tuning)) == 1, "reconfigure failed"
        return ctx

    def mass(self, ctx: int) -> dict:
        props = (c.c_double * 7)()
        assert self.get_mass(ctx, 0.0, props) == 1
        keys = ("total", "fuselage", "wing", "tail", "i_xx", "i_yy", "i_zz")
        return dict(zip(keys, props))

    def step(self, ctx: int, pilot: "_Pilot", body: "_Body") -> "_Output":
        out = _Output()
        assert self._step_fn(ctx, c.byref(pilot), c.byref(body), c.byref(out)) == 1
        return out


# --------------------------------------------------------------------------- #
# Servo presets + tuning builder
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class Servo:
    name: str
    speed_deg_s: float
    stall_torque_nm: float
    backdrive: float = 20.0
    voltage: float = 7.4


PRESETS: tuple[Servo, ...] = (
    Servo("Blue Arrow AF D43S-6.0-MG", 1500.0, 0.31, 20.0, 6.0),
    Servo("VOTIK PTK 7465 / 7465W MG", 667.0, 1.01, 20.0, 8.4),
    Servo("VOTIK PTK 7465 Plus", 750.0, 1.33, 20.0, 7.4),
    Servo("Blue Arrow D0576HT-MG-HV", 1000.0, 0.74, 20.0, 7.4),
    Servo("Inservos D0474HT-MG-HV", 750.0, 0.63, 20.0, 7.4),
    Servo("KST MR320", 750.0, 0.97, 20.0, 7.4),
    Servo("SAVOX SV-1270TG", 545.0, 6.17, 20.0, 7.4),
)


def tuning(servo: Servo, *, body_mass_kg: float = 0.050,
           flap_base_freq_dhz: float = 50.0, stroke_ferocity: float = 50.0,
           return_ferocity: float = 50.0,
           aileron_scale: float = 100.0, elevator_scale: float = 100.0,
           tail_elevator: float = 0.0, glide_angle: float = 0.0,
           flap_centre: float = 0.0, vertical_mount: float = 0.0,
           battery_resistance: float = 0.05, battery_capacity: float = 0.35,
           battery_c: float = 30.0, battery_burst_c: float = 60.0) -> _Tuning:
    return _Tuning(
        servo.speed_deg_s, servo.stall_torque_nm, servo.backdrive, servo.voltage,
        battery_resistance, battery_capacity, battery_c, battery_burst_c,
        flap_base_freq_dhz, tail_elevator, glide_angle, stroke_ferocity,
        aileron_scale, elevator_scale, flap_centre, body_mass_kg, vertical_mount,
        return_ferocity,
    )


# --------------------------------------------------------------------------- #
# Wind-tunnel thrust measurement (fixed airspeed)
# --------------------------------------------------------------------------- #


@dataclass
class Thrust:
    mean_force: tuple
    mean_power: float
    flap_amp_deg: float
    peak_flap_deg: float
    rms_rate_deg_s: float
    soc_drop: float
    separation: float


def thrust_curve(b2f: B2F, servo: Servo, *, airspeed: float = 0.0,
                 hz: int = 480, seconds: float = 3.0, warmup: float = 0.5,
                 **tuning_kwargs) -> Thrust:
    """Flap at fixed airspeed; return mean forward force + flap amplitude."""
    cfg = tuning(servo, **tuning_kwargs)
    ctx = b2f.new(cfg)
    try:
        dt = 1.0 / hz
        body = _Body(dt, (airspeed, 0.0, 0.0), (0.0, 0.0, 0.0), 1000.0, 0.008)
        pilot = _Pilot(1.0, 0.0, 0.0, 0.0, 1.0, 1.0)
        fx = fy = fz = power = sep = 0.0
        lmin = lmax = rmin = rmax = 0.0
        rate_sq = 0.0
        prev_l = prev_r = 0.0
        soc_start = soc_end = 0.0
        n = 0
        warm = int(warmup * hz)
        for i in range(int(seconds * hz)):
            out = b2f.step(ctx, pilot, body)
            if i == 0:
                soc_start = out.soc
            if i >= warm:
                fx += out.force[0]; fy += out.force[1]; fz += out.force[2]
                power += out.power; sep += out.separation
                lmin = min(lmin, out.left); lmax = max(lmax, out.left)
                rmin = min(rmin, out.right); rmax = max(rmax, out.right)
                rate_sq += (out.left - prev_l) ** 2 + (out.right - prev_r) ** 2
                n += 1
            prev_l, prev_r = out.left, out.right
            soc_end = out.soc
        amp = 0.5 * ((lmax - lmin) + (rmax - rmin))
        return Thrust(
            mean_force=(fx / n, fy / n, fz / n),
            mean_power=power / n,
            flap_amp_deg=amp,
            peak_flap_deg=max(abs(lmin), abs(lmax), abs(rmin), abs(rmax)),
            rms_rate_deg_s=math.sqrt(rate_sq / (2 * n)) * hz if n else 0.0,
            soc_drop=(soc_start - soc_end),
            separation=sep / n,
        )
    finally:
        b2f.destroy(ctx)


# --------------------------------------------------------------------------- #
# 6-DOF free-flight integrator
# --------------------------------------------------------------------------- #


@dataclass
class FlightResult:
    servo: str
    body_mass_kg: float
    duration_s: float
    max_altitude_m: float
    final_altitude_m: float
    climb_rate_m_s: float
    max_speed_m_s: float
    final_speed_m_s: float
    max_roll_deg: float
    max_pitch_deg: float
    travelled_m: float
    crashed: bool
    mean_power_w: float
    soc: float
    t: list = field(default_factory=list)
    alt: list = field(default_factory=list)
    speed: list = field(default_factory=list)
    roll: list = field(default_factory=list)
    pitch: list = field(default_factory=list)
    fx: list = field(default_factory=list)
    flap_amp: list = field(default_factory=list)


class _Quat:
    __slots__ = ("w", "x", "y", "z")

    def __init__(self, w=1.0, x=0.0, y=0.0, z=0.0):
        self.w, self.x, self.y, self.z = w, x, y, z

    def rotate(self, v):
        """Rotate vector by quaternion (body -> world)."""
        w, x, y, z = self.w, self.x, self.y, self.z
        vx, vy, vz = v
        tx = 2 * (y * vz - z * vy)
        ty = 2 * (z * vx - x * vz)
        tz = 2 * (x * vy - y * vx)
        return (
            vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx),
        )

    def integrate(self, w, dt):
        """Integrate attitude by body angular velocity w (rad/s)."""
        wx, wy, wz = w
        q = self
        dqw = 0.0
        dqx = 0.5 * (wx * q.w + wy * q.z - wz * q.y)
        dqy = 0.5 * (wy * q.w + wz * q.x - wx * q.z)
        dqz = 0.5 * (wz * q.w + wx * q.y - wy * q.x)
        qw = q.w + dqw * dt
        qx = q.x + dqx * dt
        qy = q.y + dqy * dt
        qz = q.z + dqz * dt
        norm = math.sqrt(qw * qw + qx * qx + qy * qy + qz * qz)
        return _Quat(qw / norm, qx / norm, qy / norm, qz / norm)

    def euler(self):
        """Return (roll, pitch, yaw) in radians, ZYX intrinsic order."""
        w, x, y, z = self.w, self.x, self.y, self.z
        sinp = 2 * (w * y - z * x)
        sinp = max(-1.0, min(1.0, sinp))
        pitch = math.asin(sinp)
        roll = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        return roll, pitch, yaw


def free_flight(b2f: B2F, servo: Servo, *, throttle: float = 1.0,
                roll_cmd: float = 0.0, pitch_cmd: float = 0.0,
                hz: int = 480, seconds: float = 10.0,
                launch_speed: float = 0.0, launch_pitch_deg: float = 0.0,
                ground: float = 0.0, g: float = 9.80665,
                record_every: int = 8, **tuning_kwargs) -> FlightResult:
    """Free rigid-body flight: start on/above ground, flap, integrate.

    The math DLL emits aerodynamic+propulsive force/moment in the body frame
    (X forward, Y right, Z up); this integrator adds gravity, solves the Euler
    rigid-body equations with a diagonal inertia tensor, and advances a
    quaternion attitude. Returns metrics + a coarse time series.
    """
    cfg = tuning(servo, **tuning_kwargs)
    ctx = b2f.new(cfg)
    try:
        mass = b2f.mass(ctx)
        m = mass["total"]
        I = (max(mass["i_xx"], 1e-9), max(mass["i_yy"], 1e-9), max(mass["i_zz"], 1e-9))
        dt = 1.0 / hz
        steps = int(seconds * hz)
        lp = math.radians(launch_pitch_deg)
        q = _Quat(math.cos(lp / 2), 0.0, math.sin(lp / 2), 0.0)
        vel = (launch_speed * math.cos(lp), 0.0, launch_speed * math.sin(lp))
        omega = (0.0, 0.0, 0.0)
        alt = ground
        pos = 0.0
        pilot = _Pilot(throttle, roll_cmd, pitch_cmd, 0.0, 1.0, 1.0)

        res = FlightResult(
            servo=servo.name, body_mass_kg=m, duration_s=0.0,
            max_altitude_m=alt, final_altitude_m=alt, climb_rate_m_s=0.0,
            max_speed_m_s=launch_speed, final_speed_m_s=launch_speed,
            max_roll_deg=0.0, max_pitch_deg=launch_pitch_deg, travelled_m=0.0,
            crashed=False, mean_power_w=0.0, soc=1.0,
        )
        power_sum = 0.0
        lmin = lmax = rmin = rmax = 0.0
        for i in range(steps):
            body = _Body(dt, vel, omega, max(alt, 0.0), I[0])
            out = b2f.step(ctx, pilot, body)
            F = (out.force[0], out.force[1], out.force[2])
            M = (out.moment[0], out.moment[1], out.moment[2])
            Fw = q.rotate(F)
            Fw = (Fw[0], Fw[1], Fw[2] - m * g)
            vel = (vel[0] + Fw[0] / m * dt,
                   vel[1] + Fw[1] / m * dt,
                   vel[2] + Fw[2] / m * dt)
            alt += vel[2] * dt
            pos += vel[0] * dt
            Ix, Iy, Iz = I
            wxd = (M[0] - (Iz - Iy) * omega[1] * omega[2]) / Ix
            wyd = (M[1] - (Ix - Iz) * omega[2] * omega[0]) / Iy
            wzd = (M[2] - (Iy - Ix) * omega[0] * omega[1]) / Iz
            omega = (omega[0] + wxd * dt, omega[1] + wyd * dt, omega[2] + wzd * dt)
            q = q.integrate(omega, dt)

            power_sum += out.power
            lmin = min(lmin, out.left); lmax = max(lmax, out.left)
            rmin = min(rmin, out.right); rmax = max(rmax, out.right)

            roll, pitch, _ = q.euler()
            speed = math.sqrt(vel[0] ** 2 + vel[1] ** 2 + vel[2] ** 2)
            res.max_altitude_m = max(res.max_altitude_m, alt)
            res.max_speed_m_s = max(res.max_speed_m_s, speed)
            res.max_roll_deg = max(res.max_roll_deg, abs(math.degrees(roll)))
            res.max_pitch_deg = max(res.max_pitch_deg, abs(math.degrees(pitch)))
            res.travelled_m = pos
            res.soc = out.soc

            if i % record_every == 0:
                res.t.append(i * dt)
                res.alt.append(alt)
                res.speed.append(speed)
                res.roll.append(math.degrees(roll))
                res.pitch.append(math.degrees(pitch))
                res.fx.append(Fw[0])
                res.flap_amp.append(0.5 * ((lmax - lmin) + (rmax - rmin)))

            if alt <= 0.0 and i > hz // 2 and speed > 1.0:
                res.crashed = True
                break

        res.duration_s = (res.t[-1] if res.t else 0.0) or (steps * dt)
        res.final_altitude_m = alt
        res.final_speed_m_s = math.sqrt(vel[0] ** 2 + vel[1] ** 2 + vel[2] ** 2)
        res.mean_power_w = power_sum / max(len(res.t), 1)
        if res.t and len(res.t) > 1:
            res.climb_rate_m_s = (res.alt[-1] - res.alt[0]) / (res.t[-1] - res.t[0])
        return res
    finally:
        b2f.destroy(ctx)