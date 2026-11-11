"""Plot actual Unreal angles from the controlled before/after runtime traces."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1] / '.setup/wing-air-20261111'
sys.path.insert(0, str(root / 'plot-deps'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def trace(name):
    rows = []
    for line in (root / name).read_text(encoding='utf8', errors='replace').splitlines():
        if 'WingAirTrace t=' not in line:
            continue
        t = float(re.search(r'WingAirTrace t=([-\d.]+)', line)[1])
        if not 2.95 <= t <= 4.6:
            continue
        body = float(re.search(r'bodyRoll=([-\d.]+)', line)[1])
        l,r = map(float, re.search(r'flap=\(([-\d.]+),([-\d.]+)\)', line).groups())
        rows.append((t-3,body,(r-l)/2))
    start = min(rows, key=lambda row: abs(row[0]))
    return [(t, b-start[1], w-start[2]) for t,b,w in rows]

fig, axes = plt.subplots(1, 2, figsize=(11,4.5), constrained_layout=True)
for ax, before, after, title in zip(axes,
        ['chronology-average.log','large-before.log'],
        ['chronology-fixed.log','large-after.log'],
        ['Small stick movement','Full stick movement']):
    old,new=trace(before),trace(after)
    ax.plot([r[0] for r in new], [r[2] for r in new], '--', color='#69727c', label='Shoulder half-difference')
    ax.plot([r[0] for r in old], [r[1] for r in old], color='#c55443', label='Body: frame average')
    ax.plot([r[0] for r in new], [r[1] for r in new], color='#167e9a', label='Body: chronological loads')
    ax.axvline(1,color='#aaaaaa',lw=.8)
    ax.set(xlabel='Time since roll input began (s)', ylabel='Angular displacement (degrees)', title=title)
    ax.grid(alpha=.2)
    ax.legend(fontsize=8)
fig.suptitle('Kestrel glide, 8 m/s, 60 fps, 240 Hz physics — actual Unreal telemetry')
fig.savefig(root/'roll-coupling-comparison.png',dpi=180)
fig.savefig(root/'roll-coupling-comparison.svg')
