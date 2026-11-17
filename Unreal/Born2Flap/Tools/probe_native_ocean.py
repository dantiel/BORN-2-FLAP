"""Probe the native UE5.8 Water system ocean inside the SHIOMORI editor viewport:
spawn AWaterBodyOcean + Water_Material_Ocean + wind-configured Gerstner waves,
park the camera over the bay, capture a high-res screenshot, and dump the
ocean material's parameter names (for runtime MID overrides in C++)."""
import sys
import time
import json
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
OUT = Path('V:/BORN 2 FLAP/_native_ocean_probe.png')
REPORT = Path('V:/BORN 2 FLAP/_native_ocean_probe.json')
rep = {}

E = u.EditorLevelLibrary
E.load_level('/Game/Shiomori/Maps/SHIOMORI')
print('LEVEL loaded; settling...')
time.sleep(8)

try:
    u.SystemLibrary.execute_console_command(
        E.get_editor_world(), 'r.Water.OceanFallbackDepth 250')
    rep['fallback_cvar'] = 'set 250'
except Exception as ex:
    rep['fallback_cvar'] = 'FAILED: %s' % ex

# --- Dump Water_Material_Ocean parameter names -----------------------------
lib = u.MaterialEditingLibrary
mat = u.load_asset('/Water/Materials/WaterSurface/Water_Material_Ocean')
if mat:
    rep['mat_scalars'] = [str(x) for x in lib.get_scalar_parameter_names(mat)]
    rep['mat_vectors'] = [str(x) for x in lib.get_vector_parameter_names(mat)]
    rep['mat_textures'] = [str(x) for x in lib.get_texture_parameter_names(mat)]
    rep['mat_switches'] = [str(x) for x in lib.get_static_switch_parameter_names(mat)]
else:
    rep['mat'] = 'FAILED to load Water_Material_Ocean'

ela = u.EditorAssetLibrary
rep['mpc_assets'] = [str(a) for a in ela.list_assets('/Water', recursive=True,
                                                     include_folder=False) if 'MPC' in a.upper()]
print('MAT_PARAMS', json.dumps(rep, indent=1, default=str)[:4000])

# --- Spawn the native ocean -------------------------------------------------
try:
    ocean = E.spawn_actor_from_class(u.WaterBodyOcean, u.Vector(0.0, 0.0, -50.0), u.Rotator())
    ocean.set_actor_enable_collision(False)
    body = ocean.get_water_body_component()
    if mat:
        try:
            body.set_editor_property('water_material', mat)
            rep['water_material'] = 'set via UPROPERTY'
        except Exception as ex:
            rep['water_material'] = 'set FAILED: %s' % ex

    try:
        waves = u.GerstnerWaterWaves()
        gen = u.GerstnerWaterWaveGeneratorSimple()
        gen.set_editor_property('seed', 11)
        gen.set_editor_property('num_waves', 20)
        gen.set_editor_property('min_wavelength', 320.0)
        gen.set_editor_property('max_wavelength', 4000.0)
        gen.set_editor_property('wavelength_falloff', 2.0)
        gen.set_editor_property('min_amplitude', 3.0)
        gen.set_editor_property('max_amplitude', 90.0)
        gen.set_editor_property('amplitude_falloff', 2.0)
        gen.set_editor_property('wind_angle_deg', 270.0)
        gen.set_editor_property('direction_angular_spread_deg', 20.0)
        waves.set_editor_property('gerstner_wave_generator', gen)
        try:
            waves.recompute_waves(True)
        except Exception:
            pass  # not exposed to Python; defaults stay
        try:
            body.set_editor_property('water_waves', waves)
            rep['water_waves'] = 'set via UPROPERTY'
        except Exception as ex:
            rep['water_waves'] = 'waves FAILED: %s' % ex
    except Exception as ex:
        rep['water_waves'] = 'GEN FAILED: %s' % ex
    rep['ocean'] = 'spawned'
except Exception as ex:
    rep['ocean'] = 'SPAWN FAILED: %s' % ex

print('OCEAN', rep.get('ocean'))
time.sleep(2)

# --- Park the camera over the shore looking out to sea (+Y) -----------------
try:
    sub = u.get_editor_subsystem(u.LevelEditorSubsystem)
    key = sub.get_active_viewport_config_key()
    cam = u.Vector(0.0, -16000.0, 900.0)
    rot = u.Rotator(-22.0, 90.0, 0.0)
    sub.set_level_viewport_camera_info(cam, rot, key)
    rep['camera'] = str(sub.get_level_viewport_camera_info(key))
    time.sleep(2)
    A = u.AutomationLibrary
    A.take_high_res_screenshot(1600, 900, str(OUT))
    for _ in range(40):
        time.sleep(1)
        if OUT.exists():
            break
    rep['shot'] = str(OUT.exists())
except Exception as ex:
    rep['shot'] = 'SHOT FAILED: %s' % ex

json.dump(rep, open(REPORT, 'w'), indent=1, default=str)
print('REPORT_WRITTEN', REPORT)
print('SHOT_ON_DISK', OUT.exists())