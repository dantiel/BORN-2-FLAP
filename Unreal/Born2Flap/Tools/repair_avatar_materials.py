"""Repair the broken avatar materials (root cause: glossiness/MR textures were
imported with sRGB=True, so their LinearColor samplers fail to compile and UE
falls back to the gray Default Material in game — the "no textures" look).

Fix: flip srgb=False on every linear-data texture (Glossiness/Metallic/
Roughness/ORM) in the classic roster, then recompile + save every material.

Run (editor closed, headless commandlet):
    UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/repair_avatar_materials.py" -unattended -nop4 -nosplash -stdout
"""
from pathlib import Path
import unreal as u

LIB = u.EditorAssetLibrary
MEL = u.MaterialEditingLibrary

CHARS = ['SK_Erika', 'SK_Amy', 'SK_Michelle', 'SK_Sophie', 'SK_Kate',
         'SK_James', 'SK_Adam', 'SK_Brian', 'SK_Alex']

_LINEAR_HINTS = ('glossiness', 'metallic', 'roughness', 'orm', 'mr_', '_mr', 'metalness')


def _is_linear(name):
    n = name.casefold()
    return any(h in n for h in _LINEAR_HINTS)


def main():
    fixed_tex = 0
    for c in CHARS:
        tex_dir = '/Game/Actor/%s/Textures' % c
        if not LIB.does_directory_exist(tex_dir):
            print('TEXDIR_%s=missing' % c)
            continue
        for p in LIB.list_assets(tex_dir, recursive=True, include_folder=False):
            if not _is_linear(Path(p).stem):
                continue
            tex = u.load_asset(p)
            if tex is None:
                print('TEX_%s=load-fail' % p)
                continue
            try:
                before = tex.get_editor_property('srgb')
            except Exception:
                before = '?'
            if before is True:
                try:
                    tex.set_editor_property('srgb', False)
                    LIB.save_asset(p)
                    fixed_tex += 1
                    print('TEXFIX_%s srgb True->False' % p)
                except Exception as e:
                    print('TEXFIX_%s FAILED %s' % (p, e))
            else:
                print('TEXOK_%s srgb=%s' % (p, before))

        mat_dir = '/Game/Actor/%s/Materials' % c
        if not LIB.does_directory_exist(mat_dir):
            print('MATDIR_%s=missing' % c)
            continue
        for p in LIB.list_assets(mat_dir, recursive=True, include_folder=False):
            mat = u.load_asset(p)
            if mat is None:
                print('MAT_%s=load-fail' % p)
                continue
            try:
                MEL.recompile_material(mat)
                LIB.save_asset(p)
                print('MATRECOMPILE_%s ok' % p)
            except Exception as e:
                print('MATRECOMPILE_%s FAILED %s' % (p, e))
    print('REPAIR_AVATAR_MATERIALS_DONE fixed_tex=%d' % fixed_tex)


if __name__ == '__main__':
    try:
        main()
    finally:
        u.SystemLibrary.execute_console_command(None, 'quit')