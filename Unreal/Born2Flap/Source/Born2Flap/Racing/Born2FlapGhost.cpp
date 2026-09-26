#include "Racing/Born2FlapGhost.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"

ABorn2FlapGhost::ABorn2FlapGhost()
{
    PrimaryActorTick.bCanEverTick = false; // advanced explicitly by the manager
    auto *Root = CreateDefaultSubobject<USceneComponent>(TEXT("GhostRoot"));
    SetRootComponent(Root);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    auto Part = [&](FName Name, USceneComponent *Parent, UStaticMesh *Mesh, FVector Loc, FVector Scale, FRotator Rot) {
        auto *C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        C->SetupAttachment(Parent);
        C->SetStaticMesh(Mesh);
        C->SetRelativeLocation(Loc);
        C->SetRelativeScale3D(Scale);
        C->SetRelativeRotation(Rot);
        if (!HasAnyFlags(RF_ClassDefaultObject))
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return C;
    };
    // A recognisable but simplified ornithopter silhouette — the shadow of the bird.
    Part(TEXT("Fuselage"), Root, Sphere.Object, FVector::ZeroVector, FVector(.9, .25, .27), FRotator::ZeroRotator);
    Part(TEXT("Head"), Root, Sphere.Object, FVector(38, 0, 11), FVector(.3, .23, .25), FRotator::ZeroRotator);
    Part(TEXT("Beak"), Root, Cone.Object, FVector(57, 0, 9), FVector(.12, .12, .3), FRotator(-90, 0, 0));
    for (int Side : {-1, 1})
    {
        auto *Shoulder = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("Shoulder%d"), Side));
        Shoulder->SetupAttachment(Root);
        Shoulder->SetRelativeLocation(FVector(3, Side * 12, 6));
        if (Side < 0)
            LeftShoulder = Shoulder;
        else
            RightShoulder = Shoulder;
        Part(*FString::Printf(TEXT("Wing%d"), Side), Shoulder, Sphere.Object, FVector(-4, Side * 38, 0),
             FVector(.45, .85, .045), FRotator(0, Side * 8, 0));
        for (int I = 0; I < 3; ++I)
            Part(*FString::Printf(TEXT("Feather%d_%d"), Side, I), Shoulder, Sphere.Object,
                 FVector(-16 - I * 3, Side * (49 + I * 9), -1), FVector(.43 - I * .04, .2, .035),
                 FRotator(0, Side * (15 + I * 7), 0));
        Part(*FString::Printf(TEXT("Tail%d"), Side), Root, Sphere.Object, FVector(-54, Side * 12, 3),
             FVector(.4, .18, .035), FRotator(0, Side * 24, 0));
    }
}

void ABorn2FlapGhost::SetupGhost(const FB2FGhostRecording &InRecording, bool bIsChampion)
{
    Recording = InRecording;
    bChampion = bIsChampion;
    bReady = Recording.IsValid();
    GhostTime = 0;
    const FString MaterialPath =
        bChampion ? TEXT("/Game/Training/M_Gold") : TEXT("/Game/Training/M_Ink");
    UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    TArray<UStaticMeshComponent *> Parts;
    GetComponents(Parts);
    for (auto *Part : Parts)
    {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Material)
            Part->SetMaterial(0, Material);
    }
    if (bReady)
    {
        const FB2FGhostFrame &F = Recording.Frames[0];
        SetActorLocation(F.Position);
        SetActorRotation(F.Rotation);
        LeftShoulder->SetRelativeRotation(FRotator(0, 0, F.LeftFlap));
        RightShoulder->SetRelativeRotation(FRotator(0, 0, -F.RightFlap));
    }
}

void ABorn2FlapGhost::Advance(double DeltaSeconds)
{
    if (!bReady)
        return;
    GhostTime += DeltaSeconds;
    if (GhostTime > Recording.Duration)
    {
        SetActorHiddenInGame(true); // the shadow has landed
        return;
    }
    FVector Pos;
    FQuat Rot;
    float LeftFlap, RightFlap;
    if (Recording.Sample(GhostTime, Pos, Rot, LeftFlap, RightFlap))
    {
        SetActorLocation(Pos);
        SetActorRotation(Rot);
        LeftShoulder->SetRelativeRotation(FRotator(0, 0, LeftFlap));
        RightShoulder->SetRelativeRotation(FRotator(0, 0, -RightFlap));
    }
}
