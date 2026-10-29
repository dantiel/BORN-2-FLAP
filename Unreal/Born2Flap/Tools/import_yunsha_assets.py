"""One-time import of licensed architecture and a scanned rock face.

Retain Content/WorldAssets/Yunsha in Git LFS. Ordinary Windows/macOS map
generation only reads these portable assets and never downloads art.
"""
from pathlib import Path
import sys
import os
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from create_nature_assets import import_file
from project_guard import require_project
require_project()
root=Path(__file__).resolve().parents[1]/'Saved/YunshaSource'
lib=u.EditorAssetLibrary
for name in ('TempleHalls','Pagoda','Gate','rock_face_01','GoldenHall','Paifang','Bonsai','LakePavilion','OasisPalm','PterosaurSlab','PterosaurJaw','PteranodonBones','Herbstosaurus','DinosaurEggs','FossilFish','Ammonite','FieldTent'):
    selected=os.environ.get('B2F_YUNSHA_IMPORT_ONLY','')
    if selected and name not in selected.split(','):continue
    folder='/Game/WorldAssets/Yunsha/'+name
    source=root/(name+'_prepared.glb') if name!='rock_face_01' else root/name/(name+'.gltf')
    if not lib.does_directory_exist(folder) or name in os.environ.get('B2F_YUNSHA_REIMPORT','').split(','): import_file(source,folder)
    objects=[u.load_asset(p) for p in lib.list_assets(folder)]
    meshes=[o for o in objects if isinstance(o,u.StaticMesh)]
    assert len(meshes)==1,(name,len(meshes))
    sm=meshes[0]
    sm.get_editor_property('body_setup').set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm.set_editor_property('allow_cpu_access',True)
    if name in ('GoldenHall','Paifang','Bonsai','LakePavilion','OasisPalm','PterosaurSlab','PterosaurJaw','PteranodonBones','Herbstosaurus','DinosaurEggs','FossilFish','Ammonite'):
        editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
        settings=editor.get_nanite_settings(sm)
        before=str(settings)
        # These small display trees use thin authored leaves. Conventional
        # rendering retains their silhouette without Nanite's area inflation.
        settings.enabled=name not in ('Bonsai','OasisPalm')
        settings.keep_percent_triangles=1.0
        settings.trim_relative_error=0.0
        # Preserve the full fallback mesh: complex collision must retain the
        # carved doorways on both Nanite and conventional rendering platforms.
        settings.fallback_percent_triangles=1.0
        settings.fallback_relative_error=0.0
        if str(settings)!=before:editor.set_nanite_settings(sm,settings,True)
    if name=='OasisPalm':
        # Source fronds use blended alpha. Masked two-sided foliage preserves
        # individual leaflets without translucent sorting or opaque card shapes.
        edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
        for slot,item in enumerate(sm.static_materials):
            original=item.material_interface.get_name() if item.material_interface else ''
            indices={'initialShadingGroup':0,'lambert2SG':1,'lambert3SG':2}
            if original not in indices and not original.startswith('M_PalmFrond'):continue
            index=indices.get(original,slot)
            path=folder+'/Materials/M_PalmFrond%d'%index
            mat=u.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_PalmFrond%d'%index,folder+'/Materials',u.Material,u.MaterialFactoryNew())
            edit.delete_all_material_expressions(mat)
            mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED)
            mat.set_editor_property('two_sided',True)
            mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
            mat.set_editor_property('opacity_mask_clip_value',.35)
            tex=u.load_asset(folder+'/OasisPalm_prepared/Textures/OasisPalm_prepared_texture_%d'%index)
            assert tex
            sample=edit.create_material_expression(mat,u.MaterialExpressionTextureSample);sample.texture=tex
            edit.connect_material_property(sample,'RGB',u.MaterialProperty.MP_BASE_COLOR)
            edit.connect_material_property(sample,'RGB',u.MaterialProperty.MP_SUBSURFACE_COLOR)
            edit.connect_material_property(sample,'A',u.MaterialProperty.MP_OPACITY_MASK)
            rough=edit.create_material_expression(mat,u.MaterialExpressionConstant);rough.r=.8
            edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
            edit.set_material_usage(mat,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            edit.recompile_material(mat);lib.save_loaded_asset(mat);sm.set_material(slot,mat)
    if name in ('PterosaurSlab','PteranodonBones','DinosaurEggs','FossilFish','Ammonite'):
        # The source's unlit glTF shader imports black in this rendering path.
        # Keep its photographic UV textures in conventional lit materials.
        edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
        for slot in range(len(sm.static_materials)):
            path=folder+'/Materials/M_FossilScan%d'%slot
            mat=u.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_FossilScan%d'%slot,folder+'/Materials',u.Material,u.MaterialFactoryNew())
            edit.delete_all_material_expressions(mat);mat.set_editor_property('two_sided',True)
            tex=u.load_asset(folder+'/'+name+'_prepared/Textures/'+name+'_prepared_texture_%d'%slot);assert tex
            sample=edit.create_material_expression(mat,u.MaterialExpressionTextureSample);sample.texture=tex
            edit.connect_material_property(sample,'RGB',u.MaterialProperty.MP_BASE_COLOR)
            rough=edit.create_material_expression(mat,u.MaterialExpressionConstant);rough.r=.88
            edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
            edit.recompile_material(mat);lib.save_loaded_asset(mat);sm.set_material(slot,mat)
    if name=='Herbstosaurus':
        edit=u.MaterialEditingLibrary;assets=u.AssetToolsHelpers.get_asset_tools()
        path=folder+'/Materials/M_ResearchVertexColour'
        mat=u.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('M_ResearchVertexColour',folder+'/Materials',u.Material,u.MaterialFactoryNew())
        edit.delete_all_material_expressions(mat);mat.set_editor_property('two_sided',True)
        colour=edit.create_material_expression(mat,u.MaterialExpressionVertexColor)
        # VertexColor's combined output is unnamed (unlike TextureSample.RGB).
        assert edit.connect_material_property(colour,'',u.MaterialProperty.MP_BASE_COLOR)
        rough=edit.create_material_expression(mat,u.MaterialExpressionConstant);rough.r=.9
        edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
        edit.recompile_material(mat);lib.save_loaded_asset(mat);sm.set_material(0,mat)
    lib.save_loaded_asset(sm)
    u.log('YUNSHA_IMPORTED '+name+' '+sm.get_path_name()+' '+str(sm.get_bounds()))
u.SystemLibrary.execute_console_command(None,'quit')
