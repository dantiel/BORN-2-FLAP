"""Regression checks for DC, clipping, click transients and aliased servo energy."""
from pathlib import Path
import sys,wave
import numpy as np
root=Path(sys.argv[1])
spectra={}
for name in ['wing','servo','wind','mix','wing_slow','wing_fast','servo_light','servo_loaded']:
    with wave.open(str(root/(name+'.wav')),'rb') as w:
        sr=w.getframerate();x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,2).astype(float)/32768
    y=x[sr*2:,0];peak=float(abs(x).max());dc=float(abs(y.mean()));rms=float(np.sqrt(np.mean(y*y)))
    power=abs(np.fft.rfft(y*np.hanning(len(y))))**2;freq=np.fft.rfftfreq(len(y),1/sr)
    total=max(power.sum(),1e-20);hi=float(power[freq>12000].sum()/total)
    spectra[name]=(freq,power)
    assert .0002<rms<.25 and peak<.8 and dc<.001,(name,rms,peak,dc)
    if name.startswith('wing'):
        low=float(power[(freq>25)&(freq<100)].sum()/total)
        assert low>.9 and abs(np.diff(x[:,0])).max()<.006,(name,low)
    if name.startswith('servo'):
        band=power[(freq>1800)&(freq<8000)]
        concentration=float(band.max()/band.sum())
        assert concentration<.01 and hi<.04,(name,concentration,hi)
    print(f'{name}: PASS rms={rms:.5f} peak={peak:.5f} DC={dc:.6f} >12kHz={hi:.6f}')
def bass_peak(name):
    f,p=spectra[name];mask=(f>25)&(f<100);return f[mask][p[mask].argmax()]
slow,fast=bass_peak('wing_slow'),bass_peak('wing_fast')
assert fast>slow+8,(slow,fast)
def low_power(name):
    f,p=spectra[name];return p[(f>60)&(f<500)].sum()
ratio=low_power('servo_loaded')/low_power('servo_light')
assert ratio>8,ratio
print(f'Airflow pitch: {slow:.1f} -> {fast:.1f} Hz; loaded servo low-band power: {ratio:.1f}x light load')
