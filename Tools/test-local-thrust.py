"""Read-only local thrust ABI checks; no game or saved settings are changed."""
import argparse
import ctypes as c
import json
import math
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[1]
shared = runpy.run_path(str(Path(__file__).with_name("test-roll-coupling.py")))
Pilot, Body, Output = (shared[n] for n in ("Pilot", "Body", "Output"))

class LocalThrust(c.Structure):
    _fields_ = [("point_m", c.c_double * 3), ("force_n", c.c_double * 3)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, default=shared["DLL"])
    args = parser.parse_args()
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_runtime_init() == 1
    create = lib.b2f_math_create_firmware_vehicle
    create.argtypes, create.restype = [c.c_void_p], c.c_void_p
    destroy = lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes = [c.c_void_p]
    step = lib.b2f_math_step_firmware_vehicle
    step.argtypes = [c.c_void_p, c.POINTER(Pilot), c.POINTER(Body), c.POINTER(Output)]
    read = lib.b2f_math_get_local_thrust
    read.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(LocalThrust)]
    a, b = create(None), create(None)
    buf = (LocalThrust * 64)()
    c.memset(buf, 0x5A, c.sizeof(buf))
    sentinel = bytes(buf)
    pilot = Pilot(.7, 0, 0, 0, .5, 1)
    body = Body(1/240, (8, 0, 0), (0, 0, 0), 1000, .008)
    oa, ob = Output(), Output()
    peak = 0.0
    try:
        assert read(None, 64, buf) == 0
        assert read(a, 64, buf) == 0 and bytes(buf) == sentinel
        for _ in range(240):
            assert step(a, c.byref(pilot), c.byref(body), c.byref(oa)) == 1
            assert step(b, c.byref(pilot), c.byref(body), c.byref(ob)) == 1
            assert list(oa.force) == list(ob.force) and list(oa.moment) == list(ob.moment)
            before = bytes(buf)
            assert read(a, 63, buf) == 0 and bytes(buf) == before
            assert read(a, 64, buf) == 64
            snapshot = bytes(buf)
            assert read(a, 64, buf) == 64 and bytes(buf) == snapshot
            for i, load in enumerate(buf):
                assert all(math.isfinite(v) for v in (*load.point_m, *load.force_n))
                assert max(abs(v) for v in load.point_m) < 2  # metres, not UE centimetres
                peak = max(peak, math.sqrt(sum(v*v for v in load.force_n)))
                if i < 32:
                    mirror = buf[i+32]
                    for values, reflected in ((load.point_m, mirror.point_m), (load.force_n, mirror.force_n)):
                        assert abs(values[0]-reflected[0]) < 1e-8
                        assert abs(values[1]+reflected[1]) < 1e-8
                        assert abs(values[2]-reflected[2]) < 1e-8
        assert peak > .001, "fixture must produce measurable thrust"
        print(json.dumps({"status": "PASS", "loads": 64, "peak_strip_force_N": peak,
                          "read_only": True, "mirror_symmetry": True}))
    finally:
        destroy(a)
        destroy(b)

if __name__ == "__main__":
    main()
