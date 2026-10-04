// Born2FlapFlightSettings.cpp — the semantic "flight desk" settings panel.
//
// Builds the panel as a single create_instance op (nested FNode), then reacts to
// interactive components through OnComponentAction. See the header for notes.

#include "UI/Born2FlapFlightSettings.h"

#include "UI/Born2FlapUIRenderer.h"
#include "UI/Born2FlapUiOps.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"

using namespace born2flap::ui;

// ---- compact wire-model builders (mirrors Born2FlapFlightHUD.cpp) ----------
namespace {

FValue S(const char* v) { FValue x; x.kind = FValue::Kind::String; x.str = v; return x; }
FValue Sv(const FString& v) { FValue x; x.kind = FValue::Kind::String; x.str = TCHAR_TO_UTF8(*v); return x; }
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

FPath ToPath(const TArray<int32>& Indices)
{
    return FPath(Indices.GetData(), Indices.GetData() + Indices.Num());
}

void SendUpdate(UBorn2FlapUIRenderer* R, const TArray<int32>& Path, FProps Props)
{
    if (!R)
        return;
    TArray<FOp> Ops;
    FOp O;
    O.op = EOp::UpdateProps;
    O.path = ToPath(Path);
    O.props = std::move(Props);
    Ops.Add(O);
    R->ApplyOps(Ops);
}

}  // namespace

ABorn2FlapFlightSettings::ABorn2FlapFlightSettings()
{
    // F8/Esc are owned by the FlightPawn (single toggle owner). The panel does
    // not poll keys; it only reacts to semantic widget actions.
    PrimaryActorTick.bCanEverTick = false;
}

void ABorn2FlapFlightSettings::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Intentionally empty: the FlightPawn is the single owner of F8/Esc.
}

void ABorn2FlapFlightSettings::Open(ABorn2FlapFlightPawn* InBird)
{
    Bird = InBird;

    // No input bindings here: F8/Esc are handled by the FlightPawn, and the
    // "SAVE & RETURN TO FLIGHT" button closes through the "settings.close" action.

    Renderer = NewObject<UBorn2FlapUIRenderer>(this);
    Renderer->ViewportZOrder = 10;
    Renderer->OnComponentAction.AddDynamic(this, &ABorn2FlapFlightSettings::HandleAction);
    BuildTree();
}

void ABorn2FlapFlightSettings::BuildTree()
{
    if (!Renderer || !Bird.IsValid())
        return;

    // Paths are child indices from the Overlay root ([]):
    //   [0] SizeBox → [0] Panel → [0] ScrollBox → [0] VBox → [row].
    static const TArray<int32> VBox{ 0, 0, 0, 0 };
    auto RowPath = [&](int32 Row) { TArray<int32> P = VBox; P.Add(Row); return P; };

    const int32 CurModel = Bird->GetBirdModel();
    static const TCHAR* ModelNames[] =
    {
        TEXT("RAVENCROW  /  folded obsidian & comb pinions"),
        TEXT("PROTOTYPE  /  elliptical feathers"),
        TEXT("COMMON KESTREL  /  falcon"),
    };
    static const TCHAR* MouseNames[] =
    {
        TEXT("ROLL  /  mouse X"),
        TEXT("PITCH  /  mouse Y"),
        TEXT("YAW  /  mouse X"),
    };

    ModelButtons.Reset();
    MouseGainSliders.Reset();

    std::vector<FNode> Rows;

    // Header.
    Rows.push_back(Nd("Banner", P({ {"text", S("R A V E N   /   FLIGHT DESK")}, {"tone", S("accent")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", Sv(TEXT("Flight paused  •  choose your silhouette"))}, {"tone", S("dim")}, {"size", S("s")} })));

    // Bird model — three full-width buttons with a ●/○ selection marker.
    for (int32 M = 0; M < 3; ++M)
    {
        FString Label = (M == CurModel ? TEXT("●  ") : TEXT("○  "));
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = Sv(Label);
        Props["action"] = Sv(FString::Printf(TEXT("bird.model.%d"), M));
        ModelButtons.Add(RowPath((int32)Rows.size()));
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    // Camera mode toggle (FPV / chase).
    {
        FProps Props;
        Props["action"] = S("camera.fpv");
        Props["label"] = S("AIRBORNE CAMERA");
        Props["on"] = S("FPV  /  click for chase");
        Props["off"] = S("CHASE  /  click for FPV");
        Props["value"] = B(Bird->IsFpvAirView());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }

    // Mouse response.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("MOUSE RESPONSE")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("Negative reverses direction. Zero disables that mouse axis. Magnitude sets sensitivity.")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FProps Props;
        Props["action"] = Sv(FString::Printf(TEXT("mouse.gain.%d"), Axis));
        Props["label"] = Sv(FString(MouseNames[Axis]));
        Props["value"] = N(Bird->GetMouseGains()[Axis]);
        Props["min"] = N(-2);
        Props["max"] = N(2);
        Props["step"] = N(0.05);
        Props["unit"] = Sv(TEXT("×"));
        MouseGainSliders.Add(RowPath((int32)Rows.size()));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    Rows.push_back(Nd("TextBlock", P({ {"text", Sv(TEXT("−2 reverse / faster          0 off          +2 forward / faster"))}, {"tone", S("dim")}, {"size", S("xs")} })));
    {
        FProps Props;
        Props["action"] = S("mouse.reset");
        Props["label"] = S("Reset mouse response");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    // Control expo (shown as a whole-number percentage).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("CONTROL EXPO  /  MOUSE + KEYBOARD")}, {"tone", S("accent")}, {"size", S("l")} })));
    {
        FProps Props;
        Props["action"] = S("control.expo");
        Props["label"] = S("EXPO");
        Props["value"] = N(Bird->GetControlExpo() * 100.0);
        Props["min"] = N(0);
        Props["max"] = N(100);
        Props["step"] = N(1);
        Props["unit"] = S("%");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Tuning — twelve firmware knobs (metadata in Born2FlapTuning.h).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("H A N G A R   /   TUNING")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", Sv(TEXT("Live edits reach the firmware on the next physics step — the bird re-tunes itself."))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    for (const born2flap::tuning::FRow& Row : born2flap::tuning::Rows)
    {
        FProps Props;
        Props["action"] = S(TCHAR_TO_UTF8(born2flap::tuning::Key(Row.Field)));
        Props["label"] = Sv(FString(Row.Label));
        Props["value"] = N(Bird->GetTuning(Row.Field));
        Props["min"] = N(Row.Min);
        Props["max"] = N(Row.Max);
        Props["step"] = N(FMath::Pow(10.0f, (float)-Row.Decimals));
        Props["unit"] = Sv(FString(Row.Unit));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Save & return.
    {
        FProps Props;
        Props["action"] = S("settings.close");
        Props["label"] = S("SAVE & RETURN TO FLIGHT   /   Esc or F8");
        Props["tone"] = S("good");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    // Root: centered, fixed-size card → Panel → ScrollBox → VBox of rows.
    FNode Root = Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
    {
        Nd("SizeBox", P({ {"width", N(640)}, {"height", N(720)} }),
        {
            Nd("Panel", P({ {"bg", S("solid")}, {"padding", N(24)} }),
            {
                Nd("ScrollBox", {}, { Nd("VerticalBox", P({ {"spacing", N(6)} }), std::move(Rows)) })
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

void ABorn2FlapFlightSettings::RefreshModelButtons()
{
    if (!Renderer || !Bird.IsValid())
        return;
    static const TCHAR* ModelNames[] =
    {
        TEXT("RAVENCROW  /  folded obsidian & comb pinions"),
        TEXT("PROTOTYPE  /  elliptical feathers"),
        TEXT("COMMON KESTREL  /  falcon"),
    };
    const int32 Cur = Bird->GetBirdModel();
    for (int32 M = 0; M < 3 && M < ModelButtons.Num(); ++M)
    {
        FString Label = (M == Cur ? TEXT("●  ") : TEXT("○  "));
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = Sv(Label);
        SendUpdate(Renderer, ModelButtons[M], std::move(Props));
    }
}

void ABorn2FlapFlightSettings::RefreshMouseGains()
{
    if (!Renderer || !Bird.IsValid())
        return;
    for (int32 Axis = 0; Axis < 3 && Axis < MouseGainSliders.Num(); ++Axis)
    {
        FProps Props;
        Props["value"] = N(Bird->GetMouseGains()[Axis]);
        SendUpdate(Renderer, MouseGainSliders[Axis], std::move(Props));
    }
}

void ABorn2FlapFlightSettings::HandleAction(const FString& Action, float Value, const FString& Text)
{
    if (!Bird.IsValid())
        return;

    if (Action == TEXT("settings.close")) { Bird->CloseFlightSettings(); return; }

    if (Action.StartsWith(TEXT("bird.model.")))
    {
        const int32 Model = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("bird.model."))));
        Bird->SelectBirdModel(Model);
        RefreshModelButtons();
        return;
    }

    if (Action == TEXT("camera.fpv")) { Bird->ToggleFpvView(); return; }

    if (Action.StartsWith(TEXT("mouse.gain.")))
    {
        const int32 Axis = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("mouse.gain."))));
        Bird->SetMouseGain(Axis, Value);
        return;
    }

    if (Action == TEXT("mouse.reset"))
    {
        Bird->SetMouseGain(0, 1.f);
        Bird->SetMouseGain(1, -1.f);
        Bird->SetMouseGain(2, 1.f);
        RefreshMouseGains();
        return;
    }

    if (Action == TEXT("control.expo"))
    {
        Bird->SetControlExpo(Value / 100.f);
        return;
    }

    const ETuningField Field = born2flap::tuning::FromKey(Action);
    if (Field != ETuningField::Count)
        Bird->SetTuning(Field, Value);
}

void ABorn2FlapFlightSettings::HandleClose()
{
    if (Bird.IsValid())
        Bird->CloseFlightSettings();
}

void ABorn2FlapFlightSettings::Close()
{
    if (Renderer)
        Renderer->Close();
}

void ABorn2FlapFlightSettings::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Release the input-stack entry acquired in Open() before the actor dies.
    if (Bird.IsValid())
        if (APlayerController* PC = Cast<APlayerController>(Bird->GetController()))
            DisableInput(PC);
    Super::EndPlay(EndPlayReason);
}