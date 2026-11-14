"""Summarize signed aerodynamic and actuator work from PoweredUpstrokeAudit."""
import argparse, csv, json, statistics
from pathlib import Path

def read(path):
    data=path.read_bytes()
    text=data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
    return [{k:float(v) for k,v in r.items()} for r in csv.DictReader(text.splitlines())]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path)
    args=ap.parse_args();results={}
    for path in sorted(args.directory.glob('powered-u*-*.csv')):
        rows=[r for r in read(path) if r['t']>=2]
        if not rows:continue
        cases=[]
        for side in (-1,1):
            wing=[r for r in rows if r['side']==side]
            if not wing:continue
            def mean(key,items=wing):return statistics.mean(r[key] for r in items) if items else 0
            result={'side':side,'samples':len(wing),
                'target_range':max(r['target_deg'] for r in wing)-min(r['target_deg'] for r in wing),
                'actual_range':max(r['angle_deg'] for r in wing)-min(r['angle_deg'] for r in wing),
                'mean_thrust':mean('force_x'),'mean_lift':sum(mean(k) for k in ('lift_z','pressure_z','drag_z')),
                'mean_motor_power':mean('motor_power_W'),'hz':mean('hz')}
            for name,select in (('up',lambda r:r['rate_rad_s']>0),('down',lambda r:r['rate_rad_s']<0)):
                phase=[r for r in wing if select(r)]
                result[name]={'fraction':len(phase)/len(wing),'lift':sum(mean(k,phase) for k in ('lift_z','pressure_z','drag_z')),
                    'circulation_lift':mean('lift_z',phase),'pressure_lift':mean('pressure_z',phase),
                    'thrust':mean('force_x',phase),'motor_power':mean('motor_power_W',phase),
                    'aero_power':statistics.mean(-r['Q_total']*r['rate_rad_s'] for r in phase) if phase else 0,
                    'aiding_fraction':sum(r['Q_total']*r['rate_rad_s']>0 for r in phase)/max(1,len(phase))}
            # This is a screening diagnostic, not a proof of bad braking:
            # a controller may legitimately brake near a speed/position limit.
            result['assisted_braking_samples']=sum(r['position_error_deg']*r['Q_total']>0 and
                r['position_error_deg']*r['motor_Nm']<0 and abs(r['position_error_deg'])>10 and
                abs(r['rate_rad_s'])<7 for r in wing)
            cases.append(result)
            print(path.stem,'L' if side<0 else 'R',
                  f"arc={result['actual_range']:.2f}/{result['target_range']:.2f} thrust={result['mean_thrust']:.4f} lift={result['mean_lift']:.4f}",
                  f"up/down lift={result['up']['lift']:.4f}/{result['down']['lift']:.4f}",
                  f"up/down motor W={result['up']['motor_power']:.3f}/{result['down']['motor_power']:.3f}",
                  'aided-brake samples',result['assisted_braking_samples'])
        results[path.stem]=cases
    (args.directory/'powered-summary.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':main()
