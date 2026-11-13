"""Compare the actual DLL's finite-fan circulation with the standalone AVL run.

No free-flight host integrator is used here: a very short prescribed-input step
exposes the solved tail field, and the small local-flow change is checked.
"""
import argparse, ctypes as c, json, math
from pathlib import Path
from b2f_harness import B2F, _Body, _Pilot, PRESETS, tuning

class Tail(c.Structure):
    _fields_=[(n,c.c_double*3) for n in ('position','normal','velocity','lift','pressure','drag','moment')]

def dot(a,b):return sum(x*y for x,y in zip(a,b))
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll',type=Path);ap.add_argument('--output',type=Path)
    args=ap.parse_args();b=B2F(args.dll)
    read=b._bind('b2f_math_get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(Tail)],c.c_int)
    set_mass=b._bind('b2f_math_set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double],c.c_int)
    refpath=Path(__file__).resolve().parents[1]/'.setup/knife-edge-20261112/avl/fan-summary.json'
    refs={r['beta']:r for r in json.loads(refpath.read_text()) if r['nchord']==12}
    rows=[]
    for beta,ref in refs.items():
      for alpha in (-3,0,3):
        a=math.radians(alpha);bb=math.radians(beta)
        velocity=(8*math.cos(a)*math.cos(bb),8*math.cos(a)*math.sin(bb),-8*math.sin(a))
        tc=tuning(PRESETS[3],tail_elevator=0,glide_angle=0)
        ctx=b.new(tc)
        try:
            assert b.set_wing_model(ctx,2)==1
            assert b.reconfigure(ctx,c.byref(tc))==1
            assert set_mass(ctx,0,1e3,1e3)==1
            out=b.step(ctx,_Pilot(0,0,0,0,.5,1),_Body(1e-6,velocity,(0,0,0),1000,1e3,(0,0,1)))
            assert out.flags==0
            fields=(Tail*18)();assert read(ctx,18,fields)==18
            error=max(abs(p.velocity[k]-velocity[k]) for p in fields for k in range(3))
            assert error<1e-3,error
            for p in fields:
                assert abs(dot(p.lift,p.velocity))<1e-10
                assert dot(p.drag,p.velocity)<=1e-10 and dot(p.pressure,p.velocity)<=1e-10
            lift=sum(p.lift[2] for p in fields)
            cl=lift/(.5*1.05*64*ref['area']*math.cos(a))
            row=dict(beta=beta,alpha=alpha,cl=cl,avl=ref['avl']*alpha/3,flowError=error)
            rows.append(row);print(json.dumps(row))
        finally:b.destroy(ctx)
    if args.output:args.output.write_text(json.dumps(rows,indent=2)+'\n')
if __name__=='__main__':main()
