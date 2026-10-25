"""One-time import of licensed architecture and a scanned rock face.

Retain Content/WorldAssets/Yunsha in Git LFS. Ordinary Windows/macOS map
generation only reads these portable assets and never downloads art.
"""
from pathlib import Path
import sys
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from create_nature_assets import import_file
from project_guard import require_project
require_project()
root=Path(__file__).resolve().parents[1]/'Saved/YunshaSource'
lib=u.EditorAssetLibrary
for name in ('TempleHalls','Pagoda','Gate','rock_face_01','GoldenHall','Paifang','Bonsai'):
    folder='/Game/WorldAssets/Yunsha/'+name
    source=root/(name+'_prepared.glb') if name!='rock_face_01' else root/name/(name+'.gltf')
    if not lib.does_directory_exist(folder): import_file(source,folder)
    objects=[u.load_asset(p) for p in lib.list_assets(folder)]
    meshes=[o for o in objects if isinstance(o,u.StaticMesh)]
    assert len(meshes)==1,(name,len(meshes))
    sm=meshes[0]
    sm.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm.set_editor_property('allow_cpu_access',True)
    if name in ('GoldenHall','Paifang','Bonsai'):
        editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
        settings=editor.get_nanite_settings(sm)
        before=str(settings)
        # These small display trees use thin authored leaves. Conventional
        # rendering retains their silhouette without Nanite's area inflation.
        settings.enabled=name!='Bonsai'
        settings.keep_percent_triangles=1.0
        settings.trim_relative_error=0.0
        # Preserve the full fallback mesh: complex collision must retain the
        # carved doorways on both Nanite and conventional rendering platforms.
        settings.fallback_percent_triangles=1.0
        settings.fallback_relative_error=0.0
        if str(settings)!=before:editor.set_nanite_settings(sm,settings,True)
    lib.save_loaded_asset(sm)
    u.log('YUNSHA_IMPORTED '+name+' '+sm.get_path_name()+' '+str(sm.get_bounds()))
u.SystemLibrary.execute_console_command(None,'quit')
