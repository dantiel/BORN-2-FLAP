"""Check that stiff tail damping reduces an imposed angular impulse without reversal."""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fields', ROOT/'Tools/test-strip-fields.py')
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--dll', type=Path, default=f.fixture.DLL)
ap.add_argument('--explicit', action='store_true')
ap.add_argument('--output', type=Path)
args = ap.parse_args()
lib = c.CDLL(str(args.dll.resolve()))
assert lib.b2f_math_runtime_init() == 1
def bind(name, types, result=c.c_int):
    fn = getattr(lib, 'b2f_math_'+name)
    fn.argtypes, fn.restype = types, result
    return fn
create = bind('create_firmware_vehicle', [c.c_void_p], c.c_void_p)
destroy = bind('destroy_firmware_vehicle', [c.c_void_p], None)
tune = bind('reconfigure_firmware_vehicle', [c.c_void_p,c.POINTER(f.Tuning)])
step = bind('step_firmware_vehicle', [c.c_void_p,c.POINTER(f.Pilot),c.POINTER(f.Body),c.POINTER(f.Output)])
props = bind('get_mass_properties', [c.c_void_p,c.c_double,c.POINTER(c.c_double)])
mass_frame = None if args.explicit else bind('set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double])
cfg = configparser.ConfigParser()
cfg.read(ROOT/'Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini')
tuning = f.Tuning(*[cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning', n, fallback=35 if n=='AmpMaxDeg' else 0)
                   for n,_ in f.Tuning._fields_])
tuning.TailElevatorAngle = 0
tuning.GlideAngle = 0
rows = []
for hz in (120,240,480):
    for speed in (5,15):
        for axis in (1,2):
            for rate in (-20,20):
                ctx = create(None)
                try:
                    assert tune(ctx,c.byref(tuning)) == 1
                    mass = (c.c_double*7)()
                    assert props(ctx,0,mass) == 1
                    if mass_frame:
                        assert mass_frame(ctx,0,mass[5],mass[6]) == 1
                        assert mass_frame(ctx,float('nan'),mass[5],mass[6]) == 0
                    rates = [0,0,0]
                    rates[axis] = rate
                    body = f.Body(1/hz,(speed,0,0),tuple(rates),1000,mass[4],(0,0,1))
                    pilot, out = f.Pilot(0,0,0,0,.5,1), f.Output()
                    success = step(ctx,c.byref(pilot),c.byref(body),c.byref(out)) == 1
                    after = rate+out.moment[axis]/mass[4+axis]/hz if success else None
                    passed = success and math.isfinite(after) and 0 < after/rate < 1
                    rows.append(dict(hz=hz,speed=speed,axis=axis,before=rate,after=after,passed=passed))
                finally:
                    destroy(ctx)
report = dict(implicit=not args.explicit, passed=sum(r['passed'] for r in rows),total=len(rows),results=rows)
if args.output:
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
raise SystemExit(0 if report['passed']==report['total'] else 1)
