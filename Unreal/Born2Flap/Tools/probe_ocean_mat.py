"""Minimal, spawn-free dump of Water_Material_Ocean: parameter names AND current
defaults (scalar/vector/texture/static-switch). No actors spawned — avoids the
WaterBodyOcean editor-GC crash seen in probe_native_ocean.py."""
import sys
import json
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
REPORT = Path('V:/BORN 2 FLAP/_ocean_mat_report.json')
lib = u.MaterialEditingLibrary
rep = {}
mat = u.load_asset('/Water/Materials/WaterSurface/Water_Material_Ocean')
if not mat:
    rep['mat'] = 'FAILED'
else:
    rep['scalars'] = {}
    for n in lib.get_scalar_parameter_names(mat):
        try:
            rep['scalars'][str(n)] = lib.get_scalar_parameter_default(mat, n)
        except Exception as ex:
            rep['scalars'][str(n)] = 'ERR %s' % ex
    rep['vectors'] = {}
    for n in lib.get_vector_parameter_names(mat):
        try:
            v = lib.get_vector_parameter_default(mat, n)
            rep['vectors'][str(n)] = [v.r, v.g, v.b, v.a]
        except Exception as ex:
            rep['vectors'][str(n)] = 'ERR %s' % ex
    rep['textures'] = [str(x) for x in lib.get_texture_parameter_names(mat)]
    rep['switches'] = {}
    for n in lib.get_static_switch_parameter_names(mat):
        try:
            rep['switches'][str(n)] = lib.get_static_switch_parameter_default(mat, n)
        except Exception as ex:
            rep['switches'][str(n)] = 'ERR %s' % ex
json.dump(rep, open(REPORT, 'w'), indent=1, default=str)
print('OCEAN_MAT_REPORT_WRITTEN')
