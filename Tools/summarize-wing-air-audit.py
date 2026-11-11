"""Summarize actual Unreal trace files, separating body and world wing motion."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / '.setup/wing-air-20261111'
number = r'[-+\d.eE]+'
def angle(a):
    return (a + 180) % 360 - 180

summary = {}
for path in sorted(root.glob('*.log')):
    rows = []
    text = path.read_text(encoding='utf-8', errors='replace')
    for line in text.splitlines():
        if 'WingAirTrace t=' not in line:
            continue
        row = {}
        for key in ('t', 'speed', 'bodyRoll', 'p', 'inertia', 'failures'):
            m = re.search(r'\b' + key + r'=('+number+')', line)
            if m:
                row[key] = float(m[1])
        for key in ('flap', 'bank', 'hinge'):
            m = re.search(key+r'=\(('+number+'),('+number+')\)', line)
            if m:
                row[key] = [float(m[1]), float(m[2])]
        rows.append(row)
    result = {'legacy_test': re.findall(r'RollResponseTest (?:PASS|FAIL)[^\r\n]*', text),
              'flight_test': re.findall(r'FlightTest (?:PASS|FAIL)[^\r\n]*', text),
              'handling_test': re.findall(r'HandlingTest (?:PASS|FAIL)[^\r\n]*', text),
              'soak_test': re.findall(r'FlightSoakTest (?:PASS|FAIL)[^\r\n]*', text),
              'chronology_test': re.findall(r'ChronologyAudit (?:PASS|FAIL)[^\r\n]*', text),
              'abi_rejections': text.count('expected 21. Rebuild'),
              'solver_rejections': text.count('FlightMathRejected:'), 'samples': len(rows)}
    if rows and max(r['t'] for r in rows) >= 3:
        ramp_match = re.search(r'-B2FAuditRamp=('+number+')', text)
        ramp = float(ramp_match[1]) if ramp_match else 8
        start = min(rows, key=lambda r: abs(r['t']-3))
        end = min(rows, key=lambda r: abs(r['t']-(3+ramp)))
        result.update(ramp_seconds=ramp,start=start, end=end,
            body_displacement_deg=angle(end['bodyRoll']-start['bodyRoll']),
            world_wing_displacement_deg=[angle(b-a) for a,b in zip(start['bank'],end['bank'])],
            shoulder_half_difference_deg=((end['flap'][1]-end['flap'][0])-(start['flap'][1]-start['flap'][0]))/2,
            peak_body_rate_rad_s=max(abs(r['p']) for r in rows if r['t']>=3),
            max_reported_failures=max(r['failures'] for r in rows))
    if rows or any(result[k] for k in ('legacy_test','flight_test','handling_test','soak_test','chronology_test')):
        summary[path.name] = result
(root/'runtime-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k: {f:v for f,v in r.items() if f not in ('start','end','legacy_test')}
                  for k,r in summary.items()}, indent=2))
