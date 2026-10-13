#include "World/Born2FlapBayTerrain.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace Born2FlapBay
{
double EastShoreMask(double X,double Y)
{
    return FMath::SmoothStep(48000.,59000.,X)*(1-FMath::SmoothStep(94000.,108000.,X))
        *FMath::SmoothStep(9000.,24000.,Y)*(1-FMath::SmoothStep(62000.,71000.,Y));
}
double EastShoreEdge(double X)
{
    return 51500+1700*FMath::Sin(X*.00022)+650*FMath::Sin(X*.00069+1.8);
}
double EastShoreHeight(double X,double Y,double Original)
{
    const double Mask=EastShoreMask(X,Y);if(Mask<=0)return Original;
    const double D=Y-EastShoreEdge(X);
    // A low, gently sloping shingle strand and submerged rock platform.
    // Native ground queries and the rendered/collision mesh share this profile.
    const double Back=FMath::Lerp(Original,320.,FMath::SmoothStep(-14500.,-5000.,D));
    const double Strand=FMath::Lerp(320.,-50.,FMath::SmoothStep(-5000.,300.,D));
    const double Shelf=FMath::Lerp(-50.,-650.,FMath::SmoothStep(300.,6500.,D));
    double H=D<-5000?Back:D<300?Strand:Shelf;
    const double Rock=FMath::PerlinNoise2D(FVector2D(X*.0015,Y*.0018));
    const double Detail=FMath::PerlinNoise2D(FVector2D(X*.006,Y*.005));
    H+=(Rock*75+Detail*12)*FMath::SmoothStep(-13000.,-7000.,D)
        *(1-FMath::SmoothStep(800.,4500.,D));
    return FMath::Lerp(Original,H,Mask);
}
}

void ABorn2FlapBayTerrain::BuildEastShore()
{
    TArray<UHierarchicalInstancedStaticMeshComponent*> Groups;
    auto Group=[&](const TCHAR* Path,bool Collision=false)
    {
        auto* C=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));
        // These modest groups have explicit conventional LODs; keep masked
        // shrubs and the scanned patches on the same predictable render path.
        C->SetForceDisableNanite(true);
        C->bAutoRebuildTreeOnInstanceChanges=false;C->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));
        C->RegisterComponent();Groups.Add(C);return C;
    };
    auto* RocksA=Group(TEXT("/Game/Shiomori/EastShore/SM_CoastRockA"),true);
    auto* RocksB=Group(TEXT("/Game/Shiomori/EastShore/SM_CoastRockB"),true);
    auto* Stone=Group(TEXT("/Game/Nature/SM_Rock2"),true);
    auto* Shrub=Group(TEXT("/Game/Shiomori/EastShore/SM_CoastalShrub"));
    auto* Grass=Group(TEXT("/Game/Nature/SM_Grass1"));
    auto* Fern=Group(TEXT("/Game/Nature/SM_Fern0"));
    auto* Pine=Group(TEXT("/Game/Nature/SM_Fir1"));
    auto* Tree=Group(TEXT("/Game/Nature/SM_Tree1"));
    Grass->SetCullDistances(8500,21000);Shrub->SetCullDistances(17000,31000);Fern->SetCullDistances(10000,22000);
    auto Place=[&](UInstancedStaticMeshComponent* C,double X,double Y,double Size,bool Width,double Bury,FRandomStream& R)
    {
        if(!C->GetStaticMesh())return;
        const auto B=C->GetStaticMesh()->GetBounds();
        const double S=Size/FMath::Max(1.,double(2*(Width?FMath::Max(B.BoxExtent.X,B.BoxExtent.Y):B.BoxExtent.Z)));
        const FQuat Q=FRotator(0,R.FRandRange(0,360),0).Quaternion();
        const FVector Base=Q.RotateVector((B.Origin-FVector(0,0,B.BoxExtent.Z))*S);
        double Ground=Born2FlapBay::Height(X,Y);
        if(C==RocksA || C==RocksB)
        {
            // Photogrammetry patches contain ground skirts: sink the patch
            // against its lowest surrounding terrain, not only its centre.
            for(double DX:{-.42,.42})for(double DY:{-.42,.42})
                Ground=FMath::Min(Ground,Born2FlapBay::Height(X+DX*Size,Y+DY*Size));
        }
        C->AddInstance(FTransform(Q,FVector(X,Y,Ground-Bury)-Base,FVector(S)),true);
    };
    FRandomStream R(131071);
    // Irregular clusters of low wave-worn formations across the upper strand.
    for(int I=0;I<65;++I)
    {
        const double X=R.FRandRange(59000,94000),Y=Born2FlapBay::EastShoreEdge(X)+R.FRandRange(-5600,700);
        if(Born2FlapBay::EastShoreMask(X,Y)<.65)continue;
        const double Z=Born2FlapBay::Height(X,Y);if(Z<-90)continue;
        Place(I%2?RocksA:RocksB,X,Y,R.FRandRange(500,1050),true,R.FRandRange(12,38),R);
    }
    for(int Cluster=0;Cluster<22;++Cluster)
    {
        const double CX=R.FRandRange(58500,96000),CY=Born2FlapBay::EastShoreEdge(CX)+R.FRandRange(-4800,450);
        for(int I=0;I<4;++I)
        {
            const double X=CX+R.FRandRange(-400,400),Y=CY+R.FRandRange(-280,280);
            if(Born2FlapBay::Height(X,Y)<-80)continue;
            const double Size=R.FRandRange(110,320);
            Place(Stone,X,Y,Size,true,Size*.19,R);
        }
    }
    for(int I=0;I<1900;++I)
    {
        const double X=R.FRandRange(56000,99000),Y=Born2FlapBay::EastShoreEdge(X)+R.FRandRange(-6800,1000);
        if(Born2FlapBay::EastShoreMask(X,Y)<.5 || Born2FlapBay::Height(X,Y)<-100)continue;
        const double Size=R.FRandRange(12,65);
        Place(Stone,X,Y,Size,true,Size*.23,R);
    }
    // Salt-exposed edge: scattered stiff grasses and narrow-leaved shrubs.
    // More cover towards the woodland, no plants below the upper splash zone.
    for(int I=0;I<23000;++I)
    {
        const double X=R.FRandRange(53000,101000),Y=R.FRandRange(12000,53000);
        if(Born2FlapBay::EastShoreMask(X,Y)<.35)continue;
        const double H=Born2FlapBay::Height(X,Y),D=Y-Born2FlapBay::EastShoreEdge(X);
        if(H<115 || D>-1700)continue;
        const double Patch=FMath::PerlinNoise2D(FVector2D(X*.0012,Y*.0012));
        if(Patch<.08 || R.FRand()>(D<-7000?.7:.23))continue;
        const bool Bush=I%7==0;
        Place(Bush?Shrub:Grass,X,Y,Bush?R.FRandRange(40,95):R.FRandRange(25,65),false,3,R);
    }
    // Woodland fingers taper southwards into scrub, joining the old eastern grove.
    for(int I=0;I<1400;++I)
    {
        const double X=R.FRandRange(53000,77000),Y=R.FRandRange(5000,34000);
        const double H=Born2FlapBay::Height(X,Y);
        if(H<280 || R.FRand()>1-FMath::SmoothStep(15000.,34500.,Y))continue;
        if(FMath::PerlinNoise2D(FVector2D(X*.0008,Y*.0008))<-.08)continue;
        if(I%5==0)Place(I%3?Pine:Tree,X,Y,R.FRandRange(600,1250)*(1-.35*FMath::SmoothStep(15000.,34000.,Y)),false,6,R);
        else Place(I%2?Fern:Shrub,X,Y,R.FRandRange(45,110),false,3,R);
    }
    int32 Count=0;
    for(auto* C:Groups){C->bAutoRebuildTreeOnInstanceChanges=true;C->BuildTreeIfOutdated(false,true);Count+=C->GetInstanceCount();}
    UE_LOG(LogTemp,Display,TEXT("EastShore READY details=%d formations=%d shrub=%d"),Count,RocksA->GetInstanceCount()+RocksB->GetInstanceCount(),Shrub->GetInstanceCount());
}
