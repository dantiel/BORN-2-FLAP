"""Import real lava scans and build game materials; run in the full Unreal editor."""
import ast
from pathlib import Path
import unreal as u

here=Path(__file__).resolve().parent
source=here/'create_shiomori_map.py'
tree=ast.parse(source.read_text(encoding='utf8'))
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,ela=u.EditorAssetLibrary)
# Avoid the generator's module imports: only its material/mesh helper functions.
defs=[n for n in tree.body if isinstance(n,ast.FunctionDef)]
scope.update(u=u,math=__import__('math'),Path=Path)
exec(compile(ast.Module(body=defs,type_ignores=[]),str(source),'exec'),scope)
ela=u.EditorAssetLibrary;lib=u.MaterialEditingLibrary
edit=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
assert edit, 'Use -ExecutePythonScript'
for name in ['Lava','Scoria']:
    folder='/Game/Shiomori/LavaScans/'+name
    if not ela.does_directory_exist(folder):
        task=u.AssetImportTask();task.filename=str(here.parent/'Saved/LavaSource'/(name+'.glb'))
        task.destination_path=folder;task.automated=True;task.save=True
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes=[u.load_asset(p) for p in ela.list_assets(folder) if isinstance(u.load_asset(p),u.StaticMesh)]
    assert meshes,name
    mesh=max(meshes,key=lambda m:edit.get_number_verts(m,0))
    alias='/Game/Shiomori/LavaScans/SM_'+name
    if not ela.does_asset_exist(alias):ela.duplicate_asset(mesh.get_path_name(),alias)
    mesh=u.load_asset(alias)
    node=scope['node'];wire=scope['wire'];output=scope['output'];custom=scope['custom']
    mat=scope['pbr_material']('LavaScan_'+name,color=(.065,.075,.078),rough=.83)
    wp=node(mat,'WorldPosition')
    # Real scan geometry provides the pores. Neutral charcoal removes specimen
    # labels and baked museum lighting; fine surface variation uses rock normals.
    normal=u.load_asset('/Game/Shiomori/Textures/T_Basalt_Normal')
    if normal:
        output(scope['sample_tex'](mat,normal,('world',170),normal=True),'NORMAL','RGB')
    col=custom(mat,'''float grain=sin(P.x*.008+sin(P.y*.017))*sin(P.z*.019-P.y*.004);
float wet=1-smoothstep(-40.0,85.0,P.z);
return float3(.072,.080,.082)*(1+grain*.24)*lerp(1.0,.43,wet);''',{'P':wp})
    output(col,'BASE_COLOR')
    rough=custom(mat,'return lerp(.82,.22,1-smoothstep(-40.0,85.0,P.z));',{'P':wp},1)
    output(rough,'ROUGHNESS')
    lib.recompile_material(mat);assert ela.save_loaded_asset(mat)
    for i in range(len(mesh.static_materials)):mesh.set_material(i,mat)
    mesh.set_editor_property('allow_cpu_access',True)
    assert edit.set_convex_decomposition_collisions(mesh,24,24,100000)
    mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    # Preserve the porous scan silhouette near the camera, reduce only at distance.
    opts=u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
        reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p,screen_size=s) for p,s in [(1.,1.),(.55,.16),(.22,.065),(.08,.025)]])
    edit.set_lods(mesh,opts)
    assert ela.save_loaded_asset(mesh)
    u.log('LAVA_IMPORTED '+name+' vertices='+str(edit.get_number_verts(mesh,0))+' bounds='+str(mesh.get_bounds()))
u.log('LAVA_IMPORT_READY')
u.SystemLibrary.execute_console_command(None,'quit')
