"""Persistent posed founder and complete static kestrel, independent of flight ticking."""
import unreal as u
from create_nature_assets import ROOT,LIB,MESH,TOOLS,EDIT,import_file,node,output

def prepare(scope,bronze):
    folder='/Game/Shiomori/Imported/Founder'
    if not LIB.does_directory_exist(folder):import_file(ROOT/'Founder/Founder_posed.glb',folder)
    assets=[u.load_asset(p) for p in LIB.list_assets(folder)]
    meshes=[a for a in assets if isinstance(a,u.StaticMesh)]
    assert len(meshes)==1,meshes
    founder=meshes[0]
    # Duplicate imported materials so skin/cloth normal maps and hair alpha survive
    # bronze casting. Original coloured materials remain reusable for a pilot.
    for i,slot in enumerate(founder.static_materials):
        original=slot.material_interface;path=folder+'/M_FounderBronze%d'%i
        mat=u.load_asset(path) if LIB.does_asset_exist(path) else LIB.duplicate_asset(original.get_path_name(),path)
        assert isinstance(mat,u.Material),type(mat)
        color=node(mat,'Constant3Vector',constant=u.LinearColor(.24,.145,.06,1));output(color,'BASE_COLOR')
        output(node(mat,'Constant',r=.82),'METALLIC');output(node(mat,'Constant',r=.39),'ROUGHNESS')
        mat.set_editor_property('two_sided',True);EDIT.recompile_material(mat);LIB.save_loaded_asset(mat)
        founder.set_material(i,mat)
    LIB.save_loaded_asset(founder)
    path='/Game/Shiomori/Meshes/SM_KestrelStatic'
    bird=u.load_asset(path) if LIB.does_asset_exist(path) else None
    if not bird:
        source=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapStaticBird,u.Vector(),u.Rotator())
        source.build_kestrel();parts=[]
        for c in source.get_components_by_class(u.MeshComponent):
            mesh=None
            if isinstance(c,u.StaticMeshComponent):mesh=c.static_mesh
            elif isinstance(c,u.ProceduralMeshComponent):
                v,t,n,uv,tan=u.ProceduralMeshLibrary.get_section_from_procedural_mesh(c,0)
                if not v:continue
                # build_mesh expects the opposite winding to the UE source.
                tris=[(t[k],t[k+2],t[k+1]) for k in range(0,len(t),3)]
                mesh=scope['build_mesh']('KestrelCast_'+c.get_name(),[(p.x,p.y,p.z) for p in v],tris,[(p.x,p.y) for p in uv])
            if not mesh:continue
            a=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector(),u.Rotator())
            a.static_mesh_component.set_static_mesh(mesh);a.set_actor_transform(c.get_world_transform(),False,False)
            for j in range(a.static_mesh_component.get_num_materials()):a.static_mesh_component.set_material(j,bronze)
            parts.append(a)
        opts=u.MergeStaticMeshActorsOptions();opts.base_package_name=path;opts.new_actor_label='Kestrel bake';opts.destroy_source_actors=True;opts.spawn_merged_actor=True
        merged=MESH.merge_static_mesh_actors(parts,opts);assert merged
        bird=merged.static_mesh_component.static_mesh;LIB.save_loaded_asset(bird)
        sub=u.get_editor_subsystem(u.EditorActorSubsystem);sub.destroy_actor(merged);sub.destroy_actor(source)
    return founder,bird

def place(mesh,label,position,height=None,scale=None,yaw=0,material=None):
    b=mesh.get_bounds();s=scale if scale is not None else height/(2*b.box_extent.z)
    a=u.EditorLevelLibrary.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*position),u.Rotator(yaw=yaw))
    a.set_actor_label(label);a.tags=['ShiomoriCampusV2','CampusFounder']
    a.static_mesh_component.set_static_mesh(mesh);a.static_mesh_component.set_editor_property('disallow_nanite',True)
    a.static_mesh_component.set_collision_profile_name('NoCollision');a.set_actor_scale3d(u.Vector(s,s,s))
    if material:
        for i in range(a.static_mesh_component.get_num_materials()):a.static_mesh_component.set_material(i,material)
    return a
