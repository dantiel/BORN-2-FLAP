"""Headless probe: do SK_FounderThrow / SK_FounderCatch contain real bone motion?
Samples hand/arm socket world locations at several clip positions on a spawned
SK_Founder mesh. Output: V:/BORN 2 FLAP/_probe_motion.json

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/probe_motion.py" -unattended -nop4 -nosplash -stdout
"""
import json
import traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_probe_motion.json')
diag = {}


def sample(comp, bones):
    out = {}
    for b in bones:
        try:
            loc = comp.get_socket_location(b)
            out[b] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
        except Exception as e:
            out[b] = 'err:' + str(e)[:60]
    return out


def main():
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    if not mesh:
        diag['error'] = 'mesh missing'
        OUT.write_text(json.dumps(diag, indent=2))
        print('PROBE_MOTION_DONE')
        return

    sub = u.get_editor_subsystem(u.EditorActorSubsystem)
    cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
    actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 200), u.Rotator(0, 0, 0))
    comps = actor.get_components_by_class(u.SkeletalMeshComponent)
    comp = comps[0]
    comp.set_skeletal_mesh_asset(mesh)

    bones = ['mixamorig_LeftHand', 'mixamorig_RightHand', 'mixamorig_LeftArm',
             'mixamorig_RightArm', 'mixamorig_Spine2', 'mixamorig_Head']

    for name in ('Idle', 'Walk', 'Throw', 'Catch'):
        anim = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder%s.SK_Founder%s' % (name, name))
        if not anim:
            diag[name] = {'error': 'missing'}
            continue
        comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
        comp.set_animation(anim)
        comp.stop()
        times = [0.0, 0.25, 0.5, 0.75]
        series = {}
        for t in times:
            comp.set_position(t)
            series['t=%.2f' % t] = sample(comp, bones)
        # max travel of any bone between samples
        def delta(a, b):
            if isinstance(a, str) or isinstance(b, str):
                return 0.0
            return ((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2) ** 0.5
        travels = []
        for b in bones:
            best = 0.0
            for i in range(len(times)):
                for j in range(i + 1, len(times)):
                    best = max(best, delta(series['t=%.2f' % times[i]][b], series['t=%.2f' % times[j]][b]))
            travels.append((b, round(best, 1)))
        diag[name] = {'length': round(float(anim.get_play_length()), 3), 'max_travel_cm': travels}
        diag[name]['samples'] = series

    OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    print('PROBE_MOTION_DONE')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        OUT.write_text(traceback.format_exc(), encoding='utf-8')
        raise