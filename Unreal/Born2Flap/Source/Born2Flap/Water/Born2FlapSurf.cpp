#include "Water/Born2FlapSurf.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Game/Born2FlapGameMode.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

ABorn2FlapSurf::ABorn2FlapSurf()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BreakingSurf"));
    RootComponent=Mesh;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    // Water precedes translucent bird membranes; it must not paint over them.
    Mesh->TranslucencySortPriority=-10;
    SprayFX=CreateDefaultSubobject<UNiagaraComponent>(TEXT("SurfSprayFX"));
    SprayFX->SetupAttachment(RootComponent);
    SprayFX->SetAutoActivate(false);
    SprayFX->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SprayFX->SetCastShadow(false);
    MistFX=CreateDefaultSubobject<UNiagaraComponent>(TEXT("SurfMistFX"));
    MistFX->SetupAttachment(RootComponent);
    MistFX->SetAutoActivate(false);
    MistFX->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MistFX->SetCastShadow(false);
}
void ABorn2FlapSurf::BeginPlay()
{
    Super::BeginPlay();
    // Guard against a missing/broken surf material: a null slot would make the
    // procedural foam mesh render with Unreal's default checkered material.
    if (UMaterialInterface* SurfMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weather/M_BreakingSurf")))
    {
        Mesh->SetMaterial(0, SurfMat);
    }
    else
    {
        Mesh->SetVisibility(false);
    }
    // Surf spray: NS_SurfSpray is an upward-arcing mist burst authored by the
    // NiagaraBuilder commandlet. Missing asset → no spray, never a null renderer.
    if (auto* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weather/NS_SurfSpray")))
    {
        SprayFX->SetAsset(System);
        SprayFX->Activate(true);
    }
    else
    {
        SprayFX->SetVisibility(false);
    }
    // Surf mist: NS_SurfMist is a slow, soft white haze that sits over the wash.
    if (auto* Mist = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weather/NS_SurfMist")))
    {
        MistFX->SetAsset(Mist);
        MistFX->Activate(true);
    }
    else
    {
        MistFX->SetVisibility(false);
    }
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
    // Keep the spray burst pinned to the breaking crest in front of the camera.
    if (SprayFX && SprayFX->GetAsset() && SprayFX->IsActive())
    {
        const double SprayY=Born2FlapSurf::Shore(CX)+250.;
        const double SprayZ=(Director ? Director->GetWaterHeightAt(CX,SprayY) : -50.)+20.;
        SprayFX->SetWorldLocation(FVector(CX,SprayY,SprayZ));
    }
    if (MistFX && MistFX->GetAsset() && MistFX->IsActive())
    {
        const double MistY=Born2FlapSurf::Shore(CX)+150.;
        const double MistZ=(Director ? Director->GetWaterHeightAt(CX,MistY) : -50.)+40.;
        MistFX->SetWorldLocation(FVector(CX,MistY,MistZ));
    }
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
        // Undulating crest: waves travel along the shore and rise/fall in time.
        const double H=(100+25*FMath::Sin(X*.0011+Wave)+18*FMath::Sin(X*.0023+Time*.8+Wave*2.1))*Growth*Collapse*Strength;
        const double Curl=(1-FMath::SmoothStep(1600.,3000.,Front))*Collapse;
        // Broad rounded fade at the strip ends — organic, not square-cut.
        // Smootherstep over a wider run softens the wash into rounded lobes
        // (the "thermal-air" sheets) rather than hard-edged squares.
        const double EdgeSoft=FMath::SmoothStep(0.,30.,double(FMath::Min(I,NX-I)));
        const double EdgeFade=EdgeSoft*EdgeSoft*(3.-2.*EdgeSoft);
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
                // Opaque at the foam lip, fading into the water at the base.
                Alpha=EdgeFade*Growth*Collapse*FMath::SmoothStep(0.,.15,V)*.94;
            }
            else
            {
                // White water spreads behind the falling lip, then drains back
                // as rounded, rippling lobes — not a flat rectangle.
                Y=Front-160+V*1500;
                const double Ground=GM ? GM->GroundHeight(X,Shore+Y) : -60.;
                Z=FMath::Max(-43.,Ground+3)+6*FMath::Sin(V*9+Time*2.2+X*.004+Wave*1.3);
                Foam=1;
                const double Lobe=.55+.45*FMath::Sin(X*.0016+Time*1.1+Wave*2.3);
                Alpha=EdgeFade*Lobe*(1-FMath::SmoothStep(1100.,3200.,Front))*FMath::SmoothStep(-550.,100.,Front)
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