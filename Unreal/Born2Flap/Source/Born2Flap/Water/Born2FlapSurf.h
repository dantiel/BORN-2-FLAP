#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapSurf.generated.h"
class UProceduralMeshComponent;

namespace Born2FlapSurf
{
    inline double Shore(double X)
    {
        return 10000+520*FMath::Sin(X*.00030+1.7)+300*FMath::Sin(X*.00105+4.2)
            +160*FMath::Sin(X*.0024+.6)+85*FMath::Sin(X*.0056+2.3)+45*FMath::Sin(X*.013+5.1);
    }
    inline double Front(double X,double Time,int Wave=0)
    {
        return 6100-FMath::Fmod(Time*640+Wave*2200+160*FMath::Sin(X*.0005),6600.);
    }
    inline float Wash(double X,double Time)
    {
        float Result=0;
        for(int I=0;I<3;++I) Result=FMath::Max(Result,float(FMath::Exp(-FMath::Square((Front(X,Time,I)-650)/950))));
        return Result;
    }
}

// A camera-local, collision-free representation of shoaling, curl and wash.
// Offshore water stays on the existing ocean; no level assets are regenerated.
UCLASS()
class BORN2FLAP_API ABorn2FlapSurf : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapSurf();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    bool ValidateGeometry() const;
private:
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    TArray<FVector> Vertices,Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    float Accumulator=0;
    bool bCreated=false;
};
