"""Spawn the avatar in-editor, run ApplyMesh (upright transform), and dump the
FINAL component-space state: bounds, scale, rotation, and key bone world locs.
Writes JSON to _probe_avatar_final.json for reliable capture (log truncation-proof).
"""
import json
import unreal as u

OUT = "V:/BORN 2 FLAP/_probe_avatar_final.json"
out = {}

try:
    world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
    out['map_loaded'] = bool(world)

    cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
    out['class'] = str(cls)

    if cls and world:
        sub = u.get_editor_subsystem(u.EditorActorSubsystem)
        actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
        out['spawned'] = actor is not None
        if actor:
            try:
                actor.call_method('ApplyMesh')
                out['apply_mesh'] = 'OK'
            except Exception as e:
                out['apply_mesh_error'] = str(e)

            comps = actor.get_components_by_class(u.SkeletalMeshComponent)
            out['num_skeletal_comps'] = len(comps)
            for ci, c in enumerate(comps):
                if not c.get_skeletal_mesh():
                    out['comp_%d' % ci] = 'NO_MESH'
                    continue
                sc = c.get_relative_scale3d()
                rot = c.get_relative_rotation()
                bb = c.bounds
                d = dict(
                    mesh=str(c.get_skeletal_mesh().get_name()),
                    scale=[round(v, 4) for v in (sc.x, sc.y, sc.z)],
                    rot=[round(v, 3) for v in (rot.roll, rot.pitch, rot.yaw)],
                    bounds_origin=[round(v, 2) for v in (bb.origin.x, bb.origin.y, bb.origin.z)],
                    bounds_box=[round(v, 2) for v in (bb.box_extent.x, bb.box_extent.y, bb.box_extent.z)],
                )
                # key bones world locations
                for nm in ('Hips', 'Head', 'LeftFoot', 'RightFoot', 'LeftHand', 'RightHand'):
                    try:
                        loc = c.get_bone_location(u.Name('mixamorig_' + nm), u.BoneSpaces.WORLD_SPACE)
                        d[nm] = [round(v, 2) for v in (loc.x, loc.y, loc.z)]
                    except Exception:
                        pass
                out['comp_%d' % ci] = d
            out['actor_loc'] = [round(v, 2) for v in (
                actor.get_actor_location().x,
                actor.get_actor_location().y,
                actor.get_actor_location().z)]
except Exception as e:
    out['fatal_error'] = str(e)

try:
    with open(OUT, 'w') as f:
        json.dump(out, f, indent=2)
    u.log('PROBE_AVATAR_FINAL wrote %s' % OUT)
except Exception as e:
    u.log('PROBE_AVATAR_FINAL write failed: %s' % str(e))

print('PROBE_AVATAR_FINAL ' + json.dumps(out))
u.SystemLibrary.execute_console_command(None, 'quit')
