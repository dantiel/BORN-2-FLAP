#include "Racing/Born2FlapSpirit.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

ABorn2FlapSpirit::ABorn2FlapSpirit()
{
    PrimaryActorTick.bCanEverTick = false; // advanced explicitly by the manager
    auto *Root = CreateDefaultSubobject<USceneComponent>(TEXT("SpiritRoot"));
    SetRootComponent(Root);
}

void ABorn2FlapSpirit::SetupSpirit(const FB2FSpiritRecording &InRecording, bool bIsChampion)
{
    Recording = InRecording;
    bChampion = bIsChampion;
    bReady = Recording.IsValid();
    SpiritTime = 0;

    // Build the exact same folded-shard raven-crow airframe as the player bird,
    // so the shadow doppelgänger is a true shadow of the original model.
    if (!RavenRoot)
        RavenRoot = Born2FlapRaven::Build(this, GetRootComponent(), LeftShoulder, RightShoulder, TailPivot);

    const FString MaterialPath =
        bChampion ? TEXT("/Game/Training/M_Gold") : TEXT("/Game/Training/M_Ink");
    UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    TArray<UProceduralMeshComponent *> Parts;
    GetComponents(Parts);
    for (auto *Part : Parts)
    {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Material)
            Part->SetMaterial(0, Material);
    }
    if (bReady)
    {
        const FB2FSpiritFrame &F = Recording.Frames[0];
        SetActorLocation(F.Position);
        SetActorRotation(F.Rotation);
        LeftShoulder->SetRelativeRotation(FRotator(0, 0, F.LeftFlap + 16));
        RightShoulder->SetRelativeRotation(FRotator(0, 0, -F.RightFlap - 16));
    }
}

void ABorn2FlapSpirit::Advance(double DeltaSeconds)
{
    if (!bReady)
        return;
    SpiritTime += DeltaSeconds;
    if (SpiritTime > Recording.Duration)
    {
        SetActorHiddenInGame(true); // the shadow has landed
        return;
    }
    FVector Pos;
    FQuat Rot;
    float LeftFlap, RightFlap;
    if (Recording.Sample(SpiritTime, Pos, Rot, LeftFlap, RightFlap))
    {
        SetActorLocation(Pos);
        SetActorRotation(Rot);
        LeftShoulder->SetRelativeRotation(FRotator(0, 0, LeftFlap + 16));
        RightShoulder->SetRelativeRotation(FRotator(0, 0, -RightFlap - 16));
    }
}