"""Read-only probe 7: replicate ApplyUprightTransform math in pure Python."""
import json
import math
import unreal as u

out = {}

m = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72')
sk = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton')
pose = sk.get_reference_pose()
names = [str(n) for n in pose.get_bone_names()]
rel = {}
for n in names:
    t = pose.get_bone_pose(n).translation
    rel[n] = [t.x, t.y, t.z]
parent = {}
for n in names:
    p = m.get_bone_parent(u.Name(n))
    parent[n] = str(p) if p and str(p) != 'None' else None


def comp_pos(n):
    chain = []
    cur = n
    while cur and len(chain) < 64:
        chain.append(cur)
        cur = parent.get(cur)
    acc = [0.0, 0.0, 0.0]
    for k in reversed(chain):
        r = rel[k]
        acc = [acc[0] + r[0], acc[1] + r[1], acc[2] + r[2]]
    return acc


def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def mul(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    l = math.sqrt(dot(a, a))
    return mul(a, 1.0 / l) if l > 1e-9 else [0.0, 0.0, 0.0]


def find(sub, exclude=()):
    for n in names:
        if sub in n and not any(e in n for e in exclude):
            return n
    return None


def r3(v):
    return [round(x, 3) for x in v]


HipsN = find('Hips', ('Head',))
HeadN = find('Head', ('HeadTop',))
LArmN = find('LeftArm')
RArmN = find('RightArm')
LFootN = find('LeftFoot')
RFootN = find('RightFoot')
out['found'] = dict(Hips=HipsN, Head=HeadN, LArm=LArmN, RArm=RArmN, LFoot=LFootN, RFoot=RFootN)

Hips = comp_pos(HipsN)
Head = comp_pos(HeadN)
RightRaw = sub(comp_pos(RArmN), comp_pos(LArmN))
UpRaw = sub(Head, Hips)
Up = norm(UpRaw)
Right = norm(RightRaw)
Forward = cross(Right, Up)

FootMid = mul(add(comp_pos(LFootN), comp_pos(RFootN)), 0.5)
StandingHeight = dot(sub(Head, FootMid), Up)
MeshScale = 180.0 / StandingHeight if StandingHeight > 1.0 else 1.0

out['comp'] = dict(Hips=r3(Hips), Head=r3(Head), LFoot=r3(comp_pos(LFootN)),
                   RFoot=r3(comp_pos(RFootN)), FootMid=r3(FootMid))
out['basis'] = dict(Up=r3(Up), Right=r3(Right), Forward=r3(Forward))
out['StandingHeight'] = round(StandingHeight, 3)
out['MeshScale'] = round(MeshScale, 4)
out['world_height_after_scale'] = round(StandingHeight * MeshScale, 2)

b = m.get_bounds()
out['mesh_box'] = r3([b.box_extent.x, b.box_extent.y, b.box_extent.z])
out['bone_count'] = len(names)

s = 'PROBE_AVATAR7 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
