"""Measure lateral restoring/damping derivatives using the actual Haskell backend."""
import ctypes as c
import math
import sys
from aerodynamic_flight import FlightConfig
from flight_regression import Backend, Body, Output, Pilot


def measure(backend, velocity, rates=(0, 0, 0), throttle=.72, roll=0, pitch=0):
    ctx = backend.create(c.byref(FlightConfig(1200, 8, 20, 11.1, .08, 1.3)))
    samples = []
    try:
        for i in range(240*8):
            body = Body(1/240, (c.c_double*3)(*velocity), (c.c_double*3)(*rates))
            out = Output()
            assert backend.step(ctx, c.byref(Pilot(throttle,roll,pitch,0)), c.byref(body), c.byref(out))
            assert all(math.isfinite(x) for x in out.values)
            if i >= 240*2: samples.append(list(out.values))
        return [sum(x[j] for x in samples)/len(samples) for j in range(15)]
    finally:
        backend.destroy(ctx)


def run(path):
    backend = Backend(path)
    for throttle in (0,.72):
        slip_l = measure(backend,(8,-1,-.5),throttle=throttle)
        slip_r = measure(backend,(8,1,-.5),throttle=throttle)
        rate_l = measure(backend,(8,0,-.5),rates=(-.5,0,0),throttle=throttle)
        rate_r = measure(backend,(8,0,-.5),rates=(.5,0,0),throttle=throttle)
        print(f'throttle={throttle}: sideslip Mx=({slip_l[3]:+.5f},{slip_r[3]:+.5f}), '
              f'roll-rate Mx=({rate_l[3]:+.5f},{rate_r[3]:+.5f})',flush=True)
        assert slip_l[3] < -.005 and slip_r[3] > .005, 'dihedral must oppose the bank-induced sideslip'
        assert rate_l[3] > .01 and rate_r[3] < -.01, 'roll damping must oppose roll rate'
        assert abs(slip_l[3]+slip_r[3])<1e-8, 'sideslip recovery must be symmetric'
        assert abs(rate_l[3]+rate_r[3])<1e-8, 'roll damping must be symmetric'
    print('Lateral stability: mirrored sideslip recovery and aerodynamic roll damping PASS',flush=True)


if __name__ == '__main__': run(sys.argv[1])
