"""Audit captured glide travel and fixed-tail orientation using the captured DLL.

The mixer reconstruction is labelled as commanded geometry, not a trajectory
replay. The DLL probe holds body-relative flow fixed while changing world up;
it checks for an unintended attitude multiplier without assuming a banked
aircraft continues to see the same airflow in free flight.
"""
import argparse
import configparser
import ctypes as c
import json
import math
from pathlib import Path
from b2f_harness import B2F, _Body, _Pilot, _Tuning


class Tail(c.Structure):
    _fields_ = [(n, c.c_double * 3) for n in
                ('position', 'normal', 'velocity', 'lift', 'pressure', 'drag', 'moment')]


def norm(v): return math.sqrt(sum(x*x for x in v))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def rotate_x(a,v): return (v[0], math.cos(a)*v[1]-math.sin(a)*v[2], math.sin(a)*v[1]+math.cos(a)*v[2])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--start', type=float, default=395.8)
    ap.add_argument('--end', type=float, default=399.8)
    args = ap.parse_args()
    cfg = configparser.ConfigParser(); cfg.read(args.capture/'preferences.ini')
    # Active Tuning is what this capture loaded. Model2 is a stale preset.
    def value(n):
        return cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning',
                            'FlapCentre' if n=='MountAngle' else n)
    tuning = _Tuning(*[value(n) for n,_ in _Tuning._fields_])
    frames = json.loads((args.capture/'trace.json').read_text())
    rows = []
    for r in frames:
        if not args.start <= r['t'] <= args.end or r['pilot'][0] > .0799: continue
        _, roll, pitch, yaw = r['pilot']
        common = -(tuning.GlideAngle + 60*tuning.ElevatorScale*.01*pitch)
        old_diff = 175*tuning.AileronScale*.01*roll + 12*tuning.YawScale*.01*yaw
        old_l,old_r = [max(-80,min(80,q)) for q in (common-old_diff,common+old_diff)]
        new_diff = 80*tuning.AileronScale*.01*roll
        scale = min(1,80/max(1e-9,abs(common)+abs(new_diff)))
        rows.append(dict(t=r['t'],roll=roll,pitch=pitch,body_bank=r['pose'][0],
            requested_common=common,old_common=(old_l+old_r)/2,
            corrected_common=scale*common,corrected_diff=scale*new_diff,
            actual_common=r['common'],actual_diff=(r['shoulders'][1]-r['shoulders'][0])/2,
            tail_speed=norm(r['tailFlow']),tail_alpha=r['tailAlpha'],
            tail_moment=norm(r['tailMoment']),tail_yaw_plane=r['tailYawPlane'],
            wing_yaw_plane=r['wingYawPlane'],failures=r['failures']))
    print('Captured glide (corrected targets are counterfactual commands, not simulated motion):')
    previous=-1
    for r in rows:
        if r['t']-previous<.2: continue
        previous=r['t']
        print('t={t:.3f} roll={roll:.3f} pitch={pitch:.3f} common requested/old/actual/new='
              '{requested_common:.1f}/{old_common:.1f}/{actual_common:.1f}/{corrected_common:.1f} '
              'tail alpha={tail_alpha:.2f} |M|={tail_moment:.5f} yaw={tail_yaw_plane:.5f}'.format(**r))

    b = B2F(args.capture/'physics.dll')
    read = b._bind('b2f_math_get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(Tail)],c.c_int)
    set_mass = b._bind('b2f_math_set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double],c.c_int)
    probes=[]
    for incidence in (-3.8,3.8):
      for slip in (0,30,75):
        beta=math.radians(slip)
        velocity=(8*math.cos(beta),8*math.sin(beta),-.2)
        reference=None
        for bank in (0,15,30,60,90,180):
            tc=_Tuning.from_buffer_copy(tuning);tc.TailElevatorAngle=incidence
            ctx=b.new(tc)
            try:
                assert b.set_wing_model(ctx,2)==1 and b.reconfigure(ctx,c.byref(tc))==1
                assert set_mass(ctx,.016,1e6,1e6)==1
                angle=math.radians(bank)
                out=b.step(ctx,_Pilot(0,0,0,0,.82,1),
                           _Body(1e-8,velocity,(0,0,0),1000,1e6,rotate_x(-angle,(0,0,1))))
                assert out.flags==0
                fields=(Tail*18)();assert read(ctx,18,fields)==18
                force=[sum(p.lift[k]+p.pressure[k]+p.drag[k] for p in fields) for k in range(3)]
                shift=cross((.016,0,0),force)
                moment=[sum(p.moment[k] for p in fields)-shift[k] for k in range(3)]
                if reference is None: reference=moment
                error=norm([a-z for a,z in zip(moment,reference)])/max(norm(reference),1e-9)
                assert error<1e-5,(bank,error)
                probes.append(dict(incidence=incidence,slip=slip,bank=bank,force=force,
                    moment_body=moment,moment_world=rotate_x(angle,moment),relative_error=error))
            finally:b.destroy(ctx)
    print('Captured DLL whole-craft attitude invariance: max relative torque error',
          max(p['relative_error'] for p in probes))
    print('At constant forward flow the pitch-axis projection retains cos(bank):',
          {a:round(math.cos(math.radians(a)),4) for a in (15,30,60,90)})
    (args.capture/'travel-analysis.json').write_text(json.dumps(dict(frames=rows,orientation=probes),indent=2)+'\n')


if __name__=='__main__': main()
