using UnrealBuildTool;
using System.Collections.Generic;

public class Born2FlapTarget : TargetRules
{
    public Born2FlapTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Born2Flap");
    }
}
