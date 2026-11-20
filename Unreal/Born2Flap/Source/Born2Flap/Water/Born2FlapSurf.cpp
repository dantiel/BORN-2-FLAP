#include "Water/Born2FlapSurf.h"
#include "Water/Born2FlapWaterDirector.h"
#include "Game/Born2FlapGameMode.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace
{
    // Aperiodic foam turbulence: two octaves of product-sines over world
    // coordinates, animated in time. Product-sines vary in BOTH the shore-
    // parallel (X) and shore-perpendicular (Y) directions, so there is no single
    // coherent ridge — this breaks the smooth sculpted "ribs" and lets the surf
    // read as churning white water instead of a static mesh. Returns [-1,1].
    inline double FoamTurb(double X, double Y, double T, double Seed)
    {
        // Low-frequency domain warp: the X terms are tuned to wavelengths of
        // several hundred cm so they roll smoothly across the 100 cm columns
        // instead of flipping sign every column (the old ~134 cm / ~300 cm terms
        // painted hard shore-orthogonal "ribs" onto the crest). The Y terms stay
        // shorter so the wash still churns cross-shore.
        return 0.65*FMath::Sin(X*0.008+Seed*1.3+T*0.6)*FMath::Sin(Y*0.018+Seed*2.7-T*0.5)
             + 0.35*FMath::Sin(X*0.016+Seed*3.1-T*0.9)*FMath::Sin(Y*0.031+Seed*4.2+T*0.7);
    }
}

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
    const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    if(!Camera) return;
    const FVector Cam=Camera->GetCameraLocation();
    const double CX=FMath::Clamp(FMath::GridSnap(Cam.X,200.),-34000.,34000.);
    // Adaptive rebuild rate: full rate near the shore strip, throttled when the
    // bird is far (over the hinterland/campus or far offshore), where the foam
    // is a distant strip and its per-frame motion is invisible. Keeps the foam
    // visible (no pop) while dropping ~95% of the far-distance rebuild cost.
    const double DistToShore=FMath::Abs(Cam.Y-Born2FlapSurf::Shore(CX));
    const float RebuildHz=DistToShore<9000. ? 20.f : 1.f;
    Accumulator+=Dt;
    if(Accumulator<1.f/RebuildHz) return;
    Accumulator=0;
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
    // Along-shore 100 cm columns erase the coarse vertical facets that read as
    // hard "ribs" on the foam crest (kept at 220 for that reason). Cross-shore
    // rows are trimmed to 20 — the wash/curl profiles are smooth, so the lower
    // row count saves ~29% of the vertex upload + normal recompute with no
    // visible faceting.
    constexpr int NX=220, NY=20;
    for(int Wave=0;Wave<3;++Wave)
    for(int Kind=0;Kind<2;++Kind)
    for(int I=0;I<=NX;++I)
    {
        const double X=CX-11000+I*100;
        const double Front=Born2FlapSurf::Front(X,Time,Wave);
        const double Shore=Born2FlapSurf::Shore(X);
        const double Growth=(1-FMath::SmoothStep(3400.,6100.,Front));
        const double Collapse=FMath::SmoothStep(-250.,950.,Front);
        // Aperiodic crest field: incommensurate product-sines (no regular
        // along-shore hump spacing) so the surf reads as churning white water
        // instead of repeating vertical "ribs"; the field drifts down-shore.
        const double CrestNoise=
            0.55*FMath::Sin(X*.0063+Wave*2.4+Time*.4)*FMath::Sin(X*.0117-Wave*1.7+Time*.6)
          + 0.30*FMath::Sin(X*.0171+Wave*.9-Time*.5)*FMath::Sin(X*.0297+Wave*3.3+Time*.8)
          + 0.15*FMath::Sin(X*.039+Wave*4.1+Time*1.1)*FMath::Sin(X*.071-Wave*2.2-Time*1.3);
        const double H=(100+34*CrestNoise
            +6*FoamTurb(X,Shore+Front,Time,Wave*3.7))*Growth*Collapse*Strength;
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
                const double Boil=FoamTurb(X,Shore+Front,Time,Wave*3.7+5.1);
                Foam=FMath::SmoothStep(.35,.72,V)*(1-FMath::SmoothStep(1800.,3600.,Front));
                // Boiling brightness: the R channel drives the material's
                // white emissive level, so the curl shimmers as it churns.
                Foam*=0.8+0.2*Boil;
                // Opaque at the foam lip, fading into the water at the base.
                // The 0.30 floor keeps the curl sheet always visible so the
                // surf can never read as a black gap in the water.
                Alpha=EdgeFade*Growth*Collapse*(0.30+0.70*FMath::SmoothStep(0.,.12,V))*.94;
                Alpha*=0.72+0.28*Boil;
            }
            else
            {
                // White water spreads behind the falling lip, then drains back
                // as rounded, rippling lobes — not a flat rectangle.
                Y=Front-160+V*1500;
                const double Boil=FoamTurb(X,Shore+Y,Time,Wave*3.7);
                const double Ground=GM ? GM->GroundHeight(X,Shore+Y) : -60.;
                Z=FMath::Max(-43.,Ground+3)+12*Boil;
                Foam=0.8+0.2*Boil;
                const double Lobe=.62+.38*FMath::Sin(X*.0079+Wave*2.1+Time*.9)
                    *FMath::Sin(X*.0137-Wave*1.3-Time*.7);
                // Smooth cross-shore wash profile: a soft crest at mid-wash that
                // eases to ZERO at both the lip (V=0) and the drained far edge
                // (V=1) with zero slope, so the white water blends into the dry
                // sand instead of ending in a hard line. The old Pow(Sin(pi*V),
                // .55) cusped to 0 with infinite slope at V=1 (and risked NaN via
                // a negative base at V=1) — that was the hard disappearance edge.
                const double WashLobe=FMath::SmoothStep(0.,.5,V)*(1.-FMath::SmoothStep(.5,1.,V));
                Alpha=EdgeFade*Lobe*(1-FMath::SmoothStep(1100.,3200.,Front))*FMath::SmoothStep(-550.,100.,Front)
                    *WashLobe*.9;
                Alpha*=0.75+0.25*Boil;
            }
            // Follow the same displaced liquid surface as the ocean shader.
            // Single Layer Water writes depth, so foam must actually lie above
            // that surface rather than relying on translucent draw ordering.
            const double ShoreBlend=FMath::SmoothStep(100.,3000.,Y);
            const double LiquidZ=Director ? -50+(Director->GetWaterHeightAt(X,Shore+Y)+50)*ShoreBlend : -50;
            if(Kind==0) Z+=LiquidZ+50+2;
            else Z=FMath::Max(Z,LiquidZ+4);
            Vertices.Add(FVector(X,Shore+Y,Z));
            Normals.Add(FVector::ZeroVector);
            UV.Add(FVector2D(X*.003,V));
            // R = Alpha: the material's VertexColor pin is float3 (alpha is not
            // exposed), so the tuned alpha field (edge fade, growth, collapse,
            // boil) rides in R and drives BOTH opacity and the subtle brightness
            // churn. The material reads nothing else.
            Colors.Add(FLinearColor(Alpha,Kind==1 ? 1.f : 0.f,0,Alpha));
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
    // Smooth normals from the curled-crest geometry so the foam shades like a
    // breaking wave (top bright, under-curl slightly darker) instead of a flat
    // uniformly-lit sheet. Faces are averaged per-vertex (no hard facets), and
    // the Z is biased upward so overhang faces never flip to read as top-lit —
    // this avoids the old shoreline-orthogonal "ribs" and dark facets under low
    // sun while still giving the 3D curl water-like form.
    for(FVector& N : Normals) N = FVector::ZeroVector;
    for(int t=0;t+2<Triangles.Num();t+=3)
    {
        const FVector A=Vertices[Triangles[t]], B=Vertices[Triangles[t+1]], C=Vertices[Triangles[t+2]];
        const FVector FN=FVector::CrossProduct(B-A, C-A);
        if(FN.IsNearlyZero()) continue;
        Normals[Triangles[t]]+=FN;
        Normals[Triangles[t+1]]+=FN;
        Normals[Triangles[t+2]]+=FN;
    }
    for(FVector& N : Normals)
    {
        if(N.IsNearlyZero()) { N=FVector::UpVector; continue; }
        N.Normalize();
        // Gentle upward lift instead of a hard clamp: the old max(N.Z,0.35) froze
        // every steep face at the same flat normal, which banded the curl into
        // vertical light/dark stripes. Blending toward up keeps the under-curl
        // slightly darker (readable form) but with a smooth continuous gradient.
        N = (N + FVector(0.0, 0.0, 0.45)).GetSafeNormal();
    }
    if(!bCreated) Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colors,{},false);
    else Mesh->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,{});
    bCreated=true;
    // TEMP diagnostic: surface geometry + material state, throttled to 3 s.
    static int32 SurfDbg=0;
    if(++SurfDbg%90==0)
    {
        double Peak=-1e9, Low=1e9, MinX=1e9, MaxX=-1e9, MinY=1e9, MaxY=-1e9, MaxR=-1.;
        int NanV=0, NanC=0, NanKind0=0, NanKind1=0, NanWave[3]={0,0,0};
        const int Stride=(NX+1)*(NY+1);
        for(int i=0;i<Vertices.Num();++i)
        {
            const FVector& V=Vertices[i];
            if(!FMath::IsFinite(V.X)||!FMath::IsFinite(V.Y)||!FMath::IsFinite(V.Z)){ ++NanV; }
            if(V.X<MinX)MinX=V.X; if(V.X>MaxX)MaxX=V.X;
            if(V.Y<MinY)MinY=V.Y; if(V.Y>MaxY)MaxY=V.Y;
            if(FMath::IsFinite(V.Z)){ if(V.Z>Peak)Peak=V.Z; if(V.Z<Low)Low=V.Z; }
            const FLinearColor& C=Colors[i];
            if(FMath::IsFinite(C.R)){ if(C.R>MaxR)MaxR=C.R; }
            else { ++NanC; const int K=(i/Stride)%2; const int W=(i/(2*Stride))%3;
                if(K==0)++NanKind0; else ++NanKind1; ++NanWave[W]; }
        }
        const FRotator CR=Camera->GetCameraRotation();
        UE_LOG(LogTemp,Warning,TEXT("SURFDBG created=%d verts=%d peakZ=%.1f lowZ=%.1f cam=(%.0f,%.0f,%.1f) yaw=%.0f pitch=%.0f box=(%.0f,%.0f)-(%.0f,%.0f) nanV=%d nanC=%d maxR=%.2f registered=%d proxy=%d hidden=%d secs=%d boundsC=(%.0f,%.0f,%.0f) boundsR=%.0f actor=(%.0f,%.0f,%.0f) mat=%s"),
            bCreated?1:0, Vertices.Num(), Peak, Low, Cam.X, Cam.Y, Cam.Z, CR.Yaw, CR.Pitch,
            MinX, MinY, MaxX, MaxY,
            NanV, NanC, MaxR,
            Mesh->IsRegistered()?1:0, Mesh->GetSceneProxy()!=nullptr, Mesh->bHiddenInGame?1:0,
            Mesh->GetNumSections(),
            Mesh->Bounds.Origin.X, Mesh->Bounds.Origin.Y, Mesh->Bounds.Origin.Z, Mesh->Bounds.SphereRadius,
            GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
            Mesh->GetMaterial(0) ? *Mesh->GetMaterial(0)->GetPathName() : TEXT("null"));
    }
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