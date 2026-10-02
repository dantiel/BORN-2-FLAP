// BORN 2 FLAP — Shiomori Bay coastal water system.
#include "Water/Born2FlapWaterDirector.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Scalability.h"
#include "Kismet/GameplayStatics.h"

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
    for (TActorIterator<AShiomoriWaterDirector> It(World); It; ++It)
        return *It;
    return nullptr;
}

void AShiomoriWaterDirector::BeginPlay()
{
    Super::BeginPlay();

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
}

float AShiomoriWaterDirector::GetWaterHeightAt(float X, float Y) const
{
    return Parameters.OceanLevel + Parameters.SampleWaveHeight(FVector2D(X, Y), TimeSeconds);
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
}

void AShiomoriWaterDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TimeSeconds = GetWorld()->GetTimeSeconds();

    if (!MPCInstance || !MPCInstance->GetCollection())
        return;

    using namespace Born2FlapWater;
    MPCInstance->SetScalarParameterValue(WaveTime, TimeSeconds);

    // Quality level from the engine's effects scalability (0=Low .. 3=Cinematic).
    const int32 Quality = FMath::Clamp(Scalability::GetQualityLevels().EffectsQuality, 0, 3);
    MPCInstance->SetScalarParameterValue(QualityLevel, static_cast<float>(Quality));

    // Distance-based LOD (0 = very near, 1 = far). Smoothed, configurable falloff.
    if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        const FVector Cam = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
        const float Dist = FVector2D::Distance(FVector2D(Cam.X, Cam.Y), FVector2D(GetActorLocation().X, GetActorLocation().Y));
        const float LOD = FMath::Clamp((Dist - 2000.0f) / 18000.0f, 0.0f, 1.0f);
        MPCInstance->SetScalarParameterValue(WaterLOD, LOD);
    }
}