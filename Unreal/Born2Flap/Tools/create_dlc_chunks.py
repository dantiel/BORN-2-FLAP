#!/usr/bin/env python3
"""Create PrimaryAssetLabel assets that assign each non-base world to a DLC chunk.

Run inside the Unreal Editor commandlet (PythonScriptPlugin must be enabled, it is):

    "V:\\UE_5.8\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
        "V:\\BORN 2 FLAP\\Unreal\\Born2Flap\\Born2Flap.uproject" ^
        -run=pythonscript -script="V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/create_dlc_chunks.py" ^
        -stdout -unattended -nosplash -nop4

Chunk layout (MUST stay in sync with Source/Born2Flap/DLC/Born2FlapDLCSubsystem.cpp
and the $dlcChunks table in Tools/build-android.ps1):

    chunk 0 = base game: Shiomori + all shared content (Nature/WorldAssets/Actor/UI/...)
    chunk 1 = Murex
    chunk 2 = NeonAbyss
    chunk 3 = Ravenstonefield
    chunk 4 = Yunsha

Each label uses bLabelAssetsInMyDirectory=True + ApplyRecursively, so everything
under /Game/<World> is cooked into that chunk (including shared assets the world
references — DLC paks are intentionally self-contained). This also bundles the
world's radio soundtrack, which must live at /Game/<World>/Audio (e.g.
SHIOMORI_BAY, TURBORAVEN, YUNSHA_XUANTIE), so the music ships with its world.
Unlabeled content falls to chunk 0. Re-running is idempotent (existing labels
are deleted first).
"""
import unreal

WORLDS = {
    "Murex": 1,
    "NeonAbyss": 2,
    "Ravenstonefield": 3,
    "Yunsha": 4,
}


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    made = []

    for world, chunk_id in WORLDS.items():
        package_path = f"/Game/{world}"
        asset_name = f"ChunkLabel_{world}"
        full_path = f"{package_path}/{asset_name}"

        # Idempotent: drop any stale label so ChunkId/rules are authoritative.
        if unreal.EditorAssetLibrary.does_asset_exist(full_path):
            unreal.EditorAssetLibrary.delete_asset(full_path)

        label = tools.create_asset(
            asset_name=asset_name,
            package_path=package_path,
            asset_class=unreal.PrimaryAssetLabel,
            factory=None,
        )
        if not label:
            unreal.log_error(f"DLC: create_asset failed for {full_path}")
            continue

        rules = unreal.PrimaryAssetRules()
        rules.apply_recursively = True
        rules.cook_rule = unreal.PrimaryAssetLabelCookRules.ALWAYS_COOK
        rules.chunk_id = chunk_id

        label.set_editor_property("label_assets_in_my_directory", True)
        label.set_editor_property("is_runtime_label", False)
        label.set_editor_property("rules", rules)

        unreal.EditorAssetLibrary.save_loaded_asset(label)
        made.append(f"{world}->{chunk_id}")
        unreal.log(f"DLC chunk label: {world} -> chunk {chunk_id}")

    unreal.log("DLC chunk labels created: " + ", ".join(made))


if __name__ == "__main__":
    main()