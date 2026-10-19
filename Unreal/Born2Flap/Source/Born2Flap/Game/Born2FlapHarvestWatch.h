#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/HarvestWatchSimulation.h"
#include "Born2FlapHarvestWatch.generated.h"
class UInstancedStaticMeshComponent;
class ACameraActor;

UCLASS()
class BORN2FLAP_API ABorn2FlapHarvestWatch : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapHarvestWatch();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    FString Status() const;
    FString PlotStatus() const;
private:
    void BuildScarecrows();
    void PoseBirds();
    void Validate();
    UInstancedStaticMeshComponent* Group(const TCHAR* Mesh,const TCHAR* Material,bool Collision=false);
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Bodies;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Heads;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Beaks;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Wings;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Tails;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Legs;
    UPROPERTY() TObjectPtr<ACameraActor> CaptureCamera;
    HarvestWatch::FRound Round;
    float AnimationTime=0,Accumulator=0,CaptureTime=0;
    int32 CaptureIndex=0;
    bool bValidated=false;
    bool bCaptureScare=false;
};
