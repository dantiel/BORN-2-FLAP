"""Check captured Unreal output, not only the procedural source assets."""
from pathlib import Path
import sys
import wave
import numpy as np

root=Path(__file__).resolve().parents[1]/'Saved/BouncedWavFiles'
paths=[Path(p) for p in sys.argv[1:]] if len(sys.argv)>1 else sorted(root.glob('EXPERIENCE_*.wav'))
assert paths, 'No Unreal soundscape recordings found'
for path in paths:
    with wave.open(str(path),'rb') as w:
        assert w.getsampwidth()==2
        duration=w.getnframes()/w.getframerate()
        data=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)/32768
    peak=float(abs(data).max());rms=float(np.sqrt(np.mean(data*data)))
    assert duration>10 and .0001<rms<.5 and peak<.999, (path,duration,rms,peak)
    print(f'{path.name}: PASS duration={duration:.2f}s rms={rms:.5f} peak={peak:.5f}')
