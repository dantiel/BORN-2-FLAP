"""Headless probe: are SK_FounderThrow/SK_FounderCatch valid AnimSequences on the
mesh's skeleton? Dumps JSON to V:/BORN 2 FLAP/_probe_throw.json.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/probe_throw_anim.py -unattended -nop4 -nosplash -stdout
"""
import json
import traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_probe_throw.json')
diag = {}


def desc(asset):
    if asset is None:
        return None
    d = {'class': asset.get_class().get_name(), 'path': asset.get_path_name()}
    try:
        skel = asset.get_editor_property('skeleton')
        d['skeleton'] = str(skel.get_path_name()) if skel else None
    except Exception:
        d['skeleton'] = 'n/a'
    try:
        d['length'] = round(float(asset.get_play_length()), 3)
    except Exception:
        d['length'] = 'n/a'
    try:
        d['num_frames'] = int(asset.get_editor_property('num_frames'))
    except Exception:
        d['num_frames'] = 'n/a'
    return d


def main():
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    diag['mesh'] = str(mesh.get_path_name()) if mesh else None
    if mesh:
        try:
            skel = mesh.get_editor_property('skeleton')
            diag['mesh_skeleton'] = str(skel.get_path_name()) if skel else None
            diag['mesh_skeleton_name'] = skel.get_name() if skel else None
        except Exception as e:
            diag['mesh_skeleton_err'] = str(e)[:200]

    for name in ('Idle', 'Walk', 'Run', 'Throw', 'ThrowIn', 'Catch', 'Standing',
                 'StrafeRight', 'WalkBackward', 'Lift', 'StartWalk', 'StopWalk'):
        p = '/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder%s.SK_Founder%s' % (name, name)
        diag[name] = desc(u.load_asset(p))

    # also probe the Animations folder set
    for name in ('Idle', 'Walk', 'Run', 'Throw', 'ThrowIn', 'Catch', 'Standing',
                 'StrafeRight', 'WalkBackward', 'Lift', 'StartWalk', 'StopWalk'):
        p = '/Game/Actor/SK_Founder/Animations/AM_%s.AM_%s' % (name, name)
        diag['AM_' + name] = desc(u.load_asset(p))

    OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    print('PROBE_THROW_DONE')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        OUT.write_text(traceback.format_exc(), encoding='utf-8')
        raise
