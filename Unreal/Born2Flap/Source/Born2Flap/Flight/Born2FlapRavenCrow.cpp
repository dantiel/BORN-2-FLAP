#include "Flight/Born2FlapRavenMesh.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
// Every face owns its vertices: hard normals, folded paper, no smoothed ellipsoids.
struct FShardMesh
{
    TArray<FVector> V, N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
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
        auto* Mesh = NewObject<UProceduralMeshComponent>(Owner,Name);
        Owner->AddInstanceComponent(Mesh);
        Mesh->SetupAttachment(Parent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(true);
        Mesh->RegisterComponent();
        Mesh->CreateMeshSection_LinearColor(0,V,T,N,UV,C,{},false);
        if (auto* Material = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Birds/M_RavenShard")))
            Mesh->SetMaterial(0,Material);
        else Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Training/M_Ink")));
    }
};
const FLinearColor Ink(.018,.025,.035), Slate(.05,.067,.088), Edge(.10,.13,.16);
}

USceneComponent* Born2FlapRaven::Build(AActor* Owner, USceneComponent* Parent,
                                        USceneComponent*& OutLeftShoulder,
                                        USceneComponent*& OutRightShoulder)
{
    if (!Owner || !Parent)
        return nullptr;
    OutLeftShoulder = OutRightShoulder = nullptr;
    auto Node = [&](FName Name,USceneComponent* P,FVector Position)
    {
        auto* C = NewObject<USceneComponent>(Owner,Name);
        Owner->AddInstanceComponent(C); C->SetupAttachment(P); C->SetRelativeLocation(Position); C->RegisterComponent();
        return C;
    };
    auto* RavenRoot = Node(TEXT("RavenCrow"),Parent,FVector::ZeroVector);
    FShardMesh BodyMesh;
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
        // Fanned delta tail: shallow anhedral, spread to a wide trailing edge,
        // so the tail reads as a fan rather than a single backward spike.
        auto TP = [Side](double X,double Y) { return FVector(X,Side*Y,1-Y*FMath::Tan(FMath::DegreesToRadians(12.))); };
        Tail.Tri(TP(-61,0),TP(-112,22),TP(-112,0),Slate);
        Tail.Tri(TP(-64,0),TP(-107,17),TP(-107,0),Ink);
        Tail.Install(Owner,RavenRoot,*FString::Printf(TEXT("RavenTail%d"),Side));
        auto* Shoulder=Node(*FString::Printf(TEXT("RavenShoulder%d"),Side),RavenRoot,FVector(3,Side*11,10));
        if (Side<0) OutLeftShoulder=Shoulder; else OutRightShoulder=Shoulder;
        auto W = [Side](double X,double Y,double Z=0) { return FVector(X,Side*Y*1.4,Z); };
        FShardMesh Wing;
        Wing.Fold(W(17,0),W(12,39),W(-25,43),W(-30,5),2.8,Ink);
        Wing.Tri(W(17,0,.1),W(12,39,.1),W(7,34,3.1),Slate);
        // Parallel pinions: no radial fan. Seven narrow blades make a comb silhouette.
        for (int I=0; I<7; ++I)
        {
            const double X=13-I*6.0;
            const double Tip=63+10*FMath::Sin((I+1)*PI/9.0);
            Wing.Fold(W(X,29),W(X+2.5,39),W(X-1.2,Tip),W(X-3.1,Tip-12),1.0,I%3==0 ? Slate : Ink);
            Wing.Tri(W(X-1.2,Tip,.1),W(X-2.2,Tip-19,1.1),W(X-2.6,Tip-12,.1),Edge*.6f);
        }
        // Smaller covert plates retain the rectangular inner-wing impression.
        for (int I=0; I<5; ++I)
            Wing.Fold(W(8-I*6,9),W(12-I*6,18),W(3-I*6,41),W(1-I*6,25),2.1,Slate*.65f);
        Wing.Install(Owner,Shoulder,*FString::Printf(TEXT("RavenWing%d"),Side));
    }
    BodyMesh.Install(Owner,RavenRoot,TEXT("RavenBody"));
    return RavenRoot;
}

void ABorn2FlapFlightPawn::BuildRavenCrow()
{
    RavenRoot = Born2FlapRaven::Build(this, VisualRoot, RavenLeftShoulder, RavenRightShoulder);
}
