#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "born2flap_math.h"
#include "Born2FlapWingMesh.generated.h"

// UVs describe material coordinates, and never change with aerodynamic loading.
UCLASS()
class BORN2FLAP_API UBorn2FlapWingMesh : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    void InitializeWing(int32 Side, int32 Design);
    void ApplyShape(const B2F_WingSection* Shape);
    UFUNCTION(BlueprintCallable, Category="Wing")
    void SetPaintTexture(UTexture2D* Texture);
    int32 GetWingDesign() const { return WingDesign; }
    bool HasValidDeformation();
    // Rest (unflapped) wingtip in mesh-local space — the outboard span extreme.
    // Used to anchor the outboard end of the leading-edge contact capsule.
    FVector GetWingTipLocal() const;
    // Rest (unflapped) wing root in mesh-local space — the inboard span anchor.
    FVector GetWingRootLocal() const;
private:
    int32 WingSide = 1;
    int32 WingDesign = 0;
    bool bReceivedShape = false;
    double PeakBend = 0;
    TArray<FVector> Vertices, Normals, RestVertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colours;
    TArray<FProcMeshTangent> Tangents;
};