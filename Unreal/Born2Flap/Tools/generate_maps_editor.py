"""Editor-side entry point; launched by Tools/generate_maps.py on Win/Mac."""
import json
import re
import runpy
import sys
import traceback
from pathlib import Path
import unreal as u

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from project_guard import require_project


def generate(request):
    require_project()
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(['/Game'], True)
    script = here/request['script']
    if script.parent != here or not script.is_file():
        raise RuntimeError('Invalid generator script: '+str(script))
    # Each entry constructs a new map. Never depend on the level left open by
    # EditorStartupMap, a previous editor session, or a targeted repair script.
    runpy.run_path(str(script), run_name='__main__', init_globals={'B2F_GENERATION_BATCH': True})
    actors = list(u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors())
    if not actors:
        raise RuntimeError('Generator produced an empty map')
    broken = [a.get_actor_label() for a in actors
              if isinstance(a, u.StaticMeshActor) and not a.static_mesh_component.static_mesh]
    if broken:
        raise RuntimeError('Actors without meshes: '+', '.join(broken[:20]))
    details = dict(actor_count=len(actors))
    if request['name'] == 'shiomori':
        gates = [a for a in actors if 'CoastalTorii' in [str(t) for t in a.tags]]
        if len(gates) != 1:
            raise RuntimeError('Expected exactly one campus torii')
        gate = gates[0]
        b = gate.static_mesh_component.static_mesh.get_bounds()
        height = 2*b.box_extent.z*gate.get_actor_scale3d().z
        if abs(height-1000) > .1:
            raise RuntimeError('Unexpected torii height: '+str(height))
        details['torii_height_cm'] = height
        details['campus_actors'] = sum('ShiomoriCampusV2' in [str(t) for t in a.tags] for a in actors)
        details['volcanic_actors'] = sum('ShiomoriVolcanic' in [str(t) for t in a.tags] for a in actors)
    # Validate the serialized level by reloading it, not only its in-memory actors.
    world = u.EditorLoadingAndSavingUtils.load_map(request['package'])
    if not world:
        raise RuntimeError('Saved map cannot be reloaded: '+request['package'])
    reloaded = list(u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors())
    if len(reloaded) != len(actors):
        raise RuntimeError('Saved map actor count differs after reload')
    details['reloaded'] = True
    return details


match = re.search(r'"-B2FGenerationRequest=([^"]+)"|-B2FGenerationRequest="([^"]+)"|-B2FGenerationRequest=([^\s]+)',
                  u.SystemLibrary.get_command_line())
if not match:
    raise RuntimeError('Run this script through Tools/generate_maps.py')
request = json.loads(Path(next(v for v in match.groups() if v is not None)).read_text(encoding='utf-8-sig'))
result = dict(token=request['token'], success=False, name=request['name'])
try:
    result.update(generate(request))
    result['success'] = True
    u.log('B2F_GENERATED_MAP_VERIFIED '+json.dumps(result))
except Exception:
    result['error'] = traceback.format_exc()
    u.log_error(result['error'])
finally:
    Path(request['report']).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    u.SystemLibrary.execute_console_command(None, 'quit')
