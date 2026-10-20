#include "World/Born2FlapWindLeaves.h"

#include "World/Born2FlapWind.h"
#include "Game/Born2FlapGameMode.h"

namespace {
FVector WeatherWind(const AActor* Actor,const FVector& P,double Time)
{
    const auto* GM=Cast<ABorn2FlapGameMode>(Actor->GetWorld()->GetAuthGameMode());
    return GM ? GM->WindAt(P,Time) : Born2FlapWind::Sample(P,Time);
}
}

#include "World/Born2FlapValley.h"

namespace {
// Level-aware ground: the coast and the valley have different terrain. The
// game mode dispatches to the correct profile (Born2FlapBay::Height on the
// coast, ABorn2FlapValley::GroundHeight on the valley); sampling the valley
// directly leaves coast-side blades/leaves anchored at the wrong altitude.
double WeatherGround(const AActor* Actor, double X, double Y)
{
    const auto* GM = Cast<ABorn2FlapGameMode>(Actor->GetWorld()->GetAuthGameMode());
    return GM ? GM->GroundHeight(X, Y) : ABorn2FlapValley::GroundHeight(X, Y);
}
}

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

ABorn2FlapWindLeaves::ABorn2FlapWindLeaves()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("WindLeavesRoot"));

    Leaves = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Leaves"));
    Leaves->SetupAttachment(RootComponent);
    Leaves->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Leaves->SetMobility(EComponentMobility::Movable);
    Leaves->SetCastShadow(false);

    Blades = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Blades"));
    Blades->SetupAttachment(RootComponent);
    Blades->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Blades->SetMobility(EComponentMobility::Movable);
    Blades->SetCastShadow(false);

    Dust = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Dust"));
    Dust->SetupAttachment(RootComponent);
    Dust->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Dust->SetMobility(EComponentMobility::Movable);
    Dust->SetCastShadow(false);
}

void ABorn2FlapWindLeaves::BeginPlay()
{
    Super::BeginPlay();
    UStaticMesh* LeafMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/SM_Grass0"));
    UStaticMesh* BladeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/SM_Grass1"));
    UStaticMesh* SpeckMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere"));
    Leaves->SetStaticMesh(LeafMesh);
    Blades->SetStaticMesh(BladeMesh);
    // Sand/dust motes are grains, not leaves — a sphere reads as a particle
    // (the leaf quad only remains as a fallback if the engine shape is absent).
    Dust->SetStaticMesh(SpeckMesh ? SpeckMesh : LeafMesh);
    // Sand/dust motes are grains, not leaves. Prefer M_SandGrain — an OPAQUE,
    // LIT beige material — so grains read as solid 3D specks that catch light
    // instead of flat translucent circles. Falls back to M_InsectSpeck if the
    // sand material hasn't been generated yet.
    UMaterialInterface* SandMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/Insects/M_SandGrain"));
    if (!SandMat)
        SandMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/Insects/M_InsectSpeck"));
    if (SandMat)
    {
        if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SandMat, Dust))
        {
            MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.76f, 0.64f, 0.46f));
            MID->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
            Dust->SetMaterial(0, MID);
        }
    }

    if (!LeafMesh && !BladeMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapWindLeaves: no grass meshes found — wind invisible"));
        return;
    }

    ScanTrees();

    // Season abundance: autumn sheds a storm of leaves, winter strips the
    // canopy, spring is sparse and green. Resolved once from the level's
    // chosen season (a single hermetic knob, not a per-leaf setting).
    if (const auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        Season = Born2FlapWeather::Season(GM->GetConditions().Season);

    FRandomStream R(120724);
    const FVector Cam = CameraLocation();
    LastCam = Cam;

    if (LeafMesh)
    {
        LeafPool.SetNum(MaxLeaves);
        for (int32 I = 0; I < MaxLeaves; ++I)
        {
            LeafPool[I] = SpawnLeaf(R, Cam);
            Leaves->AddInstance(FTransform(LeafPool[I].Rot, LeafPool[I].Pos, FVector(LeafPool[I].Scale)));
        }
    }
    if (BladeMesh)
    {
        BladePool.SetNum(MaxBlades);
        for (int32 I = 0; I < MaxBlades; ++I)
        {
            BladePool[I] = SpawnBlade(R, Cam);
            Blades->AddInstance(FTransform(BladePool[I].Base, BladePool[I].Anchor, FVector(BladePool[I].Scale)));
        }
    }
    if (LeafMesh)
    {
        DustPool.SetNum(MaxDust);
        for (int32 I = 0; I < MaxDust; ++I)
        {
            DustPool[I] = SpawnDust(R, Cam, Cam, false);
            Dust->AddInstance(FTransform(DustPool[I].Rot, DustPool[I].Pos, FVector(DustPool[I].Scale)));
        }
    }
    bInitialized = true;
}

void ABorn2FlapWindLeaves::ScanTrees()
{
    TreePositions.Reset();
    TreeHeights.Reset();
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        UStaticMeshComponent* SMC = It->GetStaticMeshComponent();
        if (!SMC || !SMC->GetStaticMesh()) continue;
        const FString MeshName = SMC->GetStaticMesh()->GetName();
        if (!MeshName.Contains(TEXT("Fir")) && !MeshName.Contains(TEXT("Tree"))
            && !MeshName.Contains(TEXT("Autumn")) && !MeshName.Contains(TEXT("Deadwood"))
            && !MeshName.Contains(TEXT("Jacaranda")))
            continue;
        const FBox B = SMC->GetStaticMesh()->GetBounds().GetBox();
        const float H = (B.Max.Z - B.Min.Z) * It->GetActorScale3D().Z;
        TreePositions.Add(It->GetActorLocation());
        TreeHeights.Add(FMath::Max(H, 60.0f));
    }
}

void ABorn2FlapWindLeaves::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bInitialized)
        return;

    const double Time = GetWorld()->GetTimeSeconds();
    const FVector Cam = CameraLocation();
    const bool CamMoved = FVector::Dist(Cam, LastCam) > 2500.0;

    if (LeafPool.Num() == MaxLeaves)
    {
        // Wind strength drives how many leaves are airborne: calm air holds a
        // few drifting stragglers near the trees, a gale tears whole canopies
        // loose. Horizontal wind only — thermals are lift, not "windiness".
        const FVector WindAtCam = WeatherWind(this, Cam, Time);
        const float WindMS = FMath::Sqrt(WindAtCam.X * WindAtCam.X + WindAtCam.Y * WindAtCam.Y);
        const float WindNorm = FMath::Clamp((WindMS - 1.0f) / 9.0f, 0.0f, 1.0f);
        // The season scales how full the sky gets: autumn fills it at a light
        // breeze (LeafFactor > 1), winter barely lifts a leaf even in a gale.
        const int32 Active = FMath::RoundToInt(FMath::Lerp((float)MinLeaves, (float)MaxLeaves * Season.LeafFactor, WindNorm));

        FRandomStream R(120724 + int32(Time * 0.25));  // stable-enough reseed for respawn variety
        for (int32 I = 0; I < LeafPool.Num(); ++I)
        {
            FLeaf& L = LeafPool[I];
            if (I >= Active)
            {
                // Parked while the air is calm; respawns near a tree the moment
                // stronger wind reveals it.
                L.Life = 0.0f;
                Leaves->UpdateInstanceTransform(I, FTransform(FRotator::ZeroRotator, Cam + FVector(0, 0, -50000.0), FVector::ZeroVector), true, false, false);
                continue;
            }

            const FVector Wind = WeatherWind(this, L.Pos, Time) * 100.0;  // m/s → cm/s
            L.Vel = FMath::VInterpTo(L.Vel, Wind, DeltaSeconds, 1.5f);
            L.Pos += (L.Vel + FVector(0, 0, -80.0)) * DeltaSeconds;            // gentle sink
            L.Rot += L.Spin * DeltaSeconds;
            L.Life -= DeltaSeconds;

            const double Ground = WeatherGround(this, L.Pos.X, L.Pos.Y);
            if (L.Life <= 0.0 || L.Pos.Z < Ground - 200.0 || FVector::Dist(L.Pos, Cam) > 12000.0)
                L = SpawnLeaf(R, Cam);

            Leaves->UpdateInstanceTransform(I, FTransform(L.Rot, L.Pos, FVector(L.Scale)), true, false, false);
        }
        Leaves->MarkRenderStateDirty();
    }

    if (CamMoved || BladePool.Num() != MaxBlades)
        RespawnBlades(true);

    if (BladePool.Num() == MaxBlades)
    {
        for (int32 I = 0; I < BladePool.Num(); ++I)
        {
            const FBlade& B = BladePool[I];
            const FVector Wind = WeatherWind(this,B.Anchor, Time);        // m/s
            const double Strength = Wind.Size();
            const double K = FMath::Min(1.0, Strength / 8.0);
            const double Flutter = FMath::Sin(Time * 5.0 + B.Phase) * 4.0 * K;
            const FRotator Tilt(FMath::Clamp(Wind.Z * -0.9, -22.0, 22.0),
                                FMath::Clamp(Wind.Y * -0.7, -28.0, 28.0),
                                FMath::Clamp(Wind.X * -0.7, -28.0, 28.0));
            const FRotator Rot = B.Base + Tilt + FRotator(Flutter, Flutter * 0.4, Flutter * 0.6);
            Blades->UpdateInstanceTransform(I, FTransform(Rot, B.Anchor, FVector(B.Scale)), true, false, false);
        }
        Blades->MarkRenderStateDirty();
    }

    // Ground dust/sand: stirred up by strong gusts (sand blowing along the
    // beach) and by the bird's wings when it flaps close to the ground.
    if (DustPool.Num() == MaxDust)
    {
        const APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
        const FVector Bird = Pawn ? Pawn->GetActorLocation() : Cam;
        const double BirdGround = WeatherGround(this, Bird.X, Bird.Y);
        const double BirdLow = FMath::Clamp(1.0 - (Bird.Z - BirdGround) / 300.0, 0.0, 1.0);

        const FVector WindC = WeatherWind(this, Cam, Time);
        const float WindMS = FMath::Sqrt(WindC.X * WindC.X + WindC.Y * WindC.Y);
        const float Gust = FMath::Clamp((WindMS - 4.0f) / 12.0f, 0.0f, 1.0f);

        const float Activity = FMath::Max(BirdLow, Gust);
        const int32 Active = FMath::RoundToInt(Activity * MaxDust);

        FRandomStream R(0xD157 + int32(Time * 0.5));
        for (int32 I = 0; I < DustPool.Num(); ++I)
        {
            FLeaf& D = DustPool[I];
            if (I >= Active && !D.bBurst)
            {
                D.Life = 0.0f;
                Dust->UpdateInstanceTransform(I, FTransform(FRotator::ZeroRotator, Cam + FVector(0, 0, -50000.0), FVector::ZeroVector), true, false, false);
                continue;
            }

            if (D.bBurst)
            {
                // One-shot sand grain: a true projectile. It is NOT coupled to
                // the wind field — it shoots out, arcs under gravity, bounces
                // once with damping, and fades to nothing (never morphing into
                // ambient drift, which is what read as "thermal waves").
                if (D.Life <= 0.0f)
                {
                    D.bBurst = false;
                    Dust->UpdateInstanceTransform(I, FTransform(FRotator::ZeroRotator, Cam + FVector(0, 0, -50000.0), FVector::ZeroVector), true, false, false);
                    continue;
                }
                D.Vel.Z -= 980.0 * DeltaSeconds;   // gravity, cm/s^2
                D.Pos += D.Vel * DeltaSeconds;
                D.Rot += D.Spin * DeltaSeconds;
                D.Life -= DeltaSeconds;
                const float Fade = FMath::Clamp((float)(D.Life / FMath::Max(D.LifeMax, 0.001)), 0.0f, 1.0f);
                const double G = WeatherGround(this, D.Pos.X, D.Pos.Y);
                if (D.Pos.Z < G + 4.0)
                {
                    D.Pos.Z = G + 4.0;
                    D.Vel.Z = FMath::Abs(D.Vel.Z) * 0.22f;   // damped bounce
                    D.Vel.X *= 0.5f;
                    D.Vel.Y *= 0.5f;
                }
                Dust->UpdateInstanceTransform(I, FTransform(D.Rot, D.Pos, FVector(D.Scale * Fade)), true, false, false);
                continue;
            }

            if (D.Life <= 0.0f)
                D = SpawnDust(R, Cam, Bird, BirdLow > 0.05f);
            const FVector Wind = WeatherWind(this, D.Pos, Time) * 100.0;  // m/s → cm/s
            D.Vel = FMath::VInterpTo(D.Vel, Wind, DeltaSeconds, 2.0f);
            D.Vel.Z -= 980.0 * DeltaSeconds;   // gravity: sand has weight, it settles
            D.Pos += D.Vel * DeltaSeconds;
            D.Rot += D.Spin * DeltaSeconds;
            D.Life -= DeltaSeconds;
            const double G = WeatherGround(this, D.Pos.X, D.Pos.Y);
            if (D.Pos.Z < G + 2.0) { D.Pos.Z = G + 2.0; D.Vel.Z = -FMath::Abs(D.Vel.Z) * 0.25f; D.Vel.X *= 0.7f; D.Vel.Y *= 0.7f; }
            const float FadeA = FMath::Clamp((float)(D.Life / FMath::Max(D.LifeMax, 0.001)), 0.0f, 1.0f);
            Dust->UpdateInstanceTransform(I, FTransform(D.Rot, D.Pos, FVector(D.Scale * FadeA)), true, false, false);
        }
        Dust->MarkRenderStateDirty();
    }
    LastCam = Cam;
}

FVector ABorn2FlapWindLeaves::CameraLocation() const
{
    if (APlayerCameraManager* CM = UGameplayStatics::GetPlayerCameraManager(this, 0))
        return CM->GetCameraLocation();
    return GetActorLocation();
}

ABorn2FlapWindLeaves::FLeaf ABorn2FlapWindLeaves::SpawnLeaf(FRandomStream& R, const FVector& Cam)
{
    FLeaf L;
    FVector At;
    if (TreePositions.Num() > 0)
    {
        // Shed from a real canopy: spawn at the crown of a random tree and let
        // the field carry it away, so leaves cluster where the groves stand.
        const int32 T = R.RandRange(0, TreePositions.Num() - 1);
        const FVector& P = TreePositions[T];
        const float H = TreeHeights[T];
        const double A = R.FRandRange(0.0, 2.0 * PI);
        const double Rad = R.FRandRange(0.0, FMath::Max(220.0f, H * 0.75f));
        At = P + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, H * R.FRandRange(0.25, 0.95));
    }
    else
    {
        const double A = R.FRandRange(0.0, 2.0 * PI);
        const double Rad = R.FRandRange(900.0, 5500.0);
        At = Cam + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, R.FRandRange(100.0, 2600.0));
    }
    L.Pos = At;
    const FVector Wind = WeatherWind(this,L.Pos, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) * 100.0;
    L.Vel = Wind;
    L.Rot = FRotator(R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0));
    L.Spin = FRotator(R.FRandRange(-120.0, 120.0), R.FRandRange(-180.0, 180.0), R.FRandRange(-120.0, 120.0));
    L.Scale = R.FRandRange(0.22f, 0.55f);
    L.Life = R.FRandRange(8.0, 22.0);
    return L;
}

ABorn2FlapWindLeaves::FBlade ABorn2FlapWindLeaves::SpawnBlade(FRandomStream& R, const FVector& Cam)
{
    FBlade B;
    const double A = R.FRandRange(0.0, 2.0 * PI);
    const double Rad = R.FRandRange(350.0, 5200.0);
    const FVector2D At(Cam.X + FMath::Cos(A) * Rad, Cam.Y + FMath::Sin(A) * Rad);
    const double Ground = WeatherGround(this, At.X, At.Y);
    B.Anchor = FVector(At.X, At.Y, Ground - 4.0);
    B.Base = FRotator(0.0, R.FRandRange(0.0, 360.0), 0.0);
    B.Scale = R.FRandRange(0.55f, 1.3f);
    B.Phase = R.FRandRange(0.0, 2.0 * PI);
    return B;
}

ABorn2FlapWindLeaves::FLeaf ABorn2FlapWindLeaves::SpawnDust(FRandomStream& R, const FVector& Cam, const FVector& Bird, bool bNearBird)
{
    FLeaf D;
    FVector At;
    if (bNearBird)
    {
        // Stirred by the bird's wing-beat: a tight ring around the bird, low.
        const double A = R.FRandRange(0.0, 2.0 * PI);
        const double Rad = R.FRandRange(60.0, 700.0);
        At = Bird + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, R.FRandRange(-30.0, 50.0));
    }
    else
    {
        // Sand swept up by a gust: scattered along the ground near the camera.
        const double A = R.FRandRange(0.0, 2.0 * PI);
        const double Rad = R.FRandRange(300.0, 5200.0);
        At = Cam + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, 0.0);
    }
    const double Ground = WeatherGround(this, At.X, At.Y);
    D.Pos = FVector(At.X, At.Y, Ground + R.FRandRange(2.0, 40.0));
    D.Vel = FVector(R.FRandRange(-40.0, 40.0), R.FRandRange(-40.0, 40.0), R.FRandRange(10.0, 40.0));
    D.Rot = FRotator(R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0));
    D.Spin = FRotator(R.FRandRange(-200.0, 200.0), R.FRandRange(-260.0, 260.0), R.FRandRange(-200.0, 200.0));
    D.Scale = R.FRandRange(0.015f, 0.05f);   // tiny grains, 1.5–5 cm diameter
    D.Life = R.FRandRange(1.0, 3.5);
    D.LifeMax = D.Life;
    D.bBurst = false;
    return D;
}

void ABorn2FlapWindLeaves::RespawnBlades(bool bForce)
{
    if (!bForce)
        return;
    FRandomStream R(230924);
    const FVector Cam = CameraLocation();
    for (int32 I = 0; I < BladePool.Num(); ++I)
    {
        BladePool[I] = SpawnBlade(R, Cam);
        Blades->UpdateInstanceTransform(I, FTransform(BladePool[I].Base, BladePool[I].Anchor, FVector(BladePool[I].Scale)), true, false, false);
    }
    Blades->MarkRenderStateDirty();
}

void ABorn2FlapWindLeaves::EmitSandBurst(const FVector& WorldPos, float Strength)
{
    if (!Dust || DustPool.Num() == 0)
        return;
    Strength = FMath::Clamp(Strength, 0.05f, 1.0f);
    const int32 Count = FMath::Clamp(FMath::RoundToInt(Strength * 48.0f), 10, 48);
    // Per-burst stream so two bursts in the same frame don't reuse the same seed
    // (a stable seed would place every landing's motes on identical tracks).
    FRandomStream R(int32(FMath::Rand()) ^ int32(FPlatformTime::Cycles()));
    const double Ground = WeatherGround(this, WorldPos.X, WorldPos.Y);
    for (int32 I = 0; I < Count; ++I)
    {
        const int32 Idx = (LastBurstIdx + I) % DustPool.Num();
        FLeaf& D = DustPool[Idx];
        const double A = R.FRandRange(0.0, 2.0 * PI);
        const double Rad = R.FRandRange(0.0, 220.0 * Strength);
        D.Pos = WorldPos + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, 0.0);
        D.Pos.Z = Ground + R.FRandRange(2.0, 12.0);
        // Fast outward-upward cone — ballistic, no wind coupling. The grains arc
        // under gravity and bounce, reading as kicked-up sand, not drifting mist.
        const double Eject = 220.0 + R.FRandRange(0.0, 260.0) * Strength;   // cm/s outward
        D.Vel = FVector(FMath::Cos(A) * Eject, FMath::Sin(A) * Eject, R.FRandRange(180.0, 460.0) * Strength);
        D.Rot = FRotator(R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0), R.FRandRange(0.0, 360.0));
        D.Spin = FRotator(R.FRandRange(-420.0, 420.0), R.FRandRange(-420.0, 420.0), R.FRandRange(-420.0, 420.0));
        D.Scale = R.FRandRange(0.03f, 0.08f);   // small solid grains, not big circles
        D.Life = R.FRandRange(0.7, 1.6);
        D.LifeMax = D.Life;
        D.bBurst = true;
    }
    LastBurstIdx = (LastBurstIdx + Count) % DustPool.Num();
    Dust->MarkRenderStateDirty();
}