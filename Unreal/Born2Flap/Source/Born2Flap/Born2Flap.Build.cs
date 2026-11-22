using UnrealBuildTool;
using System.IO;

public class Born2Flap : ModuleRules
{
    public Born2Flap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(ModuleDirectory);
        // MeshPartitionCompiledSection.h includes "MaterialCache/MaterialCacheVirtualTexture.h",
        // but the MaterialCache plugin is absent from this UE 5.8 engine install (the header
        // exists nowhere and the prebuilt MeshPartition DLL does not import it). The type is
        // only used for TObjectPtr properties, so a project-local forward-declaration stub
        // (Probe/Include/) satisfies any consumer. Needed by Probe/Born2FlapTerrainProbe.cpp.
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Probe", "Include"));

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate", "SlateCore", "UMG", "ProceduralMeshComponent", "ApplicationCore",
            "AudioMixer", "Json", "MoviePlayer", "Niagara", "HTTP", "PakFile",
            "MeshPartition", "Chaos", "Water"
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

            // In-process Ruby Brain runtime: mruby 3.2.0 cross-compiled to
            // arm64-apple-ios (Tools/build-mruby-ios.sh) + the regexp engine
            // oniguruma 6.9.10 (Tools/build-onig-ios.sh). Both are static
            // archives (dlopen forbidden on iOS); the regexp gem's onig_*
            // symbols resolve against libonig-ios.a at link time. mruby-math
            // needs libm (a no-op symlink into libSystem on iOS, but declared
            // for parity with the host link).
            string IosMrubyLib = Path.GetFullPath(Path.Combine(ModuleDirectory,
                "../../../../ThirdParty/mruby/build-ios/ios-arm64/lib/libmruby.a"));
            string IosOnigLib = Path.GetFullPath(Path.Combine(ModuleDirectory,
                "../../../../ThirdParty/oniguruma/dist/lib/libonig.a"));
            if (File.Exists(IosMrubyLib) && File.Exists(IosOnigLib))
            {
                PublicAdditionalLibraries.Add(IosMrubyLib);
                PublicAdditionalLibraries.Add(IosOnigLib);
                PublicSystemLibraries.Add("m");
                PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,
                    "../../../../ThirdParty/mruby/include")));
                PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,
                    "../../../../ThirdParty/oniguruma/dist/include")));
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

        // Ship the authoring runtime and sources, so packaged Windows games
        // keep the same Ruby UI instead of silently reverting to native menus.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../.."));
            foreach (string Folder in new[] { "Brain/lib", "Brain/bin", "ThirdParty/ruby" })
            {
                string SourceRoot = Path.Combine(RepoRoot, Folder);
                if (!Directory.Exists(SourceRoot)) continue;
                foreach (string Source in Directory.EnumerateFiles(SourceRoot, "*", SearchOption.AllDirectories))
                    RuntimeDependencies.Add("$(ProjectDir)/" + Folder + "/" + Path.GetRelativePath(SourceRoot, Source).Replace('\\', '/'), Source, StagedFileType.NonUFS);
            }
        }
        // iOS: ship the Brain sources (lib + mruby shim) so the in-process
        // mruby host can load the exact same Ruby UI. No bin/ scripts and no
        // vendored interpreter — the VM is statically linked (libmruby.a above).
        if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../.."));
            foreach (string Folder in new[] { "Brain/lib", "Brain/mruby" })
            {
                string SourceRoot = Path.Combine(RepoRoot, Folder);
                if (!Directory.Exists(SourceRoot)) continue;
                foreach (string Source in Directory.EnumerateFiles(SourceRoot, "*", SearchOption.AllDirectories))
                    RuntimeDependencies.Add("$(ProjectDir)/" + Folder + "/" + Path.GetRelativePath(SourceRoot, Source).Replace('\\', '/'), Source, StagedFileType.NonUFS);
            }
        }
        RuntimeDependencies.Add("$(ProjectDir)/Content/UI/LoadingView.json", StagedFileType.NonUFS);
        PrivateIncludePaths.Add(ModuleDirectory);
        RuntimeDependencies.Add("$(ProjectDir)/ThirdPartyNotices.txt", StagedFileType.NonUFS);

        PublicIncludePaths.Add(Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../../../Native/include")));
    }
}