"""Assign the GLB-derived PBR materials to the FBX mesh slots of every avatar
character. Standalone repair pass — mirrors the assignment step of
import_avatar_materials.py but writes its report to a file (python print is
invisible in the -log capture) and saves each mesh explicitly.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/assign_avatar_slots.py" -unattended -nop4 -nosplash -stdout
"""
import sys
from pathlib import Path

import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from import_avatar_materials import CHARACTERS, _material_plan

REPORT = Path('V:/BORN 2 FLAP/_avatar_slot_assign.json')

out = {}

for name in CHARACTERS:
    plan = _material_plan(name)
    # keys: raw GLB name + sanitized (':' -> '_')
    materials = {}
    for mname in plan:
        san = mname.replace(':', '_')
        mat_path = '/Game/Actor/%s/Materials/%s.%s' % (name, san, san)
        mat = u.load_asset(mat_path)
        if mat is not None and isinstance(mat, u.MaterialInterface):
            materials[mname] = mat
            materials[san] = mat
            materials[mname.casefold()] = mat
    uniq = list(dict.fromkeys(materials.values()))
    rec = {'plan': list(plan.keys()), 'slots': []}
    mesh_path = '/Game/Actor/%s/SkeletalMeshes/%s.%s' % (name, name, name)
    mesh = u.load_asset(mesh_path)
    if mesh is None or not isinstance(mesh, u.SkeletalMesh):
        rec['error'] = 'mesh missing: %s' % mesh_path
        out[name] = rec
        continue
    mats = mesh.get_editor_property('materials')
    for i, sm in enumerate(mats):
        slot = str(sm.get_editor_property('material_slot_name'))
        mat = materials.get(slot) or materials.get(slot.casefold()) \
            or materials.get(slot.replace(':', '_'))
        if mat is None:
            # FBX import sanitises material names differently than the GLB
            # ('eyes:eyes_lambert' -> 'eyes_lambert'): match by containment.
            for k in materials:
                if slot and (slot in k or k in slot):
                    mat = materials[k]
                    break
        if mat is None and i < len(uniq):
            mat = uniq[i]  # order fallback
        if mat is not None:
            try:
                sm.set_editor_property('material_interface', mat)
                # Array elements are STRUCT COPIES in UE python — the mutated
                # struct must be written back into the array slot.
                mats[i] = sm
                rec['slots'].append('%d:%s->%s' % (i, slot, mat.get_name()))
            except Exception as e:
                rec['slots'].append('%d:%s:ERR %s' % (i, slot, e))
        else:
            rec['slots'].append('%d:%s:NO_MAT' % (i, slot))
    mesh.set_editor_property('materials', mats)
    # save_asset() skips packages the python API did not mark dirty (property
    # edits via set_editor_property do NOT dirty) — force the write.
    u.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    out[name] = rec

REPORT.write_text(__import__('json').dumps(out, indent=1))
u.log('assign_avatar_slots: wrote %s' % REPORT)