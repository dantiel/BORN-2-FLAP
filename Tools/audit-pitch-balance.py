"""Sagittal free-flight audit using the production DLL, with a per-surface moment ledger.

This isolates pitch/heave from roll, wind, terrain and player input. It is not a
substitute for the corresponding Unreal flight test. Positive pitch is nose-up;
positive body-Y torque/rate is nose-down. CG input uses the game's aft-positive mm.
"""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ledger', ROOT/'Tools/audit-anhedral-lift.py')
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)
f = ledger.f
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--dll', type=Path, default=f.fixture.DLL)
ap.add_argument('--preferences', type=Path, default=ROOT/'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini')
ap.add_argument('--cg', nargs='+', type=float, default=[-100,0,100])
ap.add_argument('--throttle', type=float, default=.72)
ap.add_argument('--wheel', type=float, default=1)
ap.add_argument('--speed', type=float, default=5)
ap.add_argument('--seconds', type=float, default=8)
ap.add_argument('--hz', type=int, default=240)
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
lib = c.CDLL(str(args.dll.resolve()))
assert lib.b2f_math_runtime_init() == 1
assert lib.b2f_math_abi_version() in (17, 18)
def bind(name, types, result=c.c_int):
    fn = getattr(lib,'b2f_math_'+name)
    fn.argtypes, fn.restype = types, result
    return fn
create = bind('create_firmware_vehicle',[c.c_void_p],c.c_void_p)
destroy = bind('destroy_firmware_vehicle',[c.c_void_p],None)
tune = bind('reconfigure_firmware_vehicle',[c.c_void_p,c.POINTER(f.Tuning)])
step = bind('step_firmware_vehicle',[c.c_void_p,c.POINTER(f.Pilot),c.POINTER(f.Body),c.POINTER(f.Output)])
props = bind('get_mass_properties',[c.c_void_p,c.c_double,c.POINTER(c.c_double)])
mass_frame = bind('set_mass_frame',[c.c_void_p,c.c_double,c.c_double,c.c_double])
external = bind('get_external_force',[c.c_void_p,c.POINTER(c.c_double)])
loads = bind('get_wing_loads',[c.c_void_p,c.c_uint32,c.POINTER(ledger.yaw.Loads),c.POINTER(ledger.yaw.Loads)])
fields = bind('get_wing_fields',[c.c_void_p,c.c_uint32,c.POINTER(f.Field),c.POINTER(f.Field)])
tails = bind('get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(ledger.Tail)])
cfg = configparser.ConfigParser()
assert cfg.read(args.preferences)
tuning = f.Tuning(*[cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning', n,
    **({'fallback': 60} if n == 'YawScale' else {})) for n,_ in f.Tuning._fields_])
rows = []
for cgmm in args.cg:
    ctx = create(None)
    try:
        assert tune(ctx,c.byref(tuning)) == 1
        cg = -cgmm/1000
        mass = (c.c_double*7)()
        # A planar fixture must also constrain the solver's roll/yaw predictor.
        # Discarding a free-roll impulse outside the DLL otherwise lets tiny
        # left/right roundoff grow into artificial differential hinge motion.
        assert props(ctx,cg,mass) == 1 and mass_frame(ctx,cg,mass[5],1e3) == 1
        out, surface = f.Output(), (c.c_double*3)()
        left,right = (ledger.yaw.Loads*16)(),(ledger.yaw.Loads*16)()
        lf,rf = (f.Field*16)(),(f.Field*16)()
        tail = (ledger.Tail*18)()
        pilot = f.Pilot(args.throttle,0,0,0,args.wheel,1)
        theta = q = z = 0.
        vx,vz = args.speed,0.
        samples=[]
        dt=1/args.hz
        for i in range(round(args.seconds*args.hz)):
            co,si=math.cos(theta),math.sin(theta)
            # Origin velocity from COM velocity; all surface arms are origin-based.
            body=f.Body(dt,(co*vx-si*vz,0,si*vx+co*vz+q*cg),(0,q,0),1000,1e3,(-si,0,co))
            assert step(ctx,c.byref(pilot),c.byref(body),c.byref(out)) == 1, (cgmm,i)
            assert external(ctx,surface) == 1
            my=out.moment[1]+cg*surface[2]
            if i % max(1,args.hz//20) == 0:
                assert loads(ctx,16,left,right)==16 and fields(ctx,16,lf,rf)==16 and tails(ctx,18,tail)==18
                wings=[*left,*right]
                arm=sum(s.position[2]*l.force[0]-s.position[0]*l.force[2] for s,l in zip([*lf,*rf],wings))
                samples.append(dict(t=i*dt,pitch=-math.degrees(theta),q=q,vx=vx,vz=vz,z=z,
                    my=my,origin_my=out.moment[1],cg_my=cg*surface[2],
                    wing_my=sum(s.moment[1] for s in wings),wing_arm_my=arm,
                    wing_section_my=sum(s.moment[1] for s in wings)-arm,
                    tail_my=sum(s.moment[1] for s in tail),surface_fz=surface[2],
                    host_fz=out.force[2],left=out.left,right=out.right))
            vx+=(co*out.force[0]+si*out.force[2])/mass[0]*dt
            vz+=((-si*out.force[0]+co*out.force[2])/mass[0]-9.81)*dt
            q+=my/mass[5]*dt
            theta+=q*dt
            z+=vz*dt
            assert all(math.isfinite(v) for v in (vx,vz,q,theta,z))
        settled=[s for s in samples if s['t']>=min(3,args.seconds/2)]
        summary=dict(pitch_rate_rms_deg_s=math.degrees(math.sqrt(sum(s['q']**2 for s in settled)/len(settled))),
                     pitch_min_deg=min(s['pitch'] for s in settled),pitch_max_deg=max(s['pitch'] for s in settled))
        row=dict(cg_mm=cgmm,inertia_y=mass[5],summary=summary,samples=samples)
        rows.append(row)
        print(json.dumps(dict(cg_mm=cgmm,final=samples[-1],pitch_range=[min(s['pitch'] for s in samples),max(s['pitch'] for s in samples)])))
    finally:
        destroy(ctx)
args.output.write_text(json.dumps(dict(preferences=str(args.preferences),results=rows),indent=2),encoding='utf-8')
