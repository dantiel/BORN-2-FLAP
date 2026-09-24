#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Born2FlapGameMode.generated.h"
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
    double GroundHeight(double X, double Y) const;

  private:
    TArray<FVector> Gates;
    int32 GatesPassed = 0;
    bool bNatureLevel = true;
};
