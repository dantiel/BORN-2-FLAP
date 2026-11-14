"""Phase-resolved actual-DLL force and CG moment ledger in prescribed body flow.

The body constraint is explicit: this is not a flight trajectory. Each moment
is also decomposed independently from measured section positions and forces.
"""
import argparse,configparser,ctypes as c,csv
from pathlib import Path
from b2f_harness import B2F,_Tuning,_Pilot,_Body

class Shape(c.Structure):
    _fields_=[(n,c.c_double) for n in ('fraction','chord','bend','geo_twist','twist','camber')]
class Field(c.Structure):
    _fields_=[(n,c.c_double*3) for n in ('position','normal','lift','pressure','velocity','unused','direction')]
class Loads(c.Structure):
    _fields_=[(n,c.c_double*3) for n in ('force','drag','moment')]
class Tail(c.Structure):
    _fields_=[(n,c.c_double*3) for n in ('position','normal','velocity','lift','pressure','drag','moment')]

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('dll',type=Path);ap.add_argument('preferences',type=Path);ap.add_argument('output',type=Path)
for n,v in [('speed',8),('sink',.5),('throttle',1),('roll',0),('yaw',0),('pitch',0),('cg',.016),('bias',0),('seconds',5)]:
    ap.add_argument('--'+n,type=float,default=v)
args=ap.parse_args()
cfg=configparser.ConfigParser();cfg.read(args.preferences)
def value(n):return cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning','FlapCentre' if n=='MountAngle' else n)
tc=_Tuning(*[value(n) for n,_ in _Tuning._fields_]);b=B2F(args.dll);ctx=b.new(tc)
get_shape=b._bind('b2f_math_get_wing_shape',[c.c_void_p,c.c_uint32,c.POINTER(Shape),c.POINTER(Shape)],c.c_int)
get_field=b._bind('b2f_math_get_wing_fields',[c.c_void_p,c.c_uint32,c.POINTER(Field),c.POINTER(Field)],c.c_int)
get_load=b._bind('b2f_math_get_wing_loads',[c.c_void_p,c.c_uint32,c.POINTER(Loads),c.POINTER(Loads)],c.c_int)
get_tail=b._bind('b2f_math_get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(Tail)],c.c_int)
set_mass=b._bind('b2f_math_set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double],c.c_int)
set_bias=b._bind_optional('b2f_math_set_wing_camber_bias',[c.c_void_p,c.c_double,c.c_double],c.c_int)
sl,sr=(Shape*16)(),(Shape*16)();fl,fr=(Field*16)(),(Field*16)();ll,lr=(Loads*16)(),(Loads*16)();tail=(Tail*18)()
def cross(a,z):return [a[1]*z[2]-a[2]*z[1],a[2]*z[0]-a[0]*z[2],a[0]*z[1]-a[1]*z[0]]
try:
    assert b.set_wing_model(ctx,2)==1 and b.reconfigure(ctx,c.byref(tc))==1
    assert set_mass(ctx,args.cg,1e6,1e6)==1
    if set_bias:assert set_bias(ctx,args.bias,args.bias)==1
    elif args.bias:raise RuntimeError('DLL does not implement airfoil bias')
    dt=1/480;previous=[0,0]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('w',newline='') as handle:
      writer=None
      for i in range(round(args.seconds/dt)):
        o=b.step(ctx,_Pilot(args.throttle,args.roll,args.pitch,args.yaw,1,1),
                 _Body(dt,(args.speed,0,-args.sink),(0,0,0),1000,1e6,(0,0,1)))
        assert get_shape(ctx,16,sl,sr)==16 and get_field(ctx,16,fl,fr)==16 and get_load(ctx,16,ll,lr)==16
        assert get_tail(ctx,18,tail)==18
        tf=[sum(p.lift[k]+p.pressure[k]+p.drag[k] for p in tail) for k in range(3)]
        tm=[sum(p.moment[k] for p in tail)-cross((args.cg,0,0),tf)[k] for k in range(3)]
        for side,(shapes,fields,loads,angle) in enumerate(((sl,fl,ll,o.left),(sr,fr,lr,o.right))):
            rate=(angle-previous[side])/dt;previous[side]=angle
            force=[sum(p.force[k] for p in loads) for k in range(3)]
            moment=[sum(p.moment[k] for p in loads)-cross((args.cg,0,0),force)[k] for k in range(3)]
            couples=[sum(p.moment[k]-cross(f.position,p.force)[k] for f,p in zip(fields,loads)) for k in range(3)]
            r=dict(t=i*dt,side=-1 if side==0 else 1,angle=angle,rate_deg_s=rate,
                   Fx=force[0],Fy=force[1],Fz=force[2],Mx=moment[0],My=moment[1],Mz=moment[2],
                   Mx_vertical=sum(f.position[1]*p.force[2] for f,p in zip(fields,loads)),
                   Mx_side=-sum(f.position[2]*p.force[1] for f,p in zip(fields,loads)),
                   My_thrust=sum(f.position[2]*p.force[0] for f,p in zip(fields,loads)),
                   My_vertical=-sum((f.position[0]-args.cg)*p.force[2] for f,p in zip(fields,loads)),
                   Mz_thrust=-sum(f.position[1]*p.force[0] for f,p in zip(fields,loads)),
                   Mz_side=sum((f.position[0]-args.cg)*p.force[1] for f,p in zip(fields,loads)),
                   couple_x=couples[0],couple_y=couples[1],couple_z=couples[2],
                   circulation_z=sum(f.lift[2] for f in fields),pressure_z=sum(f.pressure[2] for f in fields),
                   tail_Fx=tf[0],tail_Fz=tf[2],tail_My=tm[1],tail_Mz=tm[2],
                   tip_twist=shapes[15].twist,tip_camber=shapes[15].camber,hz=o.flap_hz)
            assert abs(r['Mx']-r['Mx_vertical']-r['Mx_side']-r['couple_x'])<1e-9
            assert abs(r['My']-r['My_thrust']-r['My_vertical']-r['couple_y'])<1e-9
            assert abs(r['Mz']-r['Mz_thrust']-r['Mz_side']-r['couple_z'])<1e-9
            if writer is None:writer=csv.DictWriter(handle,fieldnames=r);writer.writeheader()
            writer.writerow(r)
finally:b.destroy(ctx)
