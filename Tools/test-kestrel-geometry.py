"""Compare the production DLL's strip chords/force arms with the rendered SVG outline."""
import argparse
import configparser
import ctypes as c
import importlib.util
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audit', ROOT/'Tools/audit-anhedral-lift.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
f = audit.f
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dll', type=Path, default=f.fixture.DLL)
parser.add_argument('--preferences', type=Path, required=True)
args = parser.parse_args()
source = (ROOT/'Unreal/Born2Flap/Source/Born2Flap/Flight/Born2FlapWingMesh.cpp').read_text(encoding='utf-8')
scale = float(re.search(r'KestrelMeshScale\s*=\s*([\d.]+)', source)[1])/100
def outline(name):
    block = re.search(r'const FVector2D '+name+r'\[13\] = \{(.*?)\};', source, re.S)[1]
    return [tuple(map(float, p)) for p in re.findall(r'FVector2D\(([\d.]+),\s*([-\d.]+)\)', block)]
le, te = outline('MembraneLE'), outline('MembraneTE')
def sample(points, fraction):
    y = 100*fraction
    for (a,x),(b,z) in zip(points,points[1:]):
        if y <= b:
            return x+(z-x)*(y-a)/(b-a)
    return points[-1][1]
lib = c.CDLL(str(args.dll.resolve()))
assert lib.b2f_math_abi_version() == 18 and lib.b2f_math_runtime_init() == 1
def bind(name, types, result=c.c_int):
    fn = getattr(lib,'b2f_math_'+name)
    fn.argtypes, fn.restype = types, result
    return fn
create = bind('create_firmware_vehicle',[c.c_void_p],c.c_void_p)
destroy = bind('destroy_firmware_vehicle',[c.c_void_p],None)
tune = bind('reconfigure_firmware_vehicle',[c.c_void_p,c.POINTER(f.Tuning)])
step = bind('step_firmware_vehicle',[c.c_void_p,c.POINTER(f.Pilot),c.POINTER(f.Body),c.POINTER(f.Output)])
shapes = bind('get_wing_shape',[c.c_void_p,c.c_uint32,c.POINTER(audit.Shape),c.POINTER(audit.Shape)])
fields = bind('get_wing_fields',[c.c_void_p,c.c_uint32,c.POINTER(f.Field),c.POINTER(f.Field)])
tails = bind('get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(audit.Tail)])
cfg = configparser.ConfigParser()
assert cfg.read(args.preferences)
tuning = f.Tuning(*[cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning', n,
    **({'fallback':60} if n=='YawScale' else {})) for n,_ in f.Tuning._fields_])
tuning.VerticalMountAngle = 0
ctx = create(None)
try:
    assert tune(ctx,c.byref(tuning)) == 1
    pilot, body, out = f.Pilot(0,0,0,0,.5,1), f.Body(1/240,(8,0,0),(0,0,0),1000,1e3,(0,0,1)), f.Output()
    assert step(ctx,c.byref(pilot),c.byref(body),c.byref(out)) == 1
    ls,rs = (audit.Shape*16)(),(audit.Shape*16)()
    lf,rf = (f.Field*16)(),(f.Field*16)()
    assert shapes(ctx,16,ls,rs)==16 and fields(ctx,16,lf,rf)==16
    errors=[]
    for shape,field in zip([*ls,*rs],[*lf,*rf]):
        leading=sample(le,shape.fraction)
        chord=(leading-sample(te,shape.fraction))*scale
        # The aerodynamic centre is a quarter chord behind the same LE used
        # by the mesh. LE sweep must not rotate this streamwise chord again.
        x=.03+leading*scale-.25*chord*math.cos(shape.geometric_twist+shape.elastic_twist)
        errors += [abs(shape.chord-chord),abs(field.position[0]-x)]
    assert max(errors)<1e-6, max(errors)
    print(f'KestrelGeometry PASS strips=32 maxErrorM={max(errors):.9g}')
    # Independent reconstruction from the current native tail vertices and
    # layout constants catches a scaled mesh with unscaled aerodynamic arms.
    native=(ROOT/'Unreal/Born2Flap/Source/Born2Flap/Flight/Born2FlapRavenCrow.cpp').read_text(encoding='utf-8')
    def constant(name):
        return float(re.search(r'constexpr float '+name+r'\s*=\s*([-\d.]+)',native)[1].rstrip('.'))
    tail_scale=constant('KestrelScale')
    root=constant('KestrelTailRootX')*tail_scale/100
    tip=constant('KestrelTailTipX')*tail_scale/100
    yscale=constant('KestrelTailYScale')*tail_scale/100
    vertices=re.search(r'const FVector KestrelTailPts\[8\] = \{(.*?)\};',native,re.S)[1]
    authored=[tuple(map(float,p)) for p in re.findall(r'FVector\(([-\d.]+),\s*([-\d.]+),\s*[-\d.]+\)',vertices)]
    outline=[(root+(x+61)/(-112.0293+61)*(tip-root),y*yscale) for x,y in authored]
    outline=outline[1:]+outline[:1]
    expected=[]
    inc=math.radians(tuning.TailElevatorAngle)
    for b,d in zip(outline[1:],outline[2:]):
        a=outline[0]
        for wa,wb,wd in [(2/3,1/6,1/6),(1/6,2/3,1/6),(1/6,1/6,2/3)]:
            x=wa*a[0]+wb*b[0]+wd*d[0]; y=wa*a[1]+wb*b[1]+wd*d[1]
            expected.append((root+math.cos(inc)*(x-root),y,math.sin(inc)*(x-root)))
    tf=(audit.Tail*18)()
    assert tails(ctx,18,tf)==18 and len(expected)==18
    tail_error=max(abs(p-q) for actual,wanted in zip(tf,expected) for p,q in zip(actual.position,wanted))
    assert tail_error<1e-6, tail_error
    print(f'TailGeometry PASS patches=18 maxErrorM={tail_error:.9g}')
finally:
    destroy(ctx)
