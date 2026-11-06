"""Read-only probe 5: ref pose units + real avatar spawn measurement."""
import json
import unreal as u

out = {}

# --- 1. Ref skeleton units via AnimPose ---
for label, path in (
    ('CLEAN', '/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton'),
    ('OLD', '/Game/Actor/Founder_rigged/SkeletalMeshes/Object_72_Skeleton'),
):
    sk = u.load_asset(path)
    if not sk:
        out[label] = 'MISSING'
        continue
    try:
        pose = sk.get_reference_pose()
        names = [str(n) for n in pose.get_bone_names()]
        out[label + '_bones'] = len(names)
        key = {}
        for name in names:
            if any(k in name for k in ('Hips', 'Spine2', 'Head', 'LeftFoot', 'RightFoot',
                                       'LeftHand', 'RightHand', 'rootJoint', 'Armature',
                                       'Object_4', 'RootNode')):
                t = pose.get_bone_pose(name).translation
                key[name] = [round(v, 4) for v in (t.x, t.y, t.z)]
        out[label + '_key'] = key
    except Exception as e:
        out[label + '_error'] = str(e)

# --- 2. Spawn the real avatar and measure ---
try:
    world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
    out['map_loaded'] = bool(world)
    cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
    out['class'] = str(cls)
    if cls and world:
        sub = u.get_editor_subsystem(u.EditorActorSubsystem)
        actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 200), u.Rotator(0, 0, 0))
        out['spawned'] = actor is not None
        if actor:
            try:
                actor.call_method('ApplyMesh')
                out['apply_mesh'] = 'OK'
            except Exception as e:
                out['apply_mesh_error'] = str(e)
            comps = actor.get_components_by_class(u.SkeletalMeshComponent)
            out['num_skeletal_comps'] = len(comps)
            for ci, c in enumerate(comps[:3]):
                sc = c.get_relative_scale3d()
                bb = c.bounds
                d = dict(
                    scale=[round(v, 4) for v in (sc.x, sc.y, sc.z)],
                    bounds_origin=[round(v, 2) for v in (bb.origin.x, bb.origin.y, bb.origin.z)],
                    bounds_box=[round(v, 2) for v in (bb.box_extent.x, bb.box_extent.y, bb.box_extent.z)],
                )
                try:
                    n = c.get_num_bones()
                    d['bones'] = n
                    for bi in range(n):
                        nm = str(c.get_bone_name(bi))
                        if 'Head' in nm or 'Hips' in nm or 'LeftFoot' in nm:
                            loc = c.get_bone_location(c.get_bone_name(bi), u.BoneSpaces.WORLD_SPACE)
                            d[nm] = [round(v, 2) for v in (loc.x, loc.y, loc.z)]
                except Exception as e:
                    d['bone_error'] = str(e)
                out['comp_%d' % ci] = d
            out['actor_loc'] = [round(v, 2) for v in (actor.get_actor_location().x,
                                                      actor.get_actor_location().y,
                                                      actor.get_actor_location().z)]
except Exception as e:
    out['spawn_error'] = str(e)

s = 'PROBE_AVATAR5 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
