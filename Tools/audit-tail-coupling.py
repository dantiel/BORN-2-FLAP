"""Read-only free-flight tail audit using the production DLL's complete host impulses.

Uses world angular velocity and the same normalized quaternion update as Chaos.
Does not add Euler gyroscopic torque a second time. No game settings are written.
"""
import argparse
import configparser
import ctypes as c
import json
import math
from pathlib import Path
from b2f_harness import B2F, _Tuning, _Pilot, _Body

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.setup/tail-coupling-20261112'

class Tail(c.Structure):
    _fields_ = [(n,c.c_double*3) for n in ('position','normal','velocity','lift','pressure','drag','moment')]

def add(a,b): return tuple(x+y for x,y in zip(a,b))
def scale(k,a): return tuple(k*x for x in a)
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def rotate(q,v):
    t=scale(2,cross(q[1:],v))
    return add(v,add(scale(q[0],t),cross(q[1:],t)))
def unrotate(q,v): return rotate((q[0],-q[1],-q[2],-q[3]),v)
def integrate(q,w,dt):
    dq=(-sum(a*b for a,b in zip(w,q[1:])),*add(scale(q[0],w),cross(w,q[1:])))
    q=tuple(a+dt*.5*b for a,b in zip(q,dq))
    n=math.sqrt(sum(a*a for a in q))
    return tuple(a/n for a in q)
def euler(q):
    w,x,y,z=q
    return tuple(map(math.degrees,(-math.atan2(2*(w*x+y*z),1-2*(x*x+y*y)),
        -math.asin(max(-1,min(1,2*(w*y-z*x)))), math.atan2(2*(w*z+x*y),1-2*(y*y+z*z)))))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll',type=Path,default=OUT/'before.dll')
    ap.add_argument('--hz',type=int,default=240)
    ap.add_argument('--seconds',type=float,default=7)
    ap.add_argument('--roll',type=float,default=.15)
    ap.add_argument('--common',type=float,nargs='+',default=[-25,0,25])
    ap.add_argument('--incidence',type=float,nargs='+',default=[-2.3])
    ap.add_argument('--output',type=Path,default=OUT/'free-tail-before.json')
    args=ap.parse_args()
    cfg=configparser.ConfigParser(); cfg.read(OUT/'preferences-before.ini')
    def value(n):
        key='FlapCentre' if n=='MountAngle' else n
        section='Flight' if n=='BodyMassKg' else ('Tuning.Model2' if cfg.has_option('Tuning.Model2',key) else 'Tuning')
        return cfg.getfloat(section,key,fallback=0)
    b=B2F(args.dll)
    read_tail=b._bind('b2f_math_get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(Tail)],c.c_int)
    set_mass=b._bind('b2f_math_set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double],c.c_int)
    result=[]
    for common in args.common:
      for incidence in args.incidence:
        tc=_Tuning(*[value(n) for n,_ in _Tuning._fields_])
        tc.GlideAngle=-common; tc.TailElevatorAngle=incidence
        ctx=b.new(tc)
        assert b.set_wing_model(ctx,2)==1
        assert b.reconfigure(ctx,c.byref(tc))==1
        mp=b.mass(ctx); inertia=tuple(mp[k] for k in ('i_xx','i_yy','i_zz'))
        assert set_mass(ctx,0,inertia[1],inertia[2])==1
        dt=1/args.hz; q=(1.,0.,0.,0.); vel=(8.,0.,0.); omega=(0.,0.,0.); pos=(0.,0.,30.)
        pilot=_Pilot(0,0,0,0,1,1); tails=(Tail*18)()
        rows=[]; failed=None
        try:
          for i in range(round(args.seconds*args.hz)):
            t=i*dt
            pilot.roll=args.roll*max(0,min(1,t-2))
            body=_Body(dt,unrotate(q,vel),unrotate(q,omega),1000,inertia[0],unrotate(q,(0,0,1)))
            o=b.step(ctx,pilot,body)
            if o.flags: failed={'t':t,'flags':o.flags}; break
            assert read_tail(ctx,18,tails)==18
            if i % max(1,args.hz//60)==0:
                f=tuple(sum(p.lift[k]+p.pressure[k]+p.drag[k] for p in tails) for k in range(3))
                m=tuple(sum(p.moment[k] for p in tails) for k in range(3))
                parts={name:rotate(q,tuple(sum(cross(p.position,getattr(p,name))[k] for p in tails)
                                          for k in range(3))) for name in ('lift','pressure','drag')}
                alpha=sum(incidence+math.degrees(math.atan2(-p.velocity[2],p.velocity[0])) for p in tails)/18
                rows.append(dict(t=t,pose=euler(q),velocity=vel,body_velocity=tuple(body.v),rates=omega,
                    flap=[o.left,o.right],tail_force_body=f,tail_force_world=rotate(q,f),
                    tail_moment_world=rotate(q,m),tail_alpha_deg=alpha,
                    tail_moment_parts=parts,
                    body_accel_world=rotate(q,tuple(o.moment[k]/inertia[k] for k in range(3))),
                    force=tuple(o.force),moment=tuple(o.moment),altitude=pos[2]))
            vel=add(vel,scale(dt,add(rotate(q,scale(1/mp['total'],o.force)),(0,0,-9.81))))
            omega=add(omega,scale(dt,rotate(q,tuple(o.moment[k]/inertia[k] for k in range(3)))))
            q=integrate(q,omega,dt); pos=add(pos,scale(dt,vel))
        finally: b.destroy(ctx)
        result.append(dict(common=common,incidence=incidence,failed=failed,rows=rows))
        for t in (2,3,4,6):
            row=min(rows,key=lambda r:abs(r['t']-t))
            print(f"common={common:g} tail={incidence:g} t={row['t']:.2f} attitude={tuple(round(a,2) for a in row['pose'])} "
                  f"alphaTail={row['tail_alpha_deg']:.3f} FzTailBody={row['tail_force_body'][2]:.6f} "
                  f"yawTailWorld={row['tail_moment_world'][2]:.6f} failure={failed}",flush=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__': main()
