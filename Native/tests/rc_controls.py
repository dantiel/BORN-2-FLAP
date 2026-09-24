"""Aerodynamic RC authority through the actual firmware/servo shared library.

Fixed-flow measurements isolate each channel; Unreal tests cover free flight.
No body torque or attitude helper is present in this harness.
"""
import ctypes as c
import math
import sys

from aerodynamic_flight import FlightConfig
from flight_regression import Backend, Body, Output, Pilot


def measure(backend, throttle, roll=0, pitch=0, yaw=0, velocity=(8, 0, -1)):
    context = backend.create(c.byref(FlightConfig(1200, 8, 20, 11.1, .08, 1.3)))
    assert context
    samples = []
    try:
        for i in range(6 * 240):
            body = Body(1/240, (c.c_double * 3)(*velocity), (c.c_double * 3)(0, 0, 0))
            output = Output()
            assert backend.step(context, c.byref(Pilot(throttle, roll, pitch, yaw)), c.byref(body), c.byref(output))
            assert all(math.isfinite(v) for v in output.values)
            if i >= 2 * 240:
                samples.append(list(output.values))
        mean = [sum(s[i] for s in samples) / len(samples) for i in range(11)]
        travel = [max(s[i] for s in samples) - min(s[i] for s in samples) for i in (8, 9)]
        return mean, travel
    finally:
        backend.destroy(context)


def run(path):
    backend = Backend(path)
    for throttle in (0, .72):
        neutral, _ = measure(backend, throttle)
        yaw_r, yaw_travel = measure(backend, throttle, yaw=.5)
        yaw_l, _ = measure(backend, throttle, yaw=-.5)
        roll_r, roll_travel = measure(backend, throttle, roll=.5)
        roll_l, _ = measure(backend, throttle, roll=-.5)
        pitch_up, _ = measure(backend, throttle, pitch=.5)
        pitch_down, _ = measure(backend, throttle, pitch=-.5)
        # In the +x forward,+y right,+z up layout: right bank has -Mx,
        # nose-up has -My, and nose-right has +Mz.
        assert yaw_r[5] > .01 and yaw_l[5] < -.01, 'rudder yaw authority has the wrong sign'
        assert roll_r[3] < -.005 and roll_l[3] > .005, 'aileron roll authority has the wrong sign'
        assert pitch_up[4] < neutral[4] - .05 < pitch_down[4], 'elevator cannot change pitch moment'
        assert abs(yaw_r[5] + yaw_l[5]) < 1e-8 and abs(roll_r[3] + roll_l[3]) < 1e-8, 'mirrored sticks produce asymmetric response'
        assert pitch_up[8] < neutral[8] - 2 and pitch_down[8] > neutral[8] + 2, 'pitch command does not reach wing servos'
        if throttle == 0:
            assert yaw_r[9] - yaw_r[8] > 5 and roll_r[8] - roll_r[9] > 5, 'glide steering does not move the wings'
            cancelled, _ = measure(backend, 0, roll=.325, yaw=.5)
            assert abs(cancelled[8] - cancelled[9]) < 1e-8, 'opposing glide requests must share and cancel wing travel'
        else:
            assert yaw_travel[0] > yaw_travel[1] + 15, 'rudder is missing differential stroke amplitude'
            assert min(roll_travel) > 40, 'aileron timing control pinned a loaded wing'
        print(f'RC throttle={throttle:.2f}: yawMz={yaw_r[5]:+.4f} rollMx={roll_r[3]:+.4f} '
              f'pitchMy(up,neutral,down)=({pitch_up[4]:+.4f},{neutral[4]:+.4f},{pitch_down[4]:+.4f}) '
              f'yaw_wing_travel=({yaw_travel[0]:.2f},{yaw_travel[1]:.2f})', flush=True)

    # A held glide deflection is no engine: after repositioning, zero flow
    # gives zero force/moment, and moving air cannot add body energy.
    for velocity in ((0, 0, 0), (8, 1, -1), (-5, 1, -2)):
        output, _ = measure(backend, 0, roll=.3, pitch=.2, yaw=-.4, velocity=velocity)
        if velocity == (0, 0, 0):
            assert max(abs(v) for v in output[:6]) < 1e-7, 'controls create authority without airflow'
        else:
            assert sum(a*b for a, b in zip(output[:3], velocity)) <= 0, 'held glide controls create energy'
    print('RC authority: all four channels, mirrored response, shared glide travel and airflow dependence passed')


if __name__ == '__main__':
    run(sys.argv[1])
