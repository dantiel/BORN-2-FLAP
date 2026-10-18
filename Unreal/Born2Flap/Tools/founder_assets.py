"""Persistent posed founder and complete static kestrel, independent of flight ticking."""
import unreal as u
from create_nature_assets import ROOT,LIB,MESH,TOOLS,EDIT,import_file,node,output

def prepare(scope,bronze):
    bronze.set_editor_property('two_sided',True)
    EDIT.recompile_material(bronze);LIB.save_loaded_asset(bronze)
    folder='/Game/Shiomori/Imported/Founder'
    # Always (re)import: the baked GLB changes whenever the pose is re-authored,
    # and import_file uses replace_existing so a fresh pose lands over the old one.
    import_file(ROOT/'Founder/Founder_posed.glb',folder)
    assets=[u.load_asset(p) for p in LIB.list_assets(folder)]
    meshes=[a for a in assets if isinstance(a,u.StaticMesh)]
    assert len(meshes)==1,meshes
    founder=meshes[0]
    # Duplicate imported materials so skin/cloth normal maps and hair alpha survive
    # bronze casting. Original coloured materials remain reusable for a pilot.
    for i,slot in enumerate(founder.static_materials):
        # Always read the preserved import, including on subsequent rebuilds.
        original=u.load_asset(folder+'/Founder_posed/Materials/'+['Material_001','Material_003','Material_002'][i])
        path=folder+'/M_FounderCast%d'%i
        mat=u.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset('M_FounderCast%d'%i,folder,u.Material,u.MaterialFactoryNew())
        EDIT.delete_all_material_expressions(mat)
        color=node(mat,'Constant3Vector',constant=u.LinearColor(.24,.145,.06,1));output(color,'BASE_COLOR')
        output(node(mat,'Constant',r=.82),'METALLIC');output(node(mat,'Constant',r=.52),'ROUGHNESS')
        normal=EDIT.get_material_instance_texture_parameter_value(original,'NormalTexture')
        if normal:
            tex=node(mat,'TextureSample',texture=normal,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
            strength=node(mat,'Constant3Vector',constant=u.LinearColor(.25,.25,1,1));mul=node(mat,'Multiply');norm=node(mat,'Normalize')
            EDIT.connect_material_expressions(tex,'RGB',mul,'A');EDIT.connect_material_expressions(strength,'',mul,'B')
            EDIT.connect_material_expressions(mul,'',norm,'VectorInput');output(norm,'NORMAL')
        if i==2:
            mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
            mat.set_editor_property('opacity_mask_clip_value',.305137)
            texture=EDIT.get_material_instance_texture_parameter_value(original,'BaseColorTexture')
            output(node(mat,'TextureSample',texture=texture),'OPACITY_MASK','A')
        mat.set_editor_property('two_sided',True);EDIT.recompile_material(mat);LIB.save_loaded_asset(mat)
        founder.set_material(i,mat)
    LIB.save_loaded_asset(founder)
    path='/Game/Shiomori/Meshes/SM_KestrelRaised'
    bird=u.load_asset(path) if LIB.does_asset_exist(path) else None
    if not bird and hasattr(u, 'Born2FlapStaticBird'):
        source=u.EditorLevelLibrary.spawn_actor_from_class(u.Born2FlapStaticBird,u.Vector(),u.Rotator())
        source.build_kestrel();parts=[]
        # A gentle, symmetrical 12-degree lift, around the real wing hinges.
        for c in source.get_components_by_class(u.SceneComponent):
            if c.get_name().startswith('RavenShoulder'):
                c.set_relative_rotation(u.Rotator(roll=12 if '-1' in c.get_name() else -12),False,False)
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
        opts=u.MergeStaticMeshActorsOptions();opts.base_package_name='/Game/Shiomori/Meshes/KestrelRaised';opts.new_actor_label='Kestrel bake';opts.destroy_source_actors=True;opts.spawn_merged_actor=True
        merged=MESH.merge_static_mesh_actors(parts,opts);assert merged
        bird=merged.static_mesh_component.static_mesh;LIB.save_loaded_asset(bird)
        sub=u.get_editor_subsystem(u.EditorActorSubsystem);sub.destroy_actor(merged);sub.destroy_actor(source)
    if not bird:
        raise RuntimeError('Kestrel bake failed: check the quoted .uproject path and Born2FlapEditor module. Refusing to save an incomplete campus.')
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