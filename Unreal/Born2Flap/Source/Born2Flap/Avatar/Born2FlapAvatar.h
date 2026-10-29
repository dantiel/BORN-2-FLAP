#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapAvatar.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UMaterialInterface;

// High-level locomotion / interaction state the avatar renders.
UENUM(BlueprintType)
enum class EAvatarAnimState : uint8
{
    Idle           UMETA(DisplayName = "Idle"),
    Walk           UMETA(DisplayName = "Walk"),
    Run            UMETA(DisplayName = "Run"),
    WalkBackward   UMETA(DisplayName = "Walk Backward"),
    StrafeRight    UMETA(DisplayName = "Strafe Right"),
    Throw          UMETA(DisplayName = "Throw"),
    Catch          UMETA(DisplayName = "Catch"),
    Carry          UMETA(DisplayName = "Carry"),
};

UCLASS()
class BORN2FLAP_API ABorn2FlapAvatar : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapAvatar();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    int32 GetSkin() const { return SkinIndex; }
    void SetSkin(int32 Index);
    static int32 GetSkinCount() { return 4; }

    // Distinct realistic persons (each its own rigged human). Index 0 = Founder.
    void SetPerson(int32 Index);
    int32 GetPerson() const { return PersonIndex; }
    static int32 GetPersonCount() { return 2; }

    void SetPlacement(const FVector& WorldFeet, float WorldYaw);
    // Legacy forward-only locomotion (the pilot always faces travel direction).
    void SetWalkSpeed(float CmPerSecond);
    // Direction-aware locomotion: LocalMoveDir in the avatar's local X/Y plane
    // (X = forward, Y = right). Enables walk/run/backward/strafe selection.
    void SetMovement(const FVector& LocalMoveDir, float CmPerSecond);
    void SetCarrying(bool bCarry);
    void SetFirstPerson(bool bFirstPerson);

    void BeginThrow();
    void BeginCatch(bool bHeadHeight);
    bool IsThrowing() const { return bThrowing; }
    bool IsCatching() const { return bCatching; }
    float GetFacingYaw() const { return FacingYaw; }
    USceneComponent* GetCarrySocket() const { return CarrySocket; }
    FVector GetHeadWorldLocation() const;

    DECLARE_DELEGATE(FOnThrowRelease);
    FOnThrowRelease OnThrowRelease;

private:
    void ApplyMesh();
    void ApplySkin();
    void EnsureSections(int32 Count);
    void LoadAnimations();
    void PlayState(EAvatarAnimState State, bool bLooping);
    void UpdateCarrySocket();
    void ResolveBones();
    void ApplyUprightTransform(const struct FReferenceSkeleton& RefSkel);

    UPROPERTY() USceneComponent* Root = nullptr;
    UPROPERTY() USkeletalMeshComponent* Body = nullptr;
    UPROPERTY() TArray<USkeletalMeshComponent*> ExtraSections;
    UPROPERTY() USceneComponent* CarrySocket = nullptr;

    // Animation clips (loaded at BeginPlay from /Game/Actor/Clean/Animations).
    // UPROPERTY so the GC keeps them alive across runtime collection; without
    // it the clips are collected after ~60s and PlayAnimation dereferences
    // dangling pointers (EXCEPTION_ACCESS_VIOLATION 0x0 in PlayState).
    UPROPERTY() TObjectPtr<UAnimSequence> AnimIdle = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimStanding = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimWalk = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimRun = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimWalkBackward = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimStrafeRight = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimThrow = nullptr;
    UPROPERTY() TObjectPtr<UAnimSequence> AnimCatch = nullptr;

    EAvatarAnimState CurrentState = EAvatarAnimState::Idle;

    int32 BoneHead = INDEX_NONE;
    int32 BoneLHand = INDEX_NONE;
    int32 BoneRHand = INDEX_NONE;

    UPROPERTY() TArray<UMaterialInterface*> OriginalMaterials;
    UPROPERTY() UMaterialInstanceDynamic* SkinMID = nullptr;

    int32 SkinIndex = 0;
    int32 PersonIndex = 0;
    FVector FeetWorld = FVector::ZeroVector;
    float FacingYaw = 0.f;
    float TargetFacingYaw = 0.f;
    FVector LocalMoveDir = FVector(1.f, 0.f, 0.f);
    float MoveSpeed = 0.f;
    bool bCarrying = false;
    bool bFirstPerson = false;
    bool bThrowing = false;
    bool bCatching = false;
    bool bCatchHeadHeight = false;
    float ThrowTime = 0.f;
    float CatchTime = 0.f;
    bool bReleaseFired = false;
};