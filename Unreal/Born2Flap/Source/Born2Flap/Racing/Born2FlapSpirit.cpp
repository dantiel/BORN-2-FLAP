#include "Racing/Born2FlapSpirit.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "Flight/Born2FlapWingMesh.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

ABorn2FlapSpirit::ABorn2FlapSpirit()
{
    PrimaryActorTick.bCanEverTick = false; // advanced explicitly by the manager
    auto *Root = CreateDefaultSubobject<USceneComponent>(TEXT("SpiritRoot"));
    SetRootComponent(Root);
}

namespace
{
// Tint every rendered part (procedural shards + static-mesh bodies/fuselage +
// membrane wings) with the ghost material, and kill collision.
void ApplyGhostMaterial(AActor* Owner, UMaterialInterface* Material)
{
    TArray<UProceduralMeshComponent*> Procedural;
    Owner->GetComponents(Procedural);
    for (auto *Part : Procedural)
    {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Material)
            Part->SetMaterial(0, Material);
    }
    TArray<UStaticMeshComponent*> StaticParts;
    Owner->GetComponents(StaticParts);
    for (auto *Part : StaticParts)
    {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Material)
            Part->SetMaterial(0, Material);
    }
}
} // namespace

void ABorn2FlapSpirit::BuildPrototype()
{
    // Mirrors ABorn2FlapFlightPawn::BuildGeometry (the training "Prototype"
    // silhouette) but built at runtime with dynamic components.
    auto *PrototypeRoot = NewObject<USceneComponent>(this, TEXT("PrototypeGhost"));
    AddInstanceComponent(PrototypeRoot);
    PrototypeRoot->SetupAttachment(GetRootComponent());
    PrototypeRoot->SetRelativeLocation(FVector::ZeroVector);
    PrototypeRoot->RegisterComponent();

    auto *Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto *Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    auto Part = [&](FName Name, USceneComponent *Parent, UStaticMesh *Mesh, FVector Loc, FVector Scale, FRotator Rot)
    {
        auto *C = NewObject<UStaticMeshComponent>(this, Name);
        AddInstanceComponent(C);
        C->SetupAttachment(Parent);
        C->SetStaticMesh(Mesh);
        C->SetRelativeLocation(Loc);
        C->SetRelativeScale3D(Scale);
        C->SetRelativeRotation(Rot);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->RegisterComponent();
        return C;
    };

    if (Sphere) Part(TEXT("Fuselage"), PrototypeRoot, Sphere, FVector::ZeroVector, FVector(.95, .27, .29), FRotator::ZeroRotator);
    if (Sphere) Part(TEXT("Head"), PrototypeRoot, Sphere, FVector(40, 0, 12), FVector(.32, .25, .27), FRotator::ZeroRotator);
    if (Cone) Part(TEXT("Beak"), PrototypeRoot, Cone, FVector(59, 0, 10), FVector(.12, .12, .3), FRotator(-90, 0, 0));
    if (Sphere) Part(TEXT("TailBoom"), PrototypeRoot, Sphere, FVector(-59, 0, 0), FVector(.65, .07, .07), FRotator::ZeroRotator);

    for (int Side : {-1, 1})
    {
        if (Sphere) Part(*FString::Printf(TEXT("Eye%d"), Side), PrototypeRoot, Sphere, FVector(48, Side * 10, 17), FVector(.065, .04, .065), FRotator::ZeroRotator);

        auto *Shoulder = NewObject<USceneComponent>(this, *FString::Printf(TEXT("ProtoShoulder%d"), Side));
        AddInstanceComponent(Shoulder);
        Shoulder->SetupAttachment(PrototypeRoot);
        Shoulder->SetRelativeLocation(FVector(3, Side * 12, 10));
        Shoulder->RegisterComponent();
        if (Side < 0) LeftShoulder = Shoulder; else RightShoulder = Shoulder;

        auto *Wing = NewObject<UBorn2FlapWingMesh>(this, *FString::Printf(TEXT("ProtoWing%d"), Side));
        AddInstanceComponent(Wing);
        Wing->SetupAttachment(Shoulder);
        Wing->RegisterComponent();
        Wing->InitializeWing(Side, 1);
    }
}

void ABorn2FlapSpirit::SetupSpirit(const FB2FSpiritRecording &InRecording, bool bIsChampion)
{
    Recording = InRecording;
    bChampion = bIsChampion;
    bReady = Recording.IsValid();
    SpiritTime = 0;
    BirdModel = FMath::Clamp(Recording.BirdModel, 0, 2);

    // Rebuild the same airframe the player flew, so the ghost matches the
    // original model instead of always appearing as the raven-crow.
    if (!LeftShoulder)
    {
        switch (BirdModel)
        {
        case 2:
            RavenRoot = Born2FlapRaven::Build(this, GetRootComponent(), LeftShoulder, RightShoulder, TailPivot, 2);
            break;
        case 1:
            BuildPrototype();
            break;
        default:
            RavenRoot = Born2FlapRaven::Build(this, GetRootComponent(), LeftShoulder, RightShoulder, TailPivot, 1);
            break;
        }
    }

    const FString MaterialPath =
        bChampion ? TEXT("/Game/Training/M_Gold") : TEXT("/Game/Training/M_Spirit");
    UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    ApplyGhostMaterial(this, Material);

    if (bReady)
    {
        const FB2FSpiritFrame &F = Recording.Frames[0];
        SetActorLocation(F.Position);
        SetActorRotation(F.Rotation);
        if (LeftShoulder) LeftShoulder->SetRelativeRotation(FRotator(0, 0, F.LeftFlap + 16));
        if (RightShoulder) RightShoulder->SetRelativeRotation(FRotator(0, 0, -F.RightFlap - 16));
    }
}

void ABorn2FlapSpirit::Advance(double DeltaSeconds)
{
    if (!bReady)
        return;
    SpiritTime += DeltaSeconds;
    if (SpiritTime > Recording.Duration)
    {
        SetActorHiddenInGame(true); // the ghost has landed
        return;
    }
    FVector Pos;
    FQuat Rot;
    float LeftFlap, RightFlap;
    if (Recording.Sample(SpiritTime, Pos, Rot, LeftFlap, RightFlap))
    {
        SetActorLocation(Pos);
        SetActorRotation(Rot);
        if (LeftShoulder) LeftShoulder->SetRelativeRotation(FRotator(0, 0, LeftFlap + 16));
        if (RightShoulder) RightShoulder->SetRelativeRotation(FRotator(0, 0, -RightFlap - 16));
    }
}
