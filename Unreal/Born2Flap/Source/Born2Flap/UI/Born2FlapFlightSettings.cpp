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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"

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

// Servos tested for flapping (dantiel.github.io/OrniFlight servo-efficiency
// table). Speed = 60 / s-per-60°, torque = kg·cm × 0.0980665 → N·m. Backdrive
// is not published on the table, so presets keep the simulator default (20).
struct FServoPreset { const TCHAR* Name; float SpeedDegS; float StallTorqueNm; float Backdrive; float Voltage; };
static const FServoPreset ServoPresets[] = {
    { TEXT("Blue Arrow AF D43S-6.0-MG"), 1500.f, 0.17f, 20.f, 6.0f },
    { TEXT("VOTIK PTK 7465 / 7465W MG"),   667.f, 0.56f, 20.f, 8.4f },
    { TEXT("Blue Arrow D0576HT-MG-HV"),   1000.f, 0.41f, 20.f, 7.4f },
    { TEXT("Inservos D0474HT-MG-HV"),      750.f, 0.35f, 20.f, 7.4f },
    { TEXT("KST MR320"),                   750.f, 0.54f, 20.f, 7.4f },
    { TEXT("SAVOX SV-1270TG"),             545.f, 3.43f, 20.f, 7.4f },
};
constexpr int32 ServoPresetCount = (int32)(sizeof(ServoPresets) / sizeof(ServoPresets[0]));

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
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FSettingsCapture")))
    {
        for(int Page=1;Page<=2;++Page)
        {
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this,Page](float){
                HandleAction(TEXT("settings.page"),Page,TEXT(""));return false;
            }),Page*2.f);
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[Page](float){
                FScreenshotRequest::RequestScreenshot(Page==1 ? TEXT("GLASS_CONTROLS.png") : TEXT("GLASS_TUNING.png"),true,false);return false;
            }),Page*2.f+.5f);
        }
    }
}

void ABorn2FlapFlightSettings::BuildTree()
{
    if (!Renderer || !Bird.IsValid())
        return;

    // Paths are child indices from the Overlay root ([]):
    //   [0] SizeBox → [0] Panel → [1] ScrollBox → [0] VBox → [row].
    static const TArray<int32> VBox{ 0, 0, 1, 0 };
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

    FValue Pages;Pages.kind=FValue::Kind::Array;for(const char* Page:{"CRAFT","BIRD","CONTROLS","ASSIST","TUNING"}) Pages.arr.push_back(S(Page));
    Rows.push_back(Nd("Select",P({{"options",Pages},{"value",N(SelectedPage)},{"action",S("settings.page")},{"tooltip",S("Choose your aircraft, tune the bird, adjust controls, or manage assists.")}})));
    // Header.
    Rows.push_back(Nd("TextBlock", P({ {"text", Sv(TEXT("Flight paused  •  choose your silhouette"))}, {"tone", S("dim")}, {"size", S("s")} })));

    const int32 CraftStart=Rows.size();
    // Bird model — three full-width buttons with a ●/○ selection marker.
    for (int32 M = 0; M < 3; ++M)
    {
        FString Label;
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = Sv(Label);
        Props["action"] = Sv(FString::Printf(TEXT("bird.model.%d"), M));
        Props["tone"] = S(M==CurModel ? "good" : "normal");
        Props["tooltip"] = S("Select this aircraft for the current flight.");
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

    const int32 BirdStart=Rows.size();
    // Further bird tuning (BIRD tab).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("BIRD TUNING")}, {"tone", S("accent")}, {"size", S("l")} })));
    {
        FProps Props;
        Props["action"] = S("camera.angle");
        Props["label"] = S("FPV CAMERA ANGLE");
        Props["value"] = N(Bird->GetFpvCameraAngle());
        Props["min"] = N(-45);
        Props["max"] = N(45);
        Props["step"] = N(1);
        Props["unit"] = Sv(TEXT("°"));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("bird.coupled");
        Props["label"] = S("COUPLED THROTTLE");
        Props["on"] = S("ON  /  click to decouple");
        Props["off"] = S("OFF  /  click to couple");
        Props["value"] = B(Bird->IsThrottleCoupled());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }

    const int32 ControlsStart=Rows.size();
    // Mouse response.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("MOUSE RESPONSE")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("Negative reverses direction. Zero disables that mouse axis. Magnitude sets sensitivity.")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FProps Props;
        Props["action"] = Sv(FString::Printf(TEXT("mouse.gain.%d"), Axis));
        Props["label"] = Sv(FString(MouseNames[Axis]));
        Props["tooltip"] = S("Negative values reverse the axis. Zero disables it. Larger magnitudes increase sensitivity.");
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

    // Mouse speed modifier (0..1).
    {
        FProps Props;
        Props["action"] = S("mouse.speed");
        Props["label"] = S("MOUSE SPEED");
        Props["value"] = N(Bird->GetSpeedModifier());
        Props["min"] = N(0);
        Props["max"] = N(1);
        Props["step"] = N(0.05);
        Props["unit"] = S("×");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    const int32 AssistStart=Rows.size();
    // Flight-safety reset amount — one slider, three stops.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("FLIGHT SAFETY RESET")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("0 = OFF (only glitch guards)   ·   1 = VERY LOW (acro)   ·   2 = NORMAL")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    {
        FProps Props;
        Props["action"] = S("flight.safety");
        Props["label"] = S("SAFETY");
        Props["tooltip"] = S("0: only numerical glitch guards. 1: light recovery assistance. 2: normal flight recovery assistance.");
        Props["value"] = N(Bird->GetFlightSafety());
        Props["min"] = N(0);
        Props["max"] = N(2);
        Props["step"] = N(1);
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    // Replay shadow-doppelgängers: toggle visibility + purge saved recordings.
    Rows.push_back(Nd("TextBlock", P({ {"text", S("REPLAY SHADOWS")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", S("Past flights fly alongside as dark doppelgängers (gold = best round).")}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    {
        FProps Props;
        Props["action"] = S("replay.spirits");
        Props["label"] = S("SHADOW DOPPELGÄNGERS");
        Props["on"] = S("ON  /  click to hide");
        Props["off"] = S("OFF  /  click to show");
        Props["value"] = B(Bird->GetReplaySpiritsEnabled());
        Rows.push_back(Nd("Toggle", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("replay.delete");
        Props["label"] = S("Delete all replay shadows");
        Props["tone"] = S("danger");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    const int32 TuningStart=Rows.size();
    // Tuning — firmware knobs (metadata in Born2FlapTuning.h).
    Rows.push_back(Nd("TextBlock", P({ {"text", S("H A N G A R   /   TUNING")}, {"tone", S("accent")}, {"size", S("l")} })));
    Rows.push_back(Nd("TextBlock", P({ {"text", Sv(TEXT("Live edits reach the firmware on the next physics step — the bird re-tunes itself."))}, {"tone", S("dim")}, {"size", S("xs")}, {"wrap", B(true)} })));
    // Servo preset: load speed/torque/voltage defaults from a servo tested for
    // flapping. The selected preset stays highlighted until any servo slider is
    // edited by hand, which flips the readout to CUSTOM.
    {
        FValue ServoOptions; ServoOptions.kind = FValue::Kind::Array;
        for (const FServoPreset& Sp : ServoPresets) ServoOptions.arr.push_back(Sv(FString(Sp.Name)));
        ServoSelectPath = RowPath((int32)Rows.size());
        Rows.push_back(Nd("Select", P({
            {"label", S("SERVO PRESET  /  load tested defaults")},
            {"options", std::move(ServoOptions)},
            {"value", N(ServoPresetIndex)},
            {"action", S("servo.preset")},
            {"tooltip", S("Load speed, stall torque and voltage defaults from a servo tested for flapping (dantiel.github.io/OrniFlight). Editing any servo slider afterwards marks it CUSTOM.")}
        })));
        ServoStatusPath = RowPath((int32)Rows.size());
        const FString StatusText = (ServoPresetIndex >= 0 && ServoPresetIndex < ServoPresetCount)
            ? FString(TEXT("SERVO:  ")) + ServoPresets[ServoPresetIndex].Name
            : TEXT("SERVO:  CUSTOM  /  manual");
        Rows.push_back(Nd("TextBlock", P({
            {"text", Sv(StatusText)},
            {"tone", S(ServoPresetIndex >= 0 ? "good" : "dim")},
            {"size", S("s")}
        })));
    }
    for (const born2flap::tuning::FRow& Row : born2flap::tuning::Rows)
    {
        FProps Props;
        Props["action"] = S(TCHAR_TO_UTF8(born2flap::tuning::Key(Row.Field)));
        Props["label"] = Sv(FString(Row.Label));
        Props["value"] = N(born2flap::tuning::ToDisplayValue(Row.Field, Bird->GetTuning(Row.Field)));
        Props["min"] = N(Row.Min);
        Props["max"] = N(Row.Max);
        Props["step"] = N(FMath::Pow(10.0f, (float)-Row.Decimals));
        Props["unit"] = Sv(FString(Row.Unit));
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    // Airframe mass + centre of gravity: physical body properties, not firmware
    // knobs — they act directly on the Chaos body (mass and COM offset).
    {
        FProps Props;
        Props["action"] = S("bird.weight");
        Props["label"] = S("BIRD WEIGHT");
        Props["value"] = N(Bird->GetBodyMassKg() * 1000.f);
        Props["min"] = N(100);
        Props["max"] = N(2000);
        Props["step"] = N(5);
        Props["unit"] = S("g");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }
    {
        FProps Props;
        Props["action"] = S("bird.cg");
        Props["label"] = S("CENTRE OF GRAVITY");
        Props["value"] = N(Bird->GetCgOffsetMm());
        Props["min"] = N(-30);
        Props["max"] = N(30);
        Props["step"] = N(1);
        Props["unit"] = S("mm");
        Rows.push_back(Nd("Slider", std::move(Props)));
    }

    for(int32 I=CraftStart;I<(int32)Rows.size();++I)
        Rows[I].props["visible"]=B(SelectedPage==(I<BirdStart ? 0 : I<ControlsStart ? 1 : I<AssistStart ? 2 : I<TuningStart ? 3 : 4));
    // Save & return.
    {
        FProps Props;
        Props["action"] = S("settings.close");
        Props["label"] = S("SAVE & RETURN TO FLIGHT   /   Esc");
        Props["tone"] = S("good");
        Rows.push_back(Nd("Button", std::move(Props)));
    }

    FNode Tabs=Rows.front(), Footer=Rows.back();
    Rows.front().props["visible"]=B(false);
    Rows.back().props["visible"]=B(false);
    // Root: centered, fixed-size card → Panel → ScrollBox → VBox of rows.
    FNode Root = Nd("Overlay", P({ {"align", S("center")}, {"valign", S("center")} }),
    {
        Nd("SizeBox", P({ {"width", N(900)}, {"height", N(SelectedPage==0 ? 510 : 800)} }),
        {
            Nd("Panel", P({ {"title",S("FLIGHT DESK")}, {"bg", S("solid")}, {"padding", N(24)}, {"spacing",N(12)} }),
            {
                std::move(Tabs),
                Nd("ScrollBox", {}, { Nd("VerticalBox", P({ {"spacing", N(12)} }), std::move(Rows)) }),
                std::move(Footer)
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
        FString Label;
        Label += ModelNames[M];
        FProps Props;
        Props["label"] = Sv(Label);
        Props["tone"] = S(M==Cur ? "good" : "normal");
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

    if(Action==TEXT("settings.page")){SelectedPage=FMath::Clamp(FMath::RoundToInt(Value),0,4);BuildTree();return;}
    if (Action == TEXT("settings.close")) { Bird->CloseFlightSettings(); return; }

    if (Action.StartsWith(TEXT("bird.model.")))
    {
        const int32 Model = FCString::Atoi(*Action.RightChop(FCString::Strlen(TEXT("bird.model."))));
        Bird->SelectBirdModel(Model);
        RefreshModelButtons();
        return;
    }

    if (Action == TEXT("camera.fpv")) { Bird->ToggleFpvView(); return; }

    if (Action == TEXT("camera.angle")) { Bird->SetFpvCameraAngle(Value); return; }
    if (Action == TEXT("bird.coupled")) { Bird->SetThrottleCoupled(Value > 0.5f); return; }
    if (Action == TEXT("mouse.speed")) { Bird->SetSpeedModifier(Value); return; }
    if (Action == TEXT("bird.weight")) { Bird->SetBodyMassKg(Value / 1000.f); return; }
    if (Action == TEXT("bird.cg")) { Bird->SetCgOffsetMm(Value); return; }

    if (Action == TEXT("servo.preset"))
    {
        const int32 Idx = FMath::Clamp(FMath::RoundToInt(Value), 0, ServoPresetCount - 1);
        const FServoPreset& Sp = ServoPresets[Idx];
        Bird->SetTuning(ETuningField::ServoSpeed, Sp.SpeedDegS);
        Bird->SetTuning(ETuningField::StallTorque, Sp.StallTorqueNm);
        Bird->SetTuning(ETuningField::Backdrive, Sp.Backdrive);
        Bird->SetTuning(ETuningField::BatteryVoltage, Sp.Voltage);
        ServoPresetIndex = Idx;
        BuildTree();
        return;
    }

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

    if (Action == TEXT("flight.safety"))
    {
        Bird->SetFlightSafety(Value);
        return;
    }

    if (Action == TEXT("replay.spirits"))
    {
        Bird->SetReplaySpiritsEnabled(Value > 0.5f);
        return;
    }

    if (Action == TEXT("replay.delete"))
    {
        Bird->DeleteReplaySpirits();
        return;
    }

    const ETuningField Field = born2flap::tuning::FromKey(Action);
    if (Field != ETuningField::Count)
    {
        Bird->SetTuning(Field, born2flap::tuning::FromDisplayValue(Field, Value));
        if (Field == ETuningField::ServoSpeed || Field == ETuningField::StallTorque ||
            Field == ETuningField::Backdrive || Field == ETuningField::BatteryVoltage)
            MarkServoCustom();
    }
}

void ABorn2FlapFlightSettings::MarkServoCustom()
{
    if (ServoPresetIndex == -1 || !Renderer || !Bird.IsValid())
        return;
    ServoPresetIndex = -1;
    SendUpdate(Renderer, ServoSelectPath, P({ {"value", N(-1)} }));
    SendUpdate(Renderer, ServoStatusPath, P({ {"text", S("SERVO:  CUSTOM  /  manual")}, {"tone", S("dim")} }));
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