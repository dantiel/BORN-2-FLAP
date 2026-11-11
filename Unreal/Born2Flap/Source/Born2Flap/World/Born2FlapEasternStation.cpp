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
    // The compound now guards the east headland itself, so the chain-link must
    // read all the way from the beach: fully drawn to 120 m, fading to 900 m.
    Wire->SetCullDistances(12000,90000);
    auto* Tetra=Group(TEXT("/Engine/BasicShapes/Cylinder"),TEXT("/Game/Shiomori/Materials/M_EastConcrete"));
    auto* TetraTip=Group(TEXT("/Engine/BasicShapes/Cone"),TEXT("/Game/Shiomori/Materials/M_EastConcrete"));
    auto Box=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size)
    {C->AddInstance(FTransform(FQuat::Identity,P,Size/100),true);};
    auto Rod=[](UInstancedStaticMeshComponent* C,FVector A,FVector B,double Radius)
    {C->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,(B-A).GetSafeNormal()),(A+B)*.5,FVector(Radius/50,Radius/50,(B-A).Size()/100)),true);};
    // Half-buried, windowless utility bunker. The only opening is a closed
    // steel door with visible frame, hinge straps and lock hasp. The compound
    // sits on a flat cut terrace ON the east headland (pad height 1000 in
    // Born2FlapEastShore.cpp), close in across the water from the beach, so
    // the whole station reads from the viewable shore instead of hiding 90 m
    // behind the east-shore strand.
    Box(Concrete,{63350,28600,1130},{2250,1650,320});
    Box(Concrete,{63350,28600,1320},{2490,1890,90});
    Box(Metal,{62217,28495,1137},{18,277,235});
    for(double Y:{28346.,28643.})Box(Concrete,{62200,Y,1147},{82,45,270});
    Box(Concrete,{62200,28495,1282},{82,277,35});
    for(double Z:{1070.,1210.})Box(Metal,{62196,28430,Z},{33,97,13});
    Box(Metal,{62192,28595,1140},{37,52,30});
    Box(Concrete,{61775,28495,1010},{750,600,40});
    // Three large but low dishes. Real concave paraboloids, aimed away from
    // the beach; on the headland crest they rise clear of the ridge and read
    // against the sky above the far woodland.
    for(int Dish=0;Dish<3;++Dish)
    {
        const FVector Centre(63800+Dish*1875,31150+(Dish%2)*525,1790);
        Box(Concrete,{Centre.X,Centre.Y,1050},{630,630,100});
        Rod(Poles,{Centre.X,Centre.Y,1090},Centre,127);
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
    // western approach. The fence climbs the headland slopes, so its lines are
    // already part of the eastern headland seen from the beach.
    const FVector Corners[]={{58400,23300,0},{71600,23300,0},{71600,35700,0},{58400,35700,0}};
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
    const double GateZ=Born2FlapBay::Height(58400,29500);
    Rod(Poles,{58375,29275,GateZ},{58375,29275,GateZ+235},5);
    Rod(Poles,{58375,29725,GateZ},{58375,29725,GateZ+235},5);
    Box(Metal,{58360,29500,GateZ+115},{22,30,22});
    // Heavy four-legged concrete armour, partially submerged at the outer tip.
    // Each leg is a fat hub cylinder that tapers through a cone to a blunt
    // point: massive at the crossing, clearly pointy at the tip. (The old
    // full-cone legs read as a spiny sea-mine; the flat cylinders as blobs.)
    FRandomStream Random(18717);
    for(int I=0;I<30;++I)
    {
        const double X=84200+I*420+Random.FRandRange(-280,280),Y=Born2FlapBay::EastShoreEdge(X)+Random.FRandRange(250,2200);
        const FVector C(X,Y,Born2FlapBay::Height(X,Y)+200);
        const FQuat Q=FRotator(Random.FRandRange(-16,16),Random.FRandRange(0,360),Random.FRandRange(-20,20)).Quaternion();
        for(FVector D:{FVector(0,0,1),FVector(.943,0,-.333),FVector(-.471,.816,-.333),FVector(-.471,-.816,-.333)})
        {
            const FVector Dir=Q.RotateVector(D);
            Rod(Tetra,C,C+Dir*240,150);
            // BasicShapes cone: base radius 50 at Z=-50, apex at +50. The base
            // (radius 150) sits inside the hub cylinder; the apex is pulled
            // 120 cm past the tip so the visible end keeps a ~50 cm radius
            // instead of a needle point.
            const FQuat Aim=FQuat::FindBetweenNormals(FVector::UpVector,Dir);
            TetraTip->AddInstance(FTransform(Aim,C+Dir*380,FVector(3.,3.,3.6)),true);
        }
    }
    auto* Sign=NewObject<UTextRenderComponent>(this);Sign->SetupAttachment(RootComponent);Sign->RegisterComponent();
    Sign->SetWorldLocationAndRotation(FVector(58375,29500,Born2FlapBay::Height(58400,29500)+180),FRotator(0,180,0));
    Sign->SetText(FText::FromString(TEXT("RESTRICTED\nNO ENTRY\nNO FLIGHT BEYOND EAST POINT")));
    Sign->SetWorldSize(16);Sign->SetTextRenderColor(FColor(210,193,141));
    UE_LOG(LogTemp,Display,TEXT("EasternStation READY dishes=3 tetrapods=30 boundaryX=96000"));
}