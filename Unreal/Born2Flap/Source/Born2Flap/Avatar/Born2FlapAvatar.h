#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapAvatar.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UAnimSequence;

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

    // John (the founder), Erika, + eight modern Mixamo characters, each with
    // its own mesh, skeleton and baked clip set.
    void SetPerson(int32 Index);
    static int32 GetPersonCount() { return 10; }
    static FString GetPersonName(int32 Index);

    void SetPlacement(const FVector& WorldFeet, float WorldYaw);
    // Movement relative to the avatar's facing direction: +X = forward (walk),
    // +Y = strafe right, in cm/s. Drives gait selection and cadence matching.
    void SetLocalVelocity(const FVector& LocalVelocity);
    void SetCarrying(bool bCarry);
    void SetFirstPerson(bool bFirstPerson);

    void BeginThrow();
    bool IsThrowing() const { return bThrowing; }
    // Gaze pitch at the throw aim (degrees, up positive). Drives the wind-up
    // lean so the throw is articulated in height as well as direction.
    void SetThrowAimPitch(float PitchDeg);
    // One-shot catch: the pilot's hands take the returning bird out of the air
    // (SK_FounderCatch), then return to idle. Played on a successful handcatch.
    void BeginCatch();
    bool IsCatching() const { return bCatching; }
    float GetFacingYaw() const { return FacingYaw; }
    USceneComponent* GetCarrySocket() const { return CarrySocket; }
    FVector GetHeadWorldLocation() const;
    // Currently selected locomotion/one-shot clip (diagnostics/HUD).
    FName GetCurrentAnimName() const { return CurrentAnim ? CurrentAnim->GetFName() : NAME_None; }
    // One-line diagnostic: current clip, playback position and hand/spine bone
    // world locations. Used by the throw test to capture the real pose curve.
    FString DebugThrowState() const;

    DECLARE_DELEGATE(FOnThrowRelease);
    FOnThrowRelease OnThrowRelease;

private:
    void ResolveSkeleton();
    int32 FindBone(const TCHAR* Substring) const;
    void DetectForwardAxis();
    void CaptureRestPoses();
    void LoadPerson(int32 Index);
    void ApplySkin();
    void PoseBody(bool bMoving, float Dt);
    void PoseThrow(float Dt);
    float ComputeThrowLean() const;
    void SetBoneRotation(int32 BoneIndex, const FRotator& Rot);
    void UpdateCarrySocket();

    UPROPERTY() USceneComponent* Root = nullptr;
    UPROPERTY() USkeletalMeshComponent* Body = nullptr;
    UPROPERTY() USkeletalMeshComponent* BodyExtra = nullptr;  // X-Bot second body section
    UPROPERTY() USceneComponent* CarrySocket = nullptr;

    // Retargeted Mixamo locomotion clips (kept alive by UPROPERTY; a bare
    // TObjectPtr would be GC'd and fault on the first runtime collect).
    UPROPERTY() UAnimSequence* AnimIdle = nullptr;
    UPROPERTY() UAnimSequence* AnimWalk = nullptr;
    UPROPERTY() UAnimSequence* AnimRun = nullptr;
    UPROPERTY() UAnimSequence* AnimThrow = nullptr;
    UPROPERTY() UAnimSequence* AnimCatch = nullptr;
    UPROPERTY() UAnimSequence* AnimStand = nullptr;     // relaxed idle stance
    UPROPERTY() UAnimSequence* AnimStrafe = nullptr;    // sideways gait
    UPROPERTY() UAnimSequence* AnimWalkBack = nullptr;  // backward gait
    UAnimSequence* CurrentAnim = nullptr;

    int32 BoneHips = INDEX_NONE, BoneSpine = INDEX_NONE, BoneHead = INDEX_NONE;
    int32 BoneLArm = INDEX_NONE, BoneLForeArm = INDEX_NONE, BoneLHand = INDEX_NONE;
    int32 BoneRArm = INDEX_NONE, BoneRForeArm = INDEX_NONE, BoneRHand = INDEX_NONE;
    int32 BoneLUpLeg = INDEX_NONE, BoneLLeg = INDEX_NONE, BoneLFoot = INDEX_NONE;
    int32 BoneRUpLeg = INDEX_NONE, BoneRLeg = INDEX_NONE, BoneRFoot = INDEX_NONE;

    TMap<int32, FVector> RestTranslations;
    TArray<UMaterialInterface*> OriginalMaterials;
    UPROPERTY() UMaterialInstanceDynamic* SkinMID = nullptr;

    int32 SkinIndex = 0;
    int32 PersonIndex = -1;
    FVector FeetWorld = FVector::ZeroVector;
    float FacingYaw = 0.f;
    float TargetFacingYaw = 0.f;
    // Yaw of the mesh's own forward axis (in the skeleton's local frame) relative
    // to +X. Detected at BeginPlay so any glTF axis convention is handled. The
    // Founder rig faces +Y, so this is typically +90 degrees.
    float ForwardYawOffset = 0.f;
    float LocalForwardSpeed = 0.f;
    float LocalStrafeSpeed = 0.f;
    float GaitPhase = 0.f;
    float GaitBlend = 0.f;
    float MeshScale = 1.f;
    bool bCarrying = false;
    bool bFirstPerson = false;
    bool bThrowing = false;
    float ThrowTime = 0.f;
    bool bReleaseFired = false;
    float ThrowAimPitch = 0.f;
    bool bCatching = false;
    float CatchTime = 0.f;
    double WorldTime = 0.0;
};