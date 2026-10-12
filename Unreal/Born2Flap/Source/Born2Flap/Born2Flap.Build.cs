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

        if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            // Haskell MathCore cross-compiled to native arm64-apple-ios by
            // ghc-ios 9.14.1 (Novavero-AI). The RTS + base + math core live in
            // one self-contained static archive; dlopen is forbidden on iOS, so
            // Born2FlapMathBridge binds the b2f_math_* symbols directly.
            // Build with Tools/build-math-ios.sh. GHC's Iconv encoder needs
            // libiconv; everything else links from libSystem.
            string IosMathLib = Path.GetFullPath(Path.Combine(ModuleDirectory,
                "../../../../MathCore/dist-newstyle/libborn2flap_math-ios.a"));
            if (File.Exists(IosMathLib))
            {
                PublicAdditionalLibraries.Add(IosMathLib);
                PublicSystemLibraries.Add("iconv");
            }
        }

        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // Android has no native GHC cross-compiler yet, so the math core is
            // embedded as WASM: wasm3 (no-JIT interpreter) + the static
            // b2f_math_* adapter in one static lib by Tools/build-wasm3-android.sh.
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