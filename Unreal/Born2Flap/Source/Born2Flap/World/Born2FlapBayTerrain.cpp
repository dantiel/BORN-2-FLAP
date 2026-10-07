#include "World/Born2FlapBayTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

namespace Born2FlapBay
{
bool Contains(double X,double Y)
{
    return FMath::Abs(X)<=180000 && Y>=-180000 && Y<=80000 && (FMath::Abs(X)>=40000 || Y<=-34000);
}
double Height(double X,double Y)
{
    if(Y<-3000)
    {
        const double Back=FMath::SmoothStep(34000.,95000.,-Y);
        const double Outer=FMath::SmoothStep(75000.,145000.,FMath::Abs(X));
        // Irregular connected ridgelines (layered harmonics) instead of single
        // smooth domes; quieter foothills keep the rear terrain natural.
        const double Ridge=FMath::Sin(X*.000055)*FMath::Cos(Y*.00006)
            +.45*FMath::Sin(X*.000137+1.3)*FMath::Cos(Y*.00009+2.1)
            +.28*FMath::Sin(X*.00023+4.7);
        const double OuterRidge=FMath::Sin(Y*.000065+X*.000021)
            +.4*FMath::Sin(Y*.00013-X*.00005+1.9);
        return 150+Back*(3900+1400*Ridge)+Outer*(4400+1500*OuterRidge);
    }
    const double Shore=10000+520*FMath::Sin(X*.00030+1.7)+300*FMath::Sin(X*.00105+4.2)
        +160*FMath::Sin(X*.0024+.6)+85*FMath::Sin(X*.0056+2.3)+45*FMath::Sin(X*.013+5.1);
    const double Dune=FMath::Max(20.,40+50*FMath::Sin(X*.00029+1.2)+30*FMath::Sin(X*.00091+4.1)
        +15*FMath::Sin(X*.0023+.7)+8*FMath::Sin(X*.0047+2.9));
    const double D=Y-Shore;
    double Base=D<=-2500 ? Dune : D<0 ? Dune*(-D/2500)-50*FMath::Square((D+2500)/2500) : FMath::Max(-1750.,-50-.04*D);
    if(Y<-1200) Base=FMath::Clamp(FMath::CeilToDouble((-Y-1200)/100)*25.,0.,150.);
    // Low enclosing headlands: broad rounded land tongues that rise a few
    // metres above the sea at each beach end and curve seaward, rounding the
    // open shore back into a bay. They stay low (east a touch higher than the
    // west) so the far mountains remain the only tall skyline.
    const double Inner=47000+(X<0?7000:0)-3500*FMath::Exp(-FMath::Square((Y-26000)/14000));
    const double Rise=FMath::SmoothStep(Inner-5000,Inner+14000,FMath::Abs(X));
    const double Tip=1-FMath::SmoothStep(40000.,68000.,Y);
    const double Inland=FMath::SmoothStep(-3000.,5000.,Y);
    const double HillAmp=X<0?700.:900.;
    const double HeadlandTop=-50.+HillAmp+220*FMath::Sin(X*.000065+Y*.00011);
    const double Hill=FMath::Lerp(Base,HeadlandTop,Rise*Tip);
    return FMath::Max(Base-6,FMath::Lerp(Base-6,Hill,Inland));
}
double WindExposure(const FVector& P,const FVector& Direction)
{
    // Sample upwind terrain at five distances. Protection fades above the
    // headlands and toward open water; it follows the actual wind direction.
    const FVector Upwind=-Direction.GetSafeNormal2D();
    double Obstruction=0;
    for(double Distance : {4000.,8000.,14000.,22000.,32000.})
    {
        const FVector Q=P+Upwind*Distance;
        if(!Contains(Q.X,Q.Y)) continue;
        const double Relief=Height(Q.X,Q.Y)-P.Z;
        Obstruction=FMath::Max(Obstruction,FMath::Clamp(Relief/(Distance*.12),0.,1.));
    }
    const double Interior=(1-FMath::SmoothStep(16000.,42000.,P.Y))*
        (1-FMath::SmoothStep(1200.,5500.,P.Z))*
        (1-FMath::SmoothStep(39000.,65000.,FMath::Abs(P.X)));
    return FMath::Clamp(1.-.46*Obstruction-.18*Interior,.38,1.);
}
}

ABorn2FlapBayTerrain::ABorn2FlapBayTerrain()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("BayLandRoot"));
}
void ABorn2FlapBayTerrain::BeginPlay()
{
    Super::BeginPlay();
    for(TActorIterator<AActor> It(GetWorld());It;++It)
        if(It->ActorHasTag(TEXT("Distant mountains behind")) || It->ActorHasTag(TEXT("Distant mountains east")) || It->ActorHasTag(TEXT("Distant mountains west")))
        { It->SetActorHiddenInGame(true);It->SetActorEnableCollision(false); }
    BuildLand();PlantWoodland();
}
void ABorn2FlapBayTerrain::BuildLand()
{
    // The enclosing headlands read as coastal land, not inland valley: use the
    // world-space sand/grass/basalt blend built by create_shiomori_map.py, with
    // the old valley material as a fallback if the Shiomori map hasn't cooked it.
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Shiomori/Materials/M_Headland"));
    if(!Material) Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Nature/M_ValleyGround"));
    int32 Sections=0;
    for(int X=-180000;X<180000;X+=20000)
    for(int Y=-180000;Y<80000;Y+=20000)
    {
        // Split the seam at -34000 exactly; sections crossing it omit only
        // central quads, so the back connects to the existing flat hinterland.
        if(X>=-40000 && X<40000 && Y>=-20000) continue;
        TArray<FVector> V,N;TArray<int32> T;TArray<FVector2D> UV;TArray<FLinearColor> C;
        constexpr int Grid=20;
        for(int I=0;I<=Grid;++I) for(int J=0;J<=Grid;++J)
        {
            const double PX=X+I*1000.,PY=Y+J*1000.;
            const double H=Born2FlapBay::Height(PX,PY);
            const FVector Normal=FVector(-(Born2FlapBay::Height(PX+50,PY)-Born2FlapBay::Height(PX-50,PY))/100,
                -(Born2FlapBay::Height(PX,PY+50)-Born2FlapBay::Height(PX,PY-50))/100,1).GetSafeNormal();
            V.Add(FVector(PX,PY,H));N.Add(Normal);UV.Add(FVector2D((PX*.82+PY*.57)/1700,(PY*.82-PX*.57)/1700));
            const float Patch=FMath::PerlinNoise2D(FVector2D(PX*.00022,PY*.00022));
            C.Add(FLinearColor(.5f+Patch*.42f,FMath::Clamp(float((.88-Normal.Z)*3),0.f,.85f),.83f+Patch*.17f,1));
        }
        for(int I=0;I<Grid;++I) for(int J=0;J<Grid;++J)
        {
            if(!Born2FlapBay::Contains(X+(I+.5)*1000,Y+(J+.5)*1000)) continue;
            const int A=I*(Grid+1)+J,B=A+Grid+1;
            T.Append({A,A+1,B,A+1,B+1,B});
        }
        if(T.IsEmpty()) continue;
        auto* Mesh=NewObject<UProceduralMeshComponent>(this);
        Mesh->SetupAttachment(RootComponent);Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->bUseComplexAsSimpleCollision=true;Mesh->bUseAsyncCooking=false;
        Mesh->RegisterComponent();Mesh->SetMaterial(0,Material);
        Mesh->CreateMeshSection_LinearColor(0,V,T,N,UV,C,{},true);
        ++Sections;
    }
    UE_LOG(LogTemp,Display,TEXT("BayLand READY: collision sections=%d"),Sections);
}
void ABorn2FlapBayTerrain::PlantWoodland()
{
    TArray<UInstancedStaticMeshComponent*> Trees;
    for(const TCHAR* Path : {TEXT("/Game/Nature/SM_Fir0"),TEXT("/Game/Nature/SM_Fir1"),TEXT("/Game/Nature/SM_Fir2"),TEXT("/Game/Nature/SM_Tree1"),TEXT("/Game/Nature/SM_Tree0")})
    {
        auto* Tree=NewObject<UInstancedStaticMeshComponent>(this);
        Tree->SetupAttachment(RootComponent);Tree->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));
        Tree->SetCollisionEnabled(ECollisionEnabled::NoCollision);Tree->RegisterComponent();Trees.Add(Tree);
    }
    // A few restrained mature groups behind the house/east; the west stays
    // open and the horizon/building sightlines are preserved.
    FRandomStream Random(611093);int32 Planted=0;
    for(int I=0;I<180;++I)
    {
        const double Angle=Random.FRandRange(0,2*PI),Radius=FMath::Sqrt(Random.FRand())*3200;
        const int32 Grove=Random.RandRange(0,2);
        const double CX=42000+Grove*6000;
        const double CY=-14000+(Grove%2)*12000;
        const double X=CX+FMath::Cos(Angle)*Radius,Y=CY+FMath::Sin(Angle)*Radius;
        const double Z=Born2FlapBay::Contains(X,Y)?Born2FlapBay::Height(X,Y):150;
        if(Z<40) continue;
        const double Species=Random.FRand();
        auto* Tree=Trees[Species<.5?3:Species<.75?4:Random.RandRange(0,2)];
        if(!Tree->GetStaticMesh()) continue;
        const auto Bounds=Tree->GetStaticMesh()->GetBounds();
        const double Height=Random.FRandRange(1200,2000);
        const double Scale=Height/(2*Bounds.BoxExtent.Z);
        const FQuat Rotation(FVector::UpVector,Random.FRandRange(0,2*PI));
        const FVector Offset=Rotation.RotateVector(FVector(Bounds.Origin.X,Bounds.Origin.Y,Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale);
        Tree->AddInstance(FTransform(Rotation,FVector(X,Y,Z-8)-Offset,FVector(Scale)),true);
        ++Planted;
    }
    UE_LOG(LogTemp,Display,TEXT("BayWoodland READY: mature trees=%d"),Planted);
}