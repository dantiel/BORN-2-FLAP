#include "World/Born2FlapValley.h"
#include "World/RavenSurface.h"

double ABorn2FlapValley::FootpathX(double Y)
{
    return -10500+2300*FMath::Sin((Y+30000)*.00013)+.07*(Y+30000);
}

void ABorn2FlapValley::BuildFieldDetails()
{
    TArray<TArray<FVector>> Path,Turf;
    for(double Y=-30000;Y<16000;Y+=100)
    {
        const double X=FootpathX(Y),NX=FootpathX(Y+100);
        if(Y>RiverCentre(X/100)*100-RiverWidth(X/100)*100-140)break;
        const double W=105+18*FMath::Sin(Y*.003);
        Path.Add({FVector(X-W,Y,GroundHeight(X-W,Y)+3),FVector(X+W,Y,GroundHeight(X+W,Y)+3),FVector(NX+W,Y+100,GroundHeight(NX+W,Y+100)+3),FVector(NX-W,Y+100,GroundHeight(NX-W,Y+100)+3)});
    }
    RavenSurface(this,Path,TEXT("Footpath"),false);
    // Close ground cover remains visible beyond the individual blade draw distance.
    for(double X=-18000;X<36500;X+=200)for(double Y=-12000;Y<16000;Y+=200)
    {
        if(FMath::Abs(X-18500)<2800||FVector2D(X+14300,Y+14400).Size()<4300)continue;
        if(Y+200>RiverCentre(X/100)*100-RiverWidth(X/100)*100-220)continue;
        TArray<FVector> Q;for(FVector2D D:{FVector2D(0,0),FVector2D(200,0),FVector2D(200,200),FVector2D(0,200)})Q.Add(FVector(X+D.X,Y+D.Y,GroundHeight(X+D.X,Y+D.Y)+1));
        Turf.Add(Q);
    }
    RavenSurface(this,Turf,TEXT("FieldTurf"),false);
    FRandomStream R(261010);
    TArray<TArray<FVector>> White,Gold,Purple,Stems;
    int Count=0;
    for(int I=0;I<28000;++I)
    {
        const double X=R.FRandRange(-17500,36000),Y=R.FRandRange(-11500,14500);
        if(IsWater(X,Y)||Y>RiverCentre(X/100)*100-RiverWidth(X/100)*100-250||FMath::Abs(X-18500)<3000||FMath::Abs(X-FootpathX(Y))<175)continue;
        if(FVector2D(X+14300,Y+14400).Size()<4400||(X>-1700&&X<14200&&FMath::Abs(Y)<750))continue;
        const double Patch=.5+.5*FMath::PerlinNoise2D(FVector2D(X*.00065,Y*.00065));
        if(R.FRand()>FMath::Pow(Patch,2)*1.65)continue;
        const double Z=GroundHeight(X,Y),H=R.FRandRange(42,85),A=R.FRandRange(0,2*PI);
        for(int S=0;S<2;++S)
        {
            const FVector D(FMath::Cos(A+S*PI/2)*.9,FMath::Sin(A+S*PI/2)*.9,0);
            Stems.Add({FVector(X,Y,Z)-D,FVector(X,Y,Z)+D,FVector(X+3,Y,Z+H)+D,FVector(X+3,Y,Z+H)-D});
        }
        auto& Petals=I%5==0?Purple:(I%3==0?Gold:White);
        const FVector Centre(X+3,Y,Z+H);
        const double Radius=R.FRandRange(5,9);
        for(int Petal=0;Petal<6;++Petal)
        {
            const double Angle=A+Petal*PI/3;const FVector D(FMath::Cos(Angle),FMath::Sin(Angle),0),Side(-D.Y,D.X,0);
            Petals.Add({Centre,Centre+D*Radius*.7-Side*Radius*.32,Centre+D*Radius+FVector(0,0,1.5),Centre+D*Radius*.7+Side*Radius*.32});
        }
        Gold.Add({Centre+FVector(-2,-2,1),Centre+FVector(2,-2,1),Centre+FVector(2,2,1),Centre+FVector(-2,2,1)});
        ++Count;
    }
    RavenSurface(this,White,TEXT("FlowerWhite"),false);RavenSurface(this,Gold,TEXT("FlowerGold"),false);
    RavenSurface(this,Purple,TEXT("FlowerPurple"),false);RavenSurface(this,Stems,TEXT("FlowerStem"),false);
    UE_LOG(LogTemp,Display,TEXT("RavenMeadowDetails flowers=%d; forest-to-river footpath; continuous green turf"),Count);
}
