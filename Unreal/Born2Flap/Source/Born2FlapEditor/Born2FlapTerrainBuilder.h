#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Born2FlapTerrainBuilder.generated.h"

UCLASS()
class BORN2FLAPEDITOR_API UBorn2FlapTerrainBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Use the continuous native height field across tile boundaries.
    UFUNCTION(BlueprintCallable, Category="Born2Flap|Terrain")
    static void SmoothYunshaTerrain(class UStaticMesh* Mesh, FVector2D SamplingOffset, bool bHorizon=false);
};
