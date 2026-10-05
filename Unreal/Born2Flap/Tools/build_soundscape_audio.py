"""Deterministic original procedural ambience; no downloaded audio or licenses.

Writes PCM sources under Saved/GeneratedSoundscape for the Unreal importer.
"""
from pathlib import Path
import wave
import numpy as np

RATE=32000
OUT=Path(__file__).resolve().parents[1]/'Saved/GeneratedSoundscape'
OUT.mkdir(parents=True,exist_ok=True)
rng=np.random.default_rng(68051)

def noise(seconds,low,high,slope=0):
    n=int(seconds*RATE)
    f=np.fft.rfftfreq(n,1/RATE)
    gain=(1-np.exp(-(f/max(low,1))**4))*np.exp(-(f/high)**4)/np.maximum(f,30)**slope
    y=np.fft.irfft(np.fft.rfft(rng.normal(size=n))*gain,n)
    return y/max(np.std(y),1e-9)

def save(name,y,loop=False):
    if y.ndim==1: y=y[:,None]
    if not loop:
        k=min(1600,len(y)//5)
        y[:k]*=np.linspace(0,1,k)[:,None];y[-k:]*=np.linspace(1,0,k)[:,None]
    else:
        # Periodic noise and envelope; bridge the seam over 40 ms.
        k=1280
        bridge=np.linspace(y[-k],y[k],2*k)
        y[-k:]=bridge[:k];y[:k]=bridge[k:]
    y=np.tanh(y*.16)*.72
    pcm=np.round(y*32767).astype('<i2')
    with wave.open(str(OUT/(name+'.wav')),'wb') as w:
        w.setnchannels(y.shape[1]);w.setsampwidth(2);w.setframerate(RATE);w.writeframes(pcm.tobytes())
    rms=float(np.sqrt(np.mean(y*y)));peak=float(abs(y).max())
    assert np.isfinite(y).all() and .001<rms<.3 and peak<.9
    print(f'{name}: {len(y)/RATE:.1f}s rms={rms:.4f} peak={peak:.4f}')

for name,low,high,slope in [('CoastalWind',55,3200,.55),('ForestWind',150,5500,.40),('MeadowWind',220,4200,.45),('Surf',35,7200,.6),('Rain',350,10500,.05)]:
    t=np.arange(24*RATE)/RATE
    left=noise(24,low,high,slope);right=noise(24,low,high,slope)
    env=.70+.18*np.sin(2*np.pi*t/12)+.12*np.sin(2*np.pi*t/8)
    if name=='Surf':
        rumble=noise(24,30,300,.6)
        left=.65*left+.4*rumble;right=.65*right+.4*rumble
    save(name,np.column_stack([left*env,right*env]),True)

for name in ['Seagull','Raven','Songbird','Crickets']:
    t=np.arange(5*RATE)/RATE;y=np.zeros_like(t)
    if name=='Crickets':
        env=np.maximum(0,np.sin(t*2*np.pi*3))**8*(.6+.4*np.sin(t*2*np.pi*.7)**2)
        y=.9*np.sin(t*2*np.pi*4300)*env+.18*np.sin(t*2*np.pi*5700)*env
    else:
        starts=[.3,1.1,2.05] if name!='Songbird' else [.2,.58,1.4,1.75,2.1,3.1,3.45]
        for i,start in enumerate(starts):
            duration=.7 if name=='Seagull' else (.40 if name=='Raven' else .24)
            q=(t-start)/duration;mask=(q>0)&(q<1);q=np.clip(q,0,1)
            if name=='Seagull':
                freq=650+800*np.sin(np.pi*q)**.6+35*np.sin(t*2*np.pi*37)
                env=np.sin(np.pi*q)**.65
            elif name=='Raven':
                freq=420-130*q+45*np.sin(t*2*np.pi*31);env=np.sin(np.pi*q)**.45
            else:
                freq=2200+1100*np.sin(q*np.pi)+400*q*(i%2);env=np.sin(np.pi*q)
            phase=np.cumsum(freq)*2*np.pi/RATE
            voice=np.sin(phase)+.4*np.sin(phase*2)+.18*np.sin(phase*3)
            if name=='Raven': voice+=rng.normal(0,.3,len(t))
            y+=voice*env*mask
    save(name,y)

