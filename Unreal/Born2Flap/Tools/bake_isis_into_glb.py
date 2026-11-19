"""Bake the 8 standard Mixamo locomotion clips into SK_Isis.glb as glTF
animations, so Isis uses the SAME glTF-frame pipeline as John (no FBX
retarget / axis-conversion mismatch, no FBX-export scale mangling).

Why: SK_Isis_ue.fbx is the broken export — root Lcl Scaling ~1e-4 collapses
the body to a 1 cm sliver (the "as big as the sky" / oversized symptom) and
the armature is scattered (Hips T=(1004,60,-1027)). The source
queen-rigged.glb -> SK_Isis.glb is CORRECT: Y-up meters, 196 cm tall,
24-bone mixamorig: skeleton. Baking the clips directly into that GLB and
importing it (UE converts meters->cm, note 2739) lands Isis at the right
size with the right clips — exactly the mechanism that works for John.

Run (pure Python, no UE):
    python Tools/bake_isis_into_glb.py
Then import headless (see repair_avatars_commandlet.py).
"""
import importlib.util
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_PROJECT = _HERE.parent          # Unreal/Born2Flap

GLB = _PROJECT / 'SourceAssets/Mixamo/Characters/SK_Isis.glb'
CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CLIPS = [
    ('Idle.fbx',         'Idle'),
    ('Walk.fbx',         'Walk'),
    ('Run.fbx',          'Run'),
    ('Throw.fbx',        'Throw'),
    ('Catch.fbx',        'Catch'),
    ('Standing.fbx',     'Standing'),
    ('StrafeRight.fbx',  'StrafeRight'),
    ('WalkBackward.fbx', 'WalkBackward'),
]

spec = importlib.util.spec_from_file_location('bake', _HERE / 'bake_mixamo_into_glb.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


def main():
    js, binb = bake.load_glb(str(GLB))
    buf = bytearray(binb)
    # Replace the queen's own 2 clips (walk/run) with the standard 8.
    js['animations'] = []
    for fbx_name, anim_name in CLIPS:
        src = CLIP_DIR / fbx_name
        if not src.exists():
            print('SKIP (missing)', src)
            continue
        anim = bake.extract_fbx_anim(str(src))
        obj = bake.bake_one(js, buf, anim, anim_name)
        js['animations'].append(obj)
        nkeys = 0
        if obj['samplers']:
            nkeys = js['accessors'][obj['samplers'][0]['input']]['count']
        print('baked %s: %d channels, %d keys' % (anim_name, len(obj['channels']), nkeys))
    bake.write_glb(js, bytes(buf), str(GLB))
    print('wrote %s (%d animations)' % (GLB, len(js['animations'])))


if __name__ == '__main__':
    main()
