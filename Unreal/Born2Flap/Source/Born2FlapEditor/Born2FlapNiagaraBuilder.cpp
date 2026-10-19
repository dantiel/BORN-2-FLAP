#include "Born2FlapNiagaraBuilder.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraCommon.h"
#include "NiagaraConstants.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/MaterialInterface.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

#define LOCTEXT_NAMESPACE "Born2FlapNiagaraBuilder"

namespace
{
    UNiagaraNodeFunctionCall* AddModule(const TCHAR* AssetPath, UNiagaraNodeOutput& Out)
    {
        FSoftObjectPath Path(AssetPath);
        UObject* Obj = Path.TryLoad();
        UNiagaraScript* Script = Cast<UNiagaraScript>(Obj);
        if (!Script)
        {
            UE_LOG(LogTemp, Error, TEXT("[NiagaraBuilder] Missing module: %s"), AssetPath);
            return nullptr;
        }
        UNiagaraNodeFunctionCall* Node = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
            Script, Out, INDEX_NONE, FString(), Script->GetExposedVersion().VersionGuid);
        UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] + module %s -> %s"),
            AssetPath, Node ? *Node->GetFunctionName() : TEXT("FAILED"));
        return Node;
    }

    FName BareInputName(FName In)
    {
        FString S = In.ToString();
        int32 Dot = INDEX_NONE;
        if (S.FindLastChar(TEXT('.'), Dot)) { S = S.RightChop(Dot + 1); }
        return FName(*S);
    }

    void GetInputs(UNiagaraNodeFunctionCall& Node, TArray<FNiagaraVariable>& OutVars)
    {
        TSet<FNiagaraVariable> Hidden;
        FCompileConstantResolver Resolver;
        FNiagaraStackGraphUtilities::GetStackFunctionInputs(Node, OutVars, Hidden, Resolver,
            FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, /*bIgnoreDisabled=*/false);
    }

    void LogStackInputs(UNiagaraNodeFunctionCall& Node, const FString& Label)
    {
        TArray<FNiagaraVariable> Vars;
        GetInputs(Node, Vars);
        for (const FNiagaraVariable& V : Vars)
        {
            UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder]   %s input: %s (%s)"),
                *Label, *V.GetName().ToString(), *V.GetType().GetName());
        }
    }

    // Exact-first discovery of a module input by bare name.
    bool DiscoverInput(UNiagaraNodeFunctionCall& Node, const TArray<FString>& Candidates,
        FName& OutName, FString& OutTypeName)
    {
        TArray<FNiagaraVariable> Vars;
        GetInputs(Node, Vars);

        for (const FNiagaraVariable& V : Vars)
        {
            FString Bare = BareInputName(V.GetName()).ToString();
            for (const FString& C : Candidates)
            {
                if (Bare.Equals(C, ESearchCase::IgnoreCase))
                {
                    OutName = BareInputName(V.GetName());
                    OutTypeName = V.GetType().GetName();
                    return true;
                }
            }
        }
        for (const FNiagaraVariable& V : Vars)
        {
            FString Bare = BareInputName(V.GetName()).ToString();
            for (const FString& C : Candidates)
            {
                if (Bare.Contains(C, ESearchCase::IgnoreCase))
                {
                    OutName = BareInputName(V.GetName());
                    OutTypeName = V.GetType().GetName();
                    return true;
                }
            }
        }
        return false;
    }

    template<typename T>
    void SetParam(const FString& EmitterName, UNiagaraScript& Script, UNiagaraNodeFunctionCall& Node,
        FName InputName, FNiagaraTypeDefinition Type, const T& Value)
    {
        FNiagaraParameterHandle H = FNiagaraParameterHandle::CreateModuleParameterHandle(InputName);
        FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(H, &Node);
        FNiagaraVariable InVar(Type, Aliased.GetParameterHandleString());
        FNiagaraVariable RIP = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
            InVar, *EmitterName, Script.GetUsage());
        RIP.SetValue(Value);
        const bool bAddIfMissing = true;
        Script.RapidIterationParameters.SetParameterData(RIP.GetData(), RIP, bAddIfMissing);
    }

    template<typename T>
    bool SetMatched(const FString& EmitterName, UNiagaraScript& Script, UNiagaraNodeFunctionCall& Node,
        const TArray<FString>& Candidates, FNiagaraTypeDefinition Type, const T& Value, const FString& Label)
    {
        FName Name;
        FString TypeName;
        if (!DiscoverInput(Node, Candidates, Name, TypeName))
        {
            UE_LOG(LogTemp, Warning, TEXT("[NiagaraBuilder] %s: no input matched candidates"), *Label);
            return false;
        }
        SetParam(EmitterName, Script, Node, Name, Type, Value);
        UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] %s: set %s (type=%s)"), *Label, *Name.ToString(), *TypeName);
        return true;
    }

    UNiagaraNodeOutput* OutputNode(UNiagaraScript& Script)
    {
        return FNiagaraEditorUtilities::GetScriptOutputNode(Script);
    }

    struct FBuildSpec
    {
        FString AssetName;
        float SpawnRate = 1000.0f;
        FVector3f Velocity = FVector3f(0, 0, -500.0f);
        bool bBox = true;
        FVector3f BoxSize = FVector3f(2000, 2000, 1400);
        FVector3f BoxOffset = FVector3f(0, 0, 800);
        float SphereRadius = 500.0f;
        bool bGravity = false;
        FVector3f Gravity = FVector3f(0, 0, -980.0f);
        bool bDrag = false;
        float Drag = 0.0f;
        bool bCurlNoise = false;
        float NoiseStrength = 400.0f;
    };

    UNiagaraSystem* BuildSystem(const FString& PackagePath, const FBuildSpec& S)
    {
        UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] === build %s ==="), *S.AssetName);

        UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(),
            *(S.AssetName + TEXT("_Emitter")), RF_Transactional);
        UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, /*bAddDefaultModulesAndRenderers=*/false);

        FVersionedNiagaraEmitterData* Data = Emitter->GetLatestEmitterData();
        const FString EmitterName = Emitter->GetUniqueEmitterName();

        UNiagaraSpriteRendererProperties* SpriteRenderer =
            NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("Renderer"));
        // A bare sprite renderer carries no material — the particles would draw
        // as unlit dark quads. Bind the engine's Niagara default soft sprite
        // (the exact asset the editor assigns when a sprite renderer is added
        // by hand) so rain, snow and the insect swarm render as soft points.
        SpriteRenderer->Material = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
        Emitter->AddRenderer(SpriteRenderer, Data->Version.VersionGuid);

        UNiagaraScript* EmitterUpdate = Data->EmitterUpdateScriptProps.Script;
        UNiagaraScript* ParticleSpawn = Data->SpawnScriptProps.Script;
        UNiagaraScript* ParticleUpdate = Data->UpdateScriptProps.Script;

        UNiagaraNodeOutput* EmitterUpdateOut = OutputNode(*EmitterUpdate);
        UNiagaraNodeOutput* ParticleSpawnOut = OutputNode(*ParticleSpawn);
        UNiagaraNodeOutput* ParticleUpdateOut = OutputNode(*ParticleUpdate);

        AddModule(TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"), *EmitterUpdateOut);
        UNiagaraNodeFunctionCall* SpawnRateNode = AddModule(TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), *EmitterUpdateOut);
        if (SpawnRateNode)
        {
            LogStackInputs(*SpawnRateNode, TEXT("SpawnRate"));
            SetMatched(EmitterName, *EmitterUpdate, *SpawnRateNode,
                { TEXT("SpawnRate") }, FNiagaraTypeDefinition::GetFloatDef(), S.SpawnRate, TEXT("SpawnRate"));
        }

        if (S.bBox)
        {
            UNiagaraNodeFunctionCall* Loc = AddModule(TEXT("/Niagara/Modules/Spawn/Location/BoxLocation.BoxLocation"), *ParticleSpawnOut);
            if (Loc)
            {
                LogStackInputs(*Loc, TEXT("BoxLocation"));
                SetMatched(EmitterName, *ParticleSpawn, *Loc,
                    { TEXT("Box Size"), TEXT("BoxSize"), TEXT("Box Extents"), TEXT("BoxExtent"), TEXT("Extents"), TEXT("Extent") },
                    FNiagaraTypeDefinition::GetVec3Def(), S.BoxSize, TEXT("BoxSize"));
                SetMatched(EmitterName, *ParticleSpawn, *Loc,
                    { TEXT("Box Offset"), TEXT("BoxOffset"), TEXT("Offset") },
                    FNiagaraTypeDefinition::GetVec3Def(), S.BoxOffset, TEXT("BoxOffset"));
            }
        }
        else
        {
            UNiagaraNodeFunctionCall* Loc = AddModule(TEXT("/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation"), *ParticleSpawnOut);
            if (Loc)
            {
                LogStackInputs(*Loc, TEXT("SphereLocation"));
                SetMatched(EmitterName, *ParticleSpawn, *Loc,
                    { TEXT("Sphere Radius"), TEXT("SphereRadius"), TEXT("Radius") },
                    FNiagaraTypeDefinition::GetFloatDef(), S.SphereRadius, TEXT("SphereRadius"));
            }
        }

        UNiagaraNodeFunctionCall* Vel = AddModule(TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"), *ParticleSpawnOut);
        if (Vel)
        {
            LogStackInputs(*Vel, TEXT("AddVelocity"));
            SetMatched(EmitterName, *ParticleSpawn, *Vel,
                { TEXT("Velocity") }, FNiagaraTypeDefinition::GetVec3Def(), S.Velocity, TEXT("Velocity"));
        }

        {
            TArray<FNiagaraVariable> Vars =
            {
                SYS_PARAM_PARTICLES_SPRITE_SIZE,
                SYS_PARAM_PARTICLES_SPRITE_ROTATION,
                SYS_PARAM_PARTICLES_LIFETIME
            };
            TArray<FString> Defaults =
            {
                FNiagaraConstants::GetAttributeDefaultValue(SYS_PARAM_PARTICLES_SPRITE_SIZE),
                FNiagaraConstants::GetAttributeDefaultValue(SYS_PARAM_PARTICLES_SPRITE_ROTATION),
                FNiagaraConstants::GetAttributeDefaultValue(SYS_PARAM_PARTICLES_LIFETIME)
            };
            FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars, *ParticleSpawnOut, INDEX_NONE, Defaults);
        }

        AddModule(TEXT("/Niagara/Modules/Update/Lifetime/UpdateAge.UpdateAge"), *ParticleUpdateOut);
        AddModule(TEXT("/Niagara/Modules/Update/Color/Color.Color"), *ParticleUpdateOut);
        AddModule(TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), *ParticleUpdateOut);

        if (S.bGravity)
        {
            UNiagaraNodeFunctionCall* G = AddModule(TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"), *ParticleUpdateOut);
            if (G)
            {
                LogStackInputs(*G, TEXT("GravityForce"));
                SetMatched(EmitterName, *ParticleUpdate, *G,
                    { TEXT("Gravity Acceleration"), TEXT("GravityAcceleration"), TEXT("Acceleration"), TEXT("Gravity") },
                    FNiagaraTypeDefinition::GetVec3Def(), S.Gravity, TEXT("Gravity"));
            }
        }
        if (S.bDrag)
        {
            UNiagaraNodeFunctionCall* D = AddModule(TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), *ParticleUpdateOut);
            if (D)
            {
                LogStackInputs(*D, TEXT("Drag"));
                SetMatched(EmitterName, *ParticleUpdate, *D,
                    { TEXT("Drag") }, FNiagaraTypeDefinition::GetFloatDef(), S.Drag, TEXT("Drag"));
            }
        }
        if (S.bCurlNoise)
        {
            UNiagaraNodeFunctionCall* C = AddModule(TEXT("/Niagara/Modules/Update/Forces/CurlNoiseForce.CurlNoiseForce"), *ParticleUpdateOut);
            if (C)
            {
                LogStackInputs(*C, TEXT("CurlNoiseForce"));
                SetMatched(EmitterName, *ParticleUpdate, *C,
                    { TEXT("Noise Strength"), TEXT("NoiseStrength"), TEXT("Strength") },
                    FNiagaraTypeDefinition::GetFloatDef(), S.NoiseStrength, TEXT("NoiseStrength"));
            }
        }

        UPackage* Pkg = CreatePackage(*PackagePath);
        UNiagaraSystem* System = NewObject<UNiagaraSystem>(Pkg, *S.AssetName, RF_Public | RF_Standalone);
        UNiagaraSystemFactoryNew::InitializeSystem(System, true);
        FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter,
            Emitter->GetExposedVersion().VersionGuid, /*bCreateCopy=*/true);

        FAssetRegistryModule::AssetCreated(System);
        System->MarkPackageDirty();
        Pkg->MarkAsFullyLoaded();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
        const bool bSaved = UPackage::SavePackage(Pkg, System, *FilePath, SaveArgs);
        UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] saved %s -> %s (%s)"),
            *S.AssetName, *FilePath, bSaved ? TEXT("OK") : TEXT("FAILED"));

        return System;
    }
}

UBorn2FlapNiagaraBuilderCommandlet::UBorn2FlapNiagaraBuilderCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UBorn2FlapNiagaraBuilderCommandlet::Main(const FString& Params)
{
    UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] === START ==="));

    {
        FBuildSpec S;
        S.AssetName = TEXT("NS_Rain");
        S.SpawnRate = 2500.0f;
        S.Velocity = FVector3f(0, 0, -1500.0f);
        S.BoxSize = FVector3f(2000, 2000, 1400);
        S.BoxOffset = FVector3f(0, 0, 800);
        S.bGravity = true;
        S.Gravity = FVector3f(0, 0, -400.0f);
        BuildSystem(TEXT("/Game/Weather/NS_Rain"), S);
    }

    {
        FBuildSpec S;
        S.AssetName = TEXT("NS_Snow");
        S.SpawnRate = 1200.0f;
        S.Velocity = FVector3f(0, 0, -180.0f);
        S.BoxSize = FVector3f(2400, 2400, 1800);
        S.BoxOffset = FVector3f(0, 0, 900);
        S.bGravity = true;
        S.Gravity = FVector3f(0, 0, -60.0f);
        S.bDrag = true;
        S.Drag = 0.5f;
        BuildSystem(TEXT("/Game/Weather/NS_Snow"), S);
    }

    {
        FBuildSpec S;
        S.AssetName = TEXT("NS_InsectSwarm");
        S.SpawnRate = 400.0f;
        S.Velocity = FVector3f(0, 0, 60.0f);
        S.bBox = false;
        S.SphereRadius = 550.0f;
        S.bDrag = true;
        S.Drag = 0.3f;
        S.bCurlNoise = true;
        S.NoiseStrength = 600.0f;
        BuildSystem(TEXT("/Game/Nature/Insects/NS_InsectSwarm"), S);
    }

    UE_LOG(LogTemp, Display, TEXT("[NiagaraBuilder] === DONE ==="));
    return 0;
}

#undef LOCTEXT_NAMESPACE