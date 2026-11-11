#include "Avatar/Born2FlapAvatar.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
// One full gait cycle covers this much forward travel (cm).
constexpr float StrideCm = 65.f;
// Throw animation: total duration and the release moment (fraction 0..1).
constexpr float ThrowDuration = 0.65f;
constexpr float ThrowReleaseFraction = 0.45f;
// Catch animation: fallback duration when SK_FounderCatch is missing.
constexpr float CatchDuration = 1.0f;
// Normalise any imported character to this standing height (cm).
constexpr float TargetHeightCm = 180.f;
// Native (authored) ground speed each baked clip implies, in cm/s. The baked
// Mixamo clips are in-place (no root motion), so cadence is matched to the
// pawn's actual ground speed by scaling the play rate: rate = speed / native.
// Native (authored) ground speed each baked clip implies, in cm/s. The baked
// Mixamo clips are in-place (no root motion), so cadence is matched to the
// pawn's actual ground speed by scaling the play rate: rate = speed / native.
// These are tuned so the feet don't slide at the pawn's 220 cm/s walk / 440 cm/s run.
constexpr float WalkNativeSpeed = 105.f;
// A run cycle is much shorter than a walk cycle. With the baked clips all
// authored at the same 1.0 s cycle, the run must play back faster so its
// cadence exceeds the walk's (otherwise 440 cm/s runs "too slow").
constexpr float RunNativeSpeed = 200.f;
constexpr float StrafeNativeSpeed = 90.f;   // lateral side-step, shorter stride
constexpr float WalkBackNativeSpeed = 85.f;
constexpr float PlayRateMin = 0.6f;
constexpr float PlayRateMax = 3.0f;

// Colour palettes per skin index. Cloth / Skin / Hair / Accent.
struct FB2FAvatarPalette
{
    FLinearColor Cloth, Skin, Hair, Accent;
};
struct FB2FAvatarPersonDef
{
    const TCHAR* Name;   // display name (menu/settings)
    const TCHAR* Base;   // asset folder root, e.g. /Game/Actor/SK_Xbot
};
const TArray<FB2FAvatarPersonDef>& AvatarPersons()
{
    static const TArray<FB2FAvatarPersonDef> Persons = {
        { TEXT("John"),    TEXT("/Game/Actor/SK_Founder") },
        { TEXT("Scout"),   TEXT("/Game/Actor/SK_Scout") },
        { TEXT("Soldier"), TEXT("/Game/Actor/SK_Soldier") },
        { TEXT("X-Bot"),   TEXT("/Game/Actor/SK_Xbot") },
    };
    return Persons;
}
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
    LoadPerson(PersonIndex);
    SetFirstPerson(false);
}

void ABorn2FlapAvatar::LoadPerson(int32 Index)
{
    const TArray<FB2FAvatarPersonDef>& Persons = AvatarPersons();
    Index = FMath::Clamp(Index, 0, Persons.Num() - 1);
    if (Index == PersonIndex && Body->GetSkeletalMeshAsset())
        return;
    PersonIndex = Index;
    const FB2FAvatarPersonDef& P = Persons[Index];
    const FString Short = FPaths::GetBaseFilename(P.Base);
    const FString Folder = FString(P.Base) + TEXT("/SkeletalMeshes/");

    // Mesh + skeleton come from the same glTF import; the Mixamo locomotion
    // clips are baked into that GLB (sharing its glTF frame — no FBX retarget
    // axis mismatch), so every person plays the identical clip set.
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Folder + Short + TEXT(".") + Short));
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Avatar: mesh missing for person %d (%s) - run the character import tool"), Index, *Short);
        return;
    }
    Body->SetSkeletalMesh(Mesh);

    auto LoadAnim = [&](const TCHAR* Clip) {
        return LoadObject<UAnimSequence>(nullptr, *(Folder + Short + Clip + TEXT(".") + Short + Clip));
    };
    AnimIdle     = LoadAnim(TEXT("Idle"));
    AnimWalk     = LoadAnim(TEXT("Walk"));
    AnimRun      = LoadAnim(TEXT("Run"));
    AnimThrow    = LoadAnim(TEXT("Throw"));
    AnimCatch    = LoadAnim(TEXT("Catch"));
    AnimStand    = LoadAnim(TEXT("Standing"));
    AnimStrafe   = LoadAnim(TEXT("StrafeRight"));
    AnimWalkBack = LoadAnim(TEXT("WalkBackward"));

    ResolveSkeleton();
    DetectForwardAxis();

    // Start in the relaxed idle stance. NOTE: the Standing clip is a kneel/crouch
    // (legs folded), not a standing pose, so the neutral Idle is used here.
    UAnimSequence* Initial = AnimIdle;
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    if (Initial)
    {
        CurrentAnim = Initial;
        Body->SetAnimation(Initial);
        Body->Play(true);
    }
    else
    {
        CurrentAnim = nullptr;
    }

    // Normalise to a ~180 cm human and rest the feet on the ground. The skeleton
    // root is the hips, so lift the mesh until the foot bone reaches the root.
    if (Body->GetSkeletalMeshAsset())
    {
        const FBoxSphereBounds B = Body->CalcBounds(Body->GetComponentTransform());
        const float HeightLocal = B.BoxExtent.Z * 2.f;
        MeshScale = HeightLocal > 1.f ? TargetHeightCm / HeightLocal : 1.f;
        Body->SetRelativeScale3D(FVector(MeshScale));
        if (BoneLFoot != INDEX_NONE)
        {
            const float FootZ = Body->GetBoneLocation(Body->GetBoneName(BoneLFoot), EBoneSpaces::ComponentSpace).Z;
            Body->SetRelativeLocation(FVector(0.f, 0.f, -FootZ * MeshScale));
        }
    }

    // Cache this character's own materials so skin changes can restore them.
    OriginalMaterials.Reset();
    for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
        OriginalMaterials.Add(Body->GetMaterial(i));

    ApplySkin();
}

void ABorn2FlapAvatar::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    WorldTime += DeltaSeconds;

    FacingYaw = FMath::FInterpTo(FacingYaw, TargetFacingYaw, DeltaSeconds, 10.f);
    Root->SetWorldLocation(FeetWorld);
    // The mesh's forward axis is not necessarily +X (the Founder faces +Y), so
    // rotate by the detected offset to actually face the target yaw.
    FQuat RootRot = FRotator(0.f, FacingYaw - ForwardYawOffset, 0.f).Quaternion();
    const float LeanDeg = ComputeThrowLean();
    if (FMath::Abs(LeanDeg) > 0.01f)
    {
        // Height articulation: tilt the whole throw arc with the launch pitch
        // (lean back for a high lob, forward for a flat throw). The lean is a
        // world-space rotation around the lateral axis perpendicular to the
        // facing, so it is independent of the mesh's forward convention.
        const FVector Lateral = FRotator(0.f, FacingYaw, 0.f)
            .RotateVector(FVector::XAxisVector)
            .RotateAngleAxis(90.f, FVector::ZAxisVector);
        RootRot = FQuat(Lateral, FMath::DegreesToRadians(-LeanDeg)) * RootRot;
    }
    Root->SetWorldRotation(RootRot);

    const float Fwd = LocalForwardSpeed;
    const float Str = LocalStrafeSpeed;
    const float Speed = FMath::Sqrt(Fwd * Fwd + Str * Str);
    const float AbsFwd = FMath::Abs(Fwd);
    const float AbsStr = FMath::Abs(Str);
    const bool bMoving = Speed > 12.f;

    // Pick the locomotion clip and loop it. Lateral-dominant motion uses the
    // strafe gait (the avatar orbits the bird), backward motion walks backward.
    UAnimSequence* Target = nullptr;
    float NativeSpeed = 1.f;
    if (bThrowing)
    {
        Target = AnimThrow;
    }
    else if (bCatching)
    {
        Target = AnimCatch;
    }
    else if (!bMoving)
    {
        Target = AnimIdle;   // relaxed idle (Standing is a kneel, not a stance)
    }
    else if (AbsStr > AbsFwd * 1.5f)
    {
        Target = AnimStrafe ? AnimStrafe : AnimWalk;
        NativeSpeed = StrafeNativeSpeed;
    }
    else if (Fwd < -12.f)
    {
        Target = AnimWalkBack ? AnimWalkBack : AnimWalk;
        NativeSpeed = WalkBackNativeSpeed;
    }
    else if (Speed > 260.f)
    {
        Target = AnimRun;
        NativeSpeed = RunNativeSpeed;
    }
    else
    {
        Target = AnimWalk;
        NativeSpeed = WalkNativeSpeed;
    }

    if (Target && Target != CurrentAnim)
    {
        CurrentAnim = Target;
        Body->SetAnimation(Target);
        if (!bThrowing && !bCatching)
        {
            Body->Play(true);
        }
        else
        {
            // One-shots must always start at frame 0: SetAnimation carries the
            // previous loop's playback position over, and a short throw/catch
            // clip then clamps to its end and freezes on the last keyframe
            // (the "catch shows only the final pose" bug).
            Body->SetPosition(0.f, false);
            Body->Play(false);
        }
    }

    // Match limb cadence to the actual ground speed so the feet don't slide.
    // (The baked clips play too slowly at native rate for the pawn's pace.)
    if (Target && !bThrowing && !bCatching && bMoving)
        Body->SetPlayRate(FMath::Clamp(Speed / NativeSpeed, PlayRateMin, PlayRateMax));
    else if (!bThrowing && !bCatching)
        Body->SetPlayRate(1.f);

    // One-shot throw bookkeeping: release at 30% of the clip, auto-return to idle/walk.
    if (bThrowing)
    {
        ThrowTime += DeltaSeconds;
        const float ThrowDur = AnimThrow ? AnimThrow->GetPlayLength() : ThrowDuration;
        if (!bReleaseFired && ThrowTime >= ThrowDur * ThrowReleaseFraction)
        {
            bReleaseFired = true;
            OnThrowRelease.ExecuteIfBound();
        }
        if (ThrowTime >= ThrowDur)
        {
            bThrowing = false;
            ThrowTime = 0.f;
            CurrentAnim = nullptr;   // re-select idle/walk next tick
        }
    }

    // One-shot catch bookkeeping: hold the catch pose for the full clip, then
    // return to idle/walk. The caught bird rides the CarrySocket through the
    // whole arc, so the hands visibly take it out of the air.
    if (bCatching)
    {
        CatchTime += DeltaSeconds;
        const float CatchDur = AnimCatch ? AnimCatch->GetPlayLength() : CatchDuration;
        if (CatchTime >= CatchDur)
        {
            bCatching = false;
            CatchTime = 0.f;
            CurrentAnim = nullptr;   // re-select idle/walk next tick
        }
    }

    UpdateCarrySocket();
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

void ABorn2FlapAvatar::DetectForwardAxis()
{
    // Work out which local axis the character actually faces, from the rest-pose
    // skeleton, so the glTF import's axis convention is handled automatically.
    if (!Body || BoneHead == INDEX_NONE || BoneHips == INDEX_NONE ||
        BoneLHand == INDEX_NONE || BoneRHand == INDEX_NONE)
        return;

    const FVector Up = (Body->GetBoneLocation(Body->GetBoneName(BoneHead), EBoneSpaces::ComponentSpace) -
                        Body->GetBoneLocation(Body->GetBoneName(BoneHips), EBoneSpaces::ComponentSpace)).GetSafeNormal();
    // Side points from the left hand toward the right hand (the character's right).
    const FVector Side = (Body->GetBoneLocation(Body->GetBoneName(BoneRHand), EBoneSpaces::ComponentSpace) -
                          Body->GetBoneLocation(Body->GetBoneName(BoneLHand), EBoneSpaces::ComponentSpace)).GetSafeNormal();
    // In UE's left-handed basis, forward = cross(side, up).
    FVector Fwd = FVector::CrossProduct(Side, Up);
    Fwd.Z = 0.f;
    if (!Fwd.Normalize())
        return;
    ForwardYawOffset = FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X));
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
    // Every person keeps its own imported character materials. The old
    // "coloured clones of one model" tint palette was replaced by distinct
    // characters (John/Scout/Soldier/X-Bot); this remains as the hook that
    // restores the cached originals after any material override.
    if (!Body || OriginalMaterials.Num() == 0)
        return;
    for (int32 i = 0; i < Body->GetNumMaterials() && i < OriginalMaterials.Num(); ++i)
        Body->SetMaterial(i, OriginalMaterials[i]);
}

void ABorn2FlapAvatar::SetSkin(int32 Index)
{
    SkinIndex = Index;
    if (HasActorBegunPlay())
        ApplySkin();
}

void ABorn2FlapAvatar::SetPerson(int32 Index)
{
    const int32 Clamped = FMath::Clamp(Index, 0, AvatarPersons().Num() - 1);
    if (!HasActorBegunPlay())
    {
        PersonIndex = Clamped;   // BeginPlay's LoadPerson picks it up
        return;
    }
    LoadPerson(Clamped);
}

FString ABorn2FlapAvatar::GetPersonName(int32 Index)
{
    const TArray<FB2FAvatarPersonDef>& Persons = AvatarPersons();
    return Persons[FMath::Clamp(Index, 0, Persons.Num() - 1)].Name;
}

void ABorn2FlapAvatar::SetPlacement(const FVector& WorldFeet, float WorldYaw)
{
    FeetWorld = WorldFeet;
    TargetFacingYaw = WorldYaw;
    if (Root)
        Root->SetWorldLocation(WorldFeet);
}

void ABorn2FlapAvatar::SetLocalVelocity(const FVector& LocalVelocity)
{
    LocalForwardSpeed = LocalVelocity.X;
    LocalStrafeSpeed = LocalVelocity.Y;
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

void ABorn2FlapAvatar::SetThrowAimPitch(float PitchDeg)
{
    ThrowAimPitch = PitchDeg;
}

void ABorn2FlapAvatar::BeginThrow()
{
    if (bThrowing)
        return;
    // A new throw interrupts an unfinished catch pose.
    bCatching = false;
    CatchTime = 0.f;
    bThrowing = true;
    ThrowTime = 0.f;
    bReleaseFired = false;
}

void ABorn2FlapAvatar::BeginCatch()
{
    if (bCatching || bThrowing)
        return;
    bCatching = true;
    CatchTime = 0.f;
}

FVector ABorn2FlapAvatar::GetHeadWorldLocation() const
{
    if (Body && BoneHead != INDEX_NONE)
        return Body->GetBoneLocation(Body->GetBoneName(BoneHead), EBoneSpaces::WorldSpace);
    return GetActorLocation() + FVector(0.f, 0.f, 160.f);
}

FString ABorn2FlapAvatar::DebugThrowState() const
{
    auto BonePos = [&](int32 B) -> FVector {
        return (Body && B != INDEX_NONE)
            ? Body->GetBoneLocation(Body->GetBoneName(B), EBoneSpaces::WorldSpace)
            : FVector::ZeroVector;
    };
    const FVector LH = BonePos(BoneLHand), RH = BonePos(BoneRHand), SP = BonePos(BoneSpine);
    return FString::Printf(TEXT("anim=%s pos=%.2f rate=%.2f throwing=%d hands=(%.0f,%.0f,%.0f)/(%.0f,%.0f,%.0f) spine=(%.0f,%.0f,%.0f)"),
        CurrentAnim ? *CurrentAnim->GetName() : TEXT("none"),
        Body ? Body->GetPosition() : -1.f,
        Body ? Body->GetPlayRate() : 0.f,
        bThrowing ? 1 : 0,
        LH.X, LH.Y, LH.Z, RH.X, RH.Y, RH.Z, SP.X, SP.Y, SP.Z);
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

float ABorn2FlapAvatar::ComputeThrowLean() const
{
    if (!bThrowing)
        return 0.f;
    const float ThrowDur = AnimThrow ? AnimThrow->GetPlayLength() : ThrowDuration;
    const float T = ThrowDur > 0.f ? FMath::Clamp(ThrowTime / ThrowDur, 0.f, 1.f) : 1.f;
    const float AimPitch = FMath::Clamp(ThrowAimPitch, -20.f, 60.f);
    if (T < ThrowReleaseFraction)
    {
        // Wind-up: smoothstep lean back proportional to the loft.
        const float E = FMath::Clamp(T / ThrowReleaseFraction, 0.f, 1.f);
        return AimPitch * 0.30f * E * E * (3.f - 2.f * E);
    }
    // Follow-through: ease back to upright as the bird leaves the hand.
    const float E = FMath::Clamp((T - ThrowReleaseFraction) / (1.f - ThrowReleaseFraction), 0.f, 1.f);
    return AimPitch * 0.30f * (1.f - E);
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
    // The skeleton's forward axis is not necessarily +X (the Founder rig faces
    // +Y), and the socket would inherit that offset from the mesh — a bird
    // attached with zero relative rotation would sit sideways in the hand.
    // Counter-rotate by the detected offset so the held/carried bird always
    // faces the avatar's actual facing.
    CarrySocket->SetRelativeRotation(FRotator(0.f, ForwardYawOffset, 0.f));
}