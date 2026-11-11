"""Refresh solved ripple UVs/materials on saved terrain without replacing world actors."""
from pathlib import Path
import sys
import unreal as u
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
exec(compile((HERE/'repair_yunsha_sand.py').read_text(),str(HERE/'repair_yunsha_sand.py'),'exec'))
for row in range(6):
    for col in range(6):
        sm=u.load_asset('/Game/Yunsha/Meshes/SM_Dunes_%d_%d'%(row,col));assert sm
        u.Born2FlapTerrainBuilder.smooth_yunsha_terrain(sm,u.Vector2D(0,0),False)
        assert u.EditorAssetLibrary.save_loaded_asset(sm)
sm=u.load_asset('/Game/Yunsha/Meshes/SM_BoneArchiveTerrain');assert sm
u.Born2FlapTerrainBuilder.smooth_yunsha_terrain(sm,u.Vector2D(100000,0),False)
assert u.EditorAssetLibrary.save_loaded_asset(sm)
assert u.EditorLoadingAndSavingUtils.load_map('/Game/Yunsha/Maps/YUNSHA')
u.log('YUNSHA_AEOLIAN_REFRESH_SAVED_RELOADED')
