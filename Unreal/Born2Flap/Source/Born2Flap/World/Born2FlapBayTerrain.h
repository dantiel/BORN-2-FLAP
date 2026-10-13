#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapBayTerrain.generated.h"

namespace Born2FlapBay
{
// The visible land and the flight ground/water mask share this exact function.
bool Contains(double X,double Y);
double Height(double X,double Y);
// Terrain plus the hand-placed sea wall: the top of what the wind piles against.
double TopHeight(double X,double Y);
double WindExposure(const FVector& Position,const FVector& Direction);
double CliffEdge(double X);
double CliffMask(double X);
double CliffHeight(double X,double Y,double Original);
double CliffLift(const FVector& Position,const FVector& HorizontalWind);
}

UCLASS()
class ABorn2FlapBayTerrain : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapBayTerrain();
    virtual void BeginPlay() override;
private:
    void BuildLand();
    void PlantWoodland();
    void BuildCliffDetails();
};
