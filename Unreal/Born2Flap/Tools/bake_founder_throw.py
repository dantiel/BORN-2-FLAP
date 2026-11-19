"""Bake the full-resolution Throw clip into SK_Founder.glb as a glTF animation.

Root cause of John's crippled/swapped throw: the Throw baked into the GLB is
the DEGENERATE 8-key Mixamo export (SourceAssets/Mixamo/Throw.fbx). The
follow-up "fix" import_founder_throw.py instead FBX-retargeted the full-res
Clips_Colon/Throw.fbx onto the glTF-imported Founder skeleton — which is the
exact FBX-on-glTF axis mismatch that note-5777 proved produces the
crumpled/swapped pose.

Correct fix: bake the full-res Clips_Colon/Throw.fbx (67 keys) into the GLB
using the SAME verified method as the other Founder clips (bake_mixamo_into_glb.py:
rotation(t) = rest_rotation @ euler(LclRotation), translation skipped). This
keeps mesh + skeleton + animation in one consistent glTF frame, so the GLB
re-import regenerates a correct SK_FounderThrow.

Run (pure Python, no UE needed):
    python Tools/bake_founder_throw.py
Then re-import the GLB headless so UE regenerates SK_FounderThrow:
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=".../import_founder.py" ...
"""
import importlib.util
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_PROJECT = _HERE.parent          # Unreal/Born2Flap

GLB = _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb'
SRC = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon/Throw.fbx'
ANIM_NAME = 'Throw'

# Reuse the proven parser + baker from bake_mixamo_into_glb.py
spec = importlib.util.spec_from_file_location('bake', _HERE / 'bake_mixamo_into_glb.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


def main():
    if not SRC.exists():
        raise SystemExit('missing source %s' % SRC)

    js, binb = bake.load_glb(str(GLB))
    buf = bytearray(binb)

    # Drop the degenerate Throw (and any duplicate of the same name) but keep
    # every other animation untouched.
    kept = []
    for a in js.get('animations', []):
        if a.get('name') == ANIM_NAME:
            print('removed degenerate %s (%d samplers)' % (a.get('name'), len(a.get('samplers', []))))
        else:
            kept.append(a)
    js['animations'] = kept

    anim = bake.extract_fbx_anim(str(SRC))
    obj = bake.bake_one(js, buf, anim, ANIM_NAME)
    js['animations'].append(obj)

    nkeys = 0
    if obj['samplers']:
        nkeys = js['accessors'][obj['samplers'][0]['input']]['count']
    print('baked %s: %d channels, %d keys' % (ANIM_NAME, len(obj['channels']), nkeys))

    bake.write_glb(js, bytes(buf), str(GLB))
    print('wrote %s (%d animations)' % (GLB, len(js['animations'])))


if __name__ == '__main__':
    main()