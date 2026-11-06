"""Read-only probe 6: replicate ApplyUprightTransform math from ref pose."""
import json
import unreal as u

out = {}

m = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72')
sk = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton')
pose = sk.get_reference_pose()
names = [str(n) for n in pose.get_bone_names()]
rel = {n: pose.get_bone_pose(n).translation for n in names}
parent = {}
for n in names:
    p = m.get_bone_parent(u.Name(n))
    parent[n] = str(p) if p and str(p) != 'None' else None

# component-space position = accumulate relative translations root->bone
comp = {}
for n in names:
    chain = []
    cur = n
    while cur and len(chain) < 64:
        chain.append(cur)
        cur = parent.get(cur)
    acc = u.Vector(0, 0, 0)
    for k in reversed(chain):
        acc = acc + rel[k]
    comp[n] = acc

def find(sub, exclude=()):
    for n in names:
        if sub in n and not any(e in n for e in exclude):
            return n
    return None

HipsN = find('Hips', ('Head', 'Hip_'))
HeadN = find('Head', ('HeadTop',))
LArmN = find('LeftArm')
RArmN = find('RightArm')
LFootN = find('LeftFoot', ('LeftFoot_0', 'LeftFoot_06')) if False else find('LeftFoot')
RFootN = find('RightFoot')
out['found'] = dict(Hips=HipsN, Head=HeadN, LArm=LArmN, RArm=RArmN, LFoot=LFootN, RFoot=RFootN)

def v3(v):
    return [round(x, 3) for x in (v.x, v.y, v.z)]

Hips = comp[HipsN]
Head = comp[HeadN]
RightRaw = comp[RArmN] - comp[LArmN]
UpRaw = Head - Hips
Up = UpRaw.get_safe_normal(0.0001)
Right = RightRaw.get_safe_normal(0.0001)
Forward = u.Vector.cross_product(Right, Up).get_safe_normal(0.0001)

FootMid = (comp[LFootN] + comp[RFootN]) * 0.5
StandingHeight = u.Vector.dot_product(Head - FootMid, Up)
MeshScale = 180.0 / StandingHeight if StandingHeight > 1.0 else 1.0

out['comp'] = dict(Hips=v3(Hips), Head=v3(Head), LFoot=v3(comp[LFootN]),
                   RFoot=v3(comp[RFootN]), FootMid=v3(FootMid))
out['basis'] = dict(Up=v3(Up), Right=v3(Right), Forward=v3(Forward))
out['StandingHeight'] = round(StandingHeight, 3)
out['MeshScale'] = round(MeshScale, 4)
out['world_height_after_scale'] = round(StandingHeight * MeshScale, 2)

# mesh bounds for reference
b = m.get_bounds()
out['mesh_box'] = v3(b.box_extent)

s = 'PROBE_AVATAR6 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
