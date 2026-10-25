"""Editor-side entry point; launched by Tools/generate_maps.py on Win/Mac."""
import json
import re
import gc
import sys
import traceback
from pathlib import Path
import unreal as u

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from project_guard import require_project


def inspect_world(request):
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
    if request['name'] == 'yunsha':
        tags=lambda a: {str(t) for t in a.tags}
        perches=[a for a in actors if 'YunshaPerch' in tags(a)]
        murals=[a for a in actors if 'YunshaMural' in tags(a)]
        architecture=[a for a in actors if 'YunshaArchitecture' in tags(a)]
        mountains=[a for a in actors if 'YunshaEscarpment' in tags(a)]
        assert len(perches)==5 and len(murals)==60, 'Incomplete Yunsha landmarks'
        assert len(architecture)==11 and len(mountains)==1, 'Missing imported architecture or carved mountain'
        for a in architecture:
            assert a.static_mesh_component.static_mesh.get_path_name().startswith('/Game/WorldAssets/Yunsha/'), 'Placeholder architecture returned'
        for a in perches:
            p=a.get_actor_location()
            assert p.z-u.Born2FlapYunsha.ground_height(p.x,p.y)>12000, 'Summit has a ground approach'
        assert sum('YunshaPalaceHall' in tags(a) for a in actors)==3
        assert sum('YunshaBonsai' in tags(a) for a in actors)==36
        assert sum('YunshaGarden' in tags(a) for a in actors)==4
        details.update(sky_temples=len(perches),grotto_murals=len(murals),imported_buildings=len(architecture),carved_mountains=len(mountains),seed=221022)
    return details


def generate(request):
    require_project()
    u.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/Game'], True)
    script = here/request['script']
    if script.parent != here or not script.is_file():
        raise RuntimeError('Invalid generator script: '+str(script))
    # Own one namespace instead of runpy's copied globals and function cycles.
    # Release Python wrappers while their UObjects are still alive, before
    # Unreal collects the old level or shuts down the Python interpreter.
    namespace = dict(__name__='__main__', __file__=str(script), B2F_GENERATION_BATCH=True)
    try:
        exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'), namespace)
    finally:
        namespace.clear()
        gc.collect()
    details = inspect_world(request)
    gc.collect()
    if not u.EditorLoadingAndSavingUtils.load_map(request['package']):
        raise RuntimeError('Saved map cannot be reloaded: '+request['package'])
    if inspect_world(request) != details:
        raise RuntimeError('Saved map differs after reload')
    details['reloaded'] = True
    gc.collect()
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
