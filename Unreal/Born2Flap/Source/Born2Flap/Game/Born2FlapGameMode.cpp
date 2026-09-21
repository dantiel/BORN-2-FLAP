#include "Game/Born2FlapGameMode.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"

ABorn2FlapGameMode::ABorn2FlapGameMode()
{
    DefaultPawnClass = ABorn2FlapFlightPawn::StaticClass();
}

void ABorn2FlapGameMode::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
        ADirectionalLight::StaticClass(), FVector(0.0, 0.0, 500.0), FRotator(-45.0, -35.0, 0.0), SpawnParams))
    {
        Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sun->GetLightComponent()->SetIntensity(6.0f);
        Sun->GetLightComponent()->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f));
    }

    if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(
        ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
    {
        Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->SetIntensity(0.8f);
        Sky->GetLightComponent()->SetLightColor(FLinearColor(0.55f, 0.68f, 1.0f));
    }

    if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
    {
        if (AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
            AStaticMeshActor::StaticClass(), FVector(0.0, 0.0, -100.0), FRotator::ZeroRotator, SpawnParams))
        {
            Floor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
            Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->SetActorScale3D(FVector(20.0, 20.0, 20.0));
        }
    }
}
