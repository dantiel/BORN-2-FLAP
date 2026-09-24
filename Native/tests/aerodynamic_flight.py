"""Real DLL flight-energy regression: no altitude/speed controller or extra lift.

The body is held level here. Separate Unreal tests exercise attitude and contact.
The same hinge actuator/battery selection is used by the game's math bridge.
"""
import ctypes as c
import math
import sys

from flight_regression import Backend, Body, Output, Pilot


class FlightConfig(c.Structure):
    _fields_ = [(n, c.c_double) for n in
                ('speed', 'torque', 'backdrive', 'voltage', 'resistance', 'capacity')]


def trajectory(backend, effort, seconds=30):
    config = FlightConfig(1200, 8, 20, 11.1, .08, 1.3)
    context = backend.create(c.byref(config))
    assert context
    dt, mass, altitude = 1/240, .45, 100.0
    velocity = [8.5, 0., 2.2]
    initial_energy = mass * (9.81*altitude + .5*sum(v*v for v in velocity))
    samples = []
    try:
        for i in range(round(seconds/dt)):
            state = Body(dt, (c.c_double*3)(*velocity), (c.c_double*3)(0,0,0))
            output = Output()
            assert backend.step(context, c.byref(Pilot(effort,0,0,0)), c.byref(state), c.byref(output))
            assert all(math.isfinite(v) for v in output.values)
            for axis in range(3):
                velocity[axis] += (output.values[axis]/mass - (9.81 if axis == 2 else 0))*dt
            altitude += velocity[2]*dt
            energy = mass*(9.81*altitude + .5*sum(v*v for v in velocity))
            if effort == 0:
                assert energy <= initial_energy+.02, 'glide creates energy without flapping'
            assert math.dist(velocity, [0,0,0]) < 25, 'runaway airspeed'
            samples.append((altitude, velocity[:], energy, output.values[8], output.values[10]))
        final = samples[-1]
        recent = samples[-240*5:]
        travel = max(s[3] for s in recent)-min(s[3] for s in recent)
        print(f'effort={effort:.2f}: height_change={final[0]-100:.3f}m speed={math.dist(final[1],[0,0,0]):.3f}m/s '
              f'energy_change={final[2]-initial_energy:.3f}J late_flap_travel={travel:.2f}deg battery={final[4]:.3f}', flush=True)
        return final[0], final[2]-initial_energy, travel
    finally:
        backend.destroy(context)


def run(path):
    backend = Backend(path)
    glide, flap, power = [trajectory(backend, effort) for effort in (0,.72,1)]
    assert glide[0] < 90 and glide[1] < -30, 'glide must spend height and energy'
    assert flap[0] > 105 and flap[1] > 20, 'wing-driven flight cannot sustain itself'
    assert power[0] > flap[0]+3 and power[1] > flap[1]+15, 'extra effort must change flight performance'
    assert min(flap[2], power[2]) > 50, 'flapping fades or actuator remains pinned up'
    print('Aerodynamic flight: glide loses energy, continuous flapping sustains flight, higher effort gains more height')


if __name__ == '__main__':
    run(sys.argv[1])
