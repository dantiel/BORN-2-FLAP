#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Born2FlapGameMode.generated.h"

class UAudioComponent;
class USoundWave;

UCLASS()
class BORN2FLAP_API ABorn2FlapGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ABorn2FlapGameMode();
    virtual void InitGame(const FString &MapName, const FString &Options, FString &ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    int32 GetGatesPassed() const { return GatesPassed; }
    int32 GetGateCount() const { return Gates.Num(); }
    bool IsNatureLevel() const { return bNatureLevel; }
    bool IsCoastLevel() const { return bCoastLevel; }
    bool IsWater(double X,double Y) const;
    double WaterHeight() const;
    FVector WindAt(const FVector& P,double Time) const;
    double GroundHeight(double X, double Y) const;
    bool HasCoastRadioTrack() const;
    bool IsCoastRadioOn() const;
    float GetCoastRadioVolume() const;
    FString GetCoastRadioTrackName() const;

  private:
    void BuildCoastRadio();
    UFUNCTION()
    void OnCoastTrackFinished();
    TArray<FVector> Gates;
    int32 GatesPassed = 0;
    bool bNatureLevel = true;
    bool bCoastLevel = false;
    float CoastTestTime=0;
    int32 CoastCaptureStage=0;
    UPROPERTY(Transient)
    TObjectPtr<UAudioComponent> CoastRadio;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USoundWave>> CoastPlaylist;
    int32 CoastTrackIndex = 0;
    bool bCoastRadioOn = true;
    float CoastRadioVolume = .5f;
};