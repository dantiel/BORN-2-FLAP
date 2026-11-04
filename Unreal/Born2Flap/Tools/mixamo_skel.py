"""Get the Mixamo skeleton reference pose from the FBX, compare to founder."""
import json, traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_mixamo_skel.json')
diag = {}
try:
    TOOLS = u.AssetToolsHelpers.get_asset_tools()
    LIB = u.EditorAssetLibrary
    fbx = 'V:/BORN 2 FLAP/Unreal/Born2Flap/SourceAssets/Mixamo/Walk.fbx'
    dest = '/Game/TempMixamo2'
    if LIB.does_directory_exist(dest):
        for a in LIB.list_assets(dest, recursive=True, include_folder=False):
            try: LIB.delete_asset(a)
            except Exception: pass
        try: LIB.delete_directory(dest)
        except Exception: pass
    LIB.make_directory(dest)

    task = u.AssetImportTask()
    task.filename = fbx
    task.destination_path = dest
    task.destination_name = 'MixamoWalk'
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    skel_path = None
    all_assets = []
    if LIB.does_directory_exist(dest):
        for p in LIB.list_assets(dest, recursive=True, include_folder=False):
            all_assets.append(p)
            a = u.load_asset(p)
            if isinstance(a, u.Skeleton):
                skel_path = p
    diag['all_assets'] = all_assets
    diag['skel_path'] = skel_path

    if skel_path:
        skel = u.load_asset(skel_path)
        # dump reference pose transforms
        bones = ['mixamorig_Hips','mixamorig_Head','mixamorig_LeftHand','mixamorig_RightHand',
                 'mixamorig_LeftFoot','mixamorig_RightFoot','mixamorig_LeftShoulder','mixamorig_RightShoulder']
        pose = {}
        for b in bones:
            idx = skel.get_bone_index(u.Name(b))
            if idx < 0:
                pose[b] = 'not found'
                continue
            try:
                t = skel.get_reference_pose_transform(idx)
                pose[b] = {
                    'translation': [round(t.translation.x,1), round(t.translation.y,1), round(t.translation.z,1)],
                    'rotation': [round(t.rotation.x,3), round(t.rotation.y,3), round(t.rotation.z,3), round(t.rotation.w,3)],
                }
            except Exception as e:
                pose[b] = 'ERR:' + str(e)[:60]
        diag['ref_pose'] = pose
except Exception:
    diag['fatal'] = traceback.format_exc()

OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
print(json.dumps(diag, indent=2))
