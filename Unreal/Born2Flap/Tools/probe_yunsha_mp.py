"""Probe the rebuilt Yunsha Mesh Terrain map: descriptors, compiled sections,
geometry statistics, collision setup and physics line traces at Yunsha points.

Run: UnrealEditor-Cmd.exe <uproject> /Game/Yunsha/Maps/YUNSHA -ExecutePythonScript=probe_yunsha_mp.py
"""
import json
import os
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()

world = u.EditorLevelLibrary.get_editor_world()
assert world, 'no editor world'

metrics = {'backend': 'meshpartition', 'map': world.get_path_name()}


def trace(start, end, label):
    """Line trace via TraceTypeQuery1 (the Visibility-mapped query channel)."""
    hit = u.SystemLibrary.line_trace_single(
        world, u.Vector(*start), u.Vector(*end),
        u.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
        u.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = next((v for v in hit if isinstance(v, u.HitResult)), None)
    if hit is not None:
        parts = hit.to_tuple()
        if parts[0]:
            actor = getattr(hit, 'hit_actor', None)
            metrics[label] = {
                'hit': True,
                'location': [round(parts[5].x, 1), round(parts[5].y, 1),
                             round(parts[5].z, 1)],
                'actor': actor.get_name() if actor else '',
                'distance': round(float(getattr(hit, 'distance', 0.0)), 1),
            }
            return
    metrics[label] = {'hit': False}


def _mesh_verts(static_mesh):
    for name in ('get_num_verts', 'get_num_vertices', 'get_vertex_count'):
        fn = getattr(static_mesh, name, None)
        if fn is None:
            continue
        try:
            return int(fn(0))
        except TypeError:
            try:
                return int(fn())
            except Exception:
                pass
        except Exception:
            pass
    return 0


def mesh_stats(static_mesh):
    return {
        'tris': static_mesh.get_num_triangles(0),
        'verts': _mesh_verts(static_mesh),
        'nanite': bool(static_mesh.get_editor_property('nanite_settings').enabled),
        'materials': static_mesh.get_num_sections(0),
        'collision_flag': str(static_mesh.get_editor_property('body_setup')
                              .get_editor_property('collision_trace_flag')),
    }


# Native sampler sanity: the C++ heightfield is the gameplay source of truth.
u.log('YUNSHA_PROBE_GROUND ground(0,0)=%.1f ground(13000,-49000)=%.1f spring=%s'
      % (u.Born2FlapYunsha.ground_height(0, 0),
         u.Born2FlapYunsha.ground_height(13000, -49000),
         u.Born2FlapYunsha.spring_contains(13000, -49000)))

# Mesh Terrain compiled sections hide in per-build-variant data layers with
# Unloaded runtime state; activate them or the loader adapter skips them.
u.log('PROBE_DATALAYERS_ACTIVATED=%d' % u.Born2FlapMeshPartition.activate_all_data_layers(world))
# Force-load all streaming cells so the compiled sections exist in memory.
assert u.Born2FlapMeshPartition.load_world_partition_region(
    world, u.Vector(150000, 150000, 150000)), 'WP region load failed'
for line in u.Born2FlapMeshPartition.dump_data_layer_states(world):
    u.log('PROBE_DL ' + line)
for line in u.Born2FlapMeshPartition.dump_actor_desc_details(world):
    u.log('PROBE_DESC_DETAIL ' + line)
u.log('PROBE_CS_PINLOADED=%d' % u.Born2FlapMeshPartition.load_all_compiled_sections(world))
all_actors = u.EditorLevelLibrary.get_all_level_actors()
u.log('PROBE_ACTOR_COUNT=%d' % len(all_actors))
for actor in list(all_actors)[:40]:
    u.log('PROBE_ACTOR ' + actor.get_class().get_name() + ' ' + actor.get_name())
u.log('PROBE_TA_COUNT=%d' % u.Born2FlapMeshPartition.count_compiled_sections(world))
for line in u.Born2FlapMeshPartition.debug_list_actors(world):
    u.log('PROBE_TA ' + line)
u.log('PROBE_DESC_COUNT=%d' % u.Born2FlapMeshPartition.count_actor_descs(world))
metrics['actor_descs'] = u.Born2FlapMeshPartition.count_actor_descs(world)
for line in u.Born2FlapMeshPartition.dump_actor_desc_state(world):
    u.log('PROBE_DESC ' + line)
# --- winding audit: compiled-section render meshes must face UP --------------
# The builder recomputes section normals from the source winding; DOWN-facing
# sections are backface-culled and invisible from above while their collision
# still works (the exact failure mode of a flipped base sheet).
for line in u.Born2FlapMeshPartition.audit_section_winding(world, [
        u.Vector(200000, 300000, 0), u.Vector(13000, -49000, 0),
        u.Vector(148000, -11000, 0), u.Vector(17500, -52000, 0),
        u.Vector(-80000, -30000, 0)]):
    u.log('YUNSHA_WINDING ' + line)

# --- modifier actors ---------------------------------------------------------
# Post-strip these are 0 by design (finalize_mp_authors removes the authoring
# actors); their baked result lives in the compiled sections. Reported here so
# an unexpected regeneration state cannot hide.
mod_base = mod_bool = mod_remesh = 0
for actor in all_actors:
    label = actor.get_actor_label() if hasattr(actor, 'get_actor_label') else ''
    if 'MeshPartitionBase' in label:
        mod_base += 1
    elif 'BooleanCarve' in label or 'BooleanUnion' in label:
        mod_bool += 1
    elif 'MeshPartitionRemesh' in label:
        mod_remesh += 1
metrics['modifiers'] = {'base': mod_base, 'boolean': mod_bool, 'remesh': mod_remesh}
u.log('YUNSHA_MODIFIERS base=%d boolean=%d remesh=%d (0 expected after authoring-actor strip)'
      % (mod_base, mod_bool, mod_remesh))

# The MeshPartition source meshes must SURVIVE the generation cleanup: the
# build commandlet re-resolves the boolean tool mesh from disk.
for asset in ('/Game/Yunsha/Meshes/SM_Dunes',
              '/Game/Yunsha/Meshes/SM_Tool_ExcavationPit'):
    u.log('YUNSHA_SOURCE_MESH %s exists=%d'
          % (asset, 1 if u.EditorAssetLibrary.does_asset_exist(asset) else 0))

sections = [a for a in all_actors
            if 'CompiledSection' in a.get_class().get_name()]
metrics['compiled_sections'] = len(sections)
metrics['section_meshes'] = []
total_tris = 0
total_verts = 0
nanite_all = True
collision_components = 0
collision_profiles = set()
for section in sections:
    static_mesh = None
    for component in section.get_components_by_class(u.StaticMeshComponent):
        if component.static_mesh:
            static_mesh = component.static_mesh
            break
    entry = {'actor': section.get_name()}
    if static_mesh:
        entry.update(mesh_stats(static_mesh))
        total_tris += entry['tris']
        total_verts += entry['verts']
        nanite_all = nanite_all and entry['nanite']
    for component in section.get_components_by_class(u.MeshPartitionCollisionComponent):
        collision_components += 1
        collision_profiles.add(str(component.get_collision_profile_name()))
    metrics['section_meshes'].append(entry)
metrics['total_tris'] = total_tris
metrics['total_verts'] = total_verts
metrics['nanite_all'] = nanite_all
metrics['collision_components'] = collision_components
metrics['collision_profiles'] = sorted(collision_profiles)

# --- physics validation -----------------------------------------------------
# Deep desert: expect a hit on the compiled dune surface.
trace((200000, 300000, 30000), (200000, 300000, -40000), 'trace_deep_desert')
# Lake basin: the crescent floor lies below the water plane (~ground<0).
trace((13000, -49000, 30000), (13000, -49000, -40000), 'trace_lake_basin')
# Excavation pit: MP base is carved out; the archive terrain supplies the hit.
trace((148000, -11000, 30000), (148000, -11000, -40000), 'trace_excavation_pit')
# Escarpment wall: continuous mountain with grotto mouths.
trace((-80000, -30000, 30000), (-80000, -30000, -40000), 'trace_escarpment')
# Oasis bedrock ring: near the spring shore.
trace((17500, -52000, 30000), (17500, -52000, -40000), 'trace_oasis_shore')

# --- packaging metrics ------------------------------------------------------
content_dir = Path(__file__).resolve().parents[2] / 'Content' / 'Yunsha'
if content_dir.exists():
    metrics['content_bytes'] = sum(f.stat().st_size for f in content_dir.rglob('*') if f.is_file())
map_path = content_dir / 'Maps' / 'YUNSHA.umap'
if map_path.exists():
    metrics['map_bytes'] = map_path.stat().st_size
ea_dir = Path(__file__).resolve().parents[2] / 'Content' / '__ExternalActors__' / 'Yunsha'
if ea_dir.exists():
    metrics['external_actor_files'] = sum(1 for f in ea_dir.rglob('*') if f.is_file())
    metrics['external_actor_bytes'] = sum(f.stat().st_size for f in ea_dir.rglob('*') if f.is_file())
    for f in sorted(ea_dir.rglob('*')):
        if f.is_file():
            u.log('YUNSHA_EA %s %d' % (str(f.relative_to(ea_dir)), f.stat().st_size))

metrics_dir = Path(u.Paths.project_saved_dir()) / 'YunshaMetrics'
metrics_dir.mkdir(parents=True, exist_ok=True)
out_path = metrics_dir / 'meshpartition_probe.json'
with open(out_path, 'w') as handle:
    json.dump(metrics, handle, indent=2)
u.log('YUNSHA_PROBE_READY sections=%s descs=%s tris=%s metrics=%s'
      % (metrics.get('compiled_sections'), metrics.get('actor_descs'),
         metrics['total_tris'], out_path))
u.SystemLibrary.execute_console_command(None, 'quit')