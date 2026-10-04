// Born2FlapMenu.cpp — the semantic main-menu / level-selector implementation.

#include "UI/Born2FlapMenu.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "UI/Born2FlapI18n.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

using namespace born2flap::ui;

namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue S(const FString& v) { FValue x; x.kind = FValue::Kind::String; x.str = TCHAR_TO_UTF8(*v); return x; }
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

}  // namespace

ABorn2FlapMenu::ABorn2FlapMenu()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABorn2FlapMenu::BeginPlay()
{
    Super::BeginPlay();
    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 90; // under the splash (100), above the cockpit (0)
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapMenu::OnAction);
    Build();
}

void ABorn2FlapMenu::Build()
{
    if (!Renderer)
        return;

    // Full-bleed menu artwork + a centred card with the three level buttons.
    //   []                 Overlay (root, children fill)
    //   [0]                Image (born2flap-background, full-bleed)
    //   [1]                Overlay (align center, valign center)
    //   [1,0]              Panel (dark scrim card)
    //   [1,0,0]            Banner (title)
    //   [1,0,1]            TextBlock (subtitle)
    //   [1,0,2..]          Button + desc pairs, one per level
    //   [1,0,last]         TextBlock (hint)
    FNode Root = Nd("Overlay", P({}),
    {
        Nd("Image", P({ {"texture", S("/Game/Splash/born2flap-background.born2flap-background")} })),
        Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
        {
            Nd("Panel", P({ {"bg", S("solid")}, {"padding", N(30)}, {"spacing", N(10)} }),
            {
                Nd("Banner", P({ {"text", S(Born2Flap::I18n::T("menu.title"))}, {"tone", S("accent")}, {"size", S("xxl")} })),
                Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.subtitle"))}, {"size", S("m")}, {"align", S("center")} })),
                Nd("Spacer", P({})),
                Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.level_ravenstonefield"))}, {"action", S("menu.ravenstonefield")} })),
                Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.level_ravenstonefield_desc"))}, {"size", S("s")}, {"align", S("center")} })),
                Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.level_shiomori"))}, {"action", S("menu.shiomori")} })),
                Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.level_shiomori_desc"))}, {"size", S("s")}, {"align", S("center")} })),
                Nd("Button", P({ {"label", S(Born2Flap::I18n::T("menu.level_training"))}, {"action", S("menu.training")} })),
                Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.level_training_desc"))}, {"size", S("s")}, {"align", S("center")} })),
                Nd("Spacer", P({})),
                Nd("TextBlock", P({ {"text", S(Born2Flap::I18n::T("menu.hint"))}, {"size", S("s")}, {"align", S("center")} })),
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

void ABorn2FlapMenu::SetOpacity(float Opacity)
{
    if (!Renderer)
        return;
    Renderer->EnsureAttached();
    FProps Props;
    Props["opacity"] = N(FMath::Clamp(Opacity, 0.f, 1.f));
    TArray<FOp> Ops;
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = FPath();
    O.props = std::move(Props);
    Ops.Add(O);
    Renderer->ApplyOps(Ops);
}

void ABorn2FlapMenu::OnAction(const FString& Action, float Value, const FString& Text)
{
    // Route a level button to its map. ?Level= drives the level flags in
    // InitGame; ?SkipMenu=1 keeps the menu from re-opening on the destination.
    struct FEntry { const TCHAR* Action; const TCHAR* Map; const TCHAR* Level; };
    static const FEntry Entries[] = {
        { TEXT("menu.ravenstonefield"), TEXT("/Game/Ravenstonefield/Maps/RAVENSTONEFIELD"), TEXT("Ravenstonefield") },
        { TEXT("menu.shiomori"),        TEXT("/Game/Shiomori/Maps/SHIOMORI"),               TEXT("Shiomori") },
        { TEXT("menu.training"),        TEXT("/Engine/Maps/Entry"),                         TEXT("Training") },
    };
    for (const FEntry& E : Entries)
    {
        if (Action == E.Action)
        {
            UGameplayStatics::OpenLevel(this, E.Map, true,
                FString::Printf(TEXT("?Level=%s&SkipMenu=1"), E.Level));
            return;
        }
    }
}