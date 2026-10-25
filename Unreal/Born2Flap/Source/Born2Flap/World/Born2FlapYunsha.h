#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapWeather.h"
#include "Born2FlapYunsha.generated.h"

class UMaterialInstanceDynamic;
class UAudioComponent;
class ACameraActor;

// The map owns persistent scenery. This actor directs the living air, pigment,
// seasonal mirror-wadis and wind-harp. All positions are in Unreal centimetres.
UCLASS()
class BORN2FLAP_API ABorn2FlapYunsha : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapYunsha();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="Yunsha") static double GroundHeight(double X, double Y);
    UFUNCTION(BlueprintCallable, Category="Yunsha") static bool SpringContains(double X, double Y);
    static FVector SoaringWind(const FVector& P, double Time, const FBorn2FlapConditions& Conditions, FVector Ambient);
    static double CanyonCentre(double Y);
    static double CanyonWidth(double Y);
private:
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Pigments;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> LiftMotes;
    UPROPERTY() TObjectPtr<UAudioComponent> Harp;
    UPROPERTY() TObjectPtr<UAudioComponent> Bell;
    UPROPERTY() TObjectPtr<ACameraActor> TestCamera;
    float Elapsed=0, BellCooldown=0;
    int32 CaptureStage=0;
    bool bTest=false, bPassed=true;
    void ValidateWorld();
};

UCLASS()
class BORN2FLAP_API ABorn2FlapYunshaBatch : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapYunshaBatch();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UInstancedStaticMeshComponent> Instances;
};
