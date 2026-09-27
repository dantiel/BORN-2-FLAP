#include "World/Born2FlapWindLeaves.h"

#include "World/Born2FlapWind.h"
#include "World/Born2FlapValley.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
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
        FRandomStream R(120724 + int32(Time * 0.25));  // stable-enough reseed for respawn variety
        for (int32 I = 0; I < LeafPool.Num(); ++I)
        {
            FLeaf& L = LeafPool[I];
            const FVector Wind = Born2FlapWind::Sample(L.Pos, Time) * 100.0;  // m/s → cm/s
            L.Vel = FMath::VInterpTo(L.Vel, Wind, DeltaSeconds, 1.5f);
            L.Pos += (L.Vel + FVector(0, 0, -80.0)) * DeltaSeconds;            // gentle sink
            L.Rot += L.Spin * DeltaSeconds;
            L.Life -= DeltaSeconds;

            const double Ground = ABorn2FlapValley::GroundHeight(L.Pos.X, L.Pos.Y);
            if (L.Life <= 0.0 || L.Pos.Z < Ground - 200.0 || FVector::Dist(L.Pos, Cam) > 9000.0)
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
            const FVector Wind = Born2FlapWind::Sample(B.Anchor, Time);        // m/s
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
    const double A = R.FRandRange(0.0, 2.0 * PI);
    const double Rad = R.FRandRange(900.0, 5500.0);
    L.Pos = Cam + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, R.FRandRange(100.0, 2600.0));
    const FVector Wind = Born2FlapWind::Sample(L.Pos, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) * 100.0;
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
    const double Ground = ABorn2FlapValley::GroundHeight(At.X, At.Y);
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
