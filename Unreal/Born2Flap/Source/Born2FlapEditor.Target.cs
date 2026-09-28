using UnrealBuildTool;
using System.Collections.Generic;

public class Born2FlapEditorTarget : TargetRules
{
    public Born2FlapEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        // macOS LTO (LTCG) can hang the linker indefinitely on this project —
        // disable it for deterministic, terminating builds.
        bAllowLTCG = false;
        ExtraModuleNames.Add("Born2Flap");
    }
}