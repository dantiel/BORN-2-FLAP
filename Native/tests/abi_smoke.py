"""Exercise the actual Haskell shared library from a non-Haskell host."""
import ctypes as c
import math
import sys
from pathlib import Path

class Pilot(c.Structure):
    _fields_ = [(name, c.c_double) for name in ('throttle', 'roll', 'pitch', 'yaw')]

class Body(c.Structure):
    _fields_ = [('dt', c.c_double), ('velocity', c.c_double * 3), ('rates', c.c_double * 3)]

class Output(c.Structure):
    _fields_ = [('values', c.c_double * 13), ('flags', c.c_uint32)]

path = Path(sys.argv[1])
if path.is_dir():
    matches = [p for p in path.rglob('*') if p.name in
               ('libborn2flap_math.so', 'libborn2flap_math.dylib', 'born2flap_math.dll')]
    assert len(matches) == 1, f'Expected one backend library, found {matches}'
    path = matches[0]
lib = c.CDLL(str(path.resolve()))
lib.b2f_math_abi_version.restype = c.c_uint32
lib.b2f_math_runtime_init.restype = c.c_int32
lib.b2f_math_runtime_shutdown.restype = None
create = lib.b2f_math_create_firmware_vehicle
create.argtypes = [c.c_void_p]
create.restype = c.c_void_p
destroy = lib.b2f_math_destroy_firmware_vehicle
destroy.argtypes = [c.c_void_p]
destroy.restype = None
step = lib.b2f_math_step_firmware_vehicle
step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
step.restype = c.c_int32

assert lib.b2f_math_abi_version() == 3  # safe before init
assert lib.b2f_math_runtime_init() == 1
a, b = create(None), create(None)
assert a and b
pilot = Pilot(0.7, 0, 0, 0)
body = Body(1/240, (c.c_double * 3)(5, 0, 0), (c.c_double * 3)(0, 0, 0))
out_a, out_b = Output(), Output()
for bad in (0, -1, 0.1, float('nan'), float('inf')):
    body.dt = bad
    assert step(a, c.byref(pilot), c.byref(body), c.byref(out_a)) == 0
body.dt = 1/240
pilot.throttle = float('nan')
assert step(a, c.byref(pilot), c.byref(body), c.byref(out_a)) == 0
pilot.throttle = 0.7
for _ in range(240):
    assert step(a, c.byref(pilot), c.byref(body), c.byref(out_a)) == 1
    assert step(b, c.byref(pilot), c.byref(body), c.byref(out_b)) == 1
    assert list(out_a.values) == list(out_b.values), 'rejection changed state'
    assert all(math.isfinite(v) for v in out_a.values)
destroy(a)
lib.b2f_math_runtime_shutdown()
assert step(b, c.byref(pilot), c.byref(body), c.byref(out_b)) == 1
destroy(b)
for _ in range(3):
    assert lib.b2f_math_runtime_init() == 1
    context = create(None)
    assert context
    assert step(context, c.byref(pilot), c.byref(body), c.byref(out_a)) == 1
    destroy(context)
    lib.b2f_math_runtime_shutdown()
print('Native Haskell ABI: rollback, multiple vehicles and repeated sessions passed')