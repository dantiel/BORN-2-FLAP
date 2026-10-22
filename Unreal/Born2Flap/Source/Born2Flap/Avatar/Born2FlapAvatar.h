#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapAvatar.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

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

    void SetPlacement(const FVector& WorldFeet, float WorldYaw);
    void SetWalkSpeed(float CmPerSecond);
    void SetCarrying(bool bCarry);
    void SetFirstPerson(bool bFirstPerson);

    void BeginThrow();
    bool IsThrowing() const { return bThrowing; }
    float GetFacingYaw() const { return FacingYaw; }
    USceneComponent* GetCarrySocket() const { return CarrySocket; }
    FVector GetHeadWorldLocation() const;

    DECLARE_DELEGATE(FOnThrowRelease);
    FOnThrowRelease OnThrowRelease;

private:
    void ResolveSkeleton();
    int32 FindBone(const TCHAR* Substring) const;
    void CaptureRestPoses();
    void ApplySkin();
    void PoseBody(bool bMoving, float Dt);
    void PoseThrow(float Dt);
    void SetBoneRotation(int32 BoneIndex, const FRotator& Rot);
    void UpdateCarrySocket();

    UPROPERTY() USceneComponent* Root = nullptr;
    UPROPERTY() USkeletalMeshComponent* Body = nullptr;
    UPROPERTY() USceneComponent* CarrySocket = nullptr;

    int32 BoneHips = INDEX_NONE, BoneSpine = INDEX_NONE, BoneHead = INDEX_NONE;
    int32 BoneLArm = INDEX_NONE, BoneLForeArm = INDEX_NONE, BoneLHand = INDEX_NONE;
    int32 BoneRArm = INDEX_NONE, BoneRForeArm = INDEX_NONE, BoneRHand = INDEX_NONE;
    int32 BoneLUpLeg = INDEX_NONE, BoneLLeg = INDEX_NONE, BoneLFoot = INDEX_NONE;
    int32 BoneRUpLeg = INDEX_NONE, BoneRLeg = INDEX_NONE, BoneRFoot = INDEX_NONE;

    TMap<int32, FVector> RestTranslations;
    TArray<UMaterialInterface*> OriginalMaterials;
    UPROPERTY() UMaterialInstanceDynamic* SkinMID = nullptr;

    int32 SkinIndex = 0;
    FVector FeetWorld = FVector::ZeroVector;
    float FacingYaw = 0.f;
    float TargetFacingYaw = 0.f;
    float GaitSpeed = 0.f;
    float GaitPhase = 0.f;
    float GaitBlend = 0.f;
    float MeshScale = 1.f;
    bool bCarrying = false;
    bool bFirstPerson = false;
    bool bThrowing = false;
    float ThrowTime = 0.f;
    bool bReleaseFired = false;
    double WorldTime = 0.0;
};