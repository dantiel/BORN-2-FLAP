# Mixamo Animations — Drop Zone

Drop the four locomotion clips here. The import script
`Tools/import_mixamo_animations.py` retargets them onto the Founder skeleton
(`mixamorig:*` bone names → 1:1 match).

## Exact file names (case-insensitive)

| File | Mixamo search term | Asset produced |
|------|--------------------|----------------|
| `Idle.fbx`  | **Idle**            | `AM_Idle`  |
| `Walk.fbx`  | **Walking**         | `AM_Walk`  |
| `Run.fbx`   | **Running**         | `AM_Run`   |
| `Throw.fbx` | **Throwing**        | `AM_Throw` |

## Mixamo download settings

1. On mixamo.com pick any standard character (e.g. **Y Bot**), then the clip above.
2. In the right panel:
   - **In Place** → **checked** (the game moves the avatar, so no root motion).
   - **Skin** → **Without Skin** (animation only — smaller file; the mesh is ignored anyway).
   - **Format** → **FBX** (or "FBX for Unreal" if offered).
3. Download and rename to the names in the table, then drop them in this folder.

## Import

```
UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/import_mixamo_animations.py
```

Produces `/Game/Actor/Animations/AM_Idle|AM_Walk|AM_Run|AM_Throw`.
