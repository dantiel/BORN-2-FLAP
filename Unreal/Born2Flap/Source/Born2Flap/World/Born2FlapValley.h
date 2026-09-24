#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapValley.generated.h"

UCLASS()
class BORN2FLAP_API ABorn2FlapValley : public AActor
{
    GENERATED_BODY()
  public:
    ABorn2FlapValley();
    virtual void BeginPlay() override;
    // Centimetres; samples the same triangulation as the collision mesh.
    static double GroundHeight(double X, double Y);
    static bool IsWater(double X, double Y);
    static constexpr double WaterHeight = -120;

  private:
    static double Height(double X, double Y);
    static double RiverCentre(double X);
    static double RiverWidth(double X);
    void BuildGround(double Extent, double Step, bool Outer);
    void BuildRiver();
    void PlantForest();
};
