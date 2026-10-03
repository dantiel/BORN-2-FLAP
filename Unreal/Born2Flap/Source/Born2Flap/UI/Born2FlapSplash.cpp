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

    // Full-bleed artwork behind a bottom-anchored loading readout.
    //   []         Overlay (root, children fill)
    //   [0]        Image (splash texture, full-bleed)
    //   [1]        Overlay (align center, valign bottom)
    //   [1,0]      Panel (solid, bottom-center block)
    //   [1,0,0]    ProgressBar (0..1)
    //   [1,0,1]    TextBlock  "LOADING" (below the bar, clear of the logo)
    FNode Root = Nd("Overlay", P({}),
    {
        Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-splash-new.born2flap-splash-new")} })),
        Nd("Overlay", P({ {"align", S("center")}, {"valign", S("bottom")} }),
        {
            Nd("Panel", P({ {"bg", S("solid")}, {"padding", N(18)}, {"spacing", N(8)} }),
            {
                Nd("ProgressBar", P({ {"value", N(0)}, {"tone", S("accent")} })),
                Nd("TextBlock", P({ {"text", S("LOADING")}, {"size", S("m")}, {"align", S("center")} })),
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
    // The splash may have been spawned before the PlayerController existed,
    // which defers its viewport attachment. Retry now (once per tick) so the
    // artwork actually appears instead of silently never attaching.
    if (Renderer)
        Renderer->EnsureAttached();

    Fraction = FMath::Clamp(Fraction, 0.f, 1.f);

    // Bar.
    FProps BarProps;
    BarProps["value"] = N(Fraction);
    SendUpdate(Renderer, { 1, 0, 0 }, std::move(BarProps));

    // Status label (below the bar).
    FProps TextProps;
    FValue TextV;
    TextV.kind = FValue::Kind::String;
    TextV.str = TCHAR_TO_UTF8(*FString::Printf(TEXT("LOADING %d%%"), FMath::RoundToInt(Fraction * 100.f)));
    TextProps["text"] = TextV;
    SendUpdate(Renderer, { 1, 0, 1 }, std::move(TextProps));
}

void ABorn2FlapSplash::SetOpacity(float Opacity)
{
    if (Renderer)
        Renderer->EnsureAttached();

    FProps Props;
    Props["opacity"] = N(FMath::Clamp(Opacity, 0.f, 1.f));
    SendUpdate(Renderer, {}, std::move(Props));
}