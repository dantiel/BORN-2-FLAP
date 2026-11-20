// BORN 2 FLAP — Shiomori Bay coastal water system.
#include "Water/Born2FlapWaterDirector.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Scalability.h"
#include "Kismet/GameplayStatics.h"
#include "Game/Born2FlapGameMode.h"
#include "World/Born2FlapWeather.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "GerstnerWaterWaves.h"
#include "WaterWaves.h"
#include "WaterTerrainComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

AShiomoriWaterDirector::AShiomoriWaterDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    // Default Shiomori MPC (created by create_shiomori_map.py).
    OceanMPC = TSoftObjectPtr<UMaterialParameterCollection>(
        FSoftObjectPath(TEXT("/Game/Shiomori/Materials/MPC_WaterGlobal.MPC_WaterGlobal")));
}

AShiomoriWaterDirector* AShiomoriWaterDirector::Get(const UWorld* World)
{
    if (!World)
        return nullptr;
    TActorIterator<AShiomoriWaterDirector> It(World);
    return It ? *It : nullptr;
}

void AShiomoriWaterDirector::BeginPlay()
{
    Super::BeginPlay();

    // Shiomori has no Landscape under the bay — only procedural meshes. The
    // native Water system's CPU depth query falls back to r.Water.OceanFallbackDepth
    // (3000 cm) when it finds no terrain, which reports the shallow ~2.5 m bay as
    // deep ocean. Clamp the fallback to the real bay depth (ocean Z=-50, seabed
    // Z=-300). The rendered Single Layer Water depth comes from the Water Info
    // Texture, which the UWaterTerrainComponent on the Seabed mesh feeds.
    if (IConsoleVariable* OceanDepthCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Water.OceanFallbackDepth")))
    {
        OceanDepthCVar->Set(250.0f, ECVF_SetByCode);
    }

    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UMeshComponent*> Meshes(*It);
        for(auto* Mesh:Meshes) for(int I=0;I<Mesh->GetNumMaterials();++I)
        {
            const auto* Material=Mesh->GetMaterial(I);
            if(!Material) continue;
            const FString Path=Material->GetPathName();
            if(Path.StartsWith(TEXT("/Game/Shiomori/Materials/M_Water.")) ||
               Path.StartsWith(TEXT("/Game/Shiomori/Materials/M_Shallows.")) ||
               Path.StartsWith(TEXT("/Game/Shiomori/Materials/M_Foam.")))
                Mesh->SetTranslucentSortPriority(-20);
        }
    }

    // Optional authored parameter asset overrides the inline defaults.
    if (!ParametersAsset.IsNull())
    {
        if (UOceanParametersAsset* Asset = ParametersAsset.LoadSynchronous())
            Parameters = Asset->Parameters;
    }

    if (!OceanMPC.IsNull())
    {
        LoadedMPC = OceanMPC.LoadSynchronous();
        if (LoadedMPC)
        {
            MPCInstance = GetWorld()->GetParameterCollectionInstance(LoadedMPC);
        }
    }

    ApplyParameters();
    if (bUseNativeOcean)
    {
        SpawnNativeOcean();
    }
}

float AShiomoriWaterDirector::GetWaterHeightAt(float X, float Y) const
{
    // Prefer the native Gerstner field — identical to the rendered water mesh —
    // so splash detection, surf and wetness stay locked to the visible surface.
    if (OceanWaves)
    {
        return Parameters.OceanLevel
            + OceanWaves->GetSimpleWaveHeightAtPosition(FVector(X, Y, 0.0f), 10000.0f, TimeSeconds);
    }
    return Parameters.OceanLevel + Parameters.SampleWaveHeightFast(FVector2D(X, Y), TimeSeconds);
}

void AShiomoriWaterDirector::SpawnNativeOcean()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    OceanBody = World->SpawnActor<AWaterBodyOcean>(
        AWaterBodyOcean::StaticClass(),
        FVector(0.0f, 0.0f, Parameters.OceanLevel),
        FRotator::ZeroRotator, SpawnParams);
    if (!OceanBody)
        return;

    // Water presence is analytic (GroundHeight) and the ornithopter glides over
    // it — never let the ocean's collision box trap or block the bird.
    OceanBody->SetActorEnableCollision(false);

    // Single Layer Water surface: prefer our MIC (ocean-foam switch enabled,
    // bright shallow-water tuning), fall back to the plugin default material.
    UMaterialInterface* OceanMat = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Shiomori/Materials/MI_OceanNative.MI_OceanNative"));
    if (!OceanMat)
    {
        OceanMat = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Water/Materials/WaterSurface/Water_Material_Ocean.Water_Material_Ocean"));
    }
    if (OceanMat)
    {
        OceanMID = UMaterialInstanceDynamic::Create(OceanMat, this);
        if (UWaterBodyComponent* Body = OceanBody->GetWaterBodyComponent())
            Body->SetWaterMaterial(OceanMID);
    }

    // Gerstner spectrum configured from the authorable ocean parameters.
    OceanWaves = NewObject<UGerstnerWaterWaves>(this, TEXT("ShiomoriOceanWaves"));
    if (OceanWaves)
    {
        if (UGerstnerWaterWaveGeneratorSimple* Gen =
            Cast<UGerstnerWaterWaveGeneratorSimple>(OceanWaves->GerstnerWaveGenerator))
        {
            Gen->Seed = 11;
            Gen->NumWaves = 24;
            Gen->MinWavelength = FMath::Max(1.0f, Parameters.SwellLength * 0.08f);
            Gen->MaxWavelength = Parameters.SwellLength;
            Gen->WavelengthFalloff = 2.0f;
            Gen->MinAmplitude = FMath::Max(0.5f, Parameters.SwellAmplitude * 0.03f);
            Gen->MaxAmplitude = Parameters.SwellAmplitude;
            Gen->AmplitudeFalloff = 2.0f;
            Gen->WindAngleDeg = Parameters.PrimaryWaveDirection;
            Gen->DirectionAngularSpreadDeg = 24.0f;
        }
        OceanWaves->RecomputeWaves(true);
        OceanBody->SetWaterWaves(OceanWaves);
    }


    // Register the procedural seabed as terrain for the native Water system. The
    // Water Info Texture (which drives Single Layer Water depth + absorption)
    // reads ground height from Landscapes or UWaterTerrainComponent primitives.
    // Shiomori has no Landscape, so without this the ocean falls back to the
    // 3000 cm deep-ocean depth and renders black. The Seabed mesh already encodes
    // the real 0..2.5 m depth ramp (Z=-50 at the shore → Z=-300 in the open sea).
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* SMA = *It;
        if (SMA && SMA->ActorHasTag(TEXT("Seabed")))
        {
            UWaterTerrainComponent* Terrain = NewObject<UWaterTerrainComponent>(SMA);
            SMA->AddInstanceComponent(Terrain);
            Terrain->RegisterComponent();
            break;
        }
    }

    HideLegacyOceanMesh();
}

void AShiomoriWaterDirector::HideLegacyOceanMesh()
{
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        AStaticMeshActor* SMA = *It;
        UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent();
        if (!SMC)
            continue;
        const UStaticMesh* Mesh = SMC->GetStaticMesh();
        if (!Mesh)
            continue;
        if (Mesh->GetName().Contains(TEXT("OceanGrid")))
        {
            SMA->SetActorHiddenInGame(true);
            SMC->SetVisibility(false, true);
        }
    }
}

void AShiomoriWaterDirector::ApplyParameters()
{
    if (!MPCInstance || !MPCInstance->GetCollection())
        return;

    using namespace Born2FlapWater;
    MPCInstance->SetScalarParameterValue(OceanLevel,           Parameters.OceanLevel);
    MPCInstance->SetScalarParameterValue(PrimaryWaveDirection, Parameters.PrimaryWaveDirection);
    MPCInstance->SetScalarParameterValue(WindDirection,        Parameters.WindDirection);
    MPCInstance->SetScalarParameterValue(WindSpeed,            Parameters.WindSpeed);
    MPCInstance->SetScalarParameterValue(SwellAmplitude,       Parameters.SwellAmplitude);
    MPCInstance->SetScalarParameterValue(SwellLength,          Parameters.SwellLength);
    MPCInstance->SetScalarParameterValue(SwellSpeed,           Parameters.SwellSpeed);
    MPCInstance->SetScalarParameterValue(SeaState,             Parameters.SeaState);
    MPCInstance->SetScalarParameterValue(FoamAmount,           Parameters.FoamAmount);
    MPCInstance->SetScalarParameterValue(StormAmount,          Parameters.StormAmount);
    MPCInstance->SetScalarParameterValue(ShoreBreakIntensity,  Parameters.ShoreBreakIntensity);
    MPCInstance->SetScalarParameterValue(SunMirror,           Parameters.SunMirror);
}

void AShiomoriWaterDirector::PushOceanVisuals()
{
    if (!OceanMID)
        return;
    // Bright shallow-bay tuning for the SLW shading model. STRONG scattering
    // guarantees luminous teal at any depth: the SLW output blends the scene
    // through exp(-Absorption*depth) toward the scattering colour, so a rich
    // scattering term means the water can never render as the near-black
    // deep-ocean slab again — even if the absorption units are cm-based.
    OceanMID->SetVectorParameterValue(TEXT("Water Albedo"), FLinearColor(0.03f, 0.20f, 0.26f, 1.0f));
    OceanMID->SetVectorParameterValue(TEXT("Scattering"),   FLinearColor(0.05f, 0.22f, 0.28f, 1.0f));
    OceanMID->SetVectorParameterValue(TEXT("Absorption"),   FLinearColor(0.0012f, 0.0028f, 0.0050f, 1.0f));
    OceanMID->SetScalarParameterValue(TEXT("Water Roughness"), 0.05f);
    OceanMID->SetScalarParameterValue(TEXT("Water Specular"), 1.0f);
    OceanMID->SetScalarParameterValue(TEXT("Refraction"), 1.12f);
    // Wind-driven ocean foam: more wind -> brighter, longer foam trails.
    OceanMID->SetScalarParameterValue(TEXT("Foam Boost"),
        FMath::Clamp(0.6f + Parameters.FoamAmount * 0.7f, 0.6f, 2.5f));
}

void AShiomoriWaterDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TimeSeconds = GetWorld()->GetTimeSeconds();
    if(auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        const FVector Wind=GM->WindAt(GetActorLocation(),TimeSeconds);
                Parameters.WindSpeed=Wind.Size2D();
                Parameters.WindDirection=FMath::RadiansToDegrees(FMath::Atan2(Wind.Y,Wind.X));
                // The dominant swell weathervanes into the wind: long ocean waves turn
                // so the big crest lines converge on the live wind direction while the
                // streak field follows instantly. This is what makes the wind READ on
                // the water surface (outer ocean waves follow the wind). Fast enough
                // that the open-sea swell visibly obeys a shifting wind.
                const float SwellErr=FMath::UnwindDegrees(Parameters.WindDirection-Parameters.PrimaryWaveDirection);
                Parameters.PrimaryWaveDirection+=SwellErr*FMath::Clamp(DeltaSeconds*0.45f,0.0f,1.0f);
                // Swell size and sea state are wind-determined: a calm breeze leaves a
                // long, low, glassy swell; a fresh wind stacks the waves up and roughens
                // the chop. Strong wind -> big wind sea (up to 1.9 m swell). v2: steeper
                // wind response + higher sea-state ceiling — the surface reads clearly
                // turbulent even in a moderate breeze (both the rendered Gerstner WPO and
                // SampleWaveHeight read the same parameters, so they stay in lock-step).
                Parameters.SwellAmplitude=FMath::Clamp(55.f+Parameters.WindSpeed*7.f,48.f,190.f);
                Parameters.SeaState=FMath::Clamp(2.2f+Parameters.WindSpeed*.45f,2.2f,4.5f);
                Parameters.ShoreBreakIntensity=.85f+Parameters.WindSpeed*.065f;
                Parameters.FoamAmount=1.1f+Parameters.WindSpeed*.08f;
        Parameters.StormAmount=0.f; // these presets intentionally exclude storms
        // The ocean mirror follows the live sky: sunlight grades the fresnel
        // sheen (cloudy 0.55 .. full sun 1.3). The water material reads it via
        // MPC_WaterGlobal.SunMirror every frame. Conditions.Weather is a profile
        // KEY (FString); resolve it through the weather desk's profile table.
        const FBorn2FlapWeather Wx=Born2FlapWeather::Profile(GM->GetConditions().Weather);
        Parameters.SunMirror=FMath::Clamp(0.55f+Wx.Sunlight*0.75f,0.55f,1.3f);
    }

    // Throttle the near-static parameter pushes (11 MPC scalars + ocean MID
    // visuals + quality level) to a 0.25 s cadence. These values only change
    // with wind/sun (slow); pushing them every frame was pure render-thread
    // chatter. WaveTime + WaterLOD stay per-frame below.
    ParamPushAccum += DeltaSeconds;
    if (ParamPushAccum >= 0.25f)
    {
        ParamPushAccum = 0.0f;
        ApplyParameters();
        PushOceanVisuals();
        if (MPCInstance && MPCInstance->GetCollection())
        {
            const int32 Quality = FMath::Clamp(Scalability::GetQualityLevels().EffectsQuality, 0, 3);
            MPCInstance->SetScalarParameterValue(Born2FlapWater::QualityLevel, static_cast<float>(Quality));
        }
    }

    // Wind-driven native ocean waves: re-derive the Gerstner spectrum from the
    // live wind every 0.5 s (direction = wind, amplitude = wind strength).
    WindUpdateAccum += DeltaSeconds;
    if (OceanWaves && WindUpdateAccum >= 0.5f)
    {
        WindUpdateAccum = 0.0f;
        if (UGerstnerWaterWaveGeneratorSimple* Gen =
            Cast<UGerstnerWaterWaveGeneratorSimple>(OceanWaves->GerstnerWaveGenerator))
        {
            Gen->WindAngleDeg = Parameters.PrimaryWaveDirection;
            Gen->MaxAmplitude = Parameters.SwellAmplitude * 1.5f;
            Gen->MinAmplitude = FMath::Max(0.5f, Parameters.SwellAmplitude * 0.04f);
            Gen->MaxWavelength = Parameters.SwellLength;
            Gen->MinWavelength = FMath::Max(1.0f, Parameters.SwellLength * 0.08f);
            // Strong wind = aligned swell (narrow spread); calm = confused sea.
            Gen->DirectionAngularSpreadDeg = FMath::Clamp(36.0f - Parameters.WindSpeed * 2.0f, 14.0f, 40.0f);
            OceanWaves->RecomputeWaves(true);
        }
    }

    if (!MPCInstance || !MPCInstance->GetCollection())
        return;

    using namespace Born2FlapWater;
    MPCInstance->SetScalarParameterValue(WaveTime, TimeSeconds);

    // Distance-based LOD (0 = very near, 1 = far). Smoothed, configurable falloff.
    if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        const FVector Cam = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
        const float Dist = FVector2D::Distance(FVector2D(Cam.X, Cam.Y), FVector2D(GetActorLocation().X, GetActorLocation().Y));
        const float LOD = FMath::Clamp((Dist - 2000.0f) / 18000.0f, 0.0f, 1.0f);
        MPCInstance->SetScalarParameterValue(WaterLOD, LOD);
    }
}