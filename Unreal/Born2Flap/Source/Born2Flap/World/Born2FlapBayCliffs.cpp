#include "World/Born2FlapBayTerrain.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace Born2FlapBay
{
double CliffMask(double X)
{
    return FMath::SmoothStep(-98000.,-87500.,X)*(1-FMath::SmoothStep(-61000.,-53000.,X));
}
double CliffEdge(double X)
{
    // Broad coves and unequal headlands follow the underlying coast.
    return 48500+900*FMath::Sin((X+74000)*.00028)+410*FMath::Sin(X*.00081+1.7)
        +130*FMath::Sin(X*.0033);
}
double CliffHeight(double X,double Y,double Original)
{
    const double Mask=CliffMask(X)*FMath::SmoothStep(31500.,40000.,Y);
    if(Mask<=0) return Original;
    const double D=Y-CliffEdge(X);
    const double Top=2150+260*FMath::Sin(X*.00047)+130*FMath::Sin(X*.0013+2);
    const double Back=FMath::SmoothStep(32000.,CliffEdge(X)-1700,Y);
    const double Plateau=FMath::Lerp(Original,Top,Back);
    auto Noise=[](double A,double B){return double(FMath::PerlinNoise2D(FVector2D(A,B)));};
    // An eroded continuous escarpment: broad buttresses, gullies and offset
    // sediment ledges. These are actual terrain/collision, not scattered rocks.
    const double Gully=Noise(X*.0021,7.3)*180+Noise(X*.0064,3.1)*55;
    const double Warped=D+Gully+Noise(X*.003,Y*.003)*105;
    const double Width=1.+Noise(X*.0008,11.7)*.28;
    const double Upper=Noise(X*.0012,21.3)*260;
    const double Lower=Noise(X*.0017,33.4)*220;
    const double Terraced=.28*FMath::SmoothStep((-820.+Upper)*Width,(-460.+Upper)*Width,Warped)
        +.39*FMath::SmoothStep((-390.+Lower)*Width,(-90.+Lower)*Width,Warped)
        +.33*FMath::SmoothStep(-10.*Width,320.*Width,Warped);
    const double Ledges=FMath::Clamp(.48+Noise(X*.0007,6.8)*1.4,0.,1.);
    const double Face=FMath::Lerp(FMath::SmoothStep(-800.*Width,320.*Width,Warped),Terraced,Ledges);
    const double Pocket=FMath::Exp(-FMath::Square((X+72000)/2100.));
    const double Toe=FMath::Lerp(-100.,38.,Pocket);
    const double Shore=FMath::Lerp(Toe,-620.,FMath::SmoothStep(350.+Pocket*450,4300.,D));
    const double FaceDetail=4*Face*(1-Face)*(Noise(X*.007,Y*.008)*165
        +Noise(X*.018,Y*.022)*45);
    const double Turf=Noise(X*.002,Y*.002)*38*(1-Face)*Back;
    return FMath::Lerp(Original,FMath::Lerp(Plateau,Shore,Face)+FaceDetail+Turf,Mask);
}
double CliffLift(const FVector& P,const FVector& Wind)
{
    const double Mask=CliffMask(P.X);
    if(Mask<=0) return 0;
    const double Edge=CliffEdge(P.X),Top=Height(P.X,Edge-1100);
    const double Slope=(CliffEdge(P.X+100)-CliffEdge(P.X-100))/200;
    const FVector Inland=FVector(Slope,-1,0).GetSafeNormal();
    const double Incoming=FMath::Max(0.,FVector::DotProduct(Wind,Inland));
    // Metres/second. Lift follows the cliff crest and decays above it/offshore.
    // No lift below the solid terrain, in calm air, or with offshore wind.
    if(P.Z<Height(P.X,P.Y)+10) return 0;
    return Incoming*.95*Mask*FMath::Exp(-FMath::Square((P.Y-Edge-450)/2500.))
        *FMath::Exp(-FMath::Square((P.Z-Top-350)/2600.))
        *FMath::SmoothStep(0.,Top*.75,P.Z);
}
}

void ABorn2FlapBayTerrain::BuildCliffDetails()
{
    auto Instances=[&](const TCHAR* Path,bool Collision)
    {
        auto* C=NewObject<UInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));
        C->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));C->RegisterComponent();return C;
    };
    auto* Pine=Instances(TEXT("/Game/Nature/SM_Fir1"),false);
    auto* Broadleaf=Instances(TEXT("/Game/Nature/SM_Tree1"),false);
    auto Place=[&](UInstancedStaticMeshComponent* C,FVector P,double Height,FRotator R)
    {
        if(!C->GetStaticMesh())return;
        const auto B=C->GetStaticMesh()->GetBounds();const double S=Height/FMath::Max(1.,double(2*B.BoxExtent.Z));
        const FQuat Q=R.Quaternion();
        const FVector Pivot=Q.RotateVector((B.Origin-FVector(0,0,B.BoxExtent.Z))*S);
        C->AddInstance(FTransform(Q,P-Pivot,FVector(S)),true);
    };
    FRandomStream R(131026);int32 Trees=0;
    // Open launch strip along the crest; woodland sits behind it, between
    // exposed high ground and the existing shrine garden (Y about 30000).
    for(int I=0;I<360;++I)
    {
        const double X=R.FRandRange(-86600,-61600),Y=R.FRandRange(34600,43000);
        const double Gap=FMath::Abs(X+70700+450*FMath::Sin(Y*.0005));
        if(Gap<650 || Born2FlapBay::CliffMask(X)<.7)continue;
        const double Z=Born2FlapBay::Height(X,Y);
        Place(R.FRand()<.72?Pine:Broadleaf,FVector(X,Y,Z-8),R.FRandRange(1050,1750),FRotator(0,R.FRandRange(0,360),0));++Trees;
    }
    auto* Grass=Instances(TEXT("/Game/Nature/grass_medium_01/grass_medium_01/StaticMeshes/grass_medium_01_large_a_LOD0"),false);
    Grass->SetCullDistances(12000,26000);
    if(auto* M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Nature/M_grass_medium_01_Foliage")))Grass->SetMaterial(0,M);
    for(int I=0;I<6500;++I)
    {
        const double X=R.FRandRange(-88500,-59500),Y=Born2FlapBay::CliffEdge(X)-R.FRandRange(950,5800);
        if(Born2FlapBay::CliffMask(X)<.8 || FMath::PerlinNoise2D(FVector2D(X*.0011,Y*.0011))<-.12)continue;
        const double Z=Born2FlapBay::Height(X,Y);
        Place(Grass,FVector(X,Y,Z-3),R.FRandRange(22,52),FRotator(0,R.FRandRange(0,360),0));
    }
    UE_LOG(LogTemp,Display,TEXT("BayCliffs READY continuousTerrain=1 forestTrees=%d"),Trees);
}
