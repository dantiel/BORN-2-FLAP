#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapSoundscape.generated.h"
class UAudioComponent;
class USoundWave;
UCLASS()
class BORN2FLAP_API ABorn2FlapSoundscape : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapSoundscape();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    bool ValidateAudio() const;
private:
    UPROPERTY() TObjectPtr<UAudioComponent> Bed;
    UPROPERTY() TObjectPtr<UAudioComponent> Rain;
    UPROPERTY() TObjectPtr<UAudioComponent> Surf;
    UPROPERTY() TObjectPtr<UAudioComponent> Wildlife;
    UPROPERTY() TObjectPtr<USoundWave> DayCall;
    UPROPERTY() TObjectPtr<USoundWave> NightCall;
    FString Level;
    FRandomStream Random{68051};
    float NextCall=5, Shelter=0;
    bool bStarted=false;
};
