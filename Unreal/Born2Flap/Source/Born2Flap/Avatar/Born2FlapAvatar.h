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

    // John (the founder), Erika, eight modern Mixamo characters, and Isis
    // (Desert Queen) — each with its own mesh, skeleton and baked clip set.
    void SetPerson(int32 Index);
    static int32 GetPersonCount();
    static FString GetPersonName(int32 Index);

    // Footstep player (procedural): the pawn's FootstepVolume preference rides
    // through here to the per-stride synth.
    void SetFootstepVolume(float Volume);
    float GetGaitPhase() const { return GaitPhase; }
    float GetGroundSpeed() const { return LocalForwardSpeed; }
    // Per-person locomotion: movement speed (cm/s) and the authored clip speed
    // the gait cadence is matched against. Different per character so each
    // pilot walks/runs at their own pace — tall long-strided vs short quick —
    // instead of one shared walk/run rate.
    float GetWalkSpeedCm() const { return PersonWalkSpeed; }
    float GetRunSpeedCm() const { return PersonRunSpeed; }

    void SetPlacement(const FVector& WorldFeet, float WorldYaw);
    // Movement relative to the avatar's facing direction: +X = forward (walk),
    // +Y = strafe right, in cm/s. Drives gait selection and cadence matching.
    void SetLocalVelocity(const FVector& LocalVelocity);
    void SetCarrying(bool bCarry);
    void SetFirstPerson(bool bFirstPerson);

    void BeginThrow();
    bool IsThrowing() const { return bThrowing; }
    // Wind-up phase (SPACE held): raise the bird to the throw's cocked position
    // and hold it there; BeginThrow() then completes the throw on release.
    void BeginThrowWindup();
    bool IsThrowWindup() const { return bThrowWindup; }
    // Gaze pitch at the throw aim (degrees, up positive). Drives the wind-up
    // lean so the throw is articulated in height as well as direction.
    void SetThrowAimPitch(float PitchDeg);
    // Follow-mode gaze pitch (degrees, up positive): the pilot watching the
    // bird. Rendered as a head-only nod by UBorn2FlapAvatarAnimInstance (the
    // body itself stays upright).
    void SetHeadLookPitch(float PitchDeg);
    // Strength of the charged throw (0..1), set live by the pawn while the
    // wheel tunes the charge during the SPACE wind-up: the cock pose follows
    // the charge, so more strength winds the arm back further.
    void SetThrowCharge(float Charge01);
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
    // Halt time of the throw clip (seconds from clip start): the first moment
    // the carrying hand reaches its highest point — the boundary between the
    // slow reach-back and the normal-speed throw. The SPACE-hold wind-up
    // freezes at a fraction of it (the reach-back pose). -1 until analysed.
    float GetThrowHaltTime() const { return ThrowHaltTime; }

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
    void UpdateCarrySocket(float Dt);
    // Scrub the throw clip once and measure the carrying hand's height curve to
    // find the wind-up halt (the first moment the bird is lifted high).
    void ComputeThrowHalt();
    // The reach-back hold: the wind-up stops at this fraction of the measured
    // hand apex — early enough that the pose reads "cocked back", not
    // "released". The apex itself stays the boundary between the slow
    // reach-back and the normal-speed throw sweep.
    float GetThrowCockTime(float ClipDuration) const;

    UPROPERTY() USceneComponent* Root = nullptr;
    UPROPERTY() USkeletalMeshComponent* Body = nullptr;
    UPROPERTY() USkeletalMeshComponent* BodyExtra = nullptr;  // second body section (leader-pose driven)
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
    UAnimSequence* CurrentAnim = nullptr;               // active clip alias
    UPROPERTY() class UBorn2FlapFootsteps* Footsteps = nullptr;

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
    // Per-person locomotion tuning (set in LoadPerson): actual ground speed
    // (cm/s) and the native speed each baked clip implies, so the play rate
    // (rate = speed / native) matches cadence to the character's stride.
    float PersonWalkSpeed = 220.f;
    float PersonRunSpeed = 440.f;
    float PersonWalkNative = 105.f;
    float PersonRunNative = 200.f;
    bool bCarrying = false;
    bool bFirstPerson = false;
    // Hold the bird in the right hand by default; a left-handed option will
    // flip this (and mirror the wing-outward orientation) later.
    bool bCarryLeftHand = false;
    bool bThrowing = false;
    bool bThrowWindup = false;
    float ThrowTime = 0.f;
    bool bReleaseFired = false;
    // Measured halt of the throw clip (seconds). -1 = not yet measured.
    float ThrowHaltTime = -1.f;
    bool bThrowHaltComputed = false;
    float ThrowAimPitch = 0.f;
    float HeadLookPitch = 0.f;
    float ThrowCharge = 0.f;
    // Bird roll in the carry socket: sideways (90 deg, dorsal side out) while
    // carried; eased to horizontal while the throw gesture plays.
    float CarryRoll = 0.f;
    bool bCatching = false;
    float CatchTime = 0.f;
    double WorldTime = 0.0;
};