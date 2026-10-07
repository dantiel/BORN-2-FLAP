#include "Water/Born2FlapSurf.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Game/Born2FlapGameMode.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"

ABorn2FlapSurf::ABorn2FlapSurf()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BreakingSurf"));
    RootComponent=Mesh;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    // Water precedes translucent bird membranes; it must not paint over them.
    Mesh->TranslucencySortPriority=-10;
}
void ABorn2FlapSurf::BeginPlay()
{
    Super::BeginPlay();
    Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Weather/M_BreakingSurf")));
}
void ABorn2FlapSurf::Tick(float Dt)
{
    Super::Tick(Dt);
    Accumulator+=Dt;
    if(Accumulator<1.f/30.f) return;
    Accumulator=0;
    const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    if(!Camera) return;
    const FVector Cam=Camera->GetCameraLocation();
    const double CX=FMath::Clamp(FMath::GridSnap(Cam.X,200.),-34000.,34000.);
    const double Time=GetWorld()->GetTimeSeconds();
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    const auto* Director=AShiomoriWaterDirector::Get(GetWorld());
    const double Strength=Director ? Director->GetParameters().ShoreBreakIntensity : 1.;
    Vertices.Reset();Normals.Reset();UV.Reset();Colors.Reset();
    constexpr int NX=110, NY=24;
    for(int Wave=0;Wave<3;++Wave)
    for(int Kind=0;Kind<2;++Kind)
    for(int I=0;I<=NX;++I)
    {
        const double X=CX-11000+I*200;
        const double Front=Born2FlapSurf::Front(X,Time,Wave);
        const double Shore=Born2FlapSurf::Shore(X);
        const double Growth=(1-FMath::SmoothStep(3400.,6100.,Front));
        const double Collapse=FMath::SmoothStep(-250.,950.,Front);
        const double H=(100+25*FMath::Sin(X*.0011+Wave))*Growth*Collapse*Strength;
        const double Curl=(1-FMath::SmoothStep(1600.,3000.,Front))*Collapse;
        const double SideFade=FMath::SmoothStep(0.,6.,double(FMath::Min(I,NX-I)));
        for(int J=0;J<=NY;++J)
        {
            const double V=double(J)/NY;
            double Y,Z,Alpha,Foam;
            if(Kind==0)
            {
                if(V<.6)
                {
                    const double Q=V/.6;
                    Y=Front+650*(1-Q);Z=H*FMath::Sin(Q*PI*.5);
                }
                else
                {
                    const double Q=(V-.6)/.4;
                    Y=Front+FMath::Lerp(-350*Q,-145*FMath::Sin(Q*PI*1.4),Curl);
                    Z=FMath::Lerp(H*(1-Q),H-95*(1-FMath::Cos(Q*PI*1.4)),Curl);
                }
                Z=FMath::Max(-20.,Z)-50;
                Foam=FMath::SmoothStep(.35,.72,V)*(1-FMath::SmoothStep(1800.,3600.,Front));
                Alpha=SideFade*Growth*Collapse*FMath::Sin(PI*V)*.94;
            }
            else
            {
                // White water spreads behind the falling lip, then drains back.
                Y=Front-160+V*1500;
                const double Ground=GM ? GM->GroundHeight(X,Shore+Y) : -60.;
                Z=FMath::Max(-43.,Ground+3)+4*FMath::Sin(V*12+Time*2+X*.003);
                Foam=1;
                Alpha=SideFade*(1-FMath::SmoothStep(1100.,3200.,Front))*FMath::SmoothStep(-550.,100.,Front)
                    *FMath::Pow(FMath::Sin(PI*V),.55)*.9;
            }
            // Follow the same displaced liquid surface as the ocean shader.
            // Single Layer Water writes depth, so foam must actually lie above
            // that surface rather than relying on translucent draw ordering.
            const double ShoreBlend=FMath::SmoothStep(100.,3000.,Y);
            const double LiquidZ=Director ? -50+(Director->GetWaterHeightAt(X,Shore+Y)+50)*ShoreBlend : -50;
            if(Kind==0) Z+=LiquidZ+50+2;
            else Z=FMath::Max(Z,LiquidZ+4);
            Vertices.Add(FVector(X,Shore+Y,Z));
            Normals.Add(FVector::UpVector);
            UV.Add(FVector2D(X*.003,V));
            Colors.Add(FLinearColor(Foam,Kind==1 ? 1.f : 0.f,0,Alpha));
        }
    }
    if(!bCreated)
    {
        for(int Section=0;Section<6;++Section)
        for(int I=0;I<NX;++I) for(int J=0;J<NY;++J)
        {
            const int A=Section*(NX+1)*(NY+1)+I*(NY+1)+J,B=A+NY+1;
            Triangles.Append({A,B,A+1,A+1,B,B+1});
        }
    }
    // Geometric normals retain the curl silhouette and catch low sunlight.
    for(int I=0;I<Vertices.Num();++I) Normals[I]=FVector::ZeroVector;
    for(int I=0;I<Triangles.Num();I+=3)
    {
        int A=Triangles[I],B=Triangles[I+1],C=Triangles[I+2];
        FVector N=FVector::CrossProduct(Vertices[B]-Vertices[A],Vertices[C]-Vertices[A]);
        if(N.Z<0) N=-N;
        Normals[A]+=N;Normals[B]+=N;Normals[C]+=N;
    }
    for(auto& N:Normals) N=N.GetSafeNormal();
    if(!bCreated) Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,{},false);
    else Mesh->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,{});
    bCreated=true;
}
bool ABorn2FlapSurf::ValidateGeometry() const
{
    float Peak=-10000;int Foam=0;
    for(int I=0;I<Vertices.Num();++I)
    {
        if(Vertices[I].ContainsNaN()) return false;
        Peak=FMath::Max(Peak,float(Vertices[I].Z));
        Foam+=Colors[I].R>.8 && Colors[I].A>.15;
    }
    return bCreated && Peak>0 && Foam>100;
}
