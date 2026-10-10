#pragma once
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Algo/Reverse.h"

// Small authored surfaces share vertices across each quad, with metric UVs.
inline void RavenSurface(AActor* Owner,const TArray<TArray<FVector>>& Faces,const TCHAR* Material,bool Collision)
{
    auto* Mesh=NewObject<UProceduralMeshComponent>(Owner,NAME_None,RF_Transactional);
    Mesh->ComponentTags.Add(TEXT("RavenGenerated"));Mesh->SetupAttachment(Owner->GetRootComponent());
    Mesh->SetMobility(EComponentMobility::Static);Mesh->bUseComplexAsSimpleCollision=true;
    Mesh->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));
    Owner->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<int32> T;
    for(const auto& F:Faces)
    {
        if(F.Num()<3)continue;
        const int B=V.Num();const FVector Normal=FVector::CrossProduct(F[1]-F[0],F[2]-F[0]).GetSafeNormal();
        for(const FVector& P:F){V.Add(P);N.Add(Normal);UV.Add(FVector2D(P.X/200,P.Y/200+P.Z/200));}
        for(int I=1;I<F.Num()-1;++I){T.Add(B);T.Add(B+I);T.Add(B+I+1);}
    }
    TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    Mesh->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,Tangents,Collision);
    Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Ravenstonefield/Materials/M_"))+Material)));
}
