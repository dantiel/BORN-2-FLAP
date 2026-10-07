// Born2FlapSplash.cpp — the view-framework splash implementation.

#include "UI/Born2FlapSplash.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"

using namespace born2flap::ui;

namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }

FProps P(std::initializer_list<std::pair<const char*, FValue>> Items)
{
    FProps M;
    for (const auto& KV : Items) M[KV.first] = KV.second;
    return M;
}

FNode Nd(const char* Type, FProps Props = {}, std::vector<FNode> Children = {})
{
    FNode N_;
    N_.type = Type;
    N_.props = std::move(Props);
    N_.children = std::move(Children);
    return N_;
}

void SendUpdate(UBorn2FlapUIRenderer* R, const TArray<int32>& Path, FProps Props)
{
    if (!R)
        return;
    TArray<FOp> Ops;
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = FPath(Path.GetData(), Path.GetData() + Path.Num());
    O.props = std::move(Props);
    Ops.Add(O);
    R->ApplyOps(Ops);
}

}  // namespace

ABorn2FlapSplash::ABorn2FlapSplash()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABorn2FlapSplash::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 100;
    Build();
}

void ABorn2FlapSplash::Build()
{
    if (!Renderer)
        return;

    // Small centred logo splash — the startup splash is a logo, NOT a
    // full-bleed image. The loading bar lives on the full-bleed level-loading
    // screen (FBorn2FlapModule). Tree:
    //   []         Overlay (root, fills viewport)
    //   [0]        Overlay (align center, valign center)
    //   [0,0]      SizeBox (fixed logo size)
    //   [0,0,0]    Image (logo artwork)
    FNode Root = Nd("Overlay", P({}),
    {
        Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
        {
            Nd("SizeBox", P({ {"width", N(560)}, {"height", N(315)} }),
            {
                Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-splash-new.born2flap-splash-new")} }))
            })
        })
    });

    TArray<FOp> Ops;
    FOp Op;
    Op.op = EOp::CreateInstance;
    Op.path = FPath();
    Op.node = std::move(Root);
    Ops.Add(Op);
    Renderer->ApplyOps(Ops);
}

void ABorn2FlapSplash::SetProgress(float Fraction)
{
    // The startup splash is a plain centred logo — the loading bar now lives on
    // the full-bleed level-loading screen (FBorn2FlapModule), so there is no
    // bar to drive here. Keep the retry so a late-arriving PlayerController
    // still mounts the splash viewport for its first frame.
    (void)Fraction;
    if (Renderer)
        Renderer->EnsureAttached();
}

void ABorn2FlapSplash::SetOpacity(float Opacity)
{
    if (Renderer)
        Renderer->EnsureAttached();

    FProps Props;
    Props["opacity"] = N(FMath::Clamp(Opacity, 0.f, 1.f));
    SendUpdate(Renderer, {}, std::move(Props));
}

void ABorn2FlapSplash::Close()
{
    if (Renderer)
        Renderer->Close();
}