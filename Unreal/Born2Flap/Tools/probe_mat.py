"""Headless probe: what material does the YUNSHA MeshPartition definition use?

Runs inside UnrealEditor-Cmd. Prints MPD_YUNSHA material path, blend mode,
shading model, and usage flags; then loads one compiled-section package and
prints the mesh material (MIC parent chain).
"""
import unreal as u

LOG = u.log
OUT = []


def log(msg):
    OUT.append(str(msg))
    LOG('[B2FMATPROBE] ' + str(msg))


E = u.EditorAssetLibrary
def_path = '/Game/Yunsha/DataAssets/MPD_YUNSHA'
definition = u.load_asset(def_path)
if not definition:
    log('FAIL definition not found at ' + def_path)
else:
    log('definition=' + definition.get_path_name())
    try:
        mat = definition.get_editor_property('material')
    except Exception as exc:
        try:
            mat = definition.get_editor_property('Material')
        except Exception:
            log('FAIL reading material property: ' + str(exc))
            mat = None
    if mat:
        log('mat_path=' + mat.get_path_name())
        log('mat_class=' + mat.get_class().get_name())
        log('mat_name=' + mat.get_name())
        try:
            log('blend=' + str(mat.get_editor_property('blend_mode')))
        except Exception as exc:
            log('blend_read_fail=' + str(exc))
        try:
            log('shading=' + str(mat.get_editor_property('shading_model')))
        except Exception as exc:
            log('shading_read_fail=' + str(exc))
        try:
            log('two_sided=' + str(mat.get_editor_property('two_sided')))
        except Exception as exc:
            log('two_sided_read_fail=' + str(exc))
        try:
            log('usage_flags=' + str(mat.get_editor_property('usage_flag_widgets')))
        except Exception as exc:
            try:
                log('usage_flags=' + str(mat.get_editor_property('material_usage_flags')))
            except Exception:
                log('usage_flags_read_fail=' + str(exc))
    else:
        log('mat=None')

# Load one compiled-section external-actor package and inspect its MIC.
import re
pkg = None
found = []
names = u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/__ExternalActors__/Yunsha', recursive=True)
for a in names:
    p = str(a.package_name)
    if 'CompiledSection' in str(a.get_full_name()):
        found.append(p)
        if pkg is None:
            pkg = p
log('section_packages_found=' + str(len(found)))
if pkg:
    obj = u.load_asset(pkg)
    log('loaded=' + str(pkg))
    if obj:
        comps = obj.get_components_by_class(u.StaticMeshComponent)
        log('sm_components=' + str(len(comps)))
        for c in comps:
            try:
                m = c.get_material(0)
            except Exception:
                m = None
            if m:
                log('component_material=' + m.get_path_name() + ' class=' + m.get_class().get_name())
                parent = None
                try:
                    parent = m.get_editor_property('parent')
                except Exception as exc:
                    log('parent_read_fail=' + str(exc))
                log('mic_parent=' + (parent.get_path_name() if parent else 'None'))
                if parent:
                    try:
                        log('parent_blend=' + str(parent.get_editor_property('blend_mode')))
                    except Exception:
                        pass

# Write out json for the runner.
import json
with open('V:/BORN 2 FLAP/_probe_mat.json', 'w') as f:
    json.dump(OUT, f, indent=1)
log('DONE')
