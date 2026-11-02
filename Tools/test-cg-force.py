"""Validate the surface force used by the native CG moment conversion."""
import ctypes as c
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tail', ROOT/'Tools/test-tail-airflow.py')
tail = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tail)
f = tail.f

class Loads(c.Structure):
    _fields_ = [(n, c.c_double*3) for n in ('force', 'drag', 'moment')]

lib = c.CDLL(str(f.fixture.DLL))
assert lib.b2f_math_runtime_init() == 1
def bind(name, types, result=c.c_int):
    fn = getattr(lib, 'b2f_math_'+name)
    fn.argtypes, fn.restype = types, result
    return fn
create = bind('create_firmware_vehicle', [c.c_void_p], c.c_void_p)
destroy = bind('destroy_firmware_vehicle', [c.c_void_p], None)
step = bind('step_firmware_vehicle', [c.c_void_p,c.POINTER(f.Pilot),c.POINTER(f.Body),c.POINTER(f.Output)])
read = bind('get_external_force', [c.c_void_p,c.POINTER(c.c_double)])
loads = bind('get_wing_loads', [c.c_void_p,c.c_uint32,c.POINTER(Loads),c.POINTER(Loads)])
tails = bind('get_tail_fields', [c.c_void_p,c.c_uint32,c.POINTER(tail.Tail)])
sentinel = (c.c_double*3)(123,456,789)
assert read(None, sentinel) == 0 and list(sentinel) == [123,456,789]
for speed in (0,5,8):
    ctx = create(None)
    try:
        assert ctx and read(ctx, None) == 0
        body = f.Body(1/240, (speed,0,0), (0,0,0), 1000, 1e3, (0,0,1))
        pilot, out = f.Pilot(0,0,0,0,.5,1), f.Output()
        assert step(ctx,c.byref(pilot),c.byref(body),c.byref(out)) == 1
        force = (c.c_double*3)()
        left, right, fan = (Loads*16)(), (Loads*16)(), (tail.Tail*18)()
        assert read(ctx,force) == 1 and loads(ctx,16,left,right) == 16 and tails(ctx,18,fan) == 18
        expected = [sum(s.force[j] for s in [*left,*right]) +
                    sum(t.lift[j]+t.pressure[j]+t.drag[j] for t in fan) for j in range(3)]
        expected[0] -= .5*1.05*.0004*speed*speed
        # Both aft-body panels also have profile drag at zero incidence.
        expected[0] -= 2*.5*1.05*.0063*.034*speed*speed
        assert max(abs(force[j]-expected[j]) for j in range(3)) < 1e-10, (speed,list(force),expected)
        # A second getter is read-only and returns the same solved resultant.
        again = (c.c_double*3)()
        assert read(ctx,again) == 1 and list(force) == list(again)
        # Host correction differs, particularly in the gravity/free-fall case;
        # moving it to a CG arm would create a non-aerodynamic pitch moment.
        assert abs(force[2]-out.force[2]) > 1e-4
        print(f'CG surface force PASS speed={speed}: surfaceZ={force[2]:.6f}, hostZ={out.force[2]:.6f}')
    finally:
        destroy(ctx)
