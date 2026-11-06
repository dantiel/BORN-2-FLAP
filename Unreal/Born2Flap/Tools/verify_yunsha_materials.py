"""Recompile Yunsha materials with a real RHI (no -nullrhi) so material shaders
actually compile and any Custom-node texture-binding errors surface in the log."""
import unreal as u

E = u.EditorAssetLibrary
M = u.MaterialEditingLibrary

mats = [
    'M_WindRippleSand', 'M_RedSandstone', 'M_CeladonGlaze', 'M_GardenMoss',
    'M_GardenLimestone', 'M_MoonJadeStone', 'M_CarvedNephrite', 'M_PaleCarvedStone',
    'M_JadeTiles', 'M_Cinnabar', 'M_SunGold', 'M_BoneArchiveStrata',
    'M_PterosaurEggMineral', 'M_StandingCloud',
]

ROOT = '/Game/Yunsha/Materials/'
for name in mats:
    path = ROOT + name
    mat = u.load_asset(path)
    if not mat:
        u.log_error('VERIFY_MISSING ' + path)
        continue
    ok = M.recompile_material(mat)
    saved = E.save_loaded_asset(mat)
    u.log('VERIFY_RECOMPILE %s recompile=%s saved=%s' % (name, ok, saved))

u.log('VERIFY_DONE')
