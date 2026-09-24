#include "World/Born2FlapValley.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
double Noise(double X, double Y)
{
    return FMath::PerlinNoise2D(FVector2D(X, Y));
}
} // namespace
ABorn2FlapValley::ABorn2FlapValley()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Valley"));
    RootComponent->SetMobility(EComponentMobility::Static);
}
double ABorn2FlapValley::RiverCentre(double X)
{
    return 55 + 60 * FMath::Sin(X * .0025) + 15 * FMath::Sin(X * .006);
}
double ABorn2FlapValley::RiverWidth(double X)
{
    return 13 + 8 * FMath::Square(FMath::Sin(X * .003));
}
double ABorn2FlapValley::Height(double X, double Y)
{
    // Metres internally. A gently rolling valley opens into ridged hills.
    const double Shoulder = FMath::Max(0., FMath::Abs(Y) - 100);
    const double Hills =
        .03 * FMath::Pow(Shoulder, 1.3) * (1. + .32 * Noise(X * .003, Y * .003) + .15 * Noise(X * .009, Y * .009));
    const double Detail = 1.4 * Noise(X * .014, Y * .014) + .25 * Noise(X * .06, Y * .06);
    const double Distance = (Y - RiverCentre(X)) / RiverWidth(X);
    const double Bed = 5.5 * FMath::Exp(-FMath::Pow(FMath::Abs(Distance), 4.));
    double Z = Hills + Detail + .5 - Bed;
    // A real flat clearing gives the ornithopter room to launch and recover.
    const double Clearing = FMath::Exp(-FMath::Pow(X / 90., 4.) - FMath::Pow(Y / 22., 4.));
    Z *= 1 - Clearing;
    return Z * 100;
}
double ABorn2FlapValley::GroundHeight(double X, double Y)
{
    const double Step = FMath::Max(FMath::Abs(X), FMath::Abs(Y)) <= 80000 ? 400 : 8000;
    const double X0 = FMath::FloorToDouble(X / Step) * Step;
    const double Y0 = FMath::FloorToDouble(Y / Step) * Step;
    const double U = (X - X0) / Step, V = (Y - Y0) / Step;
    auto H = [](double A, double B) { return Height(A / 100, B / 100); };
    if (U + V <= 1)
        return H(X0, Y0) * (1 - U - V) + H(X0 + Step, Y0) * U + H(X0, Y0 + Step) * V;
    return H(X0 + Step, Y0 + Step) * (U + V - 1) + H(X0 + Step, Y0) * (1 - V) + H(X0, Y0 + Step) * (1 - U);
}
bool ABorn2FlapValley::IsWater(double X, double Y)
{
    return FMath::Abs(Y / 100 - RiverCentre(X / 100)) < RiverWidth(X / 100) * 1.2 && GroundHeight(X, Y) < WaterHeight;
}
void ABorn2FlapValley::BeginPlay()
{
    Super::BeginPlay();
    BuildGround(80000, 400, false);
    BuildGround(496000, 8000, true);
    BuildRiver();
    PlantForest();
    UE_LOG(LogTemp, Display, TEXT("NatureLevel ready: forest valley, terrain collision, river and clearing"));
}
void ABorn2FlapValley::BuildGround(double Extent, double Step, bool Outer)
{
    auto *Ground = NewObject<UProceduralMeshComponent>(this);
    Ground->SetupAttachment(RootComponent);
    Ground->SetMobility(EComponentMobility::Static);
    Ground->bUseComplexAsSimpleCollision = true;
    Ground->SetCollisionProfileName(TEXT("BlockAll"));
    Ground->RegisterComponent();
    AddInstanceComponent(Ground);
    const int32 N = FMath::RoundToInt(Extent * 2 / Step);
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Triangles;
    for (int32 Y = 0; Y <= N; ++Y)
        for (int32 X = 0; X <= N; ++X)
        {
            const double Px = (-Extent + X * Step) / 100, Py = (-Extent + Y * Step) / 100;
            const double Z = Height(Px, Py);
            const FVector Normal =
                FVector(Height(Px - 1, Py) - Height(Px + 1, Py), Height(Px, Py - 1) - Height(Px, Py + 1), 200)
                    .GetSafeNormal();
            Vertices.Add(FVector(Px * 100, Py * 100, Z));
            Normals.Add(Normal);
            UV.Add(FVector2D(Px / 6., Py / 6.));
            const double Bank = FMath::Exp(-FMath::Square((FMath::Abs(Py - RiverCentre(Px)) - RiverWidth(Px)) / 5.));
            const double Rock = FMath::Clamp((1 - Normal.Z) * 3 + FMath::Max(0., Z / 100 - 180) / 240, 0., 1.);
            const double Shade = .77 + .2 * Noise(Px * .017, Py * .017) + .1 * Noise(Px * .12, Py * .12);
            Colors.Add(FLinearColor(FMath::Clamp(Bank * .8, 0., 1.), Rock, Shade, 1));
            Tangents.Add(FProcMeshTangent(FVector(1, 0, -Normal.X / FMath::Max(Normal.Z, .01)), false));
        }
    for (int32 Y = 0; Y < N; ++Y)
        for (int32 X = 0; X < N; ++X)
        {
            const double Px = -Extent + (X + .5) * Step, Py = -Extent + (Y + .5) * Step;
            if (Outer && FMath::Abs(Px) < 80000 && FMath::Abs(Py) < 80000)
                continue;
            const int32 A = Y * (N + 1) + X, B = A + 1, C = A + N + 1, D = C + 1;
            Triangles.Append({A, C, B, B, C, D});
        }
    Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, Colors, Tangents, true);
    Ground->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/M_ValleyGround")));
}
void ABorn2FlapValley::BuildRiver()
{
    auto *Water = NewObject<UProceduralMeshComponent>(this);
    Water->SetupAttachment(RootComponent);
    Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Water->SetCastShadow(false);
    Water->RegisterComponent();
    AddInstanceComponent(Water);
    TArray<FVector> V, N;
    TArray<FVector2D> UV;
    TArray<int32> T;
    for (int32 I = 0; I <= 800; ++I)
    {
        const double X = -4800 + I * 12.;
        for (int Side : {-1, 1})
        {
            const double Y = RiverCentre(X) + Side * RiverWidth(X) * 1.2;
            V.Add(FVector(X * 100, Y * 100, WaterHeight));
            N.Add(FVector::UpVector);
            UV.Add(FVector2D(X / 8, Y / 8));
        }
        if (I < 800)
        {
            const int32 A = I * 2;
            T.Append({A, A + 1, A + 2, A + 2, A + 1, A + 3});
        }
    }
    Water->CreateMeshSection_LinearColor(0, V, T, N, UV, {}, {}, false);
    Water->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/M_River")));
}
void ABorn2FlapValley::PlantForest()
{
    auto Group = [&](const FString &Path, int32 EndCull, bool Shadow) {
        auto *C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
        C->bAutoRebuildTreeOnInstanceChanges = false;
        C->SetupAttachment(RootComponent);
        C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *Path));
        C->SetCollisionProfileName(TEXT("NoCollision"));
        C->SetCastShadow(Shadow);
        C->SetCullDistances(EndCull * .75, EndCull);
        C->SetMobility(EComponentMobility::Static);
        C->RegisterComponent();
        AddInstanceComponent(C);
        return C;
    };
    TArray<UHierarchicalInstancedStaticMeshComponent *> Trees, Rocks, Grass;
    for (int32 I = 0; I < 3; ++I)
    {
        Trees.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Fir%d"), I), 180000, true));
        Grass.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Grass%d"), I), 9000, false));
    }
    for (int32 I = 0; I < 4; ++I)
        Rocks.Add(Group(FString::Printf(TEXT("/Game/Nature/SM_Rock%d"), I), 65000, true));
    auto Place = [](UHierarchicalInstancedStaticMeshComponent *Group, FVector P, FRotator R, double HeightCm) {
        if (!Group->GetStaticMesh())
            return;
        const FBoxSphereBounds B = Group->GetStaticMesh()->GetBounds();
        const double Scale = HeightCm / FMath::Max(B.BoxExtent.Z * 2, 1.);
        const FVector Offset(-B.Origin.X, -B.Origin.Y, -B.Origin.Z + B.BoxExtent.Z);
        Group->AddInstance(FTransform(R, P + R.RotateVector(Offset * Scale), FVector(Scale)));
    };
    FRandomStream Random(260924);
    int32 TreeCount = 0;
    for (int32 I = 0; I < 3600; ++I)
    {
        const double X = I < 500 ? Random.FRandRange(-9000, 28000)
                         : I < 2200 ? Random.FRandRange(-45000, 65000) : Random.FRandRange(-150000, 150000);
        const double Y = I < 500 ? Random.FRandRange(-10000, 12000)
                         : I < 2200 ? Random.FRandRange(-35000, 35000) : Random.FRandRange(-105000, 105000);
        if ((X > -3500 && X < 15000 && FMath::Abs(Y) < 2400) || FVector2D(X, Y).Size() < 1800 || IsWater(X, Y))
            continue;
        if (FMath::Abs(Y / 100 - RiverCentre(X / 100)) < RiverWidth(X / 100) + 7)
            continue;
        if (Noise(X * .00008, Y * .00008) < -.25)
            continue;
        const double Z = GroundHeight(X, Y), H = Random.FRandRange(900, 2400);
        Place(Trees[I % 3], FVector(X, Y, Z - 10), FRotator(0, Random.FRandRange(0, 360), 0), H);
        ++TreeCount;
        // Only the trunk blocks flight; the leafy canopy is not a solid wall.
        if (FVector2D(X, Y).Size() < 45000)
        {
            auto *Trunk = NewObject<UCapsuleComponent>(this);
            Trunk->SetupAttachment(RootComponent);
            Trunk->SetCapsuleSize(FMath::Max(12., H * .012), H * .3);
            Trunk->SetRelativeLocation(FVector(X, Y, Z + H * .3));
            Trunk->SetCollisionProfileName(TEXT("BlockAll"));
            Trunk->SetMobility(EComponentMobility::Static);
            Trunk->RegisterComponent();
            AddInstanceComponent(Trunk);
        }
    }
    for (int32 I = 0; I < 600; ++I)
    {
        const double X = Random.FRandRange(-65000, 65000);
        const double Y =
            I < 350 ? (RiverCentre(X / 100) + (I % 2 ? 1 : -1) * (RiverWidth(X / 100) + Random.FRandRange(0, 8))) * 100
                    : Random.FRandRange(-45000, 45000);
        if (FVector2D(X, Y).Size() < 700)
            continue;
        Place(Rocks[I % 4], FVector(X, Y, GroundHeight(X, Y) - 15), FRotator(0, Random.FRandRange(0, 360), 0),
              Random.FRandRange(35, 170));
    }
    for (int32 I = 0; I < 16000; ++I)
    {
        const double X = I < 12000 ? Random.FRandRange(-4000, 14000) : Random.FRandRange(-23000, 40000);
        const double Y = I < 12000 ? Random.FRandRange(-7000, 9000) : Random.FRandRange(-20000, 23000);
        if (IsWater(X, Y) || FVector2D(X, Y).Size() < 130)
            continue;
        Place(Grass[I % 3], FVector(X, Y, GroundHeight(X, Y) - 2), FRotator(0, Random.FRandRange(0, 360), 0),
              Random.FRandRange(18, 48));
    }
    for (const auto &Groups : {Trees, Rocks, Grass})
        for (auto *MeshGroup : Groups)
            MeshGroup->BuildTreeIfOutdated(false, true);
    UE_LOG(LogTemp, Display, TEXT("NatureVegetation trees=%d grass=16000 rocks=600"), TreeCount);
}
