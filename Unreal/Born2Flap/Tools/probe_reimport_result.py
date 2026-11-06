"""Probe: what did the reimport produce?"""
import json
import unreal as u

out = {}
lib = u.EditorAssetLibrary
paths = lib.list_assets('/Game/Actor/Clean/Founder_rigged_clean', recursive=True)
out['assets'] = [str(p) for p in paths]
for p in paths:
    a = u.load_asset(p)
    if a is None:
        out[p] = 'LOAD_FAIL'
        continue
    d = dict(cls=str(a.get_class().get_name()))
    if isinstance(a, u.SkeletalMesh):
        b = a.get_bounds()
        sk = None
        try:
            sk = a.get_editor_property('skeleton')
        except Exception:
            pass
        d['bounds'] = [round(v, 2) for v in (b.box_extent.x, b.box_extent.y, b.box_extent.z)]
        d['skeleton'] = str(sk.get_path_name()) if sk else None
        try:
            d['bones'] = a.get_bone_count() if hasattr(a, 'get_bone_count') else None
        except Exception:
            pass
    out[p] = d
s = 'PROBE_REIMPORT ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')