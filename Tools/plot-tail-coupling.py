"""Plot measured Unreal states and force partitions from the tail audit."""
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.setup/wing-air-20261111/plot-deps'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import json

OUT=ROOT/'.setup/tail-coupling-20261112'
data=json.loads((OUT/'runtime-tail.json').read_text())
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(3,3,figsize=(13,9),sharex=True,layout='constrained')
for i,d in enumerate((-25,0,25)):
    for version,color in [('before','#c75a36'),('candidate','#176bb0')]:
        rows=[r for r in data[f'yaw-d{d}-i-2.3-{version}'] if 3<=r['t']<=4.2]
        t=[r['t']-3 for r in rows]
        for j,(field,index) in enumerate([('attitude',0),('attitude',2),('momentWorld',2)]):
            values=[r[field][index]*(1000 if j==2 else 1) for r in rows]
            axes[i,j].plot(t,values,color=color,label=version.replace('candidate','corrected'))
            axes[i,j].axhline(0,color='#999999',linewidth=.6)
            axes[i,j].grid(alpha=.15)
    axes[i,0].set_ylabel(f'Wing common angle {d:+d} deg\nBody roll (deg)')
    axes[i,1].set_ylabel('Heading (deg)')
    axes[i,2].set_ylabel('Tail yaw moment (mN m)')
for ax,title in zip(axes[0],['Fuselage response','Actual Chaos heading','Sum of tail patch forces × arms']):
    ax.set_title(title)
    ax.legend()
for ax in axes[-1]: ax.set_xlabel('Seconds since roll ramp began')
fig.suptitle('Fixed tail, negative incidence: before / corrected\nUnreal 60 fps, physics 240 Hz; 8 m/s forward tow, pitch constrained; yaw, roll and lateral motion free',fontsize=13)
fig.savefig(OUT/'tail-runtime-comparison.png',dpi=160)
fig.savefig(OUT/'tail-runtime-comparison.svg')
plt.close(fig)
