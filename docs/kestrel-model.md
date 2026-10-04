# Kestrel body asset

The fuselage is generated from the supplied `kestrel_fuselage_textured.obj`
and cylindrical PBR textures in `Downloads/kestrel_fuselage_unreal_final`.
The wing membrane and tail remain separate animated geometry.

Run `python Unreal/Born2Flap/Tools/build_kestrel_fuselage_obj.py` (requires numpy),
then run `Unreal/Born2Flap/Tools/import_kestrel_fuselage.py` in Unreal's Python
commandlet with the game closed. The builder accepts `--source` and `--output`.
Use forward slashes in Unreal's `-script=` argument.

The source loft has its rounded belly in +Z. The builder flips it upright,
smooths the abrupt shoulder/tail width changes into a continuous taper, blends
the tail root to Z=0, reverses triangle winding, and exports smooth normals.
The runtime yaw of 180 degrees points the nose along the game's +X axis.

Runtime body proportions are scaled about the unchanged wing root at X=3 cm:
to a total beak-to-tail-tip length of **420 mm**, with 90% of the original width
and 85% of the original height. The longitudinal factor is `42 / 182.0293`
(approximately 23.1%). The fuselage itself is about 306.9 mm long. The tail fan
uses the same transformation, preserving its connection to the body.
The source head-width profile is broadened separately so the shorter bird keeps
a fuller head. Wing geometry and solver attachment locations are unchanged.

Texture U is rolled by a quarter-turn: back at U=0/1, belly at U=.5. V is
reversed to account for Unreal's OBJ import convention, placing the eyes and
beak at the nose. Seam triangles have separate UVs but shared smooth normals.
No raster artwork is changed. Nanite is disabled for this small mesh so its
31,680 triangles also remain intact in conventional rendering. The old automatic
fallback had only 313 vertices.

The importer checks the saved mesh's triangle count, dorsal/ventral orientation,
normals, longitudinal UV direction, and seam continuity. Success logs
`KESTREL_MESH_VALIDATED` and `KESTREL_FUSELAGE_READY`.

For visual inspection, the desktop-input test's `-B2FBirdPreview` mode accepts
`-B2FBirdPreviewPitch=-8 -B2FBirdPreviewYaw=90` for a side view. Its default is
an elevated three-quarter view. The final falcon capture is
`Saved/Screenshots/WindowsEditor/COMMON_KESTREL.png`; inspect both views, since an
overhead view alone can hide an upside-down profile or misplaced face texture.
