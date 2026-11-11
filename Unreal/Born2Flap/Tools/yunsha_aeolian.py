"""Deterministic offline sand-bed transport; no external Python packages.
Periodic patches exchange sediment through saltation, short impact hops,
and conservative slope relaxation. An art-scale model, not calibrated CFD.
"""
import math, random, struct, zlib, json
from pathlib import Path
N=128

def png(path, pixels):
    def chunk(k,v): return struct.pack('>I',len(v))+k+v+struct.pack('>I',zlib.crc32(k+v)&0xffffffff)
    rows=b''.join(b'\0'+bytes(pixels[y*N*3:(y+1)*N*3]) for y in range(N))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',N,N,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))

def simulate(seed, hop, iterations=360):
    rng=random.Random(seed)
    h=[rng.uniform(-.03,.03) for _ in range(N*N)]
    initial=sum(h)
    for step in range(iterations):
        delta=[0.0]*len(h)
        for y in range(N):
            for x in range(N):
                q=y*N+x
                up=h[y*N+(x-1)%N]
                # Impact-driven erosion focuses on exposed upwind slopes.
                erosion=.015*max(.1,min(2.0,1+3*(h[q]-up)))
                length=max(2,int(hop+9*h[q]+rng.random()*2))
                target=y*N+(x+length)%N
                delta[q]-=erosion
                delta[target]+=erosion*.75
                delta[y*N+(x+1)%N]+=erosion*.25
                # Creep/avalanching: pairwise conservative exchange.
                for r in (y*N+(x+1)%N,((y+1)%N)*N+x):
                    slope=h[q]-h[r]
                    moved=.035*slope+.12*math.copysign(max(0,abs(slope)-.3),slope)
                    delta[q]-=moved;delta[r]+=moved
        h=[a+b for a,b in zip(h,delta)]
    error=abs(sum(h)-initial)
    assert error<1e-7, error
    # Measure the dominant along-wind wavelength instead of assuming a sine.
    powers=[]
    for k in range(3,33):
        power=0
        for y in range(0,N,8):
            a=sum(h[y*N+x]*math.cos(2*math.pi*k*x/N) for x in range(N))
            b=sum(h[y*N+x]*math.sin(2*math.pi*k*x/N) for x in range(N))
            power+=a*a+b*b
        powers.append((power,k))
    peaks=max(powers)[1]
    lo,hi=min(h),max(h)
    assert hi-lo>0.01
    return [(v-lo)/(hi-lo) for v in h],peaks,error

def generate(folder):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    report=[]
    for i,(hop,wavelength,height) in enumerate(((7,5.,.5),(13,11.,1.1),(21,20.,2.))):
        h,peaks,error=simulate(722+i,hop)
        width=peaks*wavelength
        # RG stores physical slopes within +/-2; B stores normalized height.
        pixels=[]
        for y in range(N):
            for x in range(N):
                dx=(h[y*N+(x+1)%N]-h[y*N+(x-1)%N])*height*N/(2*width)
                dy=(h[((y+1)%N)*N+x]-h[((y-1)%N)*N+x])*height*N/(2*width)
                pixels.extend([round(255*max(0,min(1,.5+dx/4))),round(255*max(0,min(1,.5+dy/4))),round(h[y*N+x]*255)])
        png(folder/('Aeolian%d.png'%i),pixels)
        report.append(dict(patch=i,wavelength_cm=wavelength,height_cm=height,width_cm=width,mass_error=error,peaks=peaks))
    (folder/'report.json').write_text(json.dumps(report,indent=2))
    return report

if __name__=='__main__':
    print(json.dumps(generate(Path(__file__).resolve().parents[1]/'Saved/Aeolian'),indent=2))
