#include "Flight/Born2FlapWingMesh.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
// Every face owns its vertices: hard normals, folded paper, no smoothed ellipsoids.
struct FShardMesh
{
    // Texture projection modes matching Tools/create_falcon_texture_templates.py.
    enum class EMode { Body, Tail };
    EMode Mode = EMode::Body;
    const TCHAR* MaterialPath = TEXT("/Game/Birds/M_RavenShard");
    TArray<FVector> V, N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    // Planar UV so a painted side-view (body) or top-view (tail) texture wraps the shards.
    FVector2D Map(FVector P) const
    {
        if (Mode == EMode::Tail)
            return FVector2D((P.X+112.f)/51.f, (P.Y+22.f)/44.f); // tail: root->tip, left->right
        return FVector2D((P.X+63.f)/133.f, (P.Z+12.f)/33.f);      // body: aft->nose, belly->back
    }
    void Tri(FVector A, FVector B, FVector D, FLinearColor Colour)
    {
        FVector Normal = FVector::CrossProduct(B-A,D-A).GetSafeNormal();
        if (Normal.Z<0) { Swap(B,D); Normal=-Normal; }
        for (bool Back : {false,true})
        {
            int32 Base = V.Num();
            V.Append({A, Back ? D : B, Back ? B : D});
            // Unreal's front-face winding is opposite the cross-product normal.
            T.Append({Base, Base+2, Base+1});
            for (int I=0; I<3; ++I) { N.Add(Back ? -Normal : Normal); C.Add(Back ? Colour*.65f : Colour); UV.Add(FVector2D::ZeroVector); }
        }
    }
    void Fold(FVector A, FVector B, FVector D, FVector E, float Height, FLinearColor Colour)
    {
        FVector Ridge = (A+B+D+E)*.25 + FVector(0,0,Height);
        Tri(A,B,Ridge,Colour); Tri(B,D,Ridge,Colour*.68f);
        Tri(D,E,Ridge,Colour*1.25f); Tri(E,A,Ridge,Colour*.85f);
    }
    void Install(AActor* Owner, USceneComponent* Parent, FName Name)
    {
        for (int32 I=0; I<V.Num(); ++I) UV[I] = Map(V[I]);
        auto* Mesh = NewObject<UProceduralMeshComponent>(Owner,Name);
        Owner->AddInstanceComponent(Mesh);
        Mesh->SetupAttachment(Parent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(true);
        Mesh->RegisterComponent();
        Mesh->CreateMeshSection_LinearColor(0,V,T,N,UV,C,{},false);
        if (auto* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
            Mesh->SetMaterial(0,Material);
        else Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Training/M_Ink")));
    }
};
const FLinearColor Ink(.018,.025,.035), Slate(.05,.067,.088), Edge(.10,.13,.16);
}

USceneComponent* Born2FlapRaven::Build(AActor* Owner, USceneComponent* Parent,
                                        TObjectPtr<USceneComponent>& OutLeftShoulder,
                                        TObjectPtr<USceneComponent>& OutRightShoulder, int32 Design)
{
    if (!Owner || !Parent)
        return nullptr;
    OutLeftShoulder = OutRightShoulder = nullptr;
    auto Node = [&](FName Name,USceneComponent* P,FVector Position)
    {
        auto* C = NewObject<USceneComponent>(Owner,*FString::Printf(TEXT("%s_%d"),*Name.ToString(),Design));
        Owner->AddInstanceComponent(C); C->SetupAttachment(P); C->SetRelativeLocation(Position); C->RegisterComponent();
        return C;
    };
    auto* RavenRoot = Node(TEXT("RavenCrow"),Parent,FVector::ZeroVector);
    FShardMesh BodyMesh;
    BodyMesh.MaterialPath = Design == 2 ? TEXT("/Game/Birds/M_FalconBody") : TEXT("/Game/Birds/M_RavenShard");
    // A narrow architectural shoulder and keel, angular crow brow and spear beak.
    BodyMesh.Fold({55,0,7},{12,14,1},{-63,0,-3},{12,-14,1},8,Ink);
    BodyMesh.Fold({40,0,5},{8,-10,-2},{-50,0,-12},{8,10,-2},-8,Slate*.55f);
    BodyMesh.Fold({54,0,15},{34,11,13},{17,0,18},{34,-11,13},6,Slate);
    BodyMesh.Tri({70,0,9},{47,7,12},{49,0,17},Edge);
    BodyMesh.Tri({70,0,9},{49,0,17},{47,-7,12},Slate);
    BodyMesh.Tri({70,0,9},{47,-7,12},{47,7,12},Ink);
    // Layered scapular shards run aft along the spine.
    for (int Side : {-1,1})
    {
        for (int I=0; I<5; ++I)
        {
            const float X=16-I*10, Y=Side*(5+I*.65f);
            BodyMesh.Fold({X+12,Y*.65,13-I*2.f},{X+1,Y+Side*4,7.-I},
                          {X-29,Y*.7,2.-I},{X-2,Y*.25,10.-I},2, I%2 ? Slate : Ink);
        }
        // Small inset amber eye is a triangle, not a luminous sphere.
        BodyMesh.Tri({43,Side*8.8,16},{37,Side*10.4,17},{40,Side*10.,14},FLinearColor(.34,.19,.055));
        FShardMesh Tail;
        Tail.Mode = FShardMesh::EMode::Tail;
        Tail.MaterialPath = Design == 2 ? TEXT("/Game/Birds/M_FalconTail") : TEXT("/Game/Birds/M_RavenShard");
        // Fanned delta tail: shallow anhedral, spread to a wide trailing edge,
        // so the tail reads as a fan rather than a single backward spike.
        auto TP = [Side](double X,double Y) { return FVector(X,Side*Y,1-Y*FMath::Tan(FMath::DegreesToRadians(12.))); };
        Tail.Tri(TP(-61,0),TP(-112,22),TP(-112,0),Slate);
        Tail.Tri(TP(-64,0),TP(-107,17),TP(-107,0),Ink);
        Tail.Install(Owner,RavenRoot,*FString::Printf(TEXT("RavenTail%d_%d"),Side,Design));
        auto* Shoulder=Node(*FString::Printf(TEXT("RavenShoulder%d"),Side),RavenRoot,FVector(3,Side*11,10));
        if (Side<0) OutLeftShoulder=Shoulder; else OutRightShoulder=Shoulder;
        auto* Wing=NewObject<UBorn2FlapWingMesh>(Owner,*FString::Printf(TEXT("RavenWing%d_%d"),Side,Design));
        Owner->AddInstanceComponent(Wing);
        Wing->SetupAttachment(Shoulder);
        Wing->RegisterComponent();
        Wing->InitializeWing(Side,Design);
    }
    BodyMesh.Install(Owner,RavenRoot,*FString::Printf(TEXT("RavenBody%d"),Design));
    return RavenRoot;
}

void ABorn2FlapFlightPawn::BuildRavenCrow()
{
    RavenRoot = Born2FlapRaven::Build(this, VisualRoot, RavenLeftShoulder, RavenRightShoulder);
    MembraneRoot = Born2FlapRaven::Build(this, VisualRoot, MembraneLeftShoulder, MembraneRightShoulder,2);
}

void ABorn2FlapFlightPawn::SelectBirdModel(int32 Index)
{
    BirdModel = FMath::Clamp(Index, 0, 2);
    if (PrototypeRoot) PrototypeRoot->SetVisibility(!bBlind && BirdModel == 1, true);
    if (RavenRoot) RavenRoot->SetVisibility(!bBlind && BirdModel == 0, true);
    if (MembraneRoot) MembraneRoot->SetVisibility(!bBlind && BirdModel == 2, true);
}