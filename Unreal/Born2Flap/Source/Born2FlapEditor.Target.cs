using UnrealBuildTool;
using System.Collections.Generic;

public class Born2FlapEditorTarget : TargetRules
{
    public Born2FlapEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Born2Flap");
    }
}
