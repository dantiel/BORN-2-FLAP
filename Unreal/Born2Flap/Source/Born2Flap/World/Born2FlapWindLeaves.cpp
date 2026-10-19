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
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
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
}

void ABorn2FlapWindLeaves::BeginPlay()
{
    Super::BeginPlay();
    UStaticMesh* LeafMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/SM_Grass0"));
    UStaticMesh* BladeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Nature/SM_Grass1"));
    Leaves->SetStaticMesh(LeafMesh);
    Blades->SetStaticMesh(BladeMesh);

    if (!LeafMesh && !BladeMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Born2FlapWindLeaves: no grass meshes found — wind invisible"));
        return;
    }

    ScanTrees();

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
        const int32 Active = FMath::RoundToInt(FMath::Lerp((float)MinLeaves, (float)MaxLeaves, WindNorm));

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