"""Import the attributed torii and hokora with their authored PBR materials."""
import unreal as u
from create_nature_assets import ROOT, import_file, LIB, MESH

def prepare():
    result={}
    for name in ('CoastalTorii','CoastalShrine'):
        folder='/Game/Shiomori/Imported/'+name
        paths=LIB.list_assets(folder) if LIB.does_directory_exist(folder) else []
        meshes=[u.load_asset(p) for p in paths if isinstance(u.load_asset(p),u.StaticMesh)]
        if not meshes:
            import_file(ROOT/name/(name+'_baked.glb'),folder)
            paths=LIB.list_assets(folder)
            meshes=[u.load_asset(p) for p in paths if isinstance(u.load_asset(p),u.StaticMesh)]
        assert len(meshes)==1,(name,len(meshes))
        mesh=meshes[0]
        mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        # Preserve thin ornaments and rope silhouettes at ordinary walking range.
        opts=u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
            reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p,screen_size=s) for p,s in [(1.,1.),(.65,.10),(.3,.025)]])
        MESH.set_lods(mesh,opts);LIB.save_loaded_asset(mesh)
        result[name]=mesh
        u.log('SHRINE_ASSET '+name+' '+str(mesh.get_bounds()))
    return result
