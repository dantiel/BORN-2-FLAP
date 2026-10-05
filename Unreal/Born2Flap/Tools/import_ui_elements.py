"""Import individually extracted interface artwork, losslessly and always resident."""
from pathlib import Path
import unreal as u

source = Path(__file__).resolve().parents[3] / 'Art' / 'UI' / 'Elements'
destination = '/Game/UI/Elements'
u.EditorAssetLibrary.make_directory(destination)
for file in sorted(source.glob('*.png')):
    task = u.AssetImportTask()
    task.filename = str(file)
    task.destination_path = destination
    task.destination_name = file.stem
    task.automated = True
    task.replace_existing = True
    task.save = False
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = u.load_asset(destination + '/' + file.stem)
    assert isinstance(texture, u.Texture2D), file
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('filter', u.TextureFilter.TF_BILINEAR)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('srgb', True)
    assert u.EditorAssetLibrary.save_loaded_asset(texture)
u.log('UI_ELEMENTS_READY')
