#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Born2FlapGameMode.generated.h"

class UBorn2FlapRadioStation;
class ABorn2FlapRadioHUD;
class ABorn2FlapSplash;
class AStaticMeshActor;
class UMaterialInstanceDynamic;

UCLASS()
class BORN2FLAP_API ABorn2FlapGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ABorn2FlapGameMode();
    virtual void InitGame(const FString &MapName, const FString &Options, FString &ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
    int32 GetGatesPassed() const { return GatesPassed; }
    int32 GetGateCount() const { return Gates.Num(); }
    bool IsNatureLevel() const { return bNatureLevel; }
    bool IsCoastLevel() const { return bCoastLevel; }
    bool IsWater(double X,double Y) const;
    double WaterHeight() const;
    FVector WindAt(const FVector& P,double Time) const;
    double GroundHeight(double X, double Y) const;
    bool HasRadioTrack() const;

  private:
    void UpdateGateColors();

    TArray<FVector> Gates;
    TArray<TObjectPtr<AStaticMeshActor>> GateActors;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GateMaterials;
    int32 GatesPassed = 0;
    bool bNatureLevel = true;
    bool bCoastLevel = false;
    float CoastTestTime=0;
    int32 CoastCaptureStage=0;
    UPROPERTY(Transient)
    TObjectPtr<UBorn2FlapRadioStation> RadioStation;
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapRadioHUD> RadioHUD;

    // Startup splash (image + loading bar), shown on real launches only.
    UPROPERTY(Transient)
    TObjectPtr<ABorn2FlapSplash> SplashWidget;
    float SplashElapsed = -1.f;
};