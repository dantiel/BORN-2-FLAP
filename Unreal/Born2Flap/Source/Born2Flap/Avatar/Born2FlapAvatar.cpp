#include "Avatar/Born2FlapAvatar.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
// One full gait cycle covers this much forward travel (cm).
constexpr float StrideCm = 65.f;
// Throw animation: total duration and the release moment (fraction 0..1).
constexpr float ThrowDuration = 0.65f;
constexpr float ThrowReleaseFraction = 0.30f;
// Normalise any imported character to this standing height (cm).
constexpr float TargetHeightCm = 180.f;

// Colour palettes per skin index. Cloth / Skin / Hair / Accent.
struct FB2FAvatarPalette
{
    FLinearColor Cloth, Skin, Hair, Accent;
};
const TArray<FB2FAvatarPalette>& AvatarSkins()
{
    static const TArray<FB2FAvatarPalette> Skins = {
        // 0: founder (original textures; palette only used as tint fallback)
        { FLinearColor(0.16f, 0.22f, 0.18f), FLinearColor(0.76f, 0.58f, 0.45f),
          FLinearColor(0.09f, 0.07f, 0.05f), FLinearColor(0.80f, 0.62f, 0.20f) },
        // 1: coast
        { FLinearColor(0.02f, 0.27f, 0.30f), FLinearColor(0.85f, 0.70f, 0.55f),
          FLinearColor(0.03f, 0.03f, 0.04f), FLinearColor(1.0f, 0.49f, 0.055f) },
        // 2: neon
        { FLinearColor(0.05f, 0.05f, 0.10f), FLinearColor(0.70f, 0.75f, 0.80f),
          FLinearColor(0.02f, 0.02f, 0.03f), FLinearColor(0.0f, 1.0f, 0.9f) },
        // 3: bronze statue
        { FLinearColor(0.24f, 0.145f, 0.06f), FLinearColor(0.24f, 0.145f, 0.06f),
          FLinearColor(0.15f, 0.10f, 0.05f), FLinearColor(0.42f, 0.28f, 0.12f) },
    };
    return Skins;
}
} // namespace

ABorn2FlapAvatar::ABorn2FlapAvatar()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Root);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCastShadow(true);

    CarrySocket = CreateDefaultSubobject<USceneComponent>(TEXT("CarrySocket"));
    CarrySocket->SetupAttachment(Body);
}

void ABorn2FlapAvatar::BeginPlay()
{
    Super::BeginPlay();

    if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Actor/SK_Founder.SK_Founder")))
    {
        Body->SetSkeletalMesh(Mesh);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Avatar: /Game/Actor/SK_Founder missing - run Tools/create_avatar_assets.py"));
    }

    ResolveSkeleton();
    CaptureRestPoses();
    Body->SetAnimationMode(EAnimationMode::AnimationCustomMode);

    // Normalise to a ~180 cm human and rest the feet on the ground. The skeleton
    // root is the hips, so lift the mesh until the foot bone reaches the root.
    if (Body->GetSkeletalMeshAsset())
    {
        const FBoxSphereBounds B = Body->CalcBounds(Body->GetComponentTransform());
        const float HeightLocal = B.BoxExtent.Z * 2.f;
        if (HeightLocal > 1.f)
            MeshScale = TargetHeightCm / HeightLocal;
        Body->SetRelativeScale3D(FVector(MeshScale));
        if (BoneLFoot != INDEX_NONE)
        {
            const float FootZ = Body->GetBoneLocation(Body->GetBoneName(BoneLFoot), EBoneSpaces::ComponentSpace).Z;
            Body->SetRelativeLocation(FVector(0.f, 0.f, -FootZ * MeshScale));
        }
    }

    // Cache the founder's own materials for skin index 0 (naturalistic human).
    OriginalMaterials.Reset();
    for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
        OriginalMaterials.Add(Body->GetMaterial(i));

    ApplySkin();
    SetFirstPerson(false);
}

void ABorn2FlapAvatar::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    WorldTime += DeltaSeconds;

    FacingYaw = FMath::FInterpTo(FacingYaw, TargetFacingYaw, DeltaSeconds, 10.f);
    Root->SetWorldLocation(FeetWorld);
    Root->SetWorldRotation(FRotator(0.f, FacingYaw, 0.f));

    const bool bMoving = FMath::Abs(GaitSpeed) > 12.f;
    GaitBlend = FMath::FInterpTo(GaitBlend, bMoving ? 1.f : 0.f, DeltaSeconds, 10.f);
    if (bMoving)
        GaitPhase = FMath::Fmod(GaitPhase + (GaitSpeed * DeltaSeconds / StrideCm) * 6.2831853f, 6.2831853f);

    if (bThrowing)
        PoseThrow(DeltaSeconds);
    else
        PoseBody(bMoving, DeltaSeconds);
}

int32 ABorn2FlapAvatar::FindBone(const TCHAR* Substring) const
{
    if (!Body)
        return INDEX_NONE;
    const int32 N = Body->GetNumBones();
    for (int32 i = 0; i < N; ++i)
        if (Body->GetBoneName(i).ToString().Contains(Substring))
            return i;
    return INDEX_NONE;
}

void ABorn2FlapAvatar::ResolveSkeleton()
{
    BoneHips = FindBone(TEXT("Hips"));
    BoneSpine = FindBone(TEXT("Spine"));
    BoneHead = FindBone(TEXT("Head"));
    BoneLArm = FindBone(TEXT("LeftArm"));
    BoneLForeArm = FindBone(TEXT("LeftForeArm"));
    BoneLHand = FindBone(TEXT("LeftHand"));
    BoneRArm = FindBone(TEXT("RightArm"));
    BoneRForeArm = FindBone(TEXT("RightForeArm"));
    BoneRHand = FindBone(TEXT("RightHand"));
    BoneLUpLeg = FindBone(TEXT("LeftUpLeg"));
    BoneLLeg = FindBone(TEXT("LeftLeg"));
    BoneLFoot = FindBone(TEXT("LeftFoot"));
    BoneRUpLeg = FindBone(TEXT("RightUpLeg"));
    BoneRLeg = FindBone(TEXT("RightLeg"));
    BoneRFoot = FindBone(TEXT("RightFoot"));
}

void ABorn2FlapAvatar::CaptureRestPoses()
{
    RestTranslations.Reset();
    if (!Body)
        return;
    const int32 Bones[] = { BoneHips, BoneSpine, BoneHead, BoneLArm, BoneLForeArm, BoneLHand,
        BoneRArm, BoneRForeArm, BoneRHand, BoneLUpLeg, BoneLLeg, BoneLFoot,
        BoneRUpLeg, BoneRLeg, BoneRFoot };
    for (int32 Idx : Bones)
        if (Idx != INDEX_NONE)
            RestTranslations.Add(Idx, Body->GetBoneTransform(Body->GetBoneName(Idx), RTS_Component).GetLocation());
}

void ABorn2FlapAvatar::SetBoneRotation(int32 BoneIndex, const FRotator& Rot)
{
    // UE 5.8 has no public "set a single bone transform" API on USkeletalMeshComponent.
    // Procedural posing must go through a UAnimInstance (override NativeUpdateAnimation
    // and write into the compact pose). Until that lands the avatar renders its reference pose.
    (void)BoneIndex;
    (void)Rot;
}

void ABorn2FlapAvatar::ApplySkin()
{
    if (!Body)
        return;
    const auto& Palettes = AvatarSkins();
    const FB2FAvatarPalette& P = Palettes[FMath::Clamp(SkinIndex, 0, Palettes.Num() - 1)];

    // Skin 0 keeps the founder's own textured materials (naturalistic human).
    if (SkinIndex == 0 && OriginalMaterials.Num() > 0)
    {
        for (int32 i = 0; i < Body->GetNumMaterials() && i < OriginalMaterials.Num(); ++i)
            Body->SetMaterial(i, OriginalMaterials[i]);
        return;
    }

    // Tinted actors: use the parameterized actor material when available, else
    // fall back to the flat training material so a missing M_Actor still shows.
    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Actor/M_Actor"));
    if (!Base)
        Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Training/M_Stone"));
    if (!Base)
        return;
    if (!SkinMID || SkinMID->Parent != Base)
        SkinMID = UMaterialInstanceDynamic::Create(Base, this);
    SkinMID->SetVectorParameterValue(TEXT("Color"), P.Cloth);
    SkinMID->SetScalarParameterValue(TEXT("Roughness"), 0.7f);
    for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
        Body->SetMaterial(i, SkinMID);
}

void ABorn2FlapAvatar::SetSkin(int32 Index)
{
    SkinIndex = Index;
    if (HasActorBegunPlay())
        ApplySkin();
}

void ABorn2FlapAvatar::SetPlacement(const FVector& WorldFeet, float WorldYaw)
{
    FeetWorld = WorldFeet;
    TargetFacingYaw = WorldYaw;
    if (Root)
        Root->SetWorldLocation(WorldFeet);
}

void ABorn2FlapAvatar::SetWalkSpeed(float CmPerSecond)
{
    GaitSpeed = CmPerSecond;
}

void ABorn2FlapAvatar::SetCarrying(bool bCarry)
{
    bCarrying = bCarry;
}

void ABorn2FlapAvatar::SetFirstPerson(bool bFirstPersonView)
{
    bFirstPerson = bFirstPersonView;
    if (Body)
        Body->SetVisibility(!bFirstPerson, true);
}

void ABorn2FlapAvatar::BeginThrow()
{
    if (bThrowing)
        return;
    bThrowing = true;
    ThrowTime = 0.f;
    bReleaseFired = false;
}

FVector ABorn2FlapAvatar::GetHeadWorldLocation() const
{
    if (Body && BoneHead != INDEX_NONE)
        return Body->GetBoneLocation(Body->GetBoneName(BoneHead), EBoneSpaces::WorldSpace);
    return GetActorLocation() + FVector(0.f, 0.f, 160.f);
}

void ABorn2FlapAvatar::PoseBody(bool bMoving, float Dt)
{
    const float K = GaitBlend;
    const float L = FMath::Sin(GaitPhase);
    const float R = FMath::Sin(GaitPhase + 3.1415927f);

    // Legs: hip swing + knee flex on the recovery (swinging) phase.
    SetBoneRotation(BoneLUpLeg, FRotator(L * 38.f * K, 0.f, 0.f));
    SetBoneRotation(BoneRUpLeg, FRotator(R * 38.f * K, 0.f, 0.f));
    SetBoneRotation(BoneLLeg, FRotator(FMath::Max(0.f, -L) * 58.f * K, 0.f, 0.f));
    SetBoneRotation(BoneRLeg, FRotator(FMath::Max(0.f, -R) * 58.f * K, 0.f, 0.f));

    // Torso sway / vertical bob.
    const float Bob = K * FMath::Abs(FMath::Sin(GaitPhase * 2.f)) * 2.5f;
    const float Sway = bMoving ? -Bob * 0.2f : FMath::Sin(float(WorldTime) * 1.5f) * 1.5f;
    SetBoneRotation(BoneSpine, FRotator(Sway, 0.f, 0.f));

    // Arms.
    if (bCarrying)
    {
        const float ShoulderPitch = -70.f, ElbowPitch = -95.f;
        SetBoneRotation(BoneLArm, FRotator(ShoulderPitch, 10.f, 0.f));
        SetBoneRotation(BoneRArm, FRotator(ShoulderPitch, -10.f, 0.f));
        SetBoneRotation(BoneLForeArm, FRotator(ElbowPitch, 0.f, 0.f));
        SetBoneRotation(BoneRForeArm, FRotator(ElbowPitch, 0.f, 0.f));
    }
    else
    {
        const float ArmSwing = 26.f * K;
        SetBoneRotation(BoneLArm, FRotator(-R * ArmSwing, 8.f, 0.f));
        SetBoneRotation(BoneRArm, FRotator(-L * ArmSwing, -8.f, 0.f));
        SetBoneRotation(BoneLForeArm, FRotator(20.f, 0.f, 0.f));
        SetBoneRotation(BoneRForeArm, FRotator(20.f, 0.f, 0.f));
    }
    UpdateCarrySocket();
}

void ABorn2FlapAvatar::PoseThrow(float Dt)
{
    ThrowTime += Dt;
    const float T = ThrowDuration > 0.f ? FMath::Clamp(ThrowTime / ThrowDuration, 0.f, 1.f) : 1.f;

    float ShoulderPitch, ElbowPitch, SpinePitch;
    if (T < ThrowReleaseFraction)
    {
        const float K = T / ThrowReleaseFraction;
        const float E = K * K * (3.f - 2.f * K);   // smoothstep
        ShoulderPitch = FMath::Lerp(-70.f, -165.f, E);
        ElbowPitch = FMath::Lerp(-95.f, -30.f, E);
        SpinePitch = FMath::Lerp(0.f, -12.f, E);
    }
    else
    {
        const float K = FMath::Clamp((T - ThrowReleaseFraction) / (1.f - ThrowReleaseFraction), 0.f, 1.f);
        const float E = 1.f - FMath::Pow(1.f - K, 3.f);   // ease-out cubic
        ShoulderPitch = FMath::Lerp(-165.f, -25.f, E);
        ElbowPitch = FMath::Lerp(-30.f, -15.f, E);
        SpinePitch = FMath::Lerp(-12.f, 6.f, E);
    }

    SetBoneRotation(BoneLArm, FRotator(ShoulderPitch, 6.f, 0.f));
    SetBoneRotation(BoneRArm, FRotator(ShoulderPitch, -6.f, 0.f));
    SetBoneRotation(BoneLForeArm, FRotator(ElbowPitch, 0.f, 0.f));
    SetBoneRotation(BoneRForeArm, FRotator(ElbowPitch, 0.f, 0.f));
    SetBoneRotation(BoneSpine, FRotator(SpinePitch, 0.f, 0.f));
    UpdateCarrySocket();

    if (!bReleaseFired && T >= ThrowReleaseFraction)
    {
        bReleaseFired = true;
        OnThrowRelease.ExecuteIfBound();
    }
    if (T >= 1.f)
    {
        bThrowing = false;
        ThrowTime = 0.f;
    }
}

void ABorn2FlapAvatar::UpdateCarrySocket()
{
    if (!CarrySocket || !Body)
        return;
    FVector Mid = Body->GetComponentLocation();
    if (BoneLHand != INDEX_NONE && BoneRHand != INDEX_NONE)
    {
        Mid = (Body->GetBoneLocation(Body->GetBoneName(BoneLHand), EBoneSpaces::WorldSpace) +
               Body->GetBoneLocation(Body->GetBoneName(BoneRHand), EBoneSpaces::WorldSpace)) * 0.5f;
    }
    CarrySocket->SetRelativeLocation(Body->GetComponentTransform().InverseTransformPosition(Mid));
}