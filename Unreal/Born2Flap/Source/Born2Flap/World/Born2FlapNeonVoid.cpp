#include "World/Born2FlapNeonVoid.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

ABorn2FlapNeonVoid::ABorn2FlapNeonVoid()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Root = CreateDefaultSubobject<USceneComponent>(TEXT("NeonVoidRoot"));
    SetRootComponent(Root);
    // Distant jellyfish swarm layer: cheap instanced specks drifting through the
    // aether, mirroring the swarm/hero split of the terrestrial insect swarms.
    Swarm = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("JellySwarm"));
    Swarm->SetupAttachment(Root);
    Swarm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Swarm->SetCastShadow(false);
    Swarm->SetMobility(EComponentMobility::Movable);
}

void ABorn2FlapNeonVoid::BeginPlay()
{
    Super::BeginPlay();

    // Swarm layer (instanced specks at distance).
    if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        Swarm->SetStaticMesh(Sphere);
        if (UMaterialInterface* Star = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NeonAbyss/M_Star")))
            Swarm->SetMaterial(0, Star);
        for (int32 I = 0; I < 160; ++I)
        {
            const float R = Rng.FRandRange(3000.f, 12000.f);
            const float A = Rng.FRandRange(0.f, 2.f * PI);
            const float Z = Rng.FRandRange(-4000.f, 6000.f);
            FVector P(R * FMath::Cos(A), R * FMath::Sin(A), Z);
            SwarmPositions.Add(P);
            SwarmDrift.Add(FVector(Rng.FRandRange(-30.f, 30.f), Rng.FRandRange(-30.f, 30.f), Rng.FRandRange(-15.f, 15.f)));
            Swarm->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(0.5f + Rng.FRand())));
        }
    }

    // Hero jellyfish: a shell of pulsing, captureable bells around the bronze ring.
    for (int32 I = 0; I < TargetCount * 2; ++I)
    {
        const float R = Rng.FRandRange(2200.f, 8000.f);
        const float A = Rng.FRandRange(0.f, 2.f * PI);
        const float Z = Rng.FRandRange(-1800.f, 3200.f);
        SpawnJellyfish(FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z));
    }
    ActiveCount = Jellies.Num();
}

void ABorn2FlapNeonVoid::SpawnJellyfish(const FVector& Location)
{
    FJelly J;
    J.Root = NewObject<USceneComponent>(this);
    AddInstanceComponent(J.Root);
    J.Root->SetupAttachment(GetRootComponent());
    J.Root->SetRelativeLocation(Location);
    J.Root->RegisterComponent();
    J.Origin = Location;
    J.Phase = FVector(Rng.FRandRange(0.f, 2.f * PI), Rng.FRandRange(0.f, 2.f * PI), Rng.FRandRange(0.f, 2.f * PI));
    // Slow, drifting orbit around the ring axis + a gentle vertical bob.
    const float Speed = Rng.FRandRange(40.f, 90.f);
    const float Dir = Rng.FRandRange(0.f, 2.f * PI);
    J.Drift = FVector(FMath::Cos(Dir) * Speed, FMath::Sin(Dir) * Speed, Rng.FRandRange(-20.f, 20.f));
    J.Scale = Rng.FRandRange(0.7f, 1.3f);

    J.Mesh = NewObject<UProceduralMeshComponent>(this);
    AddInstanceComponent(J.Mesh);
    J.Mesh->SetupAttachment(J.Root);
    J.Mesh->SetRelativeScale3D(FVector(J.Scale));
    J.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    J.Mesh->SetCastShadow(false);
    J.Mesh->RegisterComponent();
    BuildJellyMesh(J.Mesh);
    if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NeonAbyss/M_JellyBell")))
        J.Mesh->SetMaterial(0, M);

    Jellies.Add(J);
}

void ABorn2FlapNeonVoid::BuildJellyMesh(UProceduralMeshComponent* Mesh)
{
    TArray<FVector> V, N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    auto AddTri = [&](FVector A, FVector B, FVector D, FLinearColor Col)
    {
        const int32 Base = V.Num();
        const FVector NN = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
        V.Append({A, B, D});
        N.Append({NN, NN, NN});
        C.Append({Col, Col, Col});
        UV.Append({FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector});
        T.Append({Base, Base + 1, Base + 2});
    };

    // Bell dome (apex up, rim down) — vertex colours pulse from deep to rim-bright.
    const int32 Longs = 20, Lats = 8;
    const float Rx = 14.f, Ry = 14.f, Rz = 12.f;
    const FLinearColor Rim(.96f, .16f, .62f), Apex(.52f, .05f, .38f);
    auto P = [&](int32 J, int32 I) -> FVector
    {
        const double Phi = PI * 0.5 * double(J) / Lats;
        const double Th = 2.0 * PI * double(I) / Longs;
        return FVector(Rx * FMath::Cos(Th) * FMath::Sin(Phi), Ry * FMath::Sin(Th) * FMath::Sin(Phi), Rz * FMath::Cos(Phi));
    };
    for (int32 J = 0; J < Lats; ++J)
        for (int32 I = 0; I < Longs; ++I)
        {
            const FLinearColor Col = FMath::Lerp(Apex, Rim, double(J) / Lats);
            const FVector A = P(J, I), B = P(J + 1, I), D = P(J, I + 1), E = P(J + 1, I + 1);
            AddTri(A, B, D, Col);
            AddTri(B, E, D, Col);
        }

    // Trailing tentacles: tapered ribbons hanging from the rim.
    const int32 Tentacles = 8, Seg = 6;
    const FLinearColor TentCol(.96f, .16f, .62f);
    for (int32 K = 0; K < Tentacles; ++K)
    {
        const double Th = 2.0 * PI * double(K) / Tentacles;
        const float Cx = FMath::Cos(Th), Sx = FMath::Sin(Th);
        const FVector Tan(-Sx, Cx, 0.f);
        for (int32 S = 0; S < Seg; ++S)
        {
            const double T0 = double(S) / Seg, T1 = double(S + 1) / Seg;
            const float R0 = Rx * (1.0f - 0.82f * T0), R1 = Rx * (1.0f - 0.82f * T1);
            const FVector C0(R0 * Cx, R0 * Sx, -T0 * 40.f), C1(R1 * Cx, R1 * Sx, -T1 * 40.f);
            AddTri(C0 + Tan * 1.2f, C0 - Tan * 1.2f, C1 + Tan * 1.2f, TentCol);
            AddTri(C0 - Tan * 1.2f, C1 - Tan * 1.2f, C1 + Tan * 1.2f, TentCol);
        }
    }

    Mesh->CreateMeshSection_LinearColor(0, V, T, N, UV, C, {}, false);
}

FVector ABorn2FlapNeonVoid::BirdLocation() const
{
    if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
        return P->GetActorLocation();
    return FVector::ZeroVector;
}

float ABorn2FlapNeonVoid::BirdSpeed() const
{
    if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
        return P->GetVelocity().Size() / 100.f;   // cm/s -> m/s
    return 0.f;
}

void ABorn2FlapNeonVoid::Capture(FJelly& Jelly)
{
    Jelly.bCaptured = true;
    Jelly.Respawn = 6.f;
    Jelly.Mesh->SetVisibility(false);
    ++Captured;
    ComboTime = 5.f;
    ++Combo;
    // Count + speed bonus + combo streak (Ruby owns the canonical scoring; this
    // is the raw increment the HUD shows immediately).
    LastCaptureScore = 1 + FMath::Max(0, FMath::RoundToInt(BirdSpeed() * 0.2f)) + (Combo - 1);
    Score += LastCaptureScore;
    BestScore = FMath::Max(BestScore, Score);
    if (Captured >= TargetCount)
        bFestivalRunning = false;
}

void ABorn2FlapNeonVoid::UpdateFestival(float DeltaSeconds)
{
    if (bFestivalRunning)
    {
        FestivalTime = FMath::Max(0.f, FestivalTime - DeltaSeconds);
        ComboTime = FMath::Max(0.f, ComboTime - DeltaSeconds);
        if (ComboTime <= 0.f)
            Combo = 0;
        if (FestivalTime <= 0.f)
            bFestivalRunning = false;
    }
}

void ABorn2FlapNeonVoid::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Time = GetWorld()->GetTimeSeconds();

    // Swarm layer drift.
    for (int32 I = 0; I < SwarmPositions.Num(); ++I)
    {
        SwarmPositions[I] += SwarmDrift[I] * DeltaSeconds;
        Swarm->UpdateInstanceTransform(I, FTransform(FRotator::ZeroRotator, SwarmPositions[I], FVector(1.f)), false);
    }

    const FVector Bird = BirdLocation();
    ActiveCount = 0;
    for (FJelly& J : Jellies)
    {
        if (J.bCaptured)
        {
            J.Respawn -= DeltaSeconds;
            if (J.Respawn <= 0.f)
            {
                // Re-release the jellyfish elsewhere in the aether.
                const float R = Rng.FRandRange(2500.f, 8500.f);
                const float A = Rng.FRandRange(0.f, 2.f * PI);
                const FVector NewLoc(R * FMath::Cos(A), R * FMath::Sin(A), Rng.FRandRange(-1500.f, 3200.f));
                J.Origin = NewLoc;
                J.Root->SetRelativeLocation(NewLoc);
                J.Mesh->SetVisibility(true);
                J.bCaptured = false;
            }
            continue;
        }
        ++ActiveCount;

        // Pulsing bell + slow drift (the aether current).
        const float Pulse = 1.f + 0.09f * FMath::Sin(Time * 3.f + J.Phase.X);
        J.Root->SetRelativeScale3D(FVector(Pulse, Pulse, Pulse));
        FVector Loc = J.Root->GetRelativeLocation();
        Loc += J.Drift * DeltaSeconds;
        Loc.Z = J.Origin.Z + 90.f * FMath::Sin(Time * 0.6f + J.Phase.Y);
        // Gentle inward drift keeps the population near the ring.
        const FVector Rad = FVector(Loc.X, Loc.Y, 0.f).GetSafeNormal();
        Loc -= Rad * 12.f * DeltaSeconds;
        J.Root->SetRelativeLocation(Loc);

        // Capture: a soft scoop, not a collision.
        if (bFestivalRunning && FVector::Dist(Bird, J.Root->GetComponentLocation()) < CaptureRadius)
            Capture(J);
    }

    UpdateFestival(DeltaSeconds);
}