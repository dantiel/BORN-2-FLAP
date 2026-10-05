// Born2FlapPoiOverlay.cpp — the semantic F8 POI-select overlay implementation.

#include "UI/Born2FlapPoiOverlay.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapI18n.h"
#include "Flight/Born2FlapFlightPawn.h"

using namespace born2flap::ui;

namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue S(const FString& v) { FValue x; x.kind = FValue::Kind::String; x.str = TCHAR_TO_UTF8(*v); return x; }
FValue N(double v) { FValue x; x.kind = FValue::Kind::Number; x.num = v; return x; }
FValue B(bool v) { FValue x; x.kind = FValue::Kind::Bool; x.b = v; return x; }

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

}  // namespace

ABorn2FlapPoiOverlay::ABorn2FlapPoiOverlay()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABorn2FlapPoiOverlay::Open(ABorn2FlapFlightPawn* InBird)
{
    Bird = InBird;
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 12;  // above the cockpit (0), below the menu (90)
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapPoiOverlay::HandleAction);
    BuildTree();
}

void ABorn2FlapPoiOverlay::BuildTree()
{
    if (!Renderer || !Bird.IsValid())
        return;

    const TArray<FBorn2FlapPoi>& POIs = Bird->GetPOIs();
    const int32 Selected = Bird->GetSelectedPoi();

    std::vector<FNode> Rows;
    Rows.push_back(Nd("Banner", P({ {"text", S("POINTS OF INTEREST")}, {"tone", S("accent")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("Choose a launch point — the bird re-places there instantly.")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));

    for (int32 I = 0; I < POIs.Num(); ++I)
    {
        FString Name = Born2Flap::I18n::T(POIs[I].Key);
        if (POIs[I].Number > 0)
            Name += TEXT(" ") + FString::FromInt(POIs[I].Number);
        Rows.push_back(Nd("Button", P({
            {"label", S(Name)},
            {"action", S(FString::Printf(TEXT("poi.%d"), I))},
            {"tone", S(I == Selected ? "good" : "accent")} })));
    }

    Rows.push_back(Nd("Button", P({ {"label", S("CLOSE  /  F8")}, {"action", S("poi.close")}, {"tone", S("dim")} })));

    FNode Root = Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
    {
        Nd("SizeBox", P({ {"width", N(440)}, {"height", N(520)} }),
        {
            Nd("Panel", P({ {"bg", S("solid")}, {"padding", N(20)} }),
            {
                Nd("ScrollBox", P({}), { Nd("VerticalBox", P({ {"spacing", N(6)} }), std::move(Rows)) })
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

void ABorn2FlapPoiOverlay::HandleAction(const FString& Action, float Value, const FString& Text)
{
    if (!Bird.IsValid())
        return;
    if (Action == TEXT("poi.close"))
    {
        Bird->ClosePoiOverlay();
        return;
    }
    if (Action.StartsWith(TEXT("poi.")))
    {
        const int32 Index = FCString::Atoi(*Action.Mid(4));
        Bird->SelectPoi(Index);
        Bird->ClosePoiOverlay();
        return;
    }
}

void ABorn2FlapPoiOverlay::Close()
{
    if (Renderer)
        Renderer->Close();
}