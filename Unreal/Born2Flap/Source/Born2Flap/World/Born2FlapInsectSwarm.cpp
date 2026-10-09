#include "World/Born2FlapInsectSwarm.h"

#include "Game/Born2FlapGameMode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Math/RandomStream.h"

ABorn2FlapInsectSwarm::ABorn2FlapInsectSwarm()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("InsectSwarmRoot"));
}

auto ABorn2FlapInsectSwarm::Species(EInsectType Type) -> const FSpecies*
{
    static const FSpecies Table[] = {
        // Name,        Color,                         BodyScale,              WingScale,              SwarmMin, SwarmMax, HeroScale, Speed, Agility, FlapRate, FlapAmp, Wings, Swarm, Heroes, Opaque, Buzzer
        { TEXT("Butterfly"), FLinearColor(1.00f, 0.55f, 0.15f), FVector(1.0f, 1.0f, 1.4f), FVector(1.2f, 0.9f, 0.05f), 0.35f, 0.60f, 1.00f, 90.f,  2.0f, 6.f,  55.f, 4, 120, 3, false, false },
        { TEXT("Dragonfly"), FLinearColor(0.15f, 0.85f, 0.80f), FVector(0.6f, 0.6f, 1.6f), FVector(1.0f, 0.35f, 0.04f), 0.45f, 0.70f, 1.40f, 220.f, 3.0f, 28.f, 30.f, 4, 100, 2, false, false },
        { TEXT("LunarMoth"), FLinearColor(0.92f, 0.94f, 0.85f), FVector(1.3f, 1.3f, 1.0f), FVector(1.4f, 1.1f, 0.05f), 0.60f, 0.90f, 1.30f, 70.f,  1.2f, 4.f,  50.f, 2,  90, 2, false, false },
        { TEXT("Mantis"),    FLinearColor(0.35f, 0.70f, 0.25f), FVector(0.5f, 0.7f, 1.6f), FVector(1.3f, 0.4f, 0.04f), 0.50f, 0.80f, 1.60f, 60.f,  1.5f, 9.f,  35.f, 4,  80, 2, false, false },
        { TEXT("BummerBug"), FLinearColor(0.25f, 0.18f, 0.12f), FVector(1.6f, 1.6f, 1.2f), FVector(0.9f, 0.8f, 0.15f), 0.90f, 1.40f, 2.40f, 140.f, 1.0f, 40.f, 18.f, 2,  60, 2, true,  true  },
    };
    return &Table[(int32)Type];
}

double ABorn2FlapInsectSwarm::GroundHeight(const FVector& P) const
{
    if (const auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        return GM->GroundHeight(P.X, P.Y);
    return 0.0;
}

FVector ABorn2FlapInsectSwarm::WindAt(const FVector& P, double Time) const
{
    if (const auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        return GM->WindAt(P, Time);
    return FVector::ZeroVector;
}

FVector ABorn2FlapInsectSwarm::PickPerch(const FVector& Home) const
{
    const double A = FMath::FRandRange(0.0, 2.0 * PI);
    const double R = FMath::FRandRange(200.0, HomeRadius);
    const FVector P(Home.X + FMath::Cos(A) * R, Home.Y + FMath::Sin(A) * R, 0.0);
    const double Ground = GroundHeight(P);
    return FVector(P.X, P.Y, Ground + FMath::FRandRange(150.0, 1400.0));
}

FVector ABorn2FlapInsectSwarm::PickVegetationHome(FRandomStream& R) const
{
    // Anchor insects to the map's actual vegetation, mirroring the seeded layout in
    // Tools/create_shiomori_map.py: the on-beach coastal thickets at each end, the
    // backshore scrub behind the promenade, the cultivated sweet-potato/fern fields,
    // and the hinterland woodland. Weighted toward what the pilot actually flies over.
    struct FVegZone { double MinX; double MaxX; double MinY; double MaxY; float Weight; };
    static const FVegZone Zones[] = {
        { -44900.0, -40000.0,   -850.0,  7600.0, 1.5f }, // coastal thicket west
        {  40000.0,  44900.0,   -850.0,  7600.0, 1.5f }, // coastal thicket east
        { -44500.0,  44500.0,  -6500.0, -5400.0, 1.3f }, // backshore scrub
        { -44000.0,  44000.0, -19500.0, -12700.0, 1.0f }, // cultivated fields
        { -48000.0,  48000.0, -34000.0, -19600.0, 0.8f }, // hinterland woodland
    };
    float Total = 0.f;
    for (const FVegZone& Z : Zones) Total += Z.Weight;
    float Pick = R.FRandRange(0.f, Total);
    const FVegZone* Zone = &Zones[0];
    for (const FVegZone& Z : Zones)
    {
        Pick -= Z.Weight;
        if (Pick <= 0.f) { Zone = &Z; break; }
    }
    const float X = R.FRandRange((float)Zone->MinX, (float)Zone->MaxX);
    const float Y = R.FRandRange((float)Zone->MinY, (float)Zone->MaxY);
    const double Ground = GroundHeight(FVector(X, Y, 0.0));
    return FVector(X, Y, Ground + R.FRandRange(150.0f, 1600.0f));
}

UMaterialInstanceDynamic* ABorn2FlapInsectSwarm::Tint(UMaterialInterface* Base, const FLinearColor& Color, float Alpha)
{
    if (!Base)
        return nullptr;
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
    if (MID)
    {
        MID->SetVectorParameterValue(TEXT("Color"), Color);
        MID->SetScalarParameterValue(TEXT("Opacity"), Alpha);
    }
    return MID;
}

auto ABorn2FlapInsectSwarm::SpawnBoid(FRandomStream& R, EInsectType Type) -> FBoid
{
    const FSpecies* S = Species(Type);
    FBoid B;
    B.Type = Type;
    B.Pos = PickVegetationHome(R);
    B.Home = B.Pos;
    B.Vel = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)).GetSafeNormal() * S->Speed;
    B.Phase = R.FRandRange(0.0, 2.0 * PI);
    B.Life = R.FRandRange(30.0, 90.0);
    B.Scale = R.FRandRange(S->SwarmMin, S->SwarmMax);
    return B;
}

void ABorn2FlapInsectSwarm::BuildSwarm()
{
    FRandomStream R(0xB0F2F1);
    for (int32 T = 0; T < (int32)EInsectType::Count; ++T)
    {
        const FSpecies* S = Species((EInsectType)T);
        UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
        Comp->SetupAttachment(RootComponent);
        Comp->SetStaticMesh(SpeckMesh);
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetCastShadow(false);
        Comp->RegisterComponent();
        Comp->SetMaterial(0, Tint(WingMaterial, S->Color, 0.72f));
        Swarm.Add(Comp);
        for (int32 I = 0; I < S->SwarmCount; ++I)
        {
            FBoid B = SpawnBoid(R, (EInsectType)T);
            Boids.Add(B);
            Comp->AddInstance(FTransform(FRotator::ZeroRotator, B.Pos, FVector(B.Scale)));
        }
    }
    UE_LOG(LogTemp, Log, TEXT("Born2FlapInsectSwarm: %d swarm insects"), Boids.Num());
}

void ABorn2FlapInsectSwarm::SpawnHero(FRandomStream& R, const FSpecies& S, EInsectType Type)
{
    FHero H;
    H.Type = Type;
    H.Home = PickVegetationHome(R);
    H.Pos = H.Home;
    H.Target = PickPerch(H.Home);
    H.Vel = FVector::ZeroVector;
    H.Phase = R.FRandRange(0.0, 2.0 * PI);
    H.FlapPhase = R.FRandRange(0.0, 2.0 * PI);
    H.Scale = S.HeroScale * R.FRandRange(0.85f, 1.15f);
    H.NextRelocate = 20.0 + 30.0 * R.FRand();

    H.Root = NewObject<USceneComponent>(this);
    H.Root->SetupAttachment(RootComponent);
    H.Root->RegisterComponent();
    H.Root->SetWorldLocation(H.Pos);

    H.Body = NewObject<UStaticMeshComponent>(this);
    H.Body->SetupAttachment(H.Root);
    H.Body->SetStaticMesh(BodyMesh);
    H.Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    H.Body->SetMobility(EComponentMobility::Movable);
    H.Body->SetCastShadow(false);
    H.Body->SetRelativeScale3D(S.BodyScale * H.Scale);
    H.Body->RegisterComponent();
    H.Body->SetMaterial(0, Tint(BodyMaterial, S.Color, 1.f));

    const TArray<float> Yaws = (S.WingCount <= 2)
        ? TArray<float>{ 90.f, 270.f }
        : TArray<float>{ 50.f, 130.f, 230.f, 310.f };

    UMaterialInterface* WingMat = S.bOpaqueWings ? BodyMaterial : WingMaterial;
    for (float Yaw : Yaws)
    {
        FHeroWing W;
        W.Yaw = Yaw;
        W.Scale = S.WingScale * H.Scale;
        W.Mesh = NewObject<UStaticMeshComponent>(this);
        W.Mesh->SetupAttachment(H.Root);
        W.Mesh->SetStaticMesh(WingMesh);
        W.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        W.Mesh->SetMobility(EComponentMobility::Movable);
        W.Mesh->SetCastShadow(false);
        W.Mesh->RegisterComponent();
        W.Mesh->SetMaterial(0, Tint(WingMat, S.Color, S.bOpaqueWings ? 1.f : 0.7f));
        H.Wings.Add(W);
    }
    Heroes.Add(H);
}

void ABorn2FlapInsectSwarm::BuildHeroes()
{
    FRandomStream R(0x1CEC7);
    for (int32 T = 0; T < (int32)EInsectType::Count; ++T)
    {
        const FSpecies* S = Species((EInsectType)T);
        for (int32 I = 0; I < S->HeroCount; ++I)
            SpawnHero(R, *S, (EInsectType)T);
    }
    UE_LOG(LogTemp, Log, TEXT("Born2FlapInsectSwarm: %d hero insects"), Heroes.Num());
}

void ABorn2FlapInsectSwarm::BeginPlay()
{
    Super::BeginPlay();
    SpeckMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/Insects/SM_Insect_Speck"));
    WingMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/Insects/SM_Insect_Wing"));
    BodyMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/Insects/SM_Insect_Body"));
    WingMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/Insects/M_InsectWing"));
    BodyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/Insects/M_InsectBody"));

    if (!SpeckMesh || !WingMesh || !BodyMesh || !WingMaterial || !BodyMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapInsectSwarm: insect assets missing — run Tools/create_insect_assets.py"));
        return;
    }
    BuildSwarm();
    BuildHeroes();
    bReady = true;
}

void ABorn2FlapInsectSwarm::TickSwarm(float DeltaSeconds)
{
    const double Time = GetWorld()->GetTimeSeconds();
    int32 BoidIdx = 0;
    for (int32 T = 0; T < (int32)EInsectType::Count; ++T)
    {
        const FSpecies* S = Species((EInsectType)T);
        UInstancedStaticMeshComponent* Comp = Swarm[T];
        for (int32 I = 0; I < S->SwarmCount; ++I)
        {
            FBoid& B = Boids[BoidIdx + I];
            B.Phase += DeltaSeconds * (S->bBuzzer ? 2.2 : 1.0);

            FVector Dir = B.Vel;
            if (Dir.IsNearlyZero())
                Dir = FVector(1, 0, 0);
            Dir.Normalize();
            const double flapHz = S->bBuzzer ? 22.0 : 4.0;
            FVector Wander(
                FMath::Sin(Time * 0.7 + B.Phase) * S->Agility,
                FMath::Cos(Time * 0.5 + B.Phase * 1.3) * S->Agility,
                FMath::Sin(Time * 1.1 + B.Phase * 0.7) * S->Agility);
            Dir = (Dir + Wander * DeltaSeconds * 3.0).GetSafeNormal();

            const FVector Flutter(
                FMath::Cos(Time * flapHz * 1.3 + B.Phase) * 25.0,
                FMath::Sin(Time * flapHz * 1.1 + B.Phase) * 25.0,
                FMath::Sin(Time * flapHz * 2.0 + B.Phase) * 40.0);

            FVector TargetVel = Dir * S->Speed + Flutter + WindAt(B.Pos, Time) * 100.0;

            // No camera attraction — each insect keeps its own territory and
            // only drifts back toward "home" when it strays too far.
            const double HomeDist = FVector::Dist2D(B.Home, B.Pos);
            if (HomeDist > HomeRadius)
            {
                const FVector Pull((B.Home.X - B.Pos.X) / HomeDist, (B.Home.Y - B.Pos.Y) / HomeDist, 0.0);
                TargetVel += Pull * (HomeDist - HomeRadius) * 0.6f;
            }

            B.Vel = FMath::VInterpTo(B.Vel, TargetVel, DeltaSeconds, 1.5f);
            B.Pos += B.Vel * DeltaSeconds;

            const double Ground = GroundHeight(B.Pos);
            const double Floor = Ground + 120.0;
            if (B.Pos.Z < Floor)
            {
                B.Pos.Z = Floor;
                B.Vel.Z = FMath::Max(B.Vel.Z, 25.0);
            }
            if (B.Pos.Z > 3200.0)
            {
                B.Pos.Z = 3200.0;
                B.Vel.Z = FMath::Min(B.Vel.Z, -15.0);
            }

            B.Life -= DeltaSeconds;
            if (B.Life <= 0.0)
            {
                FRandomStream Reseed(0xB0F2F1 + (int32)Time * 4 + BoidIdx + I);
                B = SpawnBoid(Reseed, (EInsectType)T);
            }

            FRotator Rot;
            Rot.Yaw = FMath::RadiansToDegrees(FMath::Atan2(B.Vel.Y, B.Vel.X));
            Rot.Pitch = FMath::Sin(Time * flapHz * 2.0 + B.Phase) * 30.0;
            Rot.Roll = FMath::Cos(Time * flapHz * 1.7 + B.Phase) * 28.0;
            Comp->UpdateInstanceTransform(I, FTransform(Rot, B.Pos, FVector(B.Scale)), true, false, false);
        }
        Comp->MarkRenderStateDirty();
        BoidIdx += S->SwarmCount;
    }
}

void ABorn2FlapInsectSwarm::TickHeroes(float DeltaSeconds)
{
    const double Time = GetWorld()->GetTimeSeconds();

    for (FHero& H : Heroes)
    {
        const FSpecies* S = Species(H.Type);

        H.FlapPhase += DeltaSeconds * S->FlapRate * 2.0 * PI;
        const float FlapAngle = FMath::Sin(H.FlapPhase) * S->FlapAmp;
        for (FHeroWing& W : H.Wings)
        {
            const FQuat FlapQ(FVector(1, 0, 0), FMath::DegreesToRadians(FlapAngle));
            const FQuat YawQ(FVector(0, 0, 1), FMath::DegreesToRadians(W.Yaw));
            W.Mesh->SetRelativeTransform(FTransform(FlapQ * YawQ, FVector::ZeroVector, W.Scale));
        }

        // A hero minds its own business: it lingers in a home territory, hops
        // between perches, and occasionally flies off to a fresh spot near
        // (but never into) the pilot's region. No orbiting, no face-dives.
        H.NextRelocate -= DeltaSeconds;
        if (H.NextRelocate <= 0.0)
        {
            FRandomStream R(0x1CEC7 + (int32)(Time * 4.0) + (int32)(&H - Heroes.GetData()));
            H.Home = PickVegetationHome(R);
            H.Target = PickPerch(H.Home);
            H.NextRelocate = 20.0 + 30.0 * FMath::FRand();
        }

        if (FVector::Dist(H.Target, H.Pos) < 250.0)
            H.Target = PickPerch(H.Home);

        const FVector Desired = (H.Target - H.Pos).GetSafeNormal() * S->Speed + WindAt(H.Pos, Time) * 100.0;
        H.Vel = FMath::VInterpTo(H.Vel, Desired, DeltaSeconds, 1.0f);
        H.Pos += H.Vel * DeltaSeconds;

        const double Ground = GroundHeight(H.Pos);
        if (H.Pos.Z < Ground + 150.0)
        {
            H.Pos.Z = Ground + 150.0;
            H.Vel.Z = FMath::Max(H.Vel.Z, 20.0);
        }
        if (H.Pos.Z > 3200.0)
        {
            H.Pos.Z = 3200.0;
            H.Vel.Z = FMath::Min(H.Vel.Z, -15.0);
        }

        H.Root->SetWorldLocation(H.Pos);
        if (!H.Vel.IsNearlyZero())
            H.Root->SetWorldRotation(H.Vel.GetSafeNormal().Rotation());
    }
}

void ABorn2FlapInsectSwarm::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bReady)
        return;
    TickSwarm(DeltaSeconds);
    TickHeroes(DeltaSeconds);
}