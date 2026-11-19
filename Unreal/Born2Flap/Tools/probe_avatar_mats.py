"""Dump per-person skeletal mesh material slots: assigned material, blend
mode, two-sided flag and texture references. Ground truth for the
'transparent body / missing skin' reports (James, Brian, John)."""
import json
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from project_guard import require_project

require_project()
REPORT = Path('V:/BORN 2 FLAP/_avatar_mat_report.json')

PERSONS = [
    ('John', '/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder'),
    ('Erika', '/Game/Actor/SK_Erika/SkeletalMeshes/SK_Erika.SK_Erika'),
    ('Amy', '/Game/Actor/SK_Amy/SkeletalMeshes/SK_Amy.SK_Amy'),
    ('Michelle', '/Game/Actor/SK_Michelle/SkeletalMeshes/SK_Michelle.SK_Michelle'),
    ('Sophie', '/Game/Actor/SK_Sophie/SkeletalMeshes/SK_Sophie.SK_Sophie'),
    ('Kate', '/Game/Actor/SK_Kate/SkeletalMeshes/SK_Kate.SK_Kate'),
    ('James', '/Game/Actor/SK_James/SkeletalMeshes/SK_James.SK_James'),
    ('Adam', '/Game/Actor/SK_Adam/SkeletalMeshes/SK_Adam.SK_Adam'),
    ('Brian', '/Game/Actor/SK_Brian/SkeletalMeshes/SK_Brian.SK_Brian'),
    ('Alex', '/Game/Actor/SK_Alex/SkeletalMeshes/SK_Alex.SK_Alex'),
    ('Heraklios', '/Game/Actor/SK_Heraklios/SkeletalMeshes/SK_Heraklios.SK_Heraklios'),
    ('Alien', '/Game/Actor/SK_Alien/SkeletalMeshes/SK_Alien.SK_Alien'),
    ('Ninja', '/Game/Actor/SK_Ninja/SkeletalMeshes/SK_Ninja.SK_Ninja'),
    ('Knight', '/Game/Actor/SK_Knight/SkeletalMeshes/SK_Knight.SK_Knight'),
    ('Pumpkinhulk', '/Game/Actor/SK_Pumpkinhulk/SkeletalMeshes/SK_Pumpkinhulk.SK_Pumpkinhulk'),
    ('Whiteclown', '/Game/Actor/SK_Whiteclown/SkeletalMeshes/SK_Whiteclown.SK_Whiteclown'),
    ('Goblin', '/Game/Actor/SK_Goblin/SkeletalMeshes/SK_Goblin.SK_Goblin'),
    ('Demon', '/Game/Actor/SK_Demon/SkeletalMeshes/SK_Demon.SK_Demon'),
    ('Gas Mask', '/Game/Actor/SK_GasMask/SkeletalMeshes/SK_GasMask.SK_GasMask'),
    ('X-Bot', '/Game/Actor/SK_XBot/SkeletalMeshes/SK_XBot.SK_XBot'),
]

rep = {}
for name, mesh_path in PERSONS:
    entry = {'mesh': mesh_path, 'slots': []}
    mesh = u.load_asset(mesh_path)
    if mesh is None:
        entry['error'] = 'mesh missing'
        rep[name] = entry
        continue
    mats = mesh.get_editor_property('materials')
    for i, sm in enumerate(mats):
        slot = str(sm.get_editor_property('material_slot_name'))
        mi = sm.get_editor_property('material_interface')
        info = {'index': i, 'slot': slot}
        if mi is None:
            info['material'] = None
        else:
            info['material'] = mi.get_path_name()
            info['material_class'] = mi.get_class().get_name()
            target = mi
            # MaterialInstanceConstant: blend mode / two-sided live on the
            # PARENT material (glTF imports create MICs over a shared parent).
            if isinstance(mi, u.MaterialInstanceConstant):
                parent = mi.get_editor_property('parent')
                if parent is not None:
                    info['parent'] = parent.get_path_name()
                    target = parent
                try:
                    bo = mi.get_editor_property('base_property_overrides')
                    over = {}
                    for f in ('override_two_sided', 'two_sided', 'override_blend_mode', 'blend_mode'):
                        try:
                            over[f] = str(bo.get_editor_property(f))
                        except Exception:
                            over[f] = 'n/a'
                    info['mi_overrides'] = over
                except Exception as ex:
                    info['mi_overrides'] = 'ERR %s' % ex
            try:
                info['blend_mode'] = str(target.get_editor_property('blend_mode'))
            except Exception as ex:
                info['blend_mode'] = 'ERR %s' % ex
            try:
                info['two_sided'] = bool(target.get_editor_property('two_sided'))
            except Exception as ex:
                info['two_sided'] = 'ERR %s' % ex
            texs = []
            try:
                for expr in target.get_editor_property('expressions'):
                    if isinstance(expr, u.MaterialExpressionTextureSample) and expr.texture:
                        texs.append(expr.texture.get_path_name())
            except Exception:
                pass
            if not texs and isinstance(mi, u.MaterialInstanceConstant):
                try:
                    for tpv in mi.get_editor_property('texture_parameter_values'):
                        p = str(tpv.get_editor_property('parameter_info').get_editor_property('name'))
                        v = tpv.get_editor_property('parameter_value')
                        texs.append('%s=%s' % (p, v.get_path_name() if v else 'NULL'))
                except Exception:
                    pass
            info['textures'] = texs
        entry['slots'].append(info)
    rep[name] = entry

REPORT.write_text(json.dumps(rep, indent=1, default=str), encoding='utf-8')
print('AVATAR_MAT_REPORT_WRITTEN')