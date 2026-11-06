"""Verify the baked glTF animation (SK_FounderWalk) imported into UE has a
correct human walk pose (head up, feet near ground) at several sample times."""
import unreal as u
import json, math
from pathlib import Path

OUT = Path('V:/BORN 2 FLAP/_baked_walk_check.json')

SEQ = '/Game/Actor/SK_Founder/SkeletalMeshes/SK_FounderWalk.SK_FounderWalk'

BONES = ['mixamorig_Hips', 'mixamorig_Spine', 'mixamorig_Neck', 'mixamorig_Head',
         'mixamorig_LeftArm', 'mixamorig_LeftForeArm', 'mixamorig_LeftHand',
         'mixamorig_RightArm', 'mixamorig_RightForeArm', 'mixamorig_RightHand',
         'mixamorig_LeftUpLeg', 'mixamorig_LeftLeg', 'mixamorig_LeftFoot',
         'mixamorig_RightUpLeg', 'mixamorig_RightLeg', 'mixamorig_RightFoot']

seq = u.load_asset(SEQ)
diag = {'seq': SEQ, 'loaded': seq is not None}

def q_deg(q):
    w = max(-1.0, min(1.0, float(q.w)))
    return round(math.degrees(2 * math.acos(abs(w))), 1)

for t in (0.0, 0.5):
    out = {}
    for b in BONES:
        try:
            tr = u.AnimationLibrary.get_bone_pose_for_time(seq, b, t, False)
            q = tr.rotation
            tx = tr.translation
            out[b] = {'rot_deg': q_deg(q), 't': [round(tx.x, 2), round(tx.y, 2), round(tx.z, 2)]}
        except Exception as e:
            out[b] = 'ERR:' + str(e)[:50]
    diag['t=%.1f' % t] = out

OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
u.log('BAKED_WALK_CHECK_DONE')
print('BAKED_WALK_CHECK_DONE')
