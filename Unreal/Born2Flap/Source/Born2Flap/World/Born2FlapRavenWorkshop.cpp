#include "World/Born2FlapValley.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "World/RavenSurface.h"

// A fictional farm workshop grown into a permanent research centre. Dimensions
// are centimetres; the high assembly nave stays open between two working floors.
void ABorn2FlapValley::BuildWorkshop()
{
    const FRotator Rot(0,18,0);
    FVector C(-16000,-15500,0);
    double Floor=GroundHeight(C.X,C.Y);
    for(double X:{-1750.,1750.}) for(double Y:{-1000.,1000.})
    {const FVector Q=C+Rot.RotateVector(FVector(X,Y,0));Floor=FMath::Max(Floor,GroundHeight(Q.X,Q.Y));}
    C.Z=Floor+65;
    auto P=[&](FVector V){return C+Rot.RotateVector(V);};
    auto Box=[&](FVector V,FVector Size,const TCHAR* Mat,bool Solid=true,FRotator R=FRotator::ZeroRotator)
    {Part(TEXT("Cube"),Mat,P(V),Size/100,(Rot.Quaternion()*R.Quaternion()).Rotator(),Solid);};
    auto Rod=[&](FVector A,FVector B,double Radius,const TCHAR* Mat){Beam(P(A),P(B),Radius,Mat,false);};
    auto Label=[&](FVector V,const TCHAR* Text,float Size){Sign(P(V),FRotator(0,108,0),Text,Size);};

    // Footings follow the sloping ground, rather than leaving a hovering slab.
    auto Pad=[&](FVector V,FVector Size)
    {
        for(double X=-Size.X/2;X<Size.X/2;X+=200)
        for(double Y=-Size.Y/2;Y<Size.Y/2;Y+=200)
        {
            const double SX=FMath::Min(200.,Size.X/2-X),SY=FMath::Min(200.,Size.Y/2-Y);
            const FVector Q=V+FVector(X+SX/2,Y+SY/2,0),W=P(Q);
            const double Bottom=GroundHeight(W.X,W.Y)-C.Z-35,H=FMath::Max(30.,V.Z-Bottom);
            Box(FVector(Q.X,Q.Y,V.Z-H/2),FVector(SX,SY,H),TEXT("Stone"));
        }
        Box(V+FVector(0,0,5),FVector(Size.X,Size.Y,10),TEXT("Concrete"));
    };
    Pad(FVector(0,0,0),FVector(3360,1960,0));
    const double L=1600,W=900,H=840,Rise=560,Pitch=FMath::RadiansToDegrees(FMath::Atan(Rise/W));
    // Two distinct storeys with real window openings; timber remains the primary structure.
    for(int S:{-1,1})
    {
        for(int I=-4;I<=4;++I) Box(FVector(I*400,S*W,H/2),FVector(26,32,H),TEXT("Timber"));
        for(double Z:{85.,415.,805.}) Box(FVector(0,S*W,Z),FVector(3220,32,Z==85?170.:65.),Z==85?TEXT("Stone"):TEXT("Timber"));
        for(int I=-4;I<4;++I) for(int Level=0;Level<2;++Level)
        {
            const double X=I*400+200,Z=Level*400+235;
            for(int E:{-1,1}) Box(FVector(X+E*145,S*W,Z),FVector(105,22,265),TEXT("Plaster"));
            Box(FVector(X,S*W,Z-104),FVector(190,22,57),TEXT("Plaster"));
            Box(FVector(X,S*W,Z+109),FVector(190,22,47),TEXT("Plaster"));
            for(int E:{-1,1})
            {
                Box(FVector(X+E*94,S*(W+2),Z),FVector(10,34,170),TEXT("Timber"));
                Box(FVector(X,S*(W+2),Z+E*80),FVector(198,34,10),TEXT("Timber"));
            }
            Box(FVector(X,S*W,Z),FVector(7,12,160),TEXT("Timber"),false);
            Box(FVector(X,S*W,Z),FVector(180,12,7),TEXT("Timber"),false);
        }
        // Narrow plank courses on the lower bays and braced half-timber panels above.
        for(int I=-4;I<4;++I)
            Rod(FVector(I*400,S*(W+20),445),FVector(I*400+390,S*(W+20),785),9,TEXT("Timber"));
        Box(FVector(0,S*990,H-48),FVector(3440,16,20),TEXT("Rust"),false);
        for(int I=-2;I<=2;++I)
        {
            Rod(FVector(I*500,S*W,H-15),FVector(I*500,0,H+Rise-20),14,TEXT("Timber"));
            Rod(FVector(I*500,S*W,680),FVector(I*500,S*620,840),10,TEXT("Timber"));
        }
    }
    // Half-hipped roof: clipped upper gables, shortened ridge and real bevel planes.
    TArray<TArray<FVector>> RoofFaces;
    auto RoofFace=[&](TArray<FVector> Local)
    {
        if(FVector::CrossProduct(Local[1]-Local[0],Local[2]-Local[0]).Z<0)Algo::Reverse(Local);
        TArray<FVector> World;for(auto V:Local)World.Add(P(V));RoofFaces.Add(World);
    };
    for(int S:{-1,1})RoofFace({FVector(-1720,S*980,790),FVector(1720,S*980,790),FVector(1720,S*430,1132),FVector(1180,0,1400),FVector(-1180,0,1400),FVector(-1720,S*430,1132)});
    for(int E:{-1,1})
    {
        RoofFace({FVector(E*1720,-430,1132),FVector(E*1720,430,1132),FVector(E*1180,0,1400)});
        for(int S:{-1,1})Rod(FVector(E*1180,0,1410),FVector(E*1720,S*430,1142),17,TEXT("Slate"));
        Rod(FVector(E*1720,-980,788),FVector(E*1720,-430,1132),15,TEXT("Timber"));
        Rod(FVector(E*1720,980,788),FVector(E*1720,430,1132),15,TEXT("Timber"));
        Rod(FVector(E*1720,-430,1132),FVector(E*1720,430,1132),15,TEXT("Timber"));
    }
    RavenSurface(this,RoofFaces,TEXT("Roof"),true);
    TArray<TArray<FVector>> Lining;for(auto F:RoofFaces){Algo::Reverse(F);for(auto& V:F)V.Z-=18;Lining.Add(F);}
    RavenSurface(this,Lining,TEXT("Timber"),false);
    Box(FVector(0,0,1408),FVector(2400,38,25),TEXT("Slate"),false);
    for(int E:{-1,1})
    {
        for(int S:{-1,1}) Box(FVector(E*L,S*660,420),FVector(32,480,840),TEXT("Timber"));
        Box(FVector(E*L,0,750),FVector(32,840,180),TEXT("Timber"));
        for(double Z=850;Z<1170;Z+=30)
            Box(FVector(E*L,0,Z),FVector(28,2*W*(1400-Z)/Rise,28),TEXT("Timber"));
        // Sliding doors stand open, leaving an 8.4 m x 6.6 m handling passage.
        for(int S:{-1,1})
        {
            Box(FVector(E*(L+35),S*690,322),FVector(22,500,640),TEXT("Teal"));
            Rod(FVector(E*(L+50),S*460,35),FVector(E*(L+50),S*910,605),9,TEXT("Timber"));
        }
        Box(FVector(E*(L+55),0,668),FVector(18,1950,14),TEXT("Rust"),false);
    }
    // Working gallery on the far side, connected by a proper 23-riser stair.
    Box(FVector(0,-625,402),FVector(3110,500,28),TEXT("Timber"));
    for(int I=-3;I<=3;++I)
    {
        Box(FVector(I*500,-375,200),FVector(20,20,400),TEXT("Timber"));
        Rod(FVector(I*500,-375,420),FVector(I*500,-375,525),5,TEXT("Timber"));
    }
    for(double Z:{460.,525.}) Rod(FVector(-1530,-375,Z),FVector(1500,-375,Z),5,TEXT("Timber"));
    for(int I=0;I<23;++I)
        Box(FVector(-1410+I*27,630,(I+1)*18/2.),FVector(28,130,(I+1)*18),TEXT("Timber"));
    Rod(FVector(-1420,550,100),FVector(-790,550,515),5,TEXT("Timber"));
    Box(FVector(-720,75,402),FVector(150,1220,28),TEXT("Timber"));
    for(int S:{-1,1}) Rod(FVector(-720+S*73,-370,510),FVector(-720+S*73,700,510),5,TEXT("Timber"));
    auto Bench=[&](FVector B)
    {
        Box(B+FVector(0,0,82),FVector(250,85,12),TEXT("Timber"));
        for(int X:{-1,1})for(int Y:{-1,1})Box(B+FVector(X*105,Y*30,39),FVector(10,10,78),TEXT("Rust"));
        Box(B+FVector(-35,0,92),FVector(90,50,3),TEXT("Ivory"),false);
        Box(B+FVector(80,10,99),FVector(34,22,25),TEXT("Teal"),false);
        for(int I=0;I<4;++I) Rod(B+FVector(-75+I*28,-15,94),B+FVector(-60+I*28,15,94),1.4,TEXT("Slate"));
    };
    for(double X:{-1150.,0.,950.}) Bench(FVector(X,-680,420));
    Bench(FVector(900,730,10));Bench(FVector(-1050,-660,10));
    Label(FVector(150,924,530),TEXT("RAVENSTONE  /  ORNITHOPTER WORKS"),42);
    Label(FVector(150,925,473),TEXT("ASSEMBLY   /   FLIGHT RESEARCH   /   REPAIR"),20);

    // The annex stands on its own lower pad; its open front faces the flight meadow.
    const FVector HC=P(FVector(3050,1350,0));
    double HF=-1.e9;
    for(double X:{-1000.,1000.})for(double Y:{-750.,750.})
    {const FVector Q=HC+Rot.RotateVector(FVector(X,Y,0));HF=FMath::Max(HF,GroundHeight(Q.X,Q.Y));}
    const double HZ=HF+30-C.Z;
    const FVector A(3050,1350,HZ);
    Pad(A,FVector(2220,1740,0));
    auto Annex=[&](FVector V,FVector Size,const TCHAR* M,bool Solid=true){Box(A+V,Size,M,Solid);};
    Annex(FVector(0,-800,270),FVector(2100,24,540),TEXT("Timber"));
    for(int S:{-1,1})
    {
        Annex(FVector(S*1050,0,270),FVector(24,1600,540),TEXT("Timber"));
        Annex(FVector(S*925,800,270),FVector(250,24,540),TEXT("Teal"));
        for(int I=-3;I<=3;++I) Annex(FVector(S*1066,I*250,270),FVector(10,9,530),TEXT("Rust"),false);
    }
    Annex(FVector(0,800,510),FVector(1600,30,60),TEXT("Timber"));
    // Clerestory daylight strip between two shallow roof pitches.
    for(int S:{-1,1}) Box(A+FVector(S*585,0,595),FVector(1070,1810,20),TEXT("Slate"),true,FRotator(-S*6,0,0));
    Annex(FVector(0,0,730),FVector(240,1810,18),TEXT("Slate"));
    Label(A+FVector(0,820,522),TEXT("02  /  EXPERIMENTAL AIRFRAMES"),29);
    Bench(A+FVector(-650,-650,10));

    // Bespoke ribbed membrane airframes. Each wing has camber, a swept tip,
    // scalloped trailing edge and a visible wooden spar; these are static exhibits.
    auto Aircraft=[&](FVector Origin,double Scale,bool Suspended)
    {
        auto AP=[&](FVector V){return Origin+V*Scale;};
        Rod(AP(FVector(-240,0,0)),AP(FVector(210,0,0)),5*Scale,TEXT("Timber"));
        Rod(AP(FVector(-130,-26,-18)),AP(FVector(120,26,15)),3*Scale,TEXT("Timber"));
        Box(AP(FVector(0,0,-15)),FVector(100,42,50)*Scale,TEXT("Teal"),false);
        auto* Mem=NewObject<UProceduralMeshComponent>(this,NAME_None,RF_Transactional);
        Mem->ComponentTags.Add(TEXT("RavenGenerated"));Mem->SetupAttachment(RootComponent);
        Mem->SetMobility(EComponentMobility::Static);Mem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        AddInstanceComponent(Mem);Mem->RegisterComponent();
        TArray<FVector> V,N;TArray<FVector2D> UV;TArray<int32> T;
        for(int S:{-1,1})for(int Rib=0;Rib<=16;++Rib)
        {
            const double U=Rib/16.,Y=S*(30+510*U),Lead=105-210*U*U;
            const double Chord=240*(1-.79*U),Z=25+60*FMath::Sin(U*PI*.8);
            for(int J=0;J<=6;++J)
            {
                const double F=J/6.;V.Add(P(AP(FVector(Lead-Chord*F,Y,Z+24*FMath::Sin(F*PI)*(1-U)))));
                N.Add(FVector::UpVector);UV.Add(FVector2D(U,F));
            }
            if(Rib%2==0)Rod(AP(FVector(Lead,Y,Z+2)),AP(FVector(Lead-Chord,Y,Z+2)),1.8*Scale,TEXT("Timber"));
        }
        for(int S=0;S<2;++S)for(int I=0;I<16;++I)for(int J=0;J<6;++J)
        {
            const int B=S*119+I*7+J;
            for(int K:{B,B+7,B+1,B+1,B+7,B+8,B+1,B+7,B,B+8,B+7,B+1})T.Add(K);
        }
        TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        Mem->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,Tangents,false);
        Mem->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Ravenstonefield/Materials/M_Bone")));
        for(int S:{-1,1})
        {
            Rod(AP(FVector(105,S*30,25)),AP(FVector(-105,S*540,60)),4*Scale,TEXT("Timber"));
            Rod(AP(FVector(40,0,-70)),AP(FVector(-45,S*330,72)),.8*Scale,TEXT("Rust"));
            Box(AP(FVector(-230,S*60,8)),FVector(100,130,4)*Scale,TEXT("Bone"),false,FRotator(0,S*18,0));
        }
        if(Suspended)
            for(int S:{-1,1})Rod(AP(FVector(0,S*240,75)),AP(FVector(0,S*240,600)),.7,TEXT("Rust"));
        else
            for(int S:{-1,1})
            {Box(AP(FVector(S*130,0,-125)),FVector(16,220,12)*Scale,TEXT("Rust"),false);Rod(AP(FVector(S*130,0,-125)),AP(FVector(S*130,0,-15)),5,TEXT("Rust"));}
    };
    Aircraft(FVector(350,50,540),1,true);
    Aircraft(A+FVector(-50,0,165),1.15,false);
    Aircraft(FVector(850,-640,525),.23,false);
    // Handling apron and a gently stepped connection to the old farm lane.
    Pad(A+FVector(0,1350,-8),FVector(2350,1050,0));
    for(int I=0;I<18;++I)
    {
        const double T=I/17.;const FVector Q=FMath::Lerp(FVector(1810,0,0),A+FVector(-1200,1000,-8),T);
        Pad(Q,FVector(270,500,0));
    }
    for(FVector V:{FVector(-1000,0,720),FVector(0,0,720),FVector(1000,0,720),FVector(0,-650,700),A+FVector(-550,0,440),A+FVector(550,0,440)})
    {
        auto* Light=NewObject<UPointLightComponent>(this,NAME_None,RF_Transactional);
        Light->ComponentTags.Add(TEXT("RavenGenerated"));Light->SetupAttachment(RootComponent);
        Light->SetRelativeLocation(P(V));Light->SetMobility(EComponentMobility::Movable);
        Light->SetIntensityUnits(ELightUnits::Candelas);Light->SetIntensity(18000);Light->SetAttenuationRadius(2200);Light->SetLightColor(FLinearColor(1.f,.90f,.76f));
        Light->SetCastShadows(false);AddInstanceComponent(Light);Light->RegisterComponent();
        Box(V+FVector(0,0,12),FVector(90,35,12),TEXT("Ivory"),false);
    }
    // Let the eye adapt when entering the workshops; the outdoor world uses a
    // fixed daylight exposure that otherwise crushes covered workspaces to black.
    for(int I=0;I<2;++I)
    {
        auto* Bounds=NewObject<UBoxComponent>(this,NAME_None,RF_Transactional);
        Bounds->ComponentTags.Add(TEXT("RavenGenerated"));Bounds->SetupAttachment(RootComponent);
        Bounds->SetRelativeLocationAndRotation(P(I==0?FVector(0,0,550):A+FVector(0,0,280)),Rot);
        Bounds->SetBoxExtent(I==0?FVector(1580,880,550):FVector(1040,790,280));
        Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);AddInstanceComponent(Bounds);Bounds->RegisterComponent();
        auto* Exposure=NewObject<UPostProcessComponent>(this,NAME_None,RF_Transactional);
        Exposure->ComponentTags.Add(TEXT("RavenGenerated"));Exposure->SetupAttachment(Bounds);
        Exposure->bUnbound=false;Exposure->Priority=200;Exposure->BlendRadius=180;
        auto& S=Exposure->Settings;S.bOverride_AutoExposureMinBrightness=true;S.bOverride_AutoExposureMaxBrightness=true;
        S.AutoExposureMinBrightness=7;S.AutoExposureMaxBrightness=11;
        AddInstanceComponent(Exposure);Exposure->RegisterComponent();
    }
    UE_LOG(LogTemp,Display,TEXT("RavenWorkshop: barn 32x18m, gallery 4.2m, roof 14m; barn floor %.1f hangar floor %.1f; 3 experimental airframes"),C.Z,HF+30);
}
