"""Probe a generated Woshi map (either backend): compiled sections / static-mesh
actors, geometry statistics, collision setup and physics line traces.

Run: UnrealEditor-Cmd.exe <uproject> <map> -ExecutePythonScript=probe_pocterrain.py
Env: B2F_TERRAIN_BACKEND = meshpartition | staticmesh
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

BACKEND = os.environ.get('B2F_TERRAIN_BACKEND', 'meshpartition')
world = u.EditorLevelLibrary.get_editor_world()
assert world, 'no editor world'

metrics = {'backend': BACKEND, 'map': world.get_path_name()}


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
    # UE 5.8 renamed the python vertex-count accessor; probe the candidates.
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


if BACKEND == 'meshpartition':
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
else:
    actors = [a for a in u.EditorLevelLibrary.get_all_level_actors()
              if isinstance(a, u.StaticMeshActor)
              and a.static_mesh_component and a.static_mesh_component.static_mesh]
    total_tris = 0
    total_verts = 0
    nanite_all = True
    profiles = set()
    for actor in actors:
        sm = actor.static_mesh_component.static_mesh
        stats = mesh_stats(sm)
        total_tris += stats['tris']
        total_verts += stats['verts']
        nanite_all = nanite_all and stats['nanite']
        profiles.add(str(actor.static_mesh_component.get_collision_profile_name()))
    metrics['static_mesh_actors'] = len(actors)
    metrics['total_tris'] = total_tris
    metrics['total_verts'] = total_verts
    metrics['nanite_all'] = nanite_all
    metrics['collision_profiles'] = sorted(profiles)

# --- physics validation -----------------------------------------------------
# Mountain summit: expect a hit near the dome top (~12 m above ground).
trace((0, 0, 50000), (0, 0, -50000), 'trace_mountain_summit')
# Canyon floor: through the corridor centre down to the floor (~-11 m).
trace((0, -8000, 50000), (0, -8000, -50000), 'trace_canyon_floor')
# Arch crown: ring top at ~+46 m above ground.
trace((24000, 4000, 50000), (24000, 4000, -50000), 'trace_arch_crown')
# Grotto: the mouth is carved out — no hit in front of the cliff face,
# a hit on the cave back wall when the ray continues into the cliff.
trace((30000, -8000, 2600), (30000, -13200, 2600), 'trace_grotto_mouth')
trace((30000, -8000, 2600), (30000, -17500, 2600), 'trace_grotto_backwall')
# Flat ground far away from features.
trace((35000, 35000, 50000), (35000, 35000, -50000), 'trace_flat_ground')

# --- packaging metrics ------------------------------------------------------
content_dir = Path(__file__).resolve().parents[2] / 'Content' / ('Woshi' if BACKEND == 'meshpartition' else 'WoshiSM')
if content_dir.exists():
    metrics['content_bytes'] = sum(f.stat().st_size for f in content_dir.rglob('*') if f.is_file())
map_path = content_dir / 'Maps' / ('WOSHI.umap' if BACKEND == 'meshpartition' else 'WOSHI_SM.umap')
if map_path.exists():
    metrics['map_bytes'] = map_path.stat().st_size

metrics_dir = Path(u.Paths.project_saved_dir()) / 'WoshiMetrics'
metrics_dir.mkdir(parents=True, exist_ok=True)
out_path = metrics_dir / (BACKEND + '_probe.json')
with open(out_path, 'w') as handle:
    json.dump(metrics, handle, indent=2)
u.log('WOSHI_PROBE backend=%s sections=%s tris=%s metrics=%s'
      % (BACKEND, metrics.get('compiled_sections', metrics.get('static_mesh_actors')),
         metrics['total_tris'], out_path))
u.SystemLibrary.execute_console_command(None, 'quit')