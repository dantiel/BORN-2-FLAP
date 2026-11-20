#include "Avatar/Born2FlapAvatar.h"
#include "Avatar/Born2FlapAvatarAnimInstance.h"
#include "Avatar/Born2FlapFootsteps.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
// One full gait cycle covers this much forward travel (cm).
constexpr float StrideCm = 65.f;
// Throw animation: total duration and the release moment (fraction 0..1).
constexpr float ThrowDuration = 0.65f;
constexpr float ThrowReleaseFraction = 0.45f;
// Throw wind-up (SPACE held): clip-time advanced per real second while the
// raise plays — deliberately slow so the reach-back reads. The hold stops at
// the reach-back pose (a fraction of the measured hand apex), well BEFORE the
// apex itself, which already looks like the released throw.
constexpr float ThrowWindupRate = 0.5f;
// The reach-back hold scales with the charged strength: a weak throw cocks at
// a shallow reach-back, a strong one sweeps the arm further back — the throw
// then starts from that deeper wind-up on release.
constexpr float ThrowCockMin = 0.35f;   // of the hand apex at charge 0
constexpr float ThrowCockMax = 0.85f;   // of the hand apex at charge 1
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
    const TCHAR* Base;   // asset folder root, e.g. /Game/Actor/SK_Scout
    const TCHAR* Mesh;   // skeletal mesh asset name inside the folder
    const TCHAR* Clip;   // animation clip name prefix (== Mesh for the humans)
    const TCHAR* Extra;  // optional second body mesh (X-Bot's Section1), or nullptr
    // Standing height this person is normalised to (cm). Per-character so a
    // young/short pilot does not tower and a burly one does not shrink: the
    // uniform 180 cm target made some look oversized and others undersized.
    float HeightCm;
    // Per-person locomotion: ground speed (cm/s) when walking / running, and
    // the native speed each baked clip implies (cm/s). The avatar matches
    // cadence to the actual speed with play-rate = speed / native, so a
    // character's stride and pace stay individual instead of one shared gait.
    float WalkSpeedCm;
    float RunSpeedCm;
    float WalkNativeCm;
    float RunNativeCm;
    // Optional per-slot material overrides, in mesh slot order. The Mixamo
    // FBX meshes import without usable textures (their FBX texture paths point
    // at the export machine), so the PBR materials imported from the companion
    // GLB are applied here. nullptr = keep the mesh's own material for that slot.
    const TCHAR* MaterialOverrides[4];
};
const TArray<FB2FAvatarPersonDef>& AvatarPersons()
{
    static const TArray<FB2FAvatarPersonDef> Persons = {
        { TEXT("John"),     TEXT("/Game/Actor/SK_Founder"),  TEXT("SK_Founder"),  TEXT("SK_Founder"),  nullptr, 182.f, 220.f, 440.f, 120.f, 230.f,
          { nullptr, nullptr, nullptr, nullptr } },
        { TEXT("Erika"),    TEXT("/Game/Actor/SK_Erika"),    TEXT("SK_Erika"),    TEXT("SK_Erika"),    nullptr, 170.f, 210.f, 420.f, 118.f, 222.f,
          { TEXT("/Game/Actor/SK_Erika/Materials/EyeSpec_MAT.EyeSpec_MAT"),
            TEXT("/Game/Actor/SK_Erika/Materials/Akai_MAT.Akai_MAT"),
            TEXT("/Game/Actor/SK_Erika/Materials/eyelash_MAT.eyelash_MAT"),
            TEXT("/Game/Actor/SK_Erika/Materials/Body_MAT.Body_MAT") } },
        { TEXT("Amy"),      TEXT("/Game/Actor/SK_Amy"),      TEXT("SK_Amy"),      TEXT("SK_Amy"),      nullptr, 158.f, 195.f, 385.f, 112.f, 206.f,
          { TEXT("/Game/Actor/SK_Amy/Materials/Ch46_body.Ch46_body"), nullptr, nullptr, nullptr } },
        { TEXT("Michelle"), TEXT("/Game/Actor/SK_Michelle"), TEXT("SK_Michelle"), TEXT("SK_Michelle"), nullptr, 168.f, 205.f, 410.f, 115.f, 214.f,
          { TEXT("/Game/Actor/SK_Michelle/Materials/Ch03_Body.Ch03_Body"), nullptr, nullptr, nullptr } },
        { TEXT("Sophie"),   TEXT("/Game/Actor/SK_Sophie"),   TEXT("SK_Sophie"),   TEXT("SK_Sophie"),   nullptr, 172.f, 212.f, 425.f, 117.f, 218.f,
          { TEXT("/Game/Actor/SK_Sophie/Materials/Ch02_body.Ch02_body"),
            TEXT("/Game/Actor/SK_Sophie/Materials/Ch02_hair.Ch02_hair"), nullptr, nullptr } },
        { TEXT("Kate"),     TEXT("/Game/Actor/SK_Kate"),     TEXT("SK_Kate"),     TEXT("SK_Kate"),     nullptr, 168.f, 205.f, 410.f, 115.f, 214.f,
          { TEXT("/Game/Actor/SK_Kate/Materials/Ch21_body.Ch21_body"),
            TEXT("/Game/Actor/SK_Kate/Materials/Ch21_hair.Ch21_hair"), nullptr, nullptr } },
        { TEXT("James"),    TEXT("/Game/Actor/SK_James"),    TEXT("SK_James"),    TEXT("SK_James"),    nullptr, 186.f, 228.f, 460.f, 124.f, 236.f,
          { TEXT("/Game/Actor/SK_James/Materials/Ch06_body.Ch06_body"),
            TEXT("/Game/Actor/SK_James/Materials/Ch06_eyelashes.Ch06_eyelashes"),
            TEXT("/Game/Actor/SK_James/Materials/Ch06_body1.Ch06_body1"), nullptr } },
        { TEXT("Adam"),     TEXT("/Game/Actor/SK_Adam"),     TEXT("SK_Adam"),     TEXT("SK_Adam"),     nullptr, 183.f, 224.f, 452.f, 121.f, 230.f,
          { TEXT("/Game/Actor/SK_Adam/Materials/Ch08_body.Ch08_body"),
            TEXT("/Game/Actor/SK_Adam/Materials/Ch08_body1.Ch08_body1"),
            TEXT("/Game/Actor/SK_Adam/Materials/Ch08_hair.Ch08_hair"), nullptr } },
        { TEXT("Brian"),    TEXT("/Game/Actor/SK_Brian"),    TEXT("SK_Brian"),    TEXT("SK_Brian"),    nullptr, 178.f, 216.f, 434.f, 118.f, 222.f,
          { TEXT("/Game/Actor/SK_Brian/Materials/Ch01_body.Ch01_body"),
            TEXT("/Game/Actor/SK_Brian/Materials/Ch01_hair.Ch01_hair"), nullptr, nullptr } },
        { TEXT("Alex"),     TEXT("/Game/Actor/SK_Alex"),     TEXT("SK_Alex"),     TEXT("SK_Alex"),     nullptr, 176.f, 212.f, 426.f, 117.f, 218.f,
          { TEXT("/Game/Actor/SK_Alex/Materials/Ch18_Body.Ch18_Body"), nullptr, nullptr, nullptr } },
        { TEXT("Isis"),     TEXT("/Game/Actor/SK_Isis"),     TEXT("SK_Isis"),     TEXT("SK_Isis"),     nullptr, 196.f, 198.f, 470.f, 132.f, 268.f,
          { TEXT("/Game/Actor/SK_Isis/Materials/Material_0.Material_0"), nullptr, nullptr, nullptr } },
        { TEXT("Avian Wind M"), TEXT("/Game/Actor/SK_AvianWindM"), TEXT("SK_AvianWindM"), TEXT("SK_AvianWindM"), nullptr, 180.f, 220.f, 440.f, 120.f, 230.f,
          { nullptr, nullptr, nullptr, nullptr } },
        { TEXT("Avian Wind F"), TEXT("/Game/Actor/SK_AvianWindF"), TEXT("SK_AvianWindF"), TEXT("SK_AvianWindF"), nullptr, 170.f, 210.f, 420.f, 118.f, 222.f,
          { nullptr, nullptr, nullptr, nullptr } },
        { TEXT("Anubis"),  TEXT("/Game/Actor/SK_Anubis"),  TEXT("SK_Anubis"),  TEXT("SK_Anubis"),  nullptr, 200.f, 224.f, 448.f, 122.f, 232.f,
          { nullptr, nullptr, nullptr, nullptr } },
        { TEXT("Canary"),  TEXT("/Game/Actor/SK_Canary"),  TEXT("SK_Canary"),  TEXT("SK_Canary"),  nullptr, 130.f, 205.f, 410.f, 112.f, 214.f,
          { nullptr, nullptr, nullptr, nullptr } },
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
    // Use our single-node subclass so the head-look gaze can be layered onto
    // the baked clips (per-bone posing has no public USkeletalMeshComponent
    // API in UE 5.8 — the rotation is applied by the anim instance proxy).
    Body->SetAnimInstanceClass(UBorn2FlapAvatarAnimInstance::StaticClass());

    BodyExtra = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyExtra"));
    BodyExtra->SetupAttachment(Root);
    BodyExtra->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyExtra->SetCastShadow(true);
    BodyExtra->SetVisibility(false, true);

    CarrySocket = CreateDefaultSubobject<USceneComponent>(TEXT("CarrySocket"));
    CarrySocket->SetupAttachment(Body);
}

void ABorn2FlapAvatar::BeginPlay()
{
    Super::BeginPlay();
    LoadPerson(PersonIndex);
    SetFirstPerson(false);
    // The bird hangs sideways in the carry hand (dorsal side out) and eases to
    // horizontal while the throw gesture plays.
    CarryRoll = bCarryLeftHand ? -90.f : 90.f;

    // Procedural footfalls: one player per avatar, volume set by the pawn's
    // FootstepVolume preference (0..1). Pure synth — no footstep asset exists.
    Footsteps = NewObject<UBorn2FlapFootsteps>(this);
    if (Footsteps)
    {
        Footsteps->RegisterComponent();
        Footsteps->SetVolume(1.f);
    }
}

void ABorn2FlapAvatar::SetFootstepVolume(float Volume)
{
    if (Footsteps)
        Footsteps->SetVolume(Volume);
}

void ABorn2FlapAvatar::LoadPerson(int32 Index)
{
    const TArray<FB2FAvatarPersonDef>& Persons = AvatarPersons();
    Index = FMath::Clamp(Index, 0, Persons.Num() - 1);
    if (Index == PersonIndex && Body->GetSkeletalMeshAsset())
        return;
    PersonIndex = Index;
    // A different person has a different throw clip (possibly a different
    // length/curve): the hand-apex halt must be re-measured for the new clip.
    ThrowHaltTime = -1.f;
    bThrowHaltComputed = false;
    const FB2FAvatarPersonDef& P = Persons[Index];
    // Per-person locomotion: ground pace and the native clip speed the cadence
    // is matched against. Falls back to the shared defaults when a def leaves
    // them at zero (older import tables).
    PersonWalkSpeed  = P.WalkSpeedCm  > 0.f ? P.WalkSpeedCm  : 220.f;
    PersonRunSpeed   = P.RunSpeedCm   > 0.f ? P.RunSpeedCm   : 440.f;
    PersonWalkNative = P.WalkNativeCm > 0.f ? P.WalkNativeCm : 105.f;
    PersonRunNative  = P.RunNativeCm  > 0.f ? P.RunNativeCm  : 200.f;
    const FString Folder = FString(P.Base) + TEXT("/SkeletalMeshes/");

    // Mesh + skeleton come from the same glTF import; the Mixamo locomotion
    // clips are baked into that GLB (sharing its glTF frame — no FBX retarget
    // axis mismatch), so every person plays the identical clip set.
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Folder + P.Mesh + TEXT(".") + P.Mesh));
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Avatar: mesh missing for person %d (%s) - run the character import tool"), Index, P.Mesh);
        return;
    }
    Body->SetSkeletalMesh(Mesh);

    // Clear any leftover override materials from the previously loaded person.
    // USkeletalMeshComponent keeps its OverrideMaterials array across
    // SetSkeletalMesh, so a person with fewer (or no) overrides would
    // otherwise inherit the previous person's materials — the "avian shows
    // John's texture" / "John shows the avian's texture" leak. A null override
    // falls back to the mesh's own imported material for that slot.
    for (int32 MatIdx = 0; MatIdx < Body->GetNumMaterials(); ++MatIdx)
        Body->SetMaterial(MatIdx, nullptr);

    // Apply the per-person PBR material overrides (imported from the companion
    // GLB; the FBX meshes themselves ship without usable textures). Kept
    // BEFORE the OriginalMaterials cache below so ApplySkin restores these.
    for (int32 MatIdx = 0; MatIdx < 4 && P.MaterialOverrides[MatIdx]; ++MatIdx)
    {
        if (MatIdx >= Body->GetNumMaterials())
            break;
        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, P.MaterialOverrides[MatIdx]);
        if (Mat)
            Body->SetMaterial(MatIdx, Mat);
    }

    // A person may carry a second body section sharing the same mixamorig
    // skeleton (P.Extra): drive it from the leader component so both halves
    // play the identical clip without a second animation graph.
    if (BodyExtra)
    {
        USkeletalMesh* ExtraMesh = P.Extra
            ? LoadObject<USkeletalMesh>(nullptr, *(Folder + P.Extra + TEXT(".") + P.Extra))
            : nullptr;
        BodyExtra->SetSkeletalMesh(ExtraMesh);
        BodyExtra->SetLeaderPoseComponent(ExtraMesh ? Body : nullptr);
        BodyExtra->SetVisibility(ExtraMesh != nullptr, true);
    }

    auto LoadAnim = [&](const TCHAR* ClipSuffix) {
        return LoadObject<UAnimSequence>(nullptr, *(Folder + P.Clip + ClipSuffix + TEXT(".") + P.Clip + ClipSuffix));
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

    // Strip the Mixamo root motion (the Hips' forward translation) so the baked
    // clips play in place and the pilot stops jumping forward every gait cycle.
    if (UBorn2FlapAvatarAnimInstance* AI = Cast<UBorn2FlapAvatarAnimInstance>(Body->GetAnimInstance()))
        AI->SetRootBone(BoneHips);

    // Start in the relaxed idle stance. NOTE: the Standing clip is a kneel/crouch
    // (legs folded), not a standing pose, so the neutral Idle is used here.
    UAnimSequence* Initial = AnimIdle;
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

    // Normalise to the person's own standing height and rest the feet on the
    // ground. The skeleton root is the hips, so lift the mesh until the foot
    // bone reaches the root. Some imports land the geometry displaced from
    // the skeleton root along a horizontal axis (the queen's FBX round-trip
    // puts her body ~5.7 m off the pivot), so the bounds centre is
    // counteracted too — the body always sits on the pivot.
    if (Body->GetSkeletalMeshAsset())
    {
        // Reset any leftover transform from a previously loaded person: the
        // scale/location set below must be measured from identity, otherwise a
        // switch between characters compounds the old scale into the new one.
        Body->SetRelativeScale3D(FVector::OneVector);
        Body->SetRelativeLocation(FVector::ZeroVector);

        const FBoxSphereBounds B = Body->CalcBounds(Body->GetComponentTransform());
        const float HeightLocal = B.BoxExtent.Z * 2.f;
        const float WantCm = P.HeightCm > 1.f ? P.HeightCm : TargetHeightCm;
        MeshScale = HeightLocal > 1.f ? WantCm / HeightLocal : 1.f;
        // Guard against degenerate imports: a mesh whose bounds collapse to a
        // sliver would otherwise scale the character to skyscraper size. A
        // sane human mesh normalises to ~1x; clamp outliers and flag the asset.
        // Guard against truly degenerate imports (a collapsed sliver whose
        // bounds would otherwise scale the character to skyscraper size). The
        // threshold is deliberately wide: a metres-vs-centimetres import error
        // (e.g. the queen landing at ~2 cm and needing ~98x to reach her 196 cm
        // target) must still normalise correctly instead of being flattened to
        // a barely-visible 2 cm figure.
        if (MeshScale > 200.f || MeshScale < 0.005f)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Avatar: suspicious mesh scale %.2f for %s (bounds height %.1fcm) — clamped to 1.0; re-import the mesh"),
                MeshScale, P.Name, HeightLocal);
            MeshScale = 1.f;
        }
        Body->SetRelativeScale3D(FVector(MeshScale));
        float OffsetX = 0.f, OffsetY = 0.f;
        const FBoxSphereBounds BLocal = Body->CalcBounds(FTransform::Identity);
        OffsetX = -BLocal.Origin.X * MeshScale;
        OffsetY = -BLocal.Origin.Y * MeshScale;
        if (BoneLFoot != INDEX_NONE)
        {
            const float FootZ = Body->GetBoneLocation(Body->GetBoneName(BoneLFoot), EBoneSpaces::ComponentSpace).Z;
            Body->SetRelativeLocation(FVector(OffsetX, OffsetY, -FootZ * MeshScale));
        }
        else
        {
            Body->SetRelativeLocation(FVector(OffsetX, OffsetY, 0.f));
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
    const float TiltDeg = ComputeThrowLean();
    if (FMath::Abs(TiltDeg) > 0.01f)
    {
        // Height articulation: tilt the whole throw arc with the launch pitch
        // (lean back for a high lob, forward for a flat throw). The lean is a
        // world-space rotation around the lateral axis perpendicular to the
        // facing, so it is independent of the mesh's forward convention.
        const FVector Lateral = FRotator(0.f, FacingYaw, 0.f)
            .RotateVector(FVector::XAxisVector)
            .RotateAngleAxis(90.f, FVector::ZAxisVector);
        RootRot = FQuat(Lateral, FMath::DegreesToRadians(-TiltDeg)) * RootRot;
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
    if (bThrowing || bThrowWindup)
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
        // Strafe/back cadence ride the person's walk stride proportionally so
        // a short-stepped character also side-steps and back-steps short.
        NativeSpeed = PersonWalkNative * (StrafeNativeSpeed / WalkNativeSpeed);
    }
    else if (Fwd < -12.f)
    {
        Target = AnimWalkBack ? AnimWalkBack : AnimWalk;
        NativeSpeed = PersonWalkNative * (WalkBackNativeSpeed / WalkNativeSpeed);
    }
    else if (Speed > 260.f)
    {
        Target = AnimRun;
        NativeSpeed = PersonRunNative;
    }
    else
    {
        Target = AnimWalk;
        NativeSpeed = PersonWalkNative;
    }

    if (Target && Target != CurrentAnim)
    {
        CurrentAnim = Target;
        Body->SetAnimation(Target);
        if (!bThrowing && !bCatching && !bThrowWindup)
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
    if (Target && !bThrowing && !bCatching && !bThrowWindup && bMoving)
        Body->SetPlayRate(FMath::Clamp(Speed / NativeSpeed, PlayRateMin, PlayRateMax));
    else if (!bThrowing && !bCatching && !bThrowWindup)
        Body->SetPlayRate(1.f);

    // Throw wind-up (SPACE held): drive the throw clip SLOWLY up to the reach-
    // back hold — a fraction of the measured hand apex — and hold the bird
    // there. The apex itself (hand up and already forward) is reserved for the
    // release sweep, so the held pose must stay before it.
    if (bThrowWindup)
    {
        const float ThrowDur = AnimThrow ? AnimThrow->GetPlayLength() : ThrowDuration;
        // The cock pose follows the charge live (the wheel tunes the strength
        // while SPACE is held): constant-rate motion toward the charge-
        // dependent reach-back, so more strength winds the arm back further
        // and less strength eases it out again.
        ThrowTime = FMath::FInterpConstantTo(ThrowTime, GetThrowCockTime(ThrowDur), DeltaSeconds, ThrowWindupRate);
        if (AnimThrow)
            Body->SetPosition(ThrowTime, false);
    }

    // Throw release (SPACE released): reach back FIRST — finish the wind-up
    // part at the slow rate until the hand apex (the arm sweeps up behind the
    // head) — then play forth at normal speed for the throw; the release fires
    // inside the fast part at its natural frame. Auto-return to idle/walk
    // when the clip ends.
    if (bThrowing)
    {
        const float ThrowDur = AnimThrow ? AnimThrow->GetPlayLength() : ThrowDuration;
        const float ApexTime = ThrowHaltTime >= 0.f ? ThrowHaltTime : ThrowDur * ThrowReleaseFraction;
        if (ThrowTime < ApexTime)
            ThrowTime = FMath::Min(ThrowTime + DeltaSeconds * ThrowWindupRate, ApexTime);
        else
            ThrowTime += DeltaSeconds;
        if (AnimThrow)
            Body->SetPosition(ThrowTime, false);
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

    UpdateCarrySocket(DeltaSeconds);

    // Head-follow gaze: drive the Head bone (not the body) toward the bird.
    // The throw/catch one-shots animate the head themselves, so suspend the
    // look while one of those plays.
    if (UBorn2FlapAvatarAnimInstance* HeadAnim = Cast<UBorn2FlapAvatarAnimInstance>(Body->GetAnimInstance()))
    {
        const bool bLookAtBird = !bThrowing && !bThrowWindup && !bCatching;
        HeadAnim->SetHeadLook(bLookAtBird ? HeadLookPitch : 0.f, ForwardYawOffset, BoneHead);
    }
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

int32 ABorn2FlapAvatar::GetPersonCount()
{
    return AvatarPersons().Num();
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
    if (BodyExtra && BodyExtra->GetSkeletalMeshAsset())
        BodyExtra->SetVisibility(!bFirstPerson, true);
}

void ABorn2FlapAvatar::SetThrowAimPitch(float PitchDeg)
{
    ThrowAimPitch = PitchDeg;
}

void ABorn2FlapAvatar::SetHeadLookPitch(float PitchDeg)
{
    HeadLookPitch = PitchDeg;
}

void ABorn2FlapAvatar::SetThrowCharge(float Charge01)
{
    ThrowCharge = FMath::Clamp(Charge01, 0.f, 1.f);
}

void ABorn2FlapAvatar::BeginThrow()
{
    if (bThrowing)
        return;
    // A new throw interrupts an unfinished catch pose.
    bCatching = false;
    CatchTime = 0.f;
    // Complete the throw from wherever the wind-up left the clock: a long hold
    // is already at the cocked position (the bird leaves on the next tick), a
    // quick tap finishes the remaining wind-up first.
    bThrowWindup = false;
    bThrowing = true;
    bReleaseFired = false;
}

void ABorn2FlapAvatar::BeginThrowWindup()
{
    if (bThrowWindup || bThrowing)
        return;
    // Interrupt an unfinished catch pose; a new wind-up always starts from idle.
    bCatching = false;
    CatchTime = 0.f;
    bThrowWindup = true;
    ThrowTime = 0.f;
    ThrowCharge = 0.f;
    bReleaseFired = false;
    // Measure the throw clip's hand-apex once (per person) so the wind-up can
    // halt at the first moment the bird is lifted high instead of the release
    // frame.
    if (!bThrowHaltComputed)
        ComputeThrowHalt();
}

void ABorn2FlapAvatar::ComputeThrowHalt()
{
    ThrowHaltTime = -1.f;
    bThrowHaltComputed = true;
    if (!AnimThrow || !Body || Body->GetNumBones() == 0)
        return;
    const float Dur = AnimThrow->GetPlayLength();
    if (Dur <= 0.f)
        return;

    // Save the live playback state so the scrub below is invisible.
    UAnimSequence* PrevAnim = CurrentAnim;
    const float PrevPos = Body->GetPosition();
    const float PrevRate = Body->GetPlayRate();
    const bool bPrevPlaying = Body->IsPlaying();

    const int32 HandBone = bCarryLeftHand ? BoneLHand : BoneRHand;
    float RaiseCm = 0.f;
    if (HandBone != INDEX_NONE)
    {
        Body->SetAnimation(AnimThrow);
        Body->SetPosition(0.f, false);
        Body->Play(false);

        // Scrub the raise phase only (first 60% of the clip) so a higher
        // follow-through after the release can never hijack the halt.
        const int32 Samples = 60;
        const int32 N = FMath::Max(1, FMath::FloorToInt(Samples * 0.6f));
        float StartZ = 0.f;
        float MaxZ = -FLT_MAX;
        TArray<float> Heights;
        Heights.SetNumUninitialized(N + 1);
        for (int32 i = 0; i <= N; ++i)
        {
            Body->SetPosition(Dur * i / float(Samples), false);
            Body->RefreshBoneTransforms();
            const TArray<FTransform>& CS = Body->GetComponentSpaceTransforms();
            const float Z = CS.IsValidIndex(HandBone) ? float(CS[HandBone].GetLocation().Z) : 0.f;
            Heights[i] = Z;
            if (i == 0)
                StartZ = Z;
            MaxZ = FMath::Max(MaxZ, Z);
        }
        RaiseCm = MaxZ - StartZ;
        if (RaiseCm > 3.f)
        {
            // First moment the hand reaches 98% of its raise = the cocked top.
            const float Thresh = StartZ + RaiseCm * 0.98f;
            for (int32 i = 0; i <= N; ++i)
            {
                if (Heights[i] >= Thresh)
                {
                    ThrowHaltTime = Dur * i / float(Samples);
                    break;
                }
            }
        }
    }

    // Restore the live clip/position — the scrub must be invisible.
    if (PrevAnim)
    {
        Body->SetAnimation(PrevAnim);
        Body->SetPosition(PrevPos, false);
        Body->SetPlayRate(PrevRate);
        if (UAnimSingleNodeInstance* Instance = Body->GetSingleNodeInstance())
            Instance->SetPlaying(bPrevPlaying);
    }

    // Never halt after the authored release: the hand must still have room to
    // sweep forward and release the bird on SPACE up. Degenerate (flat) clips
    // fall back to a hold just before the release frame.
    const float ReleaseAt = Dur * ThrowReleaseFraction;
    if (ThrowHaltTime < 0.f)
        ThrowHaltTime = ReleaseAt * 0.7f;
    else
        ThrowHaltTime = FMath::Min(ThrowHaltTime, ReleaseAt);

    UE_LOG(LogTemp, Display, TEXT("ThrowHalt %s dur=%.2fs halt=%.3fs (%.0f%%) cockRange=%.0f%%-%.0f%% raise=%.1fcm"),
        *AnimThrow->GetName(), Dur, ThrowHaltTime, 100.f * ThrowHaltTime / Dur,
        100.f * ThrowCockMin, 100.f * ThrowCockMax, RaiseCm);
}

float ABorn2FlapAvatar::GetThrowCockTime(float ClipDuration) const
{
    // The reach-back hold: a strength-shaped fraction of the measured hand
    // apex. The apex pose itself (hand up and already forward) reads as
    // "released", so the wind-up must stop before it — at the pose where the
    // arm is still swept back. More throw strength = deeper wind-up, and the
    // release sweep then starts from that more wound-up position.
    const float Apex = ThrowHaltTime >= 0.f ? ThrowHaltTime : ClipDuration * ThrowReleaseFraction;
    return Apex * FMath::Lerp(ThrowCockMin, ThrowCockMax, ThrowCharge);
}

void ABorn2FlapAvatar::BeginCatch()
{
    if (bCatching || bThrowing || bThrowWindup)
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
    return FString::Printf(TEXT("anim=%s pos=%.2f rate=%.2f throwing=%d charge=%.2f hands=(%.0f,%.0f,%.0f)/(%.0f,%.0f,%.0f) spine=(%.0f,%.0f,%.0f)"),
        CurrentAnim ? *CurrentAnim->GetName() : TEXT("none"),
        Body ? Body->GetPosition() : -1.f,
        Body ? Body->GetPlayRate() : 0.f,
        bThrowing ? 1 : 0,
        ThrowCharge,
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
    UpdateCarrySocket(Dt);
}

float ABorn2FlapAvatar::ComputeThrowLean() const
{
    if (!bThrowing && !bThrowWindup)
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
    UpdateCarrySocket(Dt);

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

void ABorn2FlapAvatar::UpdateCarrySocket(float Dt)
{
    if (!CarrySocket || !Body)
        return;
    // The bird is held in ONE hand — the right by default (a left-handed option
    // will flip this) — not the midpoint between both, so it tracks the actual
    // carrying hand through the walk/run/catch/throw motion.
    const int32 HandBone = bCarryLeftHand ? BoneLHand : BoneRHand;
    FVector Hand = Body->GetComponentLocation();
    if (HandBone != INDEX_NONE)
        Hand = Body->GetBoneLocation(Body->GetBoneName(HandBone), EBoneSpaces::WorldSpace);
    CarrySocket->SetRelativeLocation(Body->GetComponentTransform().InverseTransformPosition(Hand));
    // The skeleton's forward axis is not necessarily +X (the Founder rig faces
    // +Y), and the socket would inherit that offset from the mesh — a bird
    // attached with zero relative rotation would sit sideways in the hand.
    // Counter-rotate by the detected offset so the bird's nose points along the
    // avatar's facing. While the throw gesture plays (wind-up or release) the
    // bird sits HORIZONTAL in the hand — wings level, nose forward — so the
    // release already holds the flight attitude. Carried (walking) it rolls
    // sideways instead: the overside of the wings (dorsal +Z) faces OUTWARD
    // from the pilot — right for the right hand, mirrored for the left.
    const float RollTarget = (bThrowing || bThrowWindup) ? 0.f : (bCarryLeftHand ? -90.f : 90.f);
    CarryRoll = FMath::FInterpTo(CarryRoll, RollTarget, Dt, 8.f);
    CarrySocket->SetRelativeRotation(FRotator(0.f, ForwardYawOffset, CarryRoll));
}