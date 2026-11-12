"""Independent AVL 3.52 attached-flow reference. Downloaded tool stays in .setup.

Reads settled section geometry exported by born2flap-banked-turn-audit. No game
lift slope, force projection or finite-aspect-ratio law is used by AVL.
AVL is suitable for attached, modest-alpha/beta glide comparisons, not stalls.
"""
import csv, math, re, subprocess, json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.setup/knife-edge-20261112/avl'
D=math.pi/180
def v(r,names):return [r[k] for k in names.split()]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def summed(vs):return [sum(x) for x in zip(*vs)]
def rotate(a,p):return [p[0],math.cos(a)*p[1]-math.sin(a)*p[2],math.sin(a)*p[1]+math.cos(a)*p[2]]

def wing_sections(rows):
    # Use quarter-chord line and physical chord angle. Extend the first/last
    # midpoint to the strip boundary; clipping the 16-strip tip changes area.
    def station(r):
        p=v(r,'px py pz'); c=v(r,'cx cy cz')
        le=[a+.25*r['chord']*b for a,b in zip(p,c)]
        angle=math.atan2(-r['nx'],math.hypot(r['ny'],r['nz']))-r['alpha0']
        return [-le[0],le[1],le[2],r['chord'],angle/D]
    stations=[station(r) for r in sorted(rows,key=lambda r:r['py'])]
    extra=lambda a,b:[1.5*x-.5*y for x,y in zip(a,b)]
    return [extra(stations[0],stations[1])]+stations+[extra(stations[-1],stations[-2])]

def tail_sections(incidence):
    xy=[(-61.0013,5.5576),(-59.7605,0),(-61.0013,-5.5576),
        (-108.1518,-21.9963),(-109.8579,-17.1992),(-112.0293,0),
        (-109.8579,17.1992),(-108.1518,21.9963)]
    xy=[((-.38+(x+61)/(-112.0293+61)*(-.775+.38))*.55,y*1.15425*.0055) for x,y in xy]
    sections=[]
    for y in sorted(set(p[1] for p in xy)):
        xs=[]
        for a,b in zip(xy,xy[1:]+xy[:1]):
            if min(a[1],b[1])-1e-10<=y<=max(a[1],b[1])+1e-10 and a[1]!=b[1]:
                xs.append(a[0]+(b[0]-a[0])*(y-a[1])/(b[1]-a[1]))
        le=max(xs); chord=max(.00001,le-min(xs)); root=-.38*.55
        sections.append([-(root+math.cos(incidence*D)*(le-root)),y,math.sin(incidence*D)*(le-root),chord,incidence])
    return sections

def run(key,rows,tail=-8.4,nchord=6,nspan=32,with_tail=True):
    plane,common,relative,sink=key
    name=f'p{plane:g}d{common:g}r{relative:g}s{sink:g}t{tail:g}c{nchord}n{nspan}'+('' if with_tail else '-wing')
    case=OUT/name;case.mkdir(exist_ok=True)
    # Sref=Cref=Bref=1 makes force and moment coefficients multiply by Q alone.
    lines=[name,'0','0 0 0','1 1 1','0 0 0','0']
    for label,comp,stations in [('Left',1,wing_sections([r for r in rows if r['side']<0])),
                               ('Right',1,wing_sections([r for r in rows if r['side']>0]))]+([('Tail',2,tail_sections(tail))] if with_tail else []):
        lines+=['SURFACE',label,f'{nchord} 1.0 {nspan} 1.0','COMPONENT',str(comp)]
        for s in stations:lines+=['SECTION',' '.join(f'{x:.10g}' for x in s)]
    (case/'bird.avl').write_text('\n'.join(lines)+'\n')
    # Body velocity -> fluid velocity in AVL X-downstream,Y-right,Z-up axes.
    u,vv,w=rotate(-(plane+relative)*D,[8,0,-sink])
    alpha=math.atan2(-w,u)/D; beta=math.atan2(vv,math.hypot(u,w))/D
    commands=f'load bird.avl\noper\na a {alpha}\nb b {beta}\nx\nfn\nsurfaces.txt\nft\ntotal.txt\nfs\nstrips.txt\n\nquit\n'
    for f in ('surfaces.txt','total.txt','strips.txt'):
        (case/f).unlink(missing_ok=True)
    result=subprocess.run([str(OUT/'avl352.exe')],input=commands,text=True,capture_output=True,cwd=case,timeout=30)
    (case/'session.txt').write_text(result.stdout+result.stderr)
    q=.5*1.225*(64+sink*sink)
    native={}
    # FN's first table uses total reference quantities; its second uses each surface.
    for line in (case/'surfaces.txt').read_text().split('Surface area Ssurf')[0].splitlines():
        f=line.split()
        if len(f)>=11 and f[0].isdigit() and f[-1] in ('Left','Right','Tail') and f[-1] not in native:
            # n Area CL CD Cm CY Cn Cl CDi CDv name (version dependent)
            m=[-float(f[7])*q,-float(f[4])*q,float(f[6])*q]
            native[f[-1]]={'moment':m,'planeMoment':rotate(relative*D,m),'lift':float(f[2])*q,'sideforce':float(f[5])*q}
    model=summed(cross(v(r,'px py pz'),v(r,'lx ly lz')) for r in rows)
    return {'case':name,'alpha':alpha,'beta':beta,'q':q,'native':native,'modelCirculationMoment':model}

def main():
    groups={}
    for r in csv.DictReader((OUT/'sections.csv').open()):
        r={k:float(vv) for k,vv in r.items()};key=tuple(r[k] for k in ('plane','common','relative','sink'))
        groups.setdefault(key,[]).append(r)
    results=[]
    for key,rows in groups.items():
        if key[0]!=80 or key[2]!=-15 or key[3]!=2:continue
        res=run(key,rows);results.append(res);print(json.dumps(res))
    (OUT/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
