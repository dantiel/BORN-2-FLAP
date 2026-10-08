using UnrealBuildTool;
using System.IO;

public class Born2Flap : ModuleRules
{
    public Born2Flap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate", "SlateCore", "UMG", "ProceduralMeshComponent", "ApplicationCore",
            "AudioMixer", "Json", "MoviePlayer"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
            { PublicSystemLibraries.AddRange(new[] { "dinput8.lib", "dxguid.lib", "ole32.lib" });
            RuntimeDependencies.Add("$(ProjectDir)/Binaries/ThirdParty/born2flap_math.dll", StagedFileType.NonUFS); }

        if (Target.Platform == UnrealTargetPlatform.IOS ||
            Target.Platform == UnrealTargetPlatform.Android)
        {
            // Haskell MathCore embedded as WASM: wasm3 (no-JIT interpreter) +
            // the static b2f_math_* adapter are built into one static lib by
            // Tools/build-wasm3-ios.sh. dlopen is forbidden on these platforms,
            // so Born2FlapMathBridge binds the symbols directly. See
            // docs/ios-port.md and Native/src/born2flap_math_wasm.cpp.
            string WasmBackendLib = Path.GetFullPath(Path.Combine(ModuleDirectory,
                "../../../../Native/lib/libborn2flap_math_wasm.a"));
            if (File.Exists(WasmBackendLib))
            {
                PublicAdditionalLibraries.Add(WasmBackendLib);
            }
        }

        PrivateIncludePaths.Add(ModuleDirectory);
        RuntimeDependencies.Add("$(ProjectDir)/ThirdPartyNotices.txt", StagedFileType.NonUFS);

        PublicIncludePaths.Add(Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../../../Native/include")));
    }
}
