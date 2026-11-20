#include "Avatar/Born2FlapAvatarAnimInstance.h"
#include "BonePose.h"
#include "Animation/Skeleton.h"

namespace
{
    // Component-space "right" axis a head nod rotates around, derived from the
    // mesh's own forward convention (the Founder rig faces +Y, so this is -X
    // there) — any glTF axis import needs no per-character tuning.
    FVector HeadNodAxis(float ForwardYawOffsetDeg)
    {
        const FVector Forward = FRotator(0.f, ForwardYawOffsetDeg, 0.f)
            .RotateVector(FVector::XAxisVector);
        return Forward.RotateAngleAxis(90.f, FVector::ZAxisVector);
    }
}

void FB2FAvatarAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
    FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

    // Cache the gaze state on the game thread so the worker-thread Evaluate
    // below never races the avatar's Tick.
    if (const UBorn2FlapAvatarAnimInstance* AI = Cast<UBorn2FlapAvatarAnimInstance>(InAnimInstance))
    {
        HeadLookPitch = AI->HeadLookPitch;
        ForwardYawOffset = AI->ForwardYawOffset;
        HeadSkeletonBoneIndex = AI->HeadSkeletonBoneIndex;
        RootSkeletonBoneIndex = AI->RootSkeletonBoneIndex;
    }
}

bool FB2FAvatarAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
    // Play the clip exactly as UAnimSingleNodeInstance would, then layer the
    // head gaze on top of the result.
    FAnimSingleNodeInstanceProxy::Evaluate(Output);
    StripRootMotion(Output);
    ApplyHeadLook(Output);
    return true;
}

void FB2FAvatarAnimInstanceProxy::StripRootMotion(FPoseContext& Output)
{
    if (RootSkeletonBoneIndex == INDEX_NONE || Output.Pose.GetNumBones() == 0)
        return;

    const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
    const FCompactPoseBoneIndex RootIdx =
        BoneContainer.GetCompactPoseIndexFromSkeletonIndex(RootSkeletonBoneIndex);
    if (!RootIdx.IsValid())
        return;

    const USkeleton* SkeletonAsset = BoneContainer.GetSkeletonAsset();
    if (!SkeletonAsset)
        return;
    const TArray<FTransform>& RefPose = SkeletonAsset->GetReferenceSkeleton().GetRefBonePose();
    if (!RefPose.IsValidIndex(RootSkeletonBoneIndex))
        return;

    // Pin the Hips back to its reference translation: the Mixamo clip's forward
    // root motion is a per-cycle ~40-70 cm translation that otherwise reads as
    // a jump at every loop. The pawn's own movement is the only locomotion.
    FTransform& Root = Output.Pose[RootIdx];
    Root.SetTranslation(RefPose[RootSkeletonBoneIndex].GetTranslation());
}

void FB2FAvatarAnimInstanceProxy::ApplyHeadLook(FPoseContext& Output)
{
    if (HeadSkeletonBoneIndex == INDEX_NONE || FMath::IsNearlyZero(HeadLookPitch))
        return;
    if (Output.Pose.GetNumBones() == 0)
        return;

    const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
    const FCompactPoseBoneIndex HeadIdx =
        BoneContainer.GetCompactPoseIndexFromSkeletonIndex(HeadSkeletonBoneIndex);
    if (!HeadIdx.IsValid())
        return;

    // Nod the head in component space, then bake back to local bone space so
    // neck/hair/eye children follow. The sign matches the previous whole-body
    // tilt, so "looking up" still reads the same direction.
    const FQuat Delta(HeadNodAxis(ForwardYawOffset),
        FMath::DegreesToRadians(-HeadLookPitch));

    FCSPose<FCompactPose> CSPose;
    CSPose.InitPose(Output.Pose);
    FTransform HeadCS = CSPose.GetComponentSpaceTransform(HeadIdx);
    HeadCS.SetRotation(Delta * HeadCS.GetRotation());

    TArray<FBoneTransform> BoneTransforms;
    BoneTransforms.Emplace(HeadIdx, HeadCS);
    CSPose.SafeSetCSBoneTransforms(BoneTransforms);
    FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}

FAnimInstanceProxy* UBorn2FlapAvatarAnimInstance::CreateAnimInstanceProxy()
{
    return new FB2FAvatarAnimInstanceProxy(this);
}