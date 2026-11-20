#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Born2FlapAvatarAnimInstance.generated.h"

struct FAnimInstanceProxy;

/**
 * Proxy for UBorn2FlapAvatarAnimInstance. Plays the single node exactly like
 * UAnimSingleNodeInstance, then layers a per-bone head-nod on the result so
 * the pilot's gaze follows the bird with the head alone — no whole-body tilt.
 */
USTRUCT()
struct FB2FAvatarAnimInstanceProxy : public FAnimSingleNodeInstanceProxy
{
    GENERATED_BODY()

    FB2FAvatarAnimInstanceProxy() = default;
    FB2FAvatarAnimInstanceProxy(UAnimInstance* InAnimInstance)
        : FAnimSingleNodeInstanceProxy(InAnimInstance) {}

    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
    virtual bool Evaluate(FPoseContext& Output) override;

    // Applies the cached head-nod to the Head bone of the evaluated pose.
    void ApplyHeadLook(FPoseContext& Output);
    // Pins the root bone (Hips) back to its reference pose each frame. The Mixamo
    // clips carry root motion in the Hips' local translation (the body walks
    // forward ~40-70 cm then snaps back at the loop boundary), so without this
    // the pilot visibly "jumps" every gait cycle.
    void StripRootMotion(FPoseContext& Output);

    // Gaze state, cached on the game thread (PreUpdate) and read on the worker
    // thread (Evaluate) so it never races the avatar's Tick.
    float HeadLookPitch = 0.f;
    float ForwardYawOffset = 0.f;
    int32 HeadSkeletonBoneIndex = INDEX_NONE;
    // Skeleton index of the root (Hips) bone, written by the avatar once per
    // person so StripRootMotion knows which bone carries the root motion.
    int32 RootSkeletonBoneIndex = INDEX_NONE;
};

/**
 * Single-node anim instance the avatar's body uses for its baked locomotion /
 * throw / catch clips. It extends UAnimSingleNodeInstance so the existing
 * USkeletalMeshComponent::SetAnimation / Play / SetPosition / SetPlayRate
 * calls keep working unchanged, and adds a head-look layer on top.
 *
 * Per-bone posing has no public USkeletalMeshComponent API in UE 5.8; the
 * Head rotation is applied by FB2FAvatarAnimInstanceProxy during evaluation.
 */
UCLASS(transient, NotBlueprintable)
class BORN2FLAP_API UBorn2FlapAvatarAnimInstance : public UAnimSingleNodeInstance
{
    GENERATED_BODY()

public:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

    /** Head-gaze state for the next evaluation, written by the avatar each tick. */
    void SetHeadLook(float InPitchDeg, float InForwardYawOffsetDeg, int32 InHeadSkeletonBoneIndex)
    {
        HeadLookPitch = InPitchDeg;
        ForwardYawOffset = InForwardYawOffsetDeg;
        HeadSkeletonBoneIndex = InHeadSkeletonBoneIndex;
    }

    /** Root (Hips) bone index, written once per person for root-motion stripping. */
    void SetRootBone(int32 InRootSkeletonBoneIndex) { RootSkeletonBoneIndex = InRootSkeletonBoneIndex; }

    // Read by FB2FAvatarAnimInstanceProxy (game thread, PreUpdate).
    float HeadLookPitch = 0.f;
    float ForwardYawOffset = 0.f;
    int32 HeadSkeletonBoneIndex = INDEX_NONE;
    int32 RootSkeletonBoneIndex = INDEX_NONE;
};