#include "Avatar/Born2FlapAvatar.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr float TargetHeightCm = 180.f;
constexpr float IdleSpeedCm = 15.f;
constexpr float RunSpeedCm = 380.f;

struct FB2FAvatarPalette
{
    FLinearColor Cloth, Skin, Hair, Accent;
};

// Distinct realistic persons, each a separate rigged human (Mixamo). Person 0
// is the Founder character. Person 1 (the Hunter) is byte-identical to the
// founder source, so it reuses the same clean mesh until a second human is
// committed. Each person is a list of skeletal-mesh sections sharing one
// skeleton (the founder GLB splits into gambeson body x2 + hair).
const TArray<TArray<FString>>& AvatarPersonSections()
{
    static const TArray<TArray<FString>> Persons = {
        {
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72.Object_72"),
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_73.Object_73"),
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_75.Object_75"),
        },
        {
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72.Object_72"),
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_73.Object_73"),
            TEXT("/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_75.Object_75"),
        },
    };
    return Persons;
}

const TArray<FB2FAvatarPalette>& AvatarSkins()
{
    static const TArray<FB2FAvatarPalette> Skins = {
        { FLinearColor(0.16f, 0.22f, 0.18f), FLinearColor(0.76f, 0.58f, 0.45f),
          FLinearColor(0.09f, 0.07f, 0.05f), FLinearColor(0.80f, 0.62f, 0.20f) },
        { FLinearColor(0.02f, 0.27f, 0.30f), FLinearColor(0.85f, 0.70f, 0.55f),
          FLinearColor(0.03f, 0.03f, 0.04f), FLinearColor(1.0f, 0.49f, 0.055f) },
        { FLinearColor(0.05f, 0.05f, 0.10f), FLinearColor(0.70f, 0.75f, 0.80f),
          FLinearColor(0.02f, 0.02f, 0.03f), FLinearColor(0.0f, 1.0f, 0.9f) },
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
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);

    CarrySocket = CreateDefaultSubobject<USceneComponent>(TEXT("CarrySocket"));
    CarrySocket->SetupAttachment(Body);
}

void ABorn2FlapAvatar::ApplyMesh()
{
    if (!Body)
        return;

    const TArray<TArray<FString>>& Persons = AvatarPersonSections();
    const TArray<FString>& Paths = Persons[FMath::Clamp(PersonIndex, 0, Persons.Num() - 1)];

    EnsureSections(Paths.Num());

    TArray<USkeletalMeshComponent*> All;
    All.Add(Body);
    All.Append(ExtraSections);

    USkeletalMesh* PrimaryMesh = nullptr;
    for (int32 i = 0; i < Paths.Num(); ++i)
    {
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Paths[i]);
        if (!Mesh || !All.IsValidIndex(i))
            continue;
        All[i]->SetSkeletalMesh(Mesh);
        All[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        All[i]->SetCastShadow(true);
        if (i > 0)
            All[i]->SetLeaderPoseComponent(Body);
        if (i == 0)
            PrimaryMesh = Mesh;
    }

    ResolveBones();

    // The imported glTF rig rests on its back in component space; recover the
    // character's intrinsic basis from the static reference skeleton and rotate
    // the whole mesh upright. Self-healing and independent of the importer's
    // coordinate conversion.
    if (PrimaryMesh)
        ApplyUprightTransform(PrimaryMesh->GetRefSkeleton());

    OriginalMaterials.Reset();
    for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
        OriginalMaterials.Add(Body->GetMaterial(i));
    SkinMID = nullptr;
    ApplySkin();
}

void ABorn2FlapAvatar::ApplyUprightTransform(const FReferenceSkeleton& RefSkel)
{
    const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();
    if (RefPose.Num() == 0)
        return;

    auto RefIdx = [&](const TCHAR* Sub) -> int32
    {
        for (int32 i = 0; i < RefSkel.GetNum(); ++i)
            if (RefSkel.GetBoneName(i).ToString().Contains(Sub))
                return i;
        return INDEX_NONE;
    };
    auto CompPos = [&](int32 RefBoneIdx) -> FVector
    {
        if (RefBoneIdx == INDEX_NONE || RefBoneIdx >= RefPose.Num())
            return FVector::ZeroVector;
        TArray<int32> Chain;
        int32 Cur = RefBoneIdx;
        while (Cur != INDEX_NONE && Chain.Num() < 64)
        {
            Chain.Add(Cur);
            Cur = RefSkel.GetParentIndex(Cur);
        }
        FTransform Acc = FTransform::Identity;
        for (int32 k = Chain.Num() - 1; k >= 0; --k)
            Acc = Acc * RefPose[Chain[k]];
        return Acc.GetLocation();
    };

    const int32 HipsIdx = RefIdx(TEXT("Hips"));
    const int32 HeadIdx = RefIdx(TEXT("Head"));
    const int32 LArmIdx = RefIdx(TEXT("LeftArm"));
    const int32 RArmIdx = RefIdx(TEXT("RightArm"));
    const int32 LFootIdx = RefIdx(TEXT("LeftFoot"));
    const int32 RFootIdx = RefIdx(TEXT("RightFoot"));

    const FVector Hips = CompPos(HipsIdx);
    const FVector Head = CompPos(HeadIdx);
    const FVector RightRaw = CompPos(RArmIdx) - CompPos(LArmIdx);
    const FVector UpRaw = Head - Hips;

    FVector Up = UpRaw.GetSafeNormal();
    FVector Right = RightRaw.GetSafeNormal();
    FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
    Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
    Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

    if (Up.IsNearlyZero() || Right.IsNearlyZero() || Forward.IsNearlyZero())
        return;

    const FMatrix Basis(Forward, Right, Up, FVector::ZeroVector);
    const FRotator Stand = Basis.Rotator();

    FVector FootMid = FVector::ZeroVector;
    int32 FootCount = 0;
    if (LFootIdx != INDEX_NONE) { FootMid += CompPos(LFootIdx); ++FootCount; }
    if (RFootIdx != INDEX_NONE) { FootMid += CompPos(RFootIdx); ++FootCount; }
    float MeshScale = 1.f;
    if (FootCount == 2)
    {
        FootMid *= 0.5f;
        const float StandingHeight = FVector::DotProduct(Head - FootMid, Up);
        if (StandingHeight > 1.f)
            MeshScale = TargetHeightCm / StandingHeight;
    }
    const FVector RelLoc(0.f, 0.f, -Stand.RotateVector(FootMid).Z * MeshScale);

    Body->SetRelativeRotation(Stand);
    Body->SetRelativeScale3D(FVector(MeshScale));
    Body->SetRelativeLocation(RelLoc);
    for (USkeletalMeshComponent* Sec : ExtraSections)
    {
        if (!Sec)
            continue;
        Sec->SetRelativeRotation(Stand);
        Sec->SetRelativeScale3D(FVector(MeshScale));
        Sec->SetRelativeLocation(RelLoc);
    }

    UE_LOG(LogTemp, Log, TEXT("Avatar upright: Up=(%.2f,%.2f,%.2f) Fwd=(%.2f,%.2f,%.2f) Right=(%.2f,%.2f,%.2f) scale=%.3f Stand=%s"),
        Up.X, Up.Y, Up.Z, Forward.X, Forward.Y, Forward.Z, Right.X, Right.Y, Right.Z,
        MeshScale, *Stand.ToString());
}

void ABorn2FlapAvatar::EnsureSections(int32 Count)
{
    const int32 Target = FMath::Max(0, Count - 1);
    const int32 Current = ExtraSections.Num();
    if (Target > Current)
    {
        for (int32 i = Current; i < Target; ++i)
        {
            USkeletalMeshComponent* Sec = NewObject<USkeletalMeshComponent>(
                this, *FString::Printf(TEXT("Section%d"), i + 1));
            Sec->SetupAttachment(Root);
            Sec->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Sec->SetCastShadow(true);
            Sec->RegisterComponent();
            ExtraSections.Add(Sec);
        }
    }
    else if (Target < Current)
    {
        for (int32 i = Current - 1; i >= Target; --i)
        {
            ExtraSections[i]->DestroyComponent();
            ExtraSections.RemoveAt(i);
        }
    }
}

void ABorn2FlapAvatar::ResolveBones()
{
    if (!Body)
        return;
    auto Find = [&](const TCHAR* Sub) -> int32
    {
        const int32 N = Body->GetNumBones();
        for (int32 i = 0; i < N; ++i)
            if (Body->GetBoneName(i).ToString().Contains(Sub))
                return i;
        return INDEX_NONE;
    };
    BoneHead = Find(TEXT("Head"));
    BoneLHand = Find(TEXT("LeftHand"));
    BoneRHand = Find(TEXT("RightHand"));
}

void ABorn2FlapAvatar::LoadAnimations()
{
    AnimIdle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Idle"));
    AnimStanding = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Standing"));
    AnimWalk = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Walk"));
    AnimRun = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Run"));
    AnimWalkBackward = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_WalkBackward"));
    AnimStrafeRight = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_StrafeRight"));
    AnimThrow = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Throw"));
    AnimCatch = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Actor/Clean/Animations/AM_Catch"));

    UE_LOG(LogTemp, Log, TEXT("Avatar anims: idle=%d walk=%d run=%d throw=%d catch=%d"),
        AnimIdle != nullptr, AnimWalk != nullptr, AnimRun != nullptr,
        AnimThrow != nullptr, AnimCatch != nullptr);
}

void ABorn2FlapAvatar::PlayState(EAvatarAnimState State, bool bLooping)
{
    if (!Body)
        return;
    UAnimSequence* Clip = nullptr;
    switch (State)
    {
        case EAvatarAnimState::Idle:         Clip = AnimIdle; break;
        case EAvatarAnimState::Walk:         Clip = AnimWalk; break;
        case EAvatarAnimState::Run:          Clip = AnimRun; break;
        case EAvatarAnimState::WalkBackward: Clip = AnimWalkBackward; break;
        case EAvatarAnimState::StrafeRight:  Clip = AnimStrafeRight; break;
        case EAvatarAnimState::Throw:        Clip = AnimThrow; bLooping = false; break;
        case EAvatarAnimState::Catch:        Clip = AnimCatch; bLooping = false; break;
        case EAvatarAnimState::Carry:        Clip = AnimStanding ? AnimStanding : AnimIdle; break;
    }
    if (!Clip)
        Clip = AnimIdle;
    if (!Clip)
        return;

    // Never drive animation on a mesh whose skinned asset or skeleton is still
    // missing (async compilation / broken reference); PlayAnimation would walk
    // a null anim instance and fault at 0x0.
    USkeletalMesh* SkelMesh = Body->GetSkeletalMeshAsset();
    if (!SkelMesh || !SkelMesh->GetSkeleton())
        return;

    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Body->PlayAnimation(Clip, bLooping);
}

void ABorn2FlapAvatar::BeginPlay()
{
    Super::BeginPlay();
    ApplyMesh();
    LoadAnimations();
    PlayState(EAvatarAnimState::Idle, true);
    SetFirstPerson(false);
}

void ABorn2FlapAvatar::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    FacingYaw = FMath::FInterpTo(FacingYaw, TargetFacingYaw, DeltaSeconds, 10.f);
    Root->SetWorldLocation(FeetWorld);
    Root->SetWorldRotation(FRotator(0.f, FacingYaw, 0.f));

    if (bThrowing)
    {
        ThrowTime += DeltaSeconds;
        const float ThrowLen = AnimThrow ? FMath::Max(0.1f, AnimThrow->GetPlayLength()) : 0.8f;
        if (!bReleaseFired && ThrowTime >= ThrowLen * 0.40f)
        {
            bReleaseFired = true;
            OnThrowRelease.ExecuteIfBound();
        }
        if (ThrowTime >= ThrowLen)
        {
            bThrowing = false;
            PlayState(CurrentState, true);
        }
    }
    else if (bCatching)
    {
        CatchTime += DeltaSeconds;
        const float CatchLen = AnimCatch ? FMath::Max(0.1f, AnimCatch->GetPlayLength()) : 1.0f;
        if (CatchTime >= CatchLen)
        {
            bCatching = false;
            PlayState(CurrentState, true);
        }
    }
    else
    {
        EAvatarAnimState Desired;
        const float S = MoveSpeed;
        if (S < IdleSpeedCm)
        {
            Desired = EAvatarAnimState::Idle;
        }
        else
        {
            const float Fwd = LocalMoveDir.X;
            const float Right = LocalMoveDir.Y;
            if (FMath::Abs(Right) > FMath::Abs(Fwd) * 0.8f)
                Desired = EAvatarAnimState::StrafeRight;
            else if (Fwd < -0.3f)
                Desired = EAvatarAnimState::WalkBackward;
            else if (S > RunSpeedCm)
                Desired = EAvatarAnimState::Run;
            else
                Desired = EAvatarAnimState::Walk;
        }
        if (bCarrying)
            Desired = EAvatarAnimState::Carry;

        if (Desired != CurrentState)
        {
            CurrentState = Desired;
            PlayState(CurrentState, true);
        }
    }

    UpdateCarrySocket();
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

void ABorn2FlapAvatar::SetPerson(int32 Index)
{
    PersonIndex = FMath::Clamp(Index, 0, AvatarPersonSections().Num() - 1);
    if (HasActorBegunPlay())
        ApplyMesh();
}

void ABorn2FlapAvatar::SetSkin(int32 Index)
{
    SkinIndex = Index;
    if (HasActorBegunPlay())
        ApplySkin();
}

void ABorn2FlapAvatar::ApplySkin()
{
    if (!Body)
        return;
    const auto& Palettes = AvatarSkins();
    const FB2FAvatarPalette& P = Palettes[FMath::Clamp(SkinIndex, 0, Palettes.Num() - 1)];

    if (SkinIndex == 0 && OriginalMaterials.Num() > 0)
    {
        for (int32 i = 0; i < Body->GetNumMaterials() && i < OriginalMaterials.Num(); ++i)
            Body->SetMaterial(i, OriginalMaterials[i]);
        return;
    }

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

void ABorn2FlapAvatar::SetPlacement(const FVector& WorldFeet, float WorldYaw)
{
    FeetWorld = WorldFeet;
    TargetFacingYaw = WorldYaw;
    if (Root)
        Root->SetWorldLocation(WorldFeet);
}

void ABorn2FlapAvatar::SetMovement(const FVector& InLocalMoveDir, float CmPerSecond)
{
    LocalMoveDir = InLocalMoveDir.SizeSquared() > 0.01f ? InLocalMoveDir.GetSafeNormal() : FVector(1.f, 0.f, 0.f);
    MoveSpeed = CmPerSecond;
}

void ABorn2FlapAvatar::SetWalkSpeed(float CmPerSecond)
{
    SetMovement(FVector(1.f, 0.f, 0.f), CmPerSecond);
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
    for (USkeletalMeshComponent* Sec : ExtraSections)
        Sec->SetVisibility(!bFirstPerson, true);
}

void ABorn2FlapAvatar::BeginThrow()
{
    if (bThrowing)
        return;
    bThrowing = true;
    bCatching = false;
    ThrowTime = 0.f;
    bReleaseFired = false;
    PlayState(EAvatarAnimState::Throw, false);
}

void ABorn2FlapAvatar::BeginCatch(bool bHeadHeight)
{
    bCatchHeadHeight = bHeadHeight;
    bCatching = true;
    bThrowing = false;
    CatchTime = 0.f;
    PlayState(EAvatarAnimState::Catch, false);
}

FVector ABorn2FlapAvatar::GetHeadWorldLocation() const
{
    if (Body && BoneHead != INDEX_NONE)
        return Body->GetBoneLocation(Body->GetBoneName(BoneHead), EBoneSpaces::WorldSpace);
    return GetActorLocation() + FVector(0.f, 0.f, 160.f);
}