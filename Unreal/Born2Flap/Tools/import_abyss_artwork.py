"""Import the Neon Abyss menu artwork (Abyss.png) as a UI texture."""
from pathlib import Path
import unreal as u

source = Path(__file__).resolve().parents[3] / 'Art' / 'UI' / 'Elements' / 'Abyss.png'
destination = '/Game/UI/Elements'
u.EditorAssetLibrary.make_directory(destination)

task = u.AssetImportTask()
task.filename = str(source)
task.destination_path = destination
task.destination_name = 'Abyss'
task.automated = True
task.replace_existing = True
task.save = False
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

texture = u.load_asset(destination + '/Abyss')
assert isinstance(texture, u.Texture2D), source
texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property('filter', u.TextureFilter.TF_BILINEAR)
texture.set_editor_property('never_stream', True)
texture.set_editor_property('srgb', True)
assert u.EditorAssetLibrary.save_loaded_asset(texture)
u.log('ABYSS_ARTWORK_READY')
