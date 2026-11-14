"""Probe the actual DLL for left/right symmetry under symmetric powered input."""
import argparse, configparser, ctypes as c, hashlib, json
from pathlib import Path
from b2f_harness import B2F, _Tuning, _Body, _Pilot

class Shape(c.Structure):
    _fields_=[(n,c.c_double) for n in ('fraction','chord','bend','geometric_twist','elastic_twist','camber')]
class Loads(c.Structure):
    _fields_=[(n,c.c_double*3) for n in ('force','drag','moment')]

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('dll',type=Path);ap.add_argument('preferences',type=Path)
ap.add_argument('--output',type=Path)
args=ap.parse_args()
cfg=configparser.ConfigParser();cfg.read(args.preferences)
def value(n):
    return cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning', 'FlapCentre' if n=='MountAngle' else n)
tc=_Tuning(*[value(n) for n,_ in _Tuning._fields_])
b=B2F(args.dll);ctx=b.new(tc)
get_shape=b._bind('b2f_math_get_wing_shape',[c.c_void_p,c.c_uint32,c.POINTER(Shape),c.POINTER(Shape)],c.c_int)
get_load=b._bind('b2f_math_get_wing_loads',[c.c_void_p,c.c_uint32,c.POINTER(Loads),c.POINTER(Loads)],c.c_int)
sl,sr=(Shape*16)(),(Shape*16)();ll,lr=(Loads*16)(),(Loads*16)()
try:
    assert b.set_wing_model(ctx,2)==1 and b.reconfigure(ctx,c.byref(tc))==1
    rows=[]
    for i in range(2400):
        out=b.step(ctx,_Pilot(.8,0,0,0,1,1),_Body(1/480,(8,0,-.5),(0,0,0),1000,1e6,(0,0,1)))
        assert get_shape(ctx,16,sl,sr)==16 and get_load(ctx,16,ll,lr)==16
        if i>=960:
            rows.append(dict(t=i/480,left=out.left,right=out.right,twistL=sl[15].elastic_twist,twistR=sr[15].elastic_twist,
                forceL=[sum(p.force[k] for p in ll) for k in range(3)],forceR=[sum(p.force[k] for p in lr) for k in range(3)]))
    result=dict(dll=str(args.dll.resolve()),sha256=hashlib.sha256(args.dll.read_bytes()).hexdigest(),
        max_shoulder_difference=max(abs(r['left']-r['right']) for r in rows),
        max_tip_twist_difference_rad=max(abs(r['twistL']-r['twistR']) for r in rows),
        mean_forceL=[sum(r['forceL'][k] for r in rows)/len(rows) for k in range(3)],
        mean_forceR=[sum(r['forceR'][k] for r in rows)/len(rows) for k in range(3)])
    print(json.dumps(result,indent=2))
    if args.output:args.output.write_text(json.dumps(dict(summary=result,rows=rows),indent=2)+'\n')
finally:b.destroy(ctx)
