#include "Flight/Born2FlapWingMesh.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

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

// Set beak-to-tail-tip length in centimetres, scaling about the wing attachment.
// The source model runs from nose X=70 to tail tip X=-112.0293.
constexpr float KestrelWingX = 3.f;
constexpr float KestrelTotalLengthCm = 80.f;
constexpr float KestrelLengthScale = KestrelTotalLengthCm / (70.f + 112.0293f);
constexpr float KestrelWidthScale = .90f;
constexpr float KestrelHeightScale = .85f;
FVector KestrelBodyPosition(FVector P)
{
    return FVector(KestrelWingX + (P.X-KestrelWingX)*KestrelLengthScale,
                   P.Y*KestrelWidthScale, P.Z*KestrelHeightScale);
}

// Kestrel fanned tail membrane (kestreltail.svg "tailmembrane"), authored top-view
// mapped into the game body: X = nose->tail (root -61 -> aft -112), Y = left/right,
// Z = 0 (flat). UVs land on the T_KestrelTail feather raster placement; the mesh is
// two-sided (front fan + reversed back fan) so it reads from above and below.
const FVector KestrelTailPts[8] = {
    FVector(-61.0013, 5.1481, 0), FVector(-59.7605, -0.1170, 0),
    FVector(-61.0013, -5.9671, 0), FVector(-107.8416, -21.9963, 0),
    FVector(-110.0130, -17.5502, 0), FVector(-112.0293, 0.0, 0),
    FVector(-109.7028, 16.8482, 0), FVector(-108.4620, 21.9963, 0),
};
const FVector2D KestrelTailUV[8] = {
    FVector2D(0.61677, 0.02647), FVector2D(0.49669, 0.00335),
    FVector2D(0.36327, 0.02647), FVector2D(0.0, 0.89923),
    FVector2D(0.09909, 0.93969), FVector2D(0.49936, 0.97725),
    FVector2D(0.88361, 0.93391), FVector2D(1.0, 0.91079),
};

void BuildKestrelTail(AActor* Owner, USceneComponent* Parent)
{
    TArray<FVector> V, N;
    TArray<FVector2D> UV;
    TArray<int32> T;
    TArray<FLinearColor> C;
    for (int32 I = 0; I < 8; ++I)
    {
        V.Add(KestrelBodyPosition(KestrelTailPts[I])); UV.Add(KestrelTailUV[I]);
        N.Add(FVector(0, 0, 1)); C.Add(FLinearColor::White);
    }
    for (int32 I = 1; I < 7; ++I) T.Append({0, I, I + 1});  // front fan from root centre
    const int32 B = V.Num();
    for (int32 I = 0; I < 8; ++I)
    {
        V.Add(KestrelBodyPosition(KestrelTailPts[I])); UV.Add(KestrelTailUV[I]);
        N.Add(FVector(0, 0, -1)); C.Add(FLinearColor::White);
    }
    for (int32 I = 1; I < 7; ++I) T.Append({B, B + I + 1, B + I});  // back fan (reversed winding)

    auto* Mesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("KestrelTail_2"));
    Owner->AddInstanceComponent(Mesh);
    Mesh->SetupAttachment(Parent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(true);
    Mesh->RegisterComponent();
    Mesh->CreateMeshSection_LinearColor(0, V, T, N, UV, C, {}, false);
    if (auto* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Birds/M_KestrelTail")))
        Mesh->SetMaterial(0, Mat);
}
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
    if (Design == 2)
    {
        // Kestrel (falcon): the authored fuselage mesh + textured tail membrane.
        // Upright OBJ is pre-scaled to cm. Yaw points the nose forward; the
        // the shared body/tail transform gives a total length of 800 mm.
        auto* Fuselage = NewObject<UStaticMeshComponent>(Owner, TEXT("KestrelFuselage_2"));
        Owner->AddInstanceComponent(Fuselage);
        Fuselage->SetupAttachment(RavenRoot);
        Fuselage->SetRelativeLocation(KestrelBodyPosition(FVector(70, 0, 0)));
        Fuselage->SetRelativeRotation(FRotator(0, 180, 0));
        Fuselage->SetRelativeScale3D(1.33f*FVector(KestrelLengthScale, KestrelWidthScale, KestrelHeightScale));
        Fuselage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Fuselage->SetCastShadow(true);
        if (auto* FusMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Birds/SM_KestrelFuselage")))
            Fuselage->SetStaticMesh(FusMesh);
        if (auto* FusMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Birds/M_KestrelFuselage")))
            Fuselage->SetMaterial(0, FusMat);
        Fuselage->RegisterComponent();

        BuildKestrelTail(Owner, RavenRoot);

        // Shoulder distance is 1 cm per side (matching the physics wsShoulderM):
        // the wing root sits 1 cm outboard of the body centreline, so the two
        // roots are 2 cm apart instead of spreading a body-width apart.
        for (int Side : {-1, 1})
        {
            auto* Shoulder = Node(*FString::Printf(TEXT("RavenShoulder%d"), Side), RavenRoot, FVector(3, Side * 1.0, 10));
            if (Side < 0) OutLeftShoulder = Shoulder; else OutRightShoulder = Shoulder;
            auto* Wing = NewObject<UBorn2FlapWingMesh>(Owner, *FString::Printf(TEXT("RavenWing%d_%d"), Side, Design));
            Owner->AddInstanceComponent(Wing);
            Wing->SetupAttachment(Shoulder);
            Wing->RegisterComponent();
            Wing->InitializeWing(Side, Design);
        }
        return RavenRoot;
    }

    FShardMesh BodyMesh;
    BodyMesh.MaterialPath = TEXT("/Game/Birds/M_RavenShard");
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
        Tail.MaterialPath = TEXT("/Game/Birds/M_RavenShard");
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