"""Summarize paired native powered knife-edge trajectories, without Euler bank shortcuts."""
import importlib.util
import json
import argparse
from pathlib import Path

spec = importlib.util.spec_from_file_location('tail_trace', Path(__file__).with_name('summarize-tail-control.py'))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('directory',nargs='?',type=Path,default=Path('.setup/knife-upstroke-20261113'))
directory=ap.parse_args().directory
summary = []
for path in sorted(directory.glob('powered-U*.log')):
    rows = trace.parse(path)
    if not rows:continue
    released = [r for r in rows if r['t'] >= 3.05]
    samples = []
    for t in (3.1, 3.5, 4, 5, 6, 7):
        r = min(rows, key=lambda r: abs(r['t']-t))
        samples.append({k:r[k] for k in ('t','altitude','pose','wingNormalWorld','velocity','rates','pilot','shoulders','motor','tailMoment','tailFlow','tailAlpha','failures')})
    contact = next((r['t'] for r in released if r['altitude'] < .12), None)
    entry = dict(name=path.stem, contact=contact, failures=max(r['failures'] for r in rows), samples=samples)
    summary.append(entry)
    print(path.stem, 'contact=', contact, 'failures=', entry['failures'])
    for r in samples:
        print('  t={:.2f} h={:.2f} nz={:.3f} pose={} rates={} pilot={}'.format(r['t'],r['altitude'],r['wingNormalWorld'][2],
            tuple(round(x,1) for x in r['pose']),tuple(round(x,2) for x in r['rates']),tuple(round(x,2) for x in r['pilot'])))
(directory/'native-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
