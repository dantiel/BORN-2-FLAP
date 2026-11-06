#include "World/Born2FlapBayTerrain.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

void ABorn2FlapBayTerrain::BuildEasternStation()
{
    auto Group=[&](const TCHAR* Mesh,const TCHAR* Material,bool Collision=true)
    {
        auto* C=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
        C->SetupAttachment(RootComponent);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Mesh));
        C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));
        C->SetCollisionProfileName(Collision?TEXT("BlockAll"):TEXT("NoCollision"));
        C->RegisterComponent();return C;
    };
    auto* Concrete=Group(TEXT("/Engine/BasicShapes/Cube"),TEXT("/Game/Shiomori/Materials/M_EastConcrete"));
    auto* Metal=Group(TEXT("/Engine/BasicShapes/Cube"),TEXT("/Game/Shiomori/Materials/M_EastMetal"));
    auto* Poles=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/Materials/M_EastMetal"));
    auto* Wire=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/Materials/M_EastMetal"),false);
    Wire->SetCullDistances(10000,23000);
    auto* Tetra=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/Materials/M_EastConcrete"));
    auto Box=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size)
    {C->AddInstance(FTransform(FQuat::Identity,P,Size/100),true);};
    auto Rod=[](UInstancedStaticMeshComponent* C,FVector A,FVector B,double Radius)
    {C->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,(B-A).GetSafeNormal()),(A+B)*.5,FVector(Radius/50,Radius/50,(B-A).Size()/100)),true);};
    // Half-buried, windowless utility bunker. The only opening is a closed
    // steel door with visible frame, hinge straps and lock hasp. The whole
    // compound is pulled seaward of the old wooded ridge and enlarged ~1.5x.
    Box(Concrete,{79950,40900,870},{2250,1650,320});
    Box(Concrete,{79950,40900,1060},{2490,1890,90});
    Box(Metal,{78817,40795,877},{18,277,235});
    for(double Y:{40646.,40943.})Box(Concrete,{78800,Y,887},{82,45,270});
    Box(Concrete,{78800,40795,1022},{82,277,35});
    for(double Z:{810.,950.})Box(Metal,{78796,40730,Z},{33,97,13});
    Box(Metal,{78792,40895,880},{37,52,30});
    Box(Concrete,{78375,40795,750},{750,600,40});
    // Three large but low dishes. Real concave paraboloids, aimed away from
    // the beach; they now stand clear of the ridge and read against the sky.
    for(int Dish=0;Dish<3;++Dish)
    {
        const FVector Centre(80400+Dish*1875,43450+(Dish%2)*525,1530);
        Box(Concrete,{Centre.X,Centre.Y,790},{630,630,100});
        Rod(Poles,{Centre.X,Centre.Y,830},Centre,127);
        const FQuat Aim=FRotator(0,35+Dish*24,32).Quaternion();
        TArray<FVector> V,N;TArray<int32> T;TArray<FVector2D> UV;
        constexpr int Rings=16,Sides=64;const double Radius=855;
        for(int R=0;R<=Rings;++R)for(int S=0;S<=Sides;++S)
        {
            const double Rad=Radius*R/Rings,A=2*PI*S/Sides,X=Rad*FMath::Cos(A),Y=Rad*FMath::Sin(A);
            V.Add(Centre+Aim.RotateVector(FVector(X,Y,(X*X+Y*Y)/2550)));
            N.Add(Aim.RotateVector(FVector(-X/1275,-Y/1275,1).GetSafeNormal()));
            UV.Add(FVector2D(X/450,Y/450));
        }
        for(int R=0;R<Rings;++R)for(int S=0;S<Sides;++S)
        {const int A=R*(Sides+1)+S,B=A+Sides+1;T.Append({A,B,A+1,A+1,B,B+1});}
        auto* Bowl=NewObject<UProceduralMeshComponent>(this);Bowl->SetupAttachment(RootComponent);
        Bowl->SetCollisionProfileName(TEXT("BlockAll"));Bowl->RegisterComponent();
        Bowl->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Shiomori/Materials/M_EastDish")));
        Bowl->CreateMeshSection_LinearColor(0,V,T,N,UV,{}, {},true);
        const FVector Feed=Centre+Aim.RotateVector(FVector(0,0,425));
        for(int S=0;S<3;++S)
        {const double A=S*2*PI/3;Rod(Poles,Centre+Aim.RotateVector(FVector(Radius*FMath::Cos(A),Radius*FMath::Sin(A),191)),Feed,10);}
        Rod(Poles,Feed,Feed+Aim.RotateVector(FVector(0,0,70)),24);
    }
    // Diamond chain-link on the perimeter, including a closed gate at the
    // western approach. Thin wire uses instances and distance culling.
    const FVector Corners[]={{77025,37750,0},{86175,37750,0},{86175,45850,0},{77025,45850,0}};
    for(int Side=0;Side<4;++Side)
    {
        const FVector A=Corners[Side],B=Corners[(Side+1)%4];
        const int Panels=FMath::CeilToInt((B-A).Size()/300);const FVector Step=(B-A)/Panels;
        for(int I=0;I<Panels;++I)
        {
            FVector L=A+Step*I,U=L+Step;
            L.Z=Born2FlapBay::Height(L.X,L.Y);U.Z=Born2FlapBay::Height(U.X,U.Y);
            Rod(Poles,L,L+FVector(0,0,245),4);
            Rod(Poles,L+FVector(0,0,235),U+FVector(0,0,235),2.4);
            Rod(Poles,L+FVector(0,0,12),U+FVector(0,0,12),2);
            const int Diamonds=12;
            for(int K=-9;K<Diamonds+9;++K)for(int Sign:{-1,1})
            {
                // Clip diagonal runs to a 225 cm high rectangular panel.
                const double X0=FMath::Max(0.,double(K)*25),X1=FMath::Min(Step.Size(),double(K)*25+225);
                if(X1<=X0)continue;
                const double Z0=X0-K*25,Z1=X1-K*25;
                FVector P=FMath::Lerp(L,U,X0/Step.Size()),Q=FMath::Lerp(L,U,X1/Step.Size());
                P.Z+=10+(Sign>0?Z0:225-Z0);Q.Z+=10+(Sign>0?Z1:225-Z1);
                Rod(Wire,P,Q,.65);
            }
        }
    }
    const double GateZ=Born2FlapBay::Height(77025,40150);
    Rod(Poles,{77020,39925,GateZ},{77020,39925,GateZ+235},5);
    Rod(Poles,{77020,40375,GateZ},{77020,40375,GateZ+235},5);
    Box(Metal,{77005,40350,GateZ+115},{22,30,22});
    // Heavy four-legged concrete armour, partially submerged at the outer tip.
    // Legs are flat-ended cylinders meeting at a shared crossing — a proper
    // tetrapod, not the spiny sea-mine look of the old tapered cones.
    FRandomStream Random(18717);
    for(int I=0;I<30;++I)
    {
        const double X=84200+I*420+Random.FRandRange(-280,280),Y=Born2FlapBay::EastShoreEdge(X)+Random.FRandRange(250,2200);
        const FVector C(X,Y,Born2FlapBay::Height(X,Y)+200);
        const FQuat Q=FRotator(Random.FRandRange(-16,16),Random.FRandRange(0,360),Random.FRandRange(-20,20)).Quaternion();
        for(FVector D:{FVector(0,0,1),FVector(.943,0,-.333),FVector(-.471,.816,-.333),FVector(-.471,-.816,-.333)})
            Rod(Tetra,C,C+Q.RotateVector(D)*400,170);
    }
    auto* Sign=NewObject<UTextRenderComponent>(this);Sign->SetupAttachment(RootComponent);Sign->RegisterComponent();
    Sign->SetWorldLocationAndRotation(FVector(77020,40150,Born2FlapBay::Height(77025,40150)+180),FRotator(0,180,0));
    Sign->SetText(FText::FromString(TEXT("RESTRICTED\nNO ENTRY\nNO FLIGHT BEYOND EAST POINT")));
    Sign->SetWorldSize(16);Sign->SetTextRenderColor(FColor(210,193,141));
    UE_LOG(LogTemp,Display,TEXT("EasternStation READY dishes=3 tetrapods=30 boundaryX=96000"));
}