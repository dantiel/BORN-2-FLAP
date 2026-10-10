#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapValley.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UAudioComponent;
class ACameraActor;

UCLASS()
class BORN2FLAP_API ABorn2FlapValley : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapValley();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Ravenstonefield")
    void BuildWorld();
    // Centimetres; samples the same triangulation as the collision mesh.
    static double GroundHeight(double X, double Y);
    static bool IsWater(double X, double Y);
    static constexpr double WaterHeight = -120;
    static FString PlaceName(double X, double Y);
    bool IsPhotoMode() const { return PhotoIndex >= 0; }
    bool HasRadioTrack() const;

  private:
    static double Height(double X, double Y);
    static double RiverCentre(double X);
    static double RiverWidth(double X);
    void BuildGround(double Extent, double Step, bool Outer);
    void BuildRiver();
    void PlantForest();
    void BuildLandmarks();
    void BuildAtmosphere();
    void BuildBarn(FVector Centre, double Yaw, double Scale, bool House);
    void BuildWorkshop();
    void BuildFieldDetails();
    static double FootpathX(double Y);
    void BuildBridge();
    void BuildCastle();
    void BuildRelics();
    void SetPhotoView(int32 Index);
    void ValidateWorld();
    UHierarchicalInstancedStaticMeshComponent *Group(const FString &Mesh, const FString &Material,
                                                    bool Collision = false, int32 Cull = 0);
    void Part(const FString &Mesh, const FString &Material, FVector Position, FVector Scale,
              FRotator Rotation = FRotator::ZeroRotator, bool Collision = false);
    void Beam(FVector A, FVector B, double Radius, const FString &Material, bool Collision = false);
    void Sign(FVector Position, FRotator Rotation, const FString &Text, float Size);
    UPROPERTY(VisibleAnywhere, Category="Ravenstonefield")
    int32 WorldVersion = 0;
    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> PhotoCamera;
    TMap<FString, UHierarchicalInstancedStaticMeshComponent *> Groups;
    int32 PhotoIndex = -1;
    double CaptureTime = 0;
    int32 CaptureStage = 0;
};
