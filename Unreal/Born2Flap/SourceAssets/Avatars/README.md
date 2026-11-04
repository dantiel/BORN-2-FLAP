# Avatar source characters

The on-foot pilot avatars are distinct Mixamo-rigged humans (CC BY 4.0),
normalized to the shared `mixamorig:*` 65-bone skeleton so they play the same
`AM_*` animation clips as the founder statue's rig.

Each `.glb` here is already normalized (`mixamorig:*` bone names, no
`_rootJoint`). Regenerate with `Tools/normalize_mixamo_character.py`.

| Asset        | Sketchfab model                          | Objaverse UID                     | Faces | Theme    |
|--------------|------------------------------------------|-----------------------------------|-------|----------|
| SK_Pirate.glb  | Low Poly Stylize Pirate                | 44477359ce1b4295ae7f385fce303887  | 7221  | pirate   |
| SK_Soldier.glb | Army soldier Character                 | cc995450452744ec8f852d8bf5f335d6  | 9710  | soldier  |
| SK_Scout.glb   | Free Mixamo retextured Model (astronaut)| 62990b050faf48dd8590d5d15872857c  | 46108 | scout    |

All are CC BY 4.0, downloaded via the public Objaverse mirror
(https://huggingface.co/datasets/allenai/objaverse). The founder statue
(`Founder_rigged_clean.glb` / `Founder_posed.glb`) is a separate decorative
sculpture and is NOT an avatar.
