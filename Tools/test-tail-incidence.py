"""Check the installed solver's fixed-tail force/yaw signs at identical airflow."""
import argparse
import configparser
import ctypes as c
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ledger',ROOT/'Tools/audit-anhedral-lift.py')
ledger=importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)
f=ledger.f
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--dll',type=Path,default=f.fixture.DLL)
ap.add_argument('--preferences',type=Path,required=True)
ap.add_argument('--output',type=Path,required=True)
args=ap.parse_args()
lib=c.CDLL(str(args.dll.resolve()))
assert lib.b2f_math_abi_version()==18 and lib.b2f_math_runtime_init()==1
def bind(name,types,result=c.c_int):
    fn=getattr(lib,'b2f_math_'+name)
    fn.argtypes,fn.restype=types,result
    return fn
create=bind('create_firmware_vehicle',[c.c_void_p],c.c_void_p)
destroy=bind('destroy_firmware_vehicle',[c.c_void_p],None)
tune=bind('reconfigure_firmware_vehicle',[c.c_void_p,c.POINTER(f.Tuning)])
step=bind('step_firmware_vehicle',[c.c_void_p,c.POINTER(f.Pilot),c.POINTER(f.Body),c.POINTER(f.Output)])
tails=bind('get_tail_fields',[c.c_void_p,c.c_uint32,c.POINTER(ledger.Tail)])
cfg=configparser.ConfigParser()
assert cfg.read(args.preferences)
base=f.Tuning(*[cfg.getfloat('Flight' if n=='BodyMassKg' else 'Tuning',n,
    **({'fallback':60} if n=='YawScale' else {})) for n,_ in f.Tuning._fields_])
rows=[]
for throttle in (0,.72):
    for vertical_flow in (0,2):
        for incidence in (-8,0,8):
            ctx=create(None)
            try:
                tuning=f.Tuning.from_buffer_copy(base)
                tuning.TailElevatorAngle=incidence
                assert tune(ctx,c.byref(tuning))==1
                body=f.Body(1/240,(8,0,vertical_flow),(0,0,0),1000,1e3,(0,0,1))
                pilot,out=f.Pilot(throttle,0,0,0,.5,1),f.Output()
                for _ in range(10):
                    assert step(ctx,c.byref(pilot),c.byref(body),c.byref(out))==1
                patches=(ledger.Tail*18)()
                assert tails(ctx,18,patches)==18
                force=[sum(p.lift[j]+p.pressure[j]+p.drag[j] for p in patches) for j in range(3)]
                moment=[sum(p.moment[j] for p in patches) for j in range(3)]
                yaw_at_bank=math.sin(math.radians(30))*moment[1]+math.cos(math.radians(30))*moment[2]
                alpha=incidence-math.degrees(math.atan2(vertical_flow,8))
                if vertical_flow==0 and incidence==0:
                    assert abs(force[2])<1e-6 and abs(yaw_at_bank)<1e-6
                else:
                    assert alpha*force[2]>0 and alpha*yaw_at_bank>0
                rows.append(dict(throttle=throttle,incidence_deg=incidence,flow_z_m_s=vertical_flow,
                    angle_of_attack_deg=alpha,force_n=force,moment_nm=moment,yaw_at_30deg_bank_nm=yaw_at_bank))
            finally:
                destroy(ctx)
args.output.write_text(json.dumps(rows,indent=2),encoding='utf-8')
print('TailIncidence PASS: opposite incidence reverses yaw at identical level flow; local angle of attack sets the sign.')
