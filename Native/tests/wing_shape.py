"""Actual Haskell wing telemetry: bounds, finite deformation and read isolation."""
import ctypes as c
import math
import sys

class Section(c.Structure):
    _fields_ = [(n, c.c_double) for n in ('span', 'chord', 'bend', 'twist', 'camber')]
class Output(c.Structure):
    _fields_ = [('values', c.c_double * 15), ('flags', c.c_uint32)]

lib = c.CDLL(sys.argv[1])
lib.b2f_math_runtime_init()
create = lib.b2f_math_create_firmware_vehicle
create.argtypes, create.restype = [c.c_void_p], c.c_void_p
destroy = lib.b2f_math_destroy_firmware_vehicle
destroy.argtypes = [c.c_void_p]
read = lib.b2f_math_get_wing_shape
read.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(Section), c.POINTER(Section)]
step = lib.b2f_math_step_firmware_vehicle
step.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_void_p]
a, b = create(None), create(None)
left, right = (Section * 17)(), (Section * 17)()
left[16].span = right[16].span = 12345
assert read(None, 16, left, right) == 0
assert read(a, 15, left, right) == 0
assert read(a, 16, None, right) == 0
assert read(a, 16, left, right) == 16
assert all(left[i].camber > 0 for i in range(16))
initial = bytes(left)
initial_camber = [left[i].camber for i in range(16)]
peak_camber_change = 0
pilot = (c.c_double * 4)(.8, .4, .1, 0)
body = (c.c_double * 7)(1/240, 9, 0, 0, 0, 0, 0)
out_a, out_b = Output(), Output()
peak_bend = 0
for _ in range(480):
    assert read(a, 16, left, right) == 16
    snapshot = bytes(left)
    assert read(a, 16, left, right) == 16 and bytes(left) == snapshot
    assert step(a, pilot, body, c.byref(out_a)) == 1
    assert step(b, pilot, body, c.byref(out_b)) == 1
    assert list(out_a.values) == list(out_b.values), 'reading shape changed physics'
    assert all(math.isfinite(getattr(s, field)) for s in list(left)[:16] + list(right)[:16] for field, _ in Section._fields_)
    peak_camber_change = max(peak_camber_change, *(abs(left[i].camber - initial_camber[i]) for i in range(16)))
    peak_bend = max(peak_bend, *(abs(s.bend) for s in list(left)[:16]))
assert bytes(left) != initial and peak_bend > .001
assert peak_camber_change > .0001, 'membrane camber must respond to aerodynamic load'
assert bytes(left) != bytes(right), 'asymmetric input should deform wings independently'
assert left[16].span == right[16].span == 12345, 'buffer overrun'
destroy(a)
destroy(b)
print(f'WingShape PASS: 16 stations, peak bend {peak_bend:.4f} m, reads preserve physics')
