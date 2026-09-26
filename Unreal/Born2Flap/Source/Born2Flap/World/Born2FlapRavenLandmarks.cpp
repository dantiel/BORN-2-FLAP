#include "World/Born2FlapValley.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

void ABorn2FlapValley::Part(const FString& Mesh,const FString& Material,FVector P,FVector Scale,FRotator R,bool Collision)
{
    const FString MeshPath=Mesh.StartsWith(TEXT("/"))?Mesh:TEXT("/Engine/BasicShapes/")+Mesh;
    const FString MatPath=Material.IsEmpty()?TEXT(""):TEXT("/Game/Ravenstonefield/Materials/M_")+Material;
    Group(MeshPath,MatPath,Collision)->AddInstance(FTransform(R,P,Scale));
}
void ABorn2FlapValley::Beam(FVector A,FVector B,double Radius,const FString& Material,bool Collision)
{
    const FVector D=B-A;
    // Cylinder is aligned with local Z. Align that axis to the beam direction.
    Part(TEXT("Cylinder"),Material,(A+B)*.5,FVector(Radius/50,Radius/50,D.Size()/100),
         FRotationMatrix::MakeFromZ(D).Rotator(),Collision);
}
void ABorn2FlapValley::Sign(FVector P,FRotator R,const FString& Text,float Size)
{
    auto* C=NewObject<UTextRenderComponent>(this,NAME_None,RF_Transactional);
    C->ComponentTags.Add(TEXT("RavenGenerated")); C->SetupAttachment(RootComponent);
    C->SetRelativeLocationAndRotation(P,R);C->SetWorldSize(Size);C->SetText(FText::FromString(Text));
    C->SetHorizontalAlignment(EHTA_Center);C->SetVerticalAlignment(EVRTA_TextCenter);
    C->SetTextRenderColor(FColor(212,202,169));C->SetCastShadow(false);
    AddInstanceComponent(C);C->RegisterComponent();
}

void ABorn2FlapValley::BuildBarn(FVector Centre,double Yaw,double Scale,bool House)
{
    Centre.Z=GroundHeight(Centre.X,Centre.Y)+15;
    const FRotator Rot(0,Yaw,0);
    auto P=[&](FVector Local){return Centre+Rot.RotateVector(Local*Scale);};
    auto Box=[&](FVector Pos,FVector Size,const TCHAR* Mat,bool Solid=true,FRotator Extra=FRotator::ZeroRotator)
    {Part(TEXT("Cube"),Mat,P(Pos),Size*Scale/100,(Rot.Quaternion()*Extra.Quaternion()).Rotator(),Solid);};
    const double L=House?640:1020,W=House?450:550,H=House?500:560,Pitch=34;
    const double Rise=W*FMath::Tan(FMath::DegreesToRadians(Pitch));
    // Stone plinth, recessed floor, walls with posts instead of a sealed cube.
    Box(FVector(0,0,-75),FVector(L*2+80,W*2+80,160),TEXT("Stone"));
    Box(FVector(0,0,18),FVector(L*2,W*2,30),TEXT("Timber"));
    for(int Side:{-1,1})
    {
        Box(FVector(0,Side*W,H/2),FVector(L*2,30,H),House?TEXT("Plaster"):TEXT("Timber"));
        for(int I=-4;I<=4;++I)
            Box(FVector(I*L/4,Side*(W+17),H/2),FVector(18,22,H+35),TEXT("Timber"));
        Box(FVector(0,Side*(W+20),120),FVector(L*2+70,28,25),TEXT("Timber"));
        Box(FVector(0,Side*(W+20),H-16),FVector(L*2+70,28,30),TEXT("Timber"));
        if(House)
            for(int I:{-1,1})
            {
                const double X=I*L*.5;
                Box(FVector(X,Side*(W+19),285),FVector(125,10,150),TEXT("Window"),false);
                for(int Edge:{-1,1})
                {
                    Box(FVector(X+Edge*75,Side*(W+26),285),FVector(14,18,180),TEXT("Ivory"),false);
                    Box(FVector(X,Side*(W+26),285+Edge*82),FVector(165,18,15),TEXT("Ivory"),false);
                    Box(FVector(X+Edge*111,Side*(W+22),285),FVector(46,18,156),TEXT("Teal"),false);
                }
                Box(FVector(X,Side*(W+30),285),FVector(9,16,156),TEXT("Ivory"),false);
                Box(FVector(X,Side*(W+30),285),FVector(130,16,9),TEXT("Ivory"),false);
            }
        const double RoofWidth=(W+90)/FMath::Cos(FMath::DegreesToRadians(Pitch));
        Box(FVector(0,Side*(W/2+25),H+Rise/2-15),FVector(2*L+175,RoofWidth,24),
            House?TEXT("Roof"):TEXT("Slate"),true,FRotator(0,0,Side*Pitch));
        // Gutters and weathered eaves give the silhouette real thickness.
        Box(FVector(0,Side*(W+87),H-45),FVector(2*L+190,20,30),TEXT("Timber"),false);
        Box(FVector(0,Side*(W+92),H-67),FVector(2*L+180,12,12),TEXT("Rust"),false);
    }
    Box(FVector(0,0,H+Rise+8),FVector(2*L+190,32,20),House?TEXT("Roof"):TEXT("Rust"),false);
    for(int End:{-1,1})
    {
        if(House)
        {
            Box(FVector(End*L,0,H/2),FVector(28,2*W,H),TEXT("Plaster"));
            Box(FVector(End*(L+20),0,142),FVector(12,138,280),TEXT("Timber"));
            Box(FVector(End*(L+35),62,147),FVector(12,5,12),TEXT("Rust"),false);
        }
        else
        {
            // A four-metre opening is a deliberate fly-through landmark.
            for(int Side:{-1,1})
                Box(FVector(End*L,Side*(W+200)/2,H/2),FVector(35,W-200,H),TEXT("Timber"));
            Box(FVector(End*L,0,H-55),FVector(40,2*W,110),TEXT("Timber"));
        }
        for(double Z=H;Z<H+Rise;Z+=28)
        {
            const double Width=2*W*(1-(Z-H)/Rise);
            Box(FVector(End*L,0,Z+12),FVector(30,FMath::Max(Width,24.),25),TEXT("Timber"));
        }
        Box(FVector(End*(L+18),0,H+Rise*.38),FVector(9,65,100),TEXT("Window"),false);
        for(int Side:{-1,1})
            Beam(P(FVector(End*(L+35),0,H+Rise)),P(FVector(End*(L+35),Side*(W+80),H-45)),12*Scale,TEXT("Ivory"));
    }
    if(House)
    {
        Box(FVector(-L*.4,110,H+Rise+40),FVector(85,95,260),TEXT("Stone"));
        Box(FVector(-L*.4,110,H+Rise+175),FVector(112,122,26),TEXT("Slate"));
        Box(FVector(L+105,0,25),FVector(190,230,50),TEXT("Stone"));
    }
    else
    {
        for(int I=-3;I<=3;++I)
        {
            Beam(P(FVector(I*L/3,-W,H-20)),P(FVector(I*L/3,W,H-20)),12*Scale,TEXT("Timber"));
            for(int Side:{-1,1})
                Beam(P(FVector(I*L/3,Side*W,H-25)),P(FVector(I*L/3,0,H+Rise-25)),12*Scale,TEXT("Timber"));
        }
        // Uneven planks, lean-to and discarded wheels; no uniform prefab row.
        for(int I=0;I<8;++I)
            Box(FVector(-L+I*110,W+200,115+I*2),FVector(90,160,220),TEXT("Timber"),false,FRotator(0,0,-4+I));
    }
}

void ABorn2FlapValley::BuildBridge()
{
    const double X=18500,Y=RiverCentre(185)*100,Deck=1350;
    // An overbuilt concrete motorway crossing: deep beams, paired piers,
    // drainage pipes, barriers and a gloomy service bunker beside the bank.
    Part(TEXT("Cube"),TEXT("Concrete"),FVector(X,Y,Deck),FVector(19,145,2.5),FRotator::ZeroRotator,true);
    Part(TEXT("Cube"),TEXT("Slate"),FVector(X,Y,Deck+137),FVector(17.3,145,.18));
    for(int Side:{-1,1})
    {
        Part(TEXT("Cube"),TEXT("Concrete"),FVector(X+Side*730,Y,Deck-280),FVector(1.4,145,3.6),FRotator::ZeroRotator,true);
        Part(TEXT("Cube"),TEXT("Concrete"),FVector(X+Side*910,Y,Deck+210),FVector(.55,145,1.7),FRotator::ZeroRotator,true);
        for(int I=-23;I<=23;++I)
            Part(TEXT("Cube"),TEXT("Rust"),FVector(X+Side*914,Y+I*300,Deck+340),FVector(.10,.10,1.25));
        Part(TEXT("Cube"),TEXT("Rust"),FVector(X+Side*914,Y,Deck+390),FVector(.12,145,.12));
        Beam(FVector(X+Side*865,Y-7300,Deck-100),FVector(X+Side*865,Y+7300,Deck-100),13,TEXT("Rust"));
    }
    for(int End:{-1,1})
        for(int Side:{-1,1})
        {
            const double Py=Y+End*4300,G=GroundHeight(X+Side*520,Py)-90;
            Part(TEXT("Cube"),TEXT("Concrete"),FVector(X+Side*520,Py,(G+Deck-120)/2),FVector(2.1,3,(Deck-120-G)/100),FRotator::ZeroRotator,true);
            Part(TEXT("Cube"),TEXT("Concrete"),FVector(X+Side*520,Py,G),FVector(5,6,1.6),FRotator::ZeroRotator,true);
        }
    for(int I=-17;I<=17;++I)
        Part(TEXT("Cube"),TEXT("Ivory"),FVector(X,Y+I*400,Deck+148),FVector(.13,1.8,.015));
    // Long embankment ramps connect both ends to the rolling terrain.
    for(int End:{-1,1}) for(int I=0;I<30;++I)
    {
        const double Py=Y+End*(7500+I*520),Ground=GroundHeight(X,Py),Blend=1-I/30.;
        const double Top=FMath::Lerp(Ground+10,Deck+138,Blend);
        Part(TEXT("Cube"),TEXT("Concrete"),FVector(X,Py,(Top+Ground)/2-40),FVector(19,5.3,FMath::Max(.1,(Top-Ground)/100+.8)),FRotator::ZeroRotator,true);
        Part(TEXT("Cube"),TEXT("Slate"),FVector(X,Py,Top),FVector(17.3,5.3,.14));
    }
    const FVector B(X+2050,Y-4700,GroundHeight(X+2050,Y-4700));
    for(int Side:{-1,1})Part(TEXT("Cube"),TEXT("Concrete"),B+FVector(Side*340,0,240),FVector(1.4,10,4.8),FRotator::ZeroRotator,true);
    Part(TEXT("Cube"),TEXT("Concrete"),B+FVector(0,0,480),FVector(8.2,10,1.3),FRotator::ZeroRotator,true);
    Part(TEXT("Cube"),TEXT("Slate"),B+FVector(0,440,240),FVector(5.4,.3,4.8),FRotator::ZeroRotator,true);
    Sign(B+FVector(0,-505,355),FRotator(0,-90,0),TEXT("BAUWERK 07 / 1978"),29);
}

void ABorn2FlapValley::BuildCastle()
{
    const FVector C(74000,72500,GroundHeight(74000,72500));
    // Small masonry watchtower: a hollow square keep with an arched doorway,
    // slit windows and an incomplete crenellated courtyard wall.
    Part(TEXT("Cube"),TEXT("Stone"),C+FVector(0,0,-100),FVector(14,14,3),FRotator::ZeroRotator,true);
    for(int Side:{-1,1})
    {
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(Side*500,0,1200),FVector(1.8,11.5,24),FRotator::ZeroRotator,true);
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(0,Side*500,1650),FVector(8.2,1.8,15),FRotator::ZeroRotator,true);
        for(int Edge:{-1,1})
            Part(TEXT("Cube"),TEXT("Stone"),C+FVector(Edge*310,Side*500,380),FVector(2,1.8,7.6),FRotator::ZeroRotator,true);
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(0,Side*500,835),FVector(8.2,1.8,1.7),FRotator::ZeroRotator,true);
        for(int Floor=0;Floor<3;++Floor)
        {
            const double Z=1120+Floor*420;
            Part(TEXT("Cube"),TEXT("Window"),C+FVector(Side*593,-30,Z),FVector(.05,.6,1.8));
            Part(TEXT("Cube"),TEXT("Window"),C+FVector(50,Side*594,Z),FVector(.6,.05,1.8));
        }
        for(int I=-2;I<=2;++I)
        {
            Part(TEXT("Cube"),TEXT("Stone"),C+FVector(Side*500,I*245,2550),FVector(2.1,1.25,3),FRotator::ZeroRotator,true);
            if(FMath::Abs(I)<2)Part(TEXT("Cube"),TEXT("Stone"),C+FVector(I*245,Side*500,2550),FVector(1.25,2.1,3),FRotator::ZeroRotator,true);
        }
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(Side*516,0,2320),FVector(2.35,12.4,.7),FRotator::ZeroRotator,true);
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(0,Side*516,2320),FVector(12.4,2.35,.7),FRotator::ZeroRotator,true);
    }
    // Stone arch voussoirs frame the south entrance.
    for(int I=0;I<13;++I)
    {
        const double A=I*PI/12.;
        Part(TEXT("Cube"),TEXT("Stone"),C+FVector(205*FMath::Cos(A),-600,555+205*FMath::Sin(A)),
             FVector(.78,.7,1.05),FRotator(0,0,FMath::RadiansToDegrees(A)-90),true);
    }
    Part(TEXT("Cube"),TEXT("Timber"),C+FVector(0,0,2240),FVector(8.2,8.2,.3),FRotator::ZeroRotator,true);
    FRandomStream R(107);
    for(int I=-8;I<=8;++I)
    {
        const double X=C.X+I*240,Y=C.Y-2050,G=GroundHeight(X,Y);
        if(I>-2&&I<2)continue;
        const double H=R.FRandRange(220,490);
        Part(TEXT("Cube"),TEXT("Stone"),FVector(X,Y,G+H/2),FVector(2.45,1.7,H/100),FRotator(0,0,R.FRandRange(-2,2)),true);
        if(I%3==0)Part(TEXT("Cube"),TEXT("Stone"),FVector(X+60,Y-190,G+60),FVector(1.3,1.5,1.2),FRotator(8,I*15,12),true);
    }
    Beam(C+FVector(340,300,2450),C+FVector(340,300,3300),6,TEXT("Rust"));
    Part(TEXT("Cube"),TEXT("Teal"),C+FVector(350,440,3110),FVector(.07,2.7,2.4),FRotator(5,0,0));
    Sign(C+FVector(0,-735,410),FRotator(0,-90,0),TEXT("R A V E N W A C H T"),32);
    // A switchback stair winds up the lake-facing slope.
    for(int I=0;I<130;++I)
    {
        const double Y=C.Y-2050-I*180,X=C.X+FMath::Sin(I*.045)*2500;
        Part(TEXT("Cube"),TEXT("Stone"),FVector(X,Y,GroundHeight(X,Y)+12),FVector(2.4,1.85,.22),FRotator(0,FMath::Cos(I*.045)*30,0));
    }
}

void ABorn2FlapValley::BuildRelics()
{
    FRandomStream R(1106);
    // Animal-sized bleached rib cages and fallen trunks at the meadow margins.
    // The launch lane remains open; these are discoveries and optional slalom lines.
    for(const FVector2D& Site:{FVector2D(-6000,16500),FVector2D(38000,-17000),FVector2D(101000,65500),FVector2D(51000,20000)})
    {
        const FVector C(Site.X,Site.Y,GroundHeight(Site.X,Site.Y)+25);
        Beam(C+FVector(-175,0,35),C+FVector(190,0,55),9,TEXT("Bone"));
        for(int Rib=0;Rib<7;++Rib)for(int Side:{-1,1})
        {
            const double X=-145+Rib*44,Size=65+30*FMath::Sin(Rib*PI/7.);
            FVector Prev=C+FVector(X,0,55);
            for(int J=1;J<=6;++J)
            {
                const double A=J*PI/7;
                FVector Next=C+FVector(X+J*2,Side*Size*FMath::Sin(A),55+Size*(1-FMath::Cos(A))*.65);
                Beam(Prev,Next,4,TEXT("Bone"));Prev=Next;
            }
        }
        Part(TEXT("Sphere"),TEXT("Bone"),C+FVector(220,8,52),FVector(.6,.37,.38),FRotator(0,15,0));
        for(int I=0;I<4;++I)Beam(C+FVector(R.FRandRange(-250,300),R.FRandRange(180,390),5),C+FVector(R.FRandRange(-250,300),R.FRandRange(180,390),15),7,TEXT("Bone"));
    }
    // A collapsed boathouse on Crow's Acre, and almost nothing on Last Scrap.
    const FVector I1(52000,19000,GroundHeight(52000,19000));
    for(int Side:{-1,1})
    {
        for(int End:{-1,1})Beam(I1+FVector(End*230,Side*190,0),I1+FVector(End*230+35,Side*190,410),15,TEXT("Timber"),true);
        Beam(I1+FVector(-230,Side*190,410),I1+FVector(230,Side*190,410),14,TEXT("Timber"));
    }
    for(int J=0;J<8;++J)Part(TEXT("Cube"),TEXT("Timber"),I1+FVector(J*43-150,40,75),FVector(.33,4,.12),FRotator(J*4,J*17,23));
    const FVector I2(67000,9500,GroundHeight(67000,9500));
    Beam(I2,I2+FVector(90,-20,410),25,TEXT("Timber"),true);
    Beam(I2+FVector(50,0,240),I2+FVector(180,130,310),11,TEXT("Timber"));
    Part(TEXT("Cube"),TEXT("Rust"),I2+FVector(-160,100,45),FVector(1.8,.8,.2),FRotator(12,25,12));
    // Old corrugated culvert hoops provide a little flight playground off the meadow.
    for(int Gate=0;Gate<4;++Gate)
    {
        const FVector C(4500+Gate*1350,-10900-Gate*100,GroundHeight(4500+Gate*1350,-10900-Gate*100)+260);
        for(int I=0;I<32;++I)
        {
            const double A=2*PI*I/32,B=2*PI*(I+1)/32;
            Beam(C+FVector(0,260*FMath::Cos(A),260*FMath::Sin(A)),C+FVector(0,260*FMath::Cos(B),260*FMath::Sin(B)),13,TEXT("Rust"),true);
        }
    }
}

void ABorn2FlapValley::BuildLandmarks()
{
    BuildBarn(FVector(-16000,-15500,0),18,1,false);
    BuildBarn(FVector(-27000,-19000,0),-28,.8,false);
    BuildBarn(FVector(-8500,-21000,0),90,.72,false);
    BuildBarn(FVector(27000,-15500,0),-12,1,true);
    BuildBarn(FVector(32000,-13500,0),17,.85,true);
    BuildBarn(FVector(34000,-20000,0),-35,.78,true);
    BuildBridge();BuildCastle();BuildRelics();
    // A crooked split-rail fence separates the old barns from the flight field.
    for(int I=0;I<60;++I)
    {
        const double X=-32000+I*480,Y=-8200+900*FMath::Sin(I*.11),Z=GroundHeight(X,Y);
        if(I>38&&I<44)continue;
        Beam(FVector(X,Y,Z),FVector(X+I%4*3,Y+10,Z+140),9,TEXT("Timber"),true);
        if(I<59&&I!=38)for(int H:{55,113})
            Beam(FVector(X,Y,Z+H),FVector(X+480,-8200+900*FMath::Sin((I+1)*.11),GroundHeight(X+480,-8200+900*FMath::Sin((I+1)*.11))+H),5,TEXT("Timber"));
    }
    // Lake landing pier and eroded mooring posts.
    for(int I=0;I<32;++I)
    {
        const FVector P(37400+I*54,4800,-5);
        Part(TEXT("Cube"),TEXT("Timber"),P,FVector(.50,3.2,.18),FRotator(0,0,(I%3-1)*.6),true);
        if(I%6==0)for(int Side:{-1,1})Beam(P+FVector(0,Side*145,-250),P+FVector(0,Side*145,95),10,TEXT("Timber"),true);
    }
    // A field radio, workbench and launch sign anchor the player's starting point.
    const FVector Table(-700,-520,GroundHeight(-700,-520));
    Part(TEXT("Cube"),TEXT("Timber"),Table+FVector(0,0,82),FVector(1.8,.8,.09),FRotator::ZeroRotator,true);
    for(int X:{-1,1})for(int Y:{-1,1})Part(TEXT("Cube"),TEXT("Timber"),Table+FVector(X*70,Y*26,39),FVector(.08,.08,.8));
    Part(TEXT("Cube"),TEXT("Teal"),Table+FVector(0,0,108),FVector(.56,.3,.42));
    Part(TEXT("Cube"),TEXT("Slate"),Table+FVector(29,-9,108),FVector(.02,.10,.28));
    Part(TEXT("Cube"),TEXT("RadioDial"),Table+FVector(29,8,115),FVector(.025,.15,.07));
    for(int I=0;I<7;++I)Part(TEXT("Cube"),TEXT("Ivory"),Table+FVector(30,-13+I*1.5,107),FVector(.018,.006,.25));
    Beam(Table+FVector(10,8,130),Table+FVector(16,10,205),.65,TEXT("Rust"));
    Sign(Table+FVector(31,8,103),FRotator::ZeroRotator,TEXT("FM"),3);
    const FVector S(-550,-950,GroundHeight(-550,-950));
    for(int Side:{-1,1})Beam(S+FVector(0,Side*180,0),S+FVector(0,Side*180,245),10,TEXT("Timber"),true);
    Part(TEXT("Cube"),TEXT("Teal"),S+FVector(0,0,188),FVector(.14,4.6,1),FRotator::ZeroRotator,true);
    Sign(S+FVector(9,0,203),FRotator::ZeroRotator,TEXT("RAVENSTONEFIELD"),32);
    Sign(S+FVector(9,0,165),FRotator::ZeroRotator,TEXT("LONG MEADOW / EVERY WINGBEAT COUNTS"),12);
    // Thin field marker posts, never across the runway itself.
    for(int I=0;I<16;++I)
    {
        const double X=I*1700,Y=-5200,Z=GroundHeight(X,Y);
        Part(TEXT("Cube"),TEXT("Ivory"),FVector(X,Y,Z+65),FVector(.12,.12,1.3));
    }
    UE_LOG(LogTemp,Display,TEXT("RavenLandmarks barns=3 houses=3 bridge=1 castle=1 islands=2 relicSites=4"));
}

void ABorn2FlapValley::BuildAtmosphere()
{
    auto Register=[&](USceneComponent* C)
    {C->ComponentTags.Add(TEXT("RavenGenerated"));C->SetupAttachment(RootComponent);AddInstanceComponent(C);C->RegisterComponent();};
    auto* Sun=NewObject<UDirectionalLightComponent>(this,NAME_None,RF_Transactional);
    Sun->SetMobility(EComponentMobility::Movable);Sun->SetRelativeRotation(FRotator(-24,-42,0));
    Sun->SetIntensity(42000);Sun->SetLightColor(FLinearColor(1,.88,.70));Sun->SetAtmosphereSunLight(true);
    Sun->LightSourceAngle=1.1;Sun->SetDynamicShadowDistanceMovableLight(150000);Register(Sun);
    auto* Sky=NewObject<USkyLightComponent>(this,NAME_None,RF_Transactional);
    Sky->SetMobility(EComponentMobility::Movable);Sky->SetRealTimeCapture(true);Sky->SetIntensity(.9);Register(Sky);
    auto* Air=NewObject<USkyAtmosphereComponent>(this,NAME_None,RF_Transactional);Register(Air);
    auto* Fog=NewObject<UExponentialHeightFogComponent>(this,NAME_None,RF_Transactional);
    Fog->SetFogDensity(.0045);Fog->SetFogHeightFalloff(.24);Fog->SetVolumetricFog(true);
    Fog->SetFogInscatteringColor(FLinearColor(.61,.69,.73));Register(Fog);
    auto* Post=NewObject<UPostProcessComponent>(this,NAME_None,RF_Transactional);Post->bUnbound=true;
    auto& S=Post->Settings;
    S.bOverride_AutoExposureMinBrightness=true;S.bOverride_AutoExposureMaxBrightness=true;
    S.AutoExposureMinBrightness=13.2;S.AutoExposureMaxBrightness=13.2;
    S.bOverride_AutoExposureBias=true;S.AutoExposureBias=0;
    S.bOverride_DynamicGlobalIlluminationMethod=true;S.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::Lumen;
    S.bOverride_ReflectionMethod=true;S.ReflectionMethod=EReflectionMethod::Lumen;
    S.bOverride_BloomIntensity=true;S.BloomIntensity=.16;
    S.bOverride_VignetteIntensity=true;S.VignetteIntensity=.15;
    S.bOverride_MotionBlurAmount=true;S.MotionBlurAmount=0;
    S.bOverride_ColorSaturation=true;S.ColorSaturation=FVector4(.95,.96,.95,1);
    Register(Post);
}
