using UnrealBuildTool;
using System.Collections.Generic;

public class Born2FlapEditorTarget : TargetRules
{
    public Born2FlapEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        // Allow the per-project linker flag (disable-linkonceodr-outlining) even
        // though this target shares build products with UnrealEditor.
        bOverrideBuildEnvironment = true;
        // macOS LTO (LTCG) can hang the linker indefinitely on this project —
        // disable it for deterministic, terminating builds.
        bAllowLTCG = false;

        // Xcode 16+/26 linker defaults to `-mllvm -enable-linkonceodr-outlining`,
        // an ODR-outlining optimization that hangs indefinitely (hours, 90%+ CPU)
        // on large Unreal modules. It is independent of LTO, so bAllowLTCG=false
        // does not remove it — disable it explicitly.
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            AdditionalLinkerArguments += " -Wl,-mllvm,-disable-linkonceodr-outlining";
        }

        ExtraModuleNames.Add("Born2Flap");
    }
}