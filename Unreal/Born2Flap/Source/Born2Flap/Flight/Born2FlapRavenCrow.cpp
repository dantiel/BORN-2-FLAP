#include "Flight/Born2FlapWingMesh.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
// Every face owns its vertices: hard normals, folded paper, no smoothed ellipsoids.
struct FShardMesh
{
    // Texture projection modes matching Tools/create_falcon_texture_templates.py.
    enum class EMode { Body, Tail };
    EMode Mode = EMode::Body;
    const TCHAR* MaterialPath = TEXT("/Game/Birds/M_RavenShard");
    TArray<FVector> V, N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    // Planar UV so a painted side-view (body) or top-view (tail) texture wraps the shards.
    FVector2D Map(FVector P) const
    {
        if (Mode == EMode::Tail)
            return FVector2D((P.X+51.f)/51.f, (P.Y+22.f)/44.f); // tail: root->tip, left->right (re-based at root)
        return FVector2D((P.X+63.f)/133.f, (P.Z+12.f)/33.f);      // body: aft->nose, belly->back
    }
    void Tri(FVector A, FVector B, FVector D, FLinearColor Colour)
    {
        FVector Normal = FVector::CrossProduct(B-A,D-A).GetSafeNormal();
        if (Normal.Z<0) { Swap(B,D); Normal=-Normal; }
        for (bool Back : {false,true})
        {
            int32 Base = V.Num();
            V.Append({A, Back ? D : B, Back ? B : D});
            // Unreal's front-face winding is opposite the cross-product normal.
            T.Append({Base, Base+2, Base+1});
            for (int I=0; I<3; ++I) { N.Add(Back ? -Normal : Normal); C.Add(Back ? Colour*.65f : Colour); UV.Add(FVector2D::ZeroVector); }
        }
    }
    void Fold(FVector A, FVector B, FVector D, FVector E, float Height, FLinearColor Colour)
    {
        FVector Ridge = (A+B+D+E)*.25 + FVector(0,0,Height);
        Tri(A,B,Ridge,Colour); Tri(B,D,Ridge,Colour*.68f);
        Tri(D,E,Ridge,Colour*1.25f); Tri(E,A,Ridge,Colour*.85f);
    }
    void Install(AActor* Owner, USceneComponent* Parent, FName Name)
    {
        for (int32 I=0; I<V.Num(); ++I) UV[I] = Map(V[I]);
        auto* Mesh = NewObject<UProceduralMeshComponent>(Owner,Name);
        Owner->AddInstanceComponent(Mesh);
        Mesh->SetupAttachment(Parent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(true);
        Mesh->RegisterComponent();
        Mesh->CreateMeshSection_LinearColor(0,V,T,N,UV,C,{},false);
        if (auto* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
            Mesh->SetMaterial(0,Material);
        else Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Training/M_Ink")));
    }
};
const FLinearColor Ink(.018,.025,.035), Slate(.05,.067,.088), Edge(.10,.13,.16);

// ---------------------------------------------------------------------------
// Neon Turtle (Design 3) — the Aether Nursery's baby sea-turtle.
// A domed oval carapace over a small turquoise body, two broad front flippers
// (the flapping pair, driven by the shoulders) and two hind fins on the tail
// pivot. The shell and fins carry an emissive livery that blends Mesoamerican
// (Maya) stepped-fret / greca borders with Polynesian (Māori) kōwhaiwhai koru
// spirals and tāniko chevrons — painted as vertex colours the neon material
// lifts into glow.
// ---------------------------------------------------------------------------
const FLinearColor NeonShell(.04,.50,.55), NeonBody(.10,.64,.68);
const FLinearColor NeonTeal(.00,.95,.85), NeonMagenta(.96,.16,.62), NeonGold(.99,.72,.16), NeonDeep(.02,.10,.16);
constexpr float TurtleTailRootX = -30.f;   // hind fins hinge here (the tail pivot)

// Maya stepped-fret (greca): a quantised meander band. U wraps around the
// shell (0..1), V runs apex(0) -> rim(1). Returns 1.0 where the fret is on.
double TurtleGreca(double U, double V)
{
    const double Cols = 12.0, Rows = 4.0;
    return FMath::Fmod(FMath::FloorToDouble(V * Rows) + FMath::Fmod(FMath::FloorToDouble(U * Cols), 2.0), 2.0);
}

// Carapace livery: soft turquoise base with a gold stepped-fret rim band,
// radial magenta tāniko chevrons and a ring of teal koru accents.
FLinearColor TurtleShellPaint(double U, double V)
{
    FLinearColor C = FMath::Lerp(NeonShell, NeonShell * 1.3f, FMath::Clamp(1.0 - V * 2.4, 0.0, 1.0));
    if (V > 0.84 && V < 0.97)
        C = FMath::Lerp(C, NeonGold, TurtleGreca(U, (V - 0.84) / 0.13));
    if (V >= 0.97) C = NeonDeep;
    const double ang = FMath::Frac(U * 10.0);
    const double chev = FMath::Abs(2.0 * ang - 1.0);
    const double spoke = FMath::Exp(-9.0 * FMath::Square(V - 0.5)) * FMath::Clamp(1.0 - chev * 3.5, 0.0, 1.0);
    C = FMath::Lerp(C, NeonMagenta, spoke);
    const double koru = FMath::Exp(-70.0 * FMath::Square(V - 0.70)) * FMath::Clamp(1.0 - FMath::Abs(FMath::Frac(U * 6.0) - 0.5) * 5.0, 0.0, 1.0);
    C = FMath::Lerp(C, NeonTeal, koru);
    return C;
}

// Head markings: turquoise skin with teal tā-moko cheek chevrons and a deep
// nose stripe (a sensitive, curious face).
FLinearColor TurtleHeadPaint(double U, double V)
{
    FLinearColor C = NeonBody;
    if (V < 0.12) C = NeonDeep;
    const double cheek = FMath::Exp(-14.0 * FMath::Square(V - 0.55));
    C = FMath::Lerp(C, NeonTeal, cheek * 0.85);
    return C;
}

// Painted ellipsoid (smooth lat/long grid). Paint maps (U,V) in 0..1 to a
// vertex colour; the neon material lifts the bright bands into emissive glow.
void BuildTurtleEllipsoid(AActor* Owner, USceneComponent* Parent, FName Name,
                          const FVector& Centre, const FVector& Radii,
                          const TCHAR* MaterialPath,
                          const TFunction<FLinearColor(double,double)>& Paint,
                          double AftTaper = 0.0, double ForeTaper = 0.0)
{
    const int32 Longs = 44, Lats = 16;
    TArray<FVector> V, N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    for (int32 J = 0; J <= Lats; ++J)
        for (int32 I = 0; I <= Longs; ++I)
        {
            const double Phi = PI * double(J) / Lats;
            const double Theta = 2.0 * PI * double(I) / Longs;
            const double S = FMath::Sin(Phi), Co = FMath::Cos(Phi);
            const FVector Dir(FMath::Cos(Theta) * S, FMath::Sin(Theta) * S, Co);
            // Teardrop planform: when AftTaper > 0 the shell narrows aft (blunt nose,
            // pointed tail) while the dome height is preserved — an aerodynamic carapace.
            // ForeTaper mirrors that for the head: the snout (nose, +X) tapers to a
            // blunt point while the cranium stays full — a turtle's beak, not a sphere.
            double PlanTaper = 1.0;
            if (AftTaper > 0.0)
                PlanTaper = FMath::Pow(FMath::Clamp((Dir.X + 1.0) * 0.5, 0.0, 1.0), AftTaper);
            if (ForeTaper > 0.0)
                PlanTaper *= FMath::Pow(FMath::Clamp((1.0 - Dir.X) * 0.5, 0.0, 1.0), ForeTaper);
            V.Add(Centre + FVector(Radii.X * Dir.X, Radii.Y * Dir.Y * PlanTaper, Radii.Z * Dir.Z));
            N.Add(FVector(Dir.X / (Radii.X * Radii.X), Dir.Y / (Radii.Y * Radii.Y), Dir.Z / (Radii.Z * Radii.Z)).GetSafeNormal());
            const double U = double(I) / Longs;
            const double Vv = double(J) / Lats;          // geometric: 0 = apex (top), 1 = rim (bottom)
            C.Add(Paint(U, Vv));                          // vertex livery (now superseded by the texture)
            UV.Add(FVector2D(U, 1.0 - Vv));               // flip texture V (Unreal samples image bottom at V=0)
        }
    for (int32 J = 0; J < Lats; ++J)
        for (int32 I = 0; I < Longs; ++I)
        {
            const int32 A = J * (Longs + 1) + I, B = A + 1, D = A + Longs + 1, E = D + 1;
            T.Append({A, B, D}); T.Append({B, E, D});
        }
    auto* Mesh = NewObject<UProceduralMeshComponent>(Owner, Name);
    Owner->AddInstanceComponent(Mesh);
    Mesh->SetupAttachment(Parent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(true);
    Mesh->RegisterComponent();
    Mesh->CreateMeshSection_LinearColor(0, V, T, N, UV, C, {}, false);
    if (auto* M = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
        Mesh->SetMaterial(0, M);
    else
        Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Birds/M_RavenShard")));
}

// Shell + plastron.
void BuildTurtleShell(AActor* Owner, USceneComponent* Parent)
{
    // Flat, wide, aft-tapered aerodynamic carapace (a gliding pancake-disc, not a
    // balloon): the dome height collapses from a tall oval to a low, elongated disc
    // that still tapers to a point aft via AftTaper.
    BuildTurtleEllipsoid(Owner, Parent, TEXT("TurtleShell_3"), FVector(0, 0, 1), FVector(46, 32, 9),
                          TEXT("/Game/NeonAbyss/M_TurtleShell"), &TurtleShellPaint, 0.55);
    FShardMesh Plastron;
    Plastron.MaterialPath = TEXT("/Game/NeonAbyss/M_TurtleShell");
    Plastron.Fold({18, 0, -4}, {8, 24, -7}, {-32, 0, -5}, {8, -24, -7}, -5, NeonBody);
    Plastron.Install(Owner, Parent, TEXT("TurtlePlastron_3"));
}

// Head (the eyes are painted onto the head texture, so no separate eye meshes).
void BuildTurtleHead(AActor* Owner, USceneComponent* Parent)
{
    // A turtle's head, not a balloon: rounded cranium that tapers forward into a
    // blunt beak/snout (ForeTaper) and slightly inward at the neck (AftTaper),
    // with a lower, flatter profile to read aerodynamic against the flat carapace.
    BuildTurtleEllipsoid(Owner, Parent, TEXT("TurtleHead_3"), FVector(44, 0, 5), FVector(14, 11, 9),
                          TEXT("/Game/NeonAbyss/M_TurtleHead"), &TurtleHeadPaint,
                          0.4,  // neck tapers slightly aft
                          0.9); // snout tapers forward into the beak
}

// The hind-fin pair (rudder/elevator), folded shards on the tail pivot.
void BuildTurtleHindFins(AActor* Owner, USceneComponent* Parent)
{
    for (int Side : {-1, 1})
    {
        FShardMesh Fin;
        Fin.MaterialPath = TEXT("/Game/NeonAbyss/M_TurtleShell");
        // Trailing rear flippers: stretched aft and wider so they trail clear of the
        // flattened body, reading as the turtle's hind paddles in the aether.
        Fin.Fold({0, Side * 8.0, 0}, {0, Side * 27.0, 2}, {-30, Side * 22.0, -2}, {-30, Side * 6.0, -4}, 4, NeonBody);
        Fin.Install(Owner, Parent, *FString::Printf(TEXT("TurtleHind%d_3"), Side));
    }
}

// Kestrel (Design 2) body layout, in centimetres (game space, +X forward = nose).
//
// The authored fuselage mesh SM_KestrelFuselage is pre-scaled to cm with its
// pivot at the nose: X = 0 (nose) -> +100 (tail tip), Y = +/-7.67 (half width),
// Z dorsal up. It is yawed 180 so +X points aft, then scaled to the target
// silhouette. The wing membrane (Born2FlapWingMesh, Design 2) hangs from the
// shoulders at RavenRoot X=+3 and its trailing-edge root reaches X=-35.3, so the
// fuselage must extend aft of that to overlap the tail fan.
constexpr float KestrelNoseX = 28.5f;                // nose position (+forward); shifted aft 4 cm
constexpr float KestrelFuselageLengthCm = 82.f;      // nose -> aft end of the body (elongated, slim)
constexpr float KestrelFuselageWidthCm = 15.f;       // full side-to-side width (de-bulked)
constexpr float KestrelFuselageHeightScale = 0.62f;  // authored height (~16.7 cm, slim)
constexpr float KestrelFuselageXScale = KestrelFuselageLengthCm / 100.f;
constexpr float KestrelFuselageYScale = KestrelFuselageWidthCm / 15.34f;

// Tail fan: the root tucks under the elongated body taper (body aft end ~-53.5)
// so the body extends a bit further over the fan; the tip sits at the overall
// falcon length. Authored tail points run X -61 (root) to -112.0293 (tip) with
// Y +/-22; UVs stay pinned to the T_KestrelTail feather raster. The mapping is
// expressed through the authored span (negative delta) so the fan always sweeps
// AFT — root nearer the nose, tip further aft. Tuning pass: fan moved forward
// (root -42 -> -38), 10% slimmer in width (Y 1.35 -> 1.215), 7% smaller overall
// (span 42.5 -> 39.5 cm); then 5% narrower again (Y 1.215 -> 1.154).
constexpr float KestrelTailAuthoredRootX = -61.f;
constexpr float KestrelTailAuthoredTipX = -112.0293f;
constexpr float KestrelTailRootX = -38.f;
// Pinned to a literal (not derived from KestrelNoseX) so shifting the body aft
// does not drag the tail with it: the wing-to-tail distance stays fixed while
// the fuselage overlaps the fan more.
constexpr float KestrelTailTipX = -77.5f;
constexpr float KestrelTailYScale = 1.15425f;   // 5% narrower than 1.215
constexpr float RavenTailRootX = -61.f;         // raven/crow tail base (where the fan meets the body)
FVector KestrelTailPosition(FVector P)
{
    const float T = (P.X - KestrelTailAuthoredRootX) / (KestrelTailAuthoredTipX - KestrelTailAuthoredRootX);
    return FVector(FMath::Lerp(KestrelTailRootX, KestrelTailTipX, T),
                   P.Y * KestrelTailYScale, 0.f);
}

// Kestrel fanned tail membrane (kestreltail.svg "tailmembrane"), authored top-view
// mapped into the game body: X = nose->tail (root -61 -> aft -112), Y = left/right,
// Z = 0 (flat). UVs land on the T_KestrelTail feather raster placement; the mesh is
// rendered from both sides by its material, without duplicate coplanar triangles.
const FVector KestrelTailPts[8] = {
    FVector(-61.0013, 5.1481, 0), FVector(-59.7605, -0.1170, 0),
    FVector(-61.0013, -5.9671, 0), FVector(-107.8416, -21.9963, 0),
    FVector(-110.0130, -17.5502, 0), FVector(-112.0293, 0.0, 0),
    FVector(-109.7028, 16.8482, 0), FVector(-108.4620, 21.9963, 0),
};
const FVector2D KestrelTailUV[8] = {
    FVector2D(0.61677, 0.02647), FVector2D(0.49669, 0.00335),
    FVector2D(0.36327, 0.02647), FVector2D(0.0, 0.89923),
    FVector2D(0.09909, 0.93969), FVector2D(0.49936, 0.97725),
    FVector2D(0.88361, 0.93391), FVector2D(1.0, 0.91079),
};

void BuildKestrelTail(AActor* Owner, USceneComponent* Parent)
{
    TArray<FVector> V, N;
    TArray<FVector2D> UV;
    TArray<int32> T;
    TArray<FLinearColor> C;
    for (int32 I = 0; I < 8; ++I)
    {
        FVector Pt = KestrelTailPosition(KestrelTailPts[I]);
        Pt.X -= KestrelTailRootX;   // hinge at the base (pivot sits at the root)
        V.Add(Pt); UV.Add(KestrelTailUV[I]);
        N.Add(FVector(0, 0, 1)); C.Add(FLinearColor::White);
    }
    for (int32 I = 1; I < 7; ++I) T.Append({0, I, I + 1});  // front fan from root centre
    // The material is two-sided. A coincident reversed fan double-blends the
    // translucent paint and creates unstable overlap at grazing angles.

    auto* Mesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("KestrelTail_2"));
    Owner->AddInstanceComponent(Mesh);
    Mesh->SetupAttachment(Parent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(true);
    Mesh->RegisterComponent();
    Mesh->CreateMeshSection_LinearColor(0, V, T, N, UV, C, {}, false);
    if (auto* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Birds/M_KestrelTail")))
        Mesh->SetMaterial(0, Mat);
}
}

USceneComponent* Born2FlapRaven::Build(AActor* Owner, USceneComponent* Parent,
                                        TObjectPtr<USceneComponent>& OutLeftShoulder,
                                        TObjectPtr<USceneComponent>& OutRightShoulder,
                                        TObjectPtr<USceneComponent>& OutTailPivot, int32 Design)
{
    if (!Owner || !Parent)
        return nullptr;
    OutLeftShoulder = OutRightShoulder = OutTailPivot = nullptr;
    auto Node = [&](FName Name,USceneComponent* P,FVector Position)
    {
        auto* C = NewObject<USceneComponent>(Owner,*FString::Printf(TEXT("%s_%d"),*Name.ToString(),Design));
        Owner->AddInstanceComponent(C); C->SetupAttachment(P); C->SetRelativeLocation(Position); C->RegisterComponent();
        return C;
    };
    auto* RavenRoot = Node(TEXT("RavenCrow"),Parent,FVector::ZeroVector);
    // Tail fan pivot: the tail elevator trim pitches this node so the angle is
    // visible in the rendered bird. The pivot sits exactly at the tail's base
    // (where the fan meets the body) so it hinges there instead of swinging the
    // whole fan around the body origin and floating above/below the bird.
    const float TailRootX = (Design == 2) ? KestrelTailRootX : (Design == 3) ? TurtleTailRootX : RavenTailRootX;
    auto* TailPivot = Node(TEXT("TailPivot"), RavenRoot, FVector(TailRootX, 0.f, 0.f));
    OutTailPivot = TailPivot;
    if (Design == 3)
    {
        // --- Neon Turtle (the Aether Nursery's baby sea-turtle) --------------
        BuildTurtleShell(Owner, RavenRoot);
        BuildTurtleHead(Owner, RavenRoot);
        BuildTurtleHindFins(Owner, TailPivot);
        for (int Side : {-1, 1})
        {
            auto* Shoulder = Node(*FString::Printf(TEXT("TurtleShoulder%d"), Side), RavenRoot, FVector(6, Side * 29.0, 4));
            if (Side < 0) OutLeftShoulder = Shoulder; else OutRightShoulder = Shoulder;
            auto* Flipper = NewObject<UBorn2FlapWingMesh>(Owner, *FString::Printf(TEXT("TurtleFlipper%d_3"), Side));
            Owner->AddInstanceComponent(Flipper);
            Flipper->SetupAttachment(Shoulder);
            Flipper->RegisterComponent();
            Flipper->InitializeWing(Side, 3);
        }
        return RavenRoot;
    }
    if (Design == 2)
    {
        // Kestrel (falcon): the authored fuselage mesh + textured tail membrane.
        // The mesh is yawed 180 (nose forward) and scaled by the layout constants
        // above; the fuselage reaches aft past the wing TE to overlap the tail fan.
        auto* Fuselage = NewObject<UStaticMeshComponent>(Owner, TEXT("KestrelFuselage_2"));
        Owner->AddInstanceComponent(Fuselage);
        Fuselage->SetupAttachment(RavenRoot);
        Fuselage->SetRelativeLocation(FVector(KestrelNoseX, 0.f, 0.f));
        Fuselage->SetRelativeRotation(FRotator(0, 180, 0));
        Fuselage->SetRelativeScale3D(FVector(KestrelFuselageXScale, KestrelFuselageYScale, KestrelFuselageHeightScale));
        Fuselage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Fuselage->SetCastShadow(true);
        if (auto* FusMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Birds/SM_KestrelFuselage")))
            Fuselage->SetStaticMesh(FusMesh);
        if (auto* FusMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Birds/M_KestrelFuselage")))
            Fuselage->SetMaterial(0, FusMat);
        Fuselage->RegisterComponent();

        BuildKestrelTail(Owner, TailPivot);

        // Shoulder distance is 1 cm per side (matching the physics wsShoulderM):
        // the wing root sits 1 cm outboard of the body centreline, so the two
        // roots are 2 cm apart instead of spreading a body-width apart.
        for (int Side : {-1, 1})
        {
            auto* Shoulder = Node(*FString::Printf(TEXT("RavenShoulder%d"), Side), RavenRoot, FVector(3, Side * 1.0, 4));
            if (Side < 0) OutLeftShoulder = Shoulder; else OutRightShoulder = Shoulder;
            auto* Wing = NewObject<UBorn2FlapWingMesh>(Owner, *FString::Printf(TEXT("RavenWing%d_%d"), Side, Design));
            Owner->AddInstanceComponent(Wing);
            Wing->SetupAttachment(Shoulder);
            Wing->RegisterComponent();
            Wing->InitializeWing(Side, Design);
        }
        return RavenRoot;
    }

    FShardMesh BodyMesh;
    BodyMesh.MaterialPath = TEXT("/Game/Birds/M_RavenShard");
    // A narrow architectural shoulder and keel, angular crow brow and spear beak.
    BodyMesh.Fold({55,0,7},{12,14,1},{-63,0,-3},{12,-14,1},8,Ink);
    BodyMesh.Fold({40,0,5},{8,-10,-2},{-50,0,-12},{8,10,-2},-8,Slate*.55f);
    BodyMesh.Fold({54,0,15},{34,11,13},{17,0,18},{34,-11,13},6,Slate);
    BodyMesh.Tri({70,0,9},{47,7,12},{49,0,17},Edge);
    BodyMesh.Tri({70,0,9},{49,0,17},{47,-7,12},Slate);
    BodyMesh.Tri({70,0,9},{47,-7,12},{47,7,12},Ink);
    // Layered scapular shards run aft along the spine.
    for (int Side : {-1,1})
    {
        for (int I=0; I<5; ++I)
        {
            const float X=16-I*10, Y=Side*(5+I*.65f);
            BodyMesh.Fold({X+12,Y*.65,13-I*2.f},{X+1,Y+Side*4,7.-I},
                          {X-29,Y*.7,2.-I},{X-2,Y*.25,10.-I},2, I%2 ? Slate : Ink);
        }
        // Small inset amber eye is a triangle, not a luminous sphere.
        BodyMesh.Tri({43,Side*8.8,16},{37,Side*10.4,17},{40,Side*10.,14},FLinearColor(.34,.19,.055));
        FShardMesh Tail;
        Tail.Mode = FShardMesh::EMode::Tail;
        Tail.MaterialPath = TEXT("/Game/Birds/M_RavenShard");
        // Fanned delta tail: shallow anhedral, spread to a wide trailing edge,
        // so the tail reads as a fan rather than a single backward spike.
        auto TP = [Side](double X,double Y) { return FVector(X - RavenTailRootX,Side*Y,1-Y*FMath::Tan(FMath::DegreesToRadians(12.))); };
        Tail.Tri(TP(-61,0),TP(-112,22),TP(-112,0),Slate);
        Tail.Tri(TP(-64,0),TP(-107,17),TP(-107,0),Ink);
        Tail.Install(Owner,TailPivot,*FString::Printf(TEXT("RavenTail%d_%d"),Side,Design));
        auto* Shoulder=Node(*FString::Printf(TEXT("RavenShoulder%d"),Side),RavenRoot,FVector(3,Side*11,10));
        if (Side<0) OutLeftShoulder=Shoulder; else OutRightShoulder=Shoulder;
        auto* Wing=NewObject<UBorn2FlapWingMesh>(Owner,*FString::Printf(TEXT("RavenWing%d_%d"),Side,Design));
        Owner->AddInstanceComponent(Wing);
        Wing->SetupAttachment(Shoulder);
        Wing->RegisterComponent();
        Wing->InitializeWing(Side,Design);
    }
    BodyMesh.Install(Owner,RavenRoot,*FString::Printf(TEXT("RavenBody%d"),Design));
    return RavenRoot;
}

void ABorn2FlapFlightPawn::BuildRavenCrow()
{
    RavenRoot = Born2FlapRaven::Build(this, VisualRoot, RavenLeftShoulder, RavenRightShoulder, RavenTailPivot);
    MembraneRoot = Born2FlapRaven::Build(this, VisualRoot, MembraneLeftShoulder, MembraneRightShoulder, MembraneTailPivot,2);
    TurtleRoot = Born2FlapRaven::Build(this, VisualRoot, TurtleLeftShoulder, TurtleRightShoulder, TurtleTailPivot, 3);
}

void ABorn2FlapFlightPawn::SelectBirdModel(int32 Index)
{
    BirdModel = FMath::Clamp(Index, 0, 4);
    if (PrototypeRoot) PrototypeRoot->SetVisibility(!bBlind && BirdModel == 1, true);
    if (RavenRoot) RavenRoot->SetVisibility(!bBlind && BirdModel == 0, true);
    if (MembraneRoot) MembraneRoot->SetVisibility(!bBlind && (BirdModel == 2 || BirdModel == 4), true);
    if (TurtleRoot) TurtleRoot->SetVisibility(!bBlind && BirdModel == 3, true);
    // Each silhouette is a distinct bird: its own dry mass (fed to Chaos) and
    // its own wing planform + carbon-rod inventory (fed to the Haskell solver
    // via SetWingModel). Per-model mass lives in the Haskell model config
    // (kestrel 50 g reference, raven ~86 g, prototype ~36 g) — queried here, not
    // hardcoded — so a model change never leaves a stale host-side constant.
    // Mass must be set BEFORE ApplyBodyMass so the inertia tensor reads the
    // correct model.
    BodyMassKg = 0.050f;  // fallback: kestrel reference
    if (MathBridge && MathBridge->IsReady())
    {
        const double ModelMass = MathBridge->GetModelMassKg((uint32_t)BirdModel);
        if (ModelMass > 0.0)
            BodyMassKg = (float)ModelMass;
        MathBridge->SetWingModel((uint32_t)BirdModel);
    }
    // The CG travel is length-dependent, so switching silhouettes must re-clamp
    // the centre of mass and re-apply the body mass (idempotent).
    ApplyBodyMass();
}