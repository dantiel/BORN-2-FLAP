"""Read-only, servo-coupled wind-tunnel audit. This does not prove free-flight trim."""
import argparse
import configparser
import ctypes as c
import csv
import math
from pathlib import Path
import runpy
import sys

shared = runpy.run_path(str(Path(__file__).with_name('test-roll-coupling.py')))
Pilot, Body, Output, Tuning = (shared[n] for n in ('Pilot','Body','Output','Tuning'))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', type=Path, required=True)
    ap.add_argument('--preferences', type=Path, required=True)
    ap.add_argument('--speed', type=float, nargs='+', default=[0,3,5,8])
    ap.add_argument('--incidence', type=float, nargs='+', default=[-10,0,10])
    ap.add_argument('--frequency-dhz', type=float)
    ap.add_argument('--roll-rate', type=float, default=0)
    ap.add_argument('--throttle', type=float, default=.8)
    ap.add_argument('--sample-seconds', type=float, default=1)
    ap.add_argument('--side-speed', type=float, default=0)
    ap.add_argument('--servo-speed', type=float, help='Temporary no-load speed override in degrees/s')
    ap.add_argument('--tail-trim', type=float, help='Temporary stroke-centre trim override in degrees')
    ap.add_argument('--speed-modifier', type=float, default=.5)
    ap.add_argument('--hz', type=int, default=240, help='Integration rate for convergence checks')
    args = ap.parse_args()
    if args.sample_seconds <= 0: ap.error('--sample-seconds must be positive')
    if args.hz < 20: ap.error('--hz must be at least 20')
    config = configparser.ConfigParser()
    config.read(args.preferences)
    tuning = Tuning(*(config.getfloat('Flight' if n=='BodyMassKg' else 'Tuning', n)
                      for n,_ in Tuning._fields_))
    if args.frequency_dhz is not None: tuning.FlapBaseFreq=args.frequency_dhz
    if args.servo_speed is not None: tuning.ServoSpeed=args.servo_speed
    if args.tail_trim is not None: tuning.TailElevatorAngle=args.tail_trim
    lib = c.CDLL(str(args.dll.resolve()))
    assert lib.b2f_math_abi_version()==14 and lib.b2f_math_runtime_init()==1
    create=lib.b2f_math_create_firmware_vehicle
    create.argtypes,create.restype=[c.c_void_p],c.c_void_p
    destroy=lib.b2f_math_destroy_firmware_vehicle
    destroy.argtypes=[c.c_void_p]
    reconfigure=lib.b2f_math_reconfigure_firmware_vehicle
    reconfigure.argtypes=[c.c_void_p,c.POINTER(Tuning)]
    step=lib.b2f_math_step_firmware_vehicle
    step.argtypes=[c.c_void_p,c.POINTER(Pilot),c.POINTER(Body),c.POINTER(Output)]
    mass=lib.b2f_math_get_mass_properties
    mass.argtypes=[c.c_void_p,c.c_double,c.POINTER(c.c_double)]
    shape=lib.b2f_math_get_wing_shape
    shape.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_double),c.POINTER(c.c_double)]
    writer=csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(['speed_ms','incidence_deg','mean_fx_N','mean_fy_N','mean_fz_N','mean_pitch_Nm','mean_roll_Nm','weight_N','stroke_range_deg','mean_aero_power_W','mean_battery_power_W','max_bend_mm','max_aero_twist_deg','rejected_steps'])
    for speed in args.speed:
        for incidence in args.incidence:
            ctx=create(None)
            assert ctx
            try:
                tuning.VerticalMountAngle=incidence
                assert reconfigure(ctx,c.byref(tuning))==1
                props=(c.c_double*7)()
                assert mass(ctx,0,props)==1
                body=Body(1/args.hz,(speed,args.side_speed,0),(args.roll_rate,0,0),1000,props[4])
                pilot=Pilot(args.throttle,0,0,0,args.speed_modifier,1)
                out=Output()
                samples=[]; rejected=0; start_soc=1
                left_shape=(c.c_double*96)(); right_shape=(c.c_double*96)()
                max_bend=0; max_twist=0
                settle=2*args.hz
                for i in range(settle + round(args.hz*args.sample_seconds)):
                    if step(ctx,c.byref(pilot),c.byref(body),c.byref(out))!=1:
                        rejected+=1
                        continue
                    assert all(math.isfinite(v) for v in (*out.force,*out.moment,out.power,out.soc))
                    if i==settle-1: start_soc=out.soc
                    if i>=settle:
                        samples.append((out.force[0],out.force[2],out.moment[1],out.left,out.power,out.moment[0],out.force[1]))
                        assert shape(ctx,16,left_shape,right_shape)==16
                        for wing in (left_shape,right_shape):
                            max_bend=max(max_bend,max(abs(wing[j*6+2]) for j in range(16)))
                            max_twist=max(max_twist,max(abs(wing[j*6+4]) for j in range(16)))
                if not samples: raise RuntimeError('all settled steps rejected')
                mean=lambda j:sum(x[j] for x in samples)/len(samples)
                shaft_range=max(x[3] for x in samples)-min(x[3] for x in samples)
                electrical=(start_soc-out.soc)*tuning.BatteryCapacity*3600*tuning.BatteryVoltage/args.sample_seconds
                writer.writerow([speed,incidence,*[round(v,6) for v in (mean(0),mean(6),mean(1),mean(2),mean(5),props[0]*9.81,shaft_range,mean(4),electrical,max_bend*1000,math.degrees(max_twist))],rejected])
                sys.stdout.flush()
            finally: destroy(ctx)

if __name__=='__main__': main()
