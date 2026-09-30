#include "Flight/Born2FlapFlightPawn.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
FString PreferencesPath()
{
    return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("B2FDesktopInputTest"))
        ? TEXT("Automation/FlightPreferences.ini") : TEXT("Config/FlightPreferences.ini"));
}
const TCHAR* TuningKey(ETuningField Field)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed: return TEXT("ServoSpeed");
    case ETuningField::StallTorque: return TEXT("StallTorque");
    case ETuningField::Backdrive: return TEXT("Backdrive");
    case ETuningField::BatteryVoltage: return TEXT("BatteryVoltage");
    case ETuningField::BatteryResistance: return TEXT("BatteryResistance");
    case ETuningField::BatteryCapacity: return TEXT("BatteryCapacity");
    case ETuningField::FlapBaseFreq: return TEXT("FlapBaseFreq");
    case ETuningField::MountAngle: return TEXT("MountAngle");
    case ETuningField::GlideAngle: return TEXT("GlideAngle");
    case ETuningField::StrokeFerocity: return TEXT("StrokeFerocity");
    case ETuningField::AileronScale: return TEXT("AileronScale");
    case ETuningField::ElevatorScale: return TEXT("ElevatorScale");
    default: return TEXT("");
    }
}
struct FTuningRow
{
    ETuningField Field;
    const TCHAR* Label;
    const TCHAR* Unit;
    float Min, Max;
    int32 Decimals;
};
const FTuningRow TuningRows[] =
{
    { ETuningField::ServoSpeed, TEXT("SERVO SPEED"), TEXT("°/s"), 100, 2400, 0 },
    { ETuningField::StallTorque, TEXT("STALL TORQUE"), TEXT("N·m"), 0.5f, 20, 1 },
    { ETuningField::Backdrive, TEXT("BACKDRIVE"), TEXT("°/s per N·m"), 0, 100, 0 },
    { ETuningField::BatteryVoltage, TEXT("BATTERY VOLTAGE"), TEXT("V"), 3.7f, 22.2f, 1 },
    { ETuningField::BatteryResistance, TEXT("BATTERY RESISTANCE"), TEXT("Ω"), 0.01f, 0.5f, 2 },
    { ETuningField::BatteryCapacity, TEXT("BATTERY CAPACITY"), TEXT("Ah"), 0.1f, 5, 2 },
    { ETuningField::FlapBaseFreq, TEXT("FLAP FREQ CEILING"), TEXT("dHz"), 10, 200, 0 },
    { ETuningField::MountAngle, TEXT("MOUNT ANGLE"), TEXT("°"), -15, 15, 0 },
    { ETuningField::GlideAngle, TEXT("GLIDE ANGLE"), TEXT("°"), -15, 15, 0 },
    { ETuningField::StrokeFerocity, TEXT("STROKE FEROCITY"), TEXT("%"), 0, 100, 0 },
    { ETuningField::AileronScale, TEXT("AILERON SCALE"), TEXT("%"), 0, 100, 0 },
    { ETuningField::ElevatorScale, TEXT("ELEVATOR SCALE"), TEXT("%"), 0, 100, 0 },
};
class SFlightSettings : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFlightSettings) {} SLATE_ARGUMENT(ABorn2FlapFlightPawn*, Bird) SLATE_END_ARGS()
    TWeakObjectPtr<ABorn2FlapFlightPawn> Bird;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
    {
        if (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::F8)
        { if (Bird.IsValid()) Bird->CloseFlightSettings(); return FReply::Handled(); }
        return SCompoundWidget::OnKeyDown(Geometry,Event);
    }
    void Construct(const FArguments& Args)
    {
        Bird=Args._Bird;
        TSharedPtr<SVerticalBox> Rows;
        ChildSlot.HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(600).MaxDesiredHeight(650)
          [ SNew(SBorder).Padding(32).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
              .BorderBackgroundColor(FLinearColor(.025,.034,.043,.98))
            [ SNew(SScrollBox) + SScrollBox::Slot() [ SAssignNew(Rows,SVerticalBox) ] ] ] ];
        auto Label=[&](FString Text,int32 Size,FLinearColor Colour=FLinearColor(.82,.85,.86))
        {
            Rows->AddSlot().AutoHeight().Padding(0,0,0,14)
            [SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(Colour)];
        };
        Label(TEXT("R A V E N   /   FLIGHT DESK"),24);
        Label(TEXT("Flight paused  •  choose your silhouette"),12);
        for (int32 Model=0; Model<3; ++Model)
            Rows->AddSlot().AutoHeight().Padding(0,0,0,8)
            [SNew(SButton).ContentPadding(FMargin(14,10))
              .Text_Lambda([this,Model] { return FText::FromString(FString(Bird.IsValid() && Bird->GetBirdModel()==Model ? TEXT("●  ") : TEXT("○  "))+
                  (Model==0 ? TEXT("RAVENCROW  /  folded obsidian & comb pinions") : Model==1 ? TEXT("PROTOTYPE  /  elliptical feathers") : TEXT("PEREGRINE  /  falcon"))); })
              .OnClicked_Lambda([this,Model] { if(Bird.IsValid()) Bird->SelectBirdModel(Model); return FReply::Handled(); })];
        Rows->AddSlot().AutoHeight().Padding(0,5,0,14)
        [SNew(SButton).ContentPadding(10)
          .Text_Lambda([this] { return FText::FromString(Bird.IsValid() && Bird->IsFpvAirView() ? TEXT("AIRBORNE CAMERA: FPV  /  click for chase") : TEXT("AIRBORNE CAMERA: CHASE  /  click for FPV")); })
          .OnClicked_Lambda([this] { if(Bird.IsValid()) Bird->ToggleFpvView(); return FReply::Handled(); })];
        Label(TEXT("MOUSE RESPONSE"),16);
        Label(TEXT("Negative reverses direction. Zero disables that mouse axis.\nMagnitude sets sensitivity; keyboard controls keep their direction."),12);
        const TCHAR* Names[]={TEXT("ROLL  /  mouse X"),TEXT("PITCH  /  mouse Y"),TEXT("YAW  /  mouse X")};
        for (int32 Axis=0; Axis<3; ++Axis)
        {
            Rows->AddSlot().AutoHeight().Padding(0,7,0,5)
            [SNew(STextBlock).Text_Lambda([this,Axis,Name=FString(Names[Axis])] {
                return FText::FromString(FString::Printf(TEXT("%s                         %+.2f ×"),*Name,Bird.IsValid() ? Bird->GetMouseGains()[Axis] : 0)); })];
            Rows->AddSlot().AutoHeight().Padding(0,2,0,9)
            [SNew(SSlider).MinValue(-2.f).MaxValue(2.f).StepSize(.05f).MouseUsesStep(true)
              .Value_Lambda([this,Axis] { return Bird.IsValid() ? float(Bird->GetMouseGains()[Axis]) : 0.f; })
              .OnValueChanged_Lambda([this,Axis](float Value) { if(Bird.IsValid()) Bird->SetMouseGain(Axis,Value); })];
        }
        Label(TEXT("−2 reverse / faster          0 off          +2 forward / faster"),11,FLinearColor(.53,.61,.65));
        Rows->AddSlot().AutoHeight().Padding(0,12,0,8)
        [SNew(SButton).Text(FText::FromString(TEXT("Reset mouse response"))).ContentPadding(10)
          .OnClicked_Lambda([this] { if(Bird.IsValid()) { Bird->SetMouseGain(0,1); Bird->SetMouseGain(1,-1); Bird->SetMouseGain(2,1); } return FReply::Handled(); })];
        Label(TEXT("CONTROL EXPO / MOUSE + KEYBOARD"),16);
        Rows->AddSlot().AutoHeight().Padding(0,0,0,6)
        [SNew(STextBlock).Text_Lambda([this] {
            return FText::FromString(FString::Printf(TEXT("%.0f%%   /   0 = linear, 100 = very fine centre; full throw stays full"),
                Bird.IsValid() ? Bird->GetControlExpo()*100 : 0)); })];
        Rows->AddSlot().AutoHeight().Padding(0,0,0,16)
        [SNew(SSlider).MinValue(0).MaxValue(1).StepSize(.05f).MouseUsesStep(true)
          .Value_Lambda([this] { return Bird.IsValid() ? Bird->GetControlExpo() : .65f; })
          .OnValueChanged_Lambda([this](float Value) { if(Bird.IsValid()) Bird->SetControlExpo(Value); })];
        Label(TEXT("H A N G A R   /   TUNING"),16);
        Label(TEXT("Live edits reach the firmware on the next physics step — the bird re-tunes itself."),12);
        for (const FTuningRow& Row : TuningRows)
        {
            Rows->AddSlot().AutoHeight().Padding(0,5,0,2)
            [SNew(STextBlock).Text_Lambda([this,Row] {
                const float Value = Bird.IsValid() ? Bird->GetTuning(Row.Field) : 0.f;
                return FText::FromString(FString::Printf(TEXT("%s   %s %s"), Row.Label,
                    *FString::SanitizeFloat(Value, Row.Decimals), Row.Unit));
            })];
            Rows->AddSlot().AutoHeight().Padding(0,2,0,9)
            [SNew(SSlider).MinValue(Row.Min).MaxValue(Row.Max).MouseUsesStep(false)
              .Value_Lambda([this,Row] { return Bird.IsValid() ? Bird->GetTuning(Row.Field) : 0.f; })
              .OnValueChanged_Lambda([this,Row](float Value) { if(Bird.IsValid()) Bird->SetTuning(Row.Field, Value); })];
        }
        Rows->AddSlot().AutoHeight()
        [SNew(SButton).Text(FText::FromString(TEXT("SAVE & RETURN TO FLIGHT   /   Esc or F8"))).ContentPadding(14)
          .OnClicked_Lambda([this] { if(Bird.IsValid()) Bird->CloseFlightSettings(); return FReply::Handled(); })];
    }
};
}

void ABorn2FlapFlightPawn::LoadFlightPreferences()
{
    // Automated channel tests use known gains, independently of personal settings.
    if (bFlightTest || bDesktopInputTest) { MouseGains=FVector(1); return; }
    const FString Path=PreferencesPath();
    FConfigFile Config; Config.Read(Path); Config.bCanSaveAllSections=true;
    Config.GetFloat(TEXT("Controls"),TEXT("Expo"),ControlExpo);
    ControlExpo=FMath::IsFinite(ControlExpo) ? FMath::Clamp(ControlExpo,0.f,1.f) : .65f;
    Config.GetBool(TEXT("Flight"),TEXT("FpvAirView"),bFpvAirView);
    for(int32 Axis=0;Axis<3;++Axis)
    {
        double Gain=MouseGains[Axis];
        Config.GetDouble(TEXT("Mouse"),*FString::Printf(TEXT("Gain%d"),Axis),Gain);
        MouseGains[Axis]=FMath::IsFinite(Gain) ? FMath::Clamp(Gain,-2.,2.) : 1.;
    }
    for(uint8 I=0;I<uint8(ETuningField::Count);++I)
    {
        const ETuningField Field=ETuningField(I);
        float Value=GetTuning(Field);
        Config.GetFloat(TEXT("Tuning"),TuningKey(Field),Value);
        SetTuning(Field,Value);
    }
}
void ABorn2FlapFlightPawn::SaveFlightPreferences()
{
    const FString Path=PreferencesPath();
    FConfigFile Config; Config.Read(Path); Config.bCanSaveAllSections=true;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    Config.SetFloat(TEXT("Controls"),TEXT("Expo"),ControlExpo);
    Config.SetBool(TEXT("Flight"),TEXT("FpvAirView"),bFpvAirView);
    for(int32 Axis=0;Axis<3;++Axis)
        Config.SetDouble(TEXT("Mouse"),*FString::Printf(TEXT("Gain%d"),Axis),MouseGains[Axis]);
    for(uint8 I=0;I<uint8(ETuningField::Count);++I)
    {
        const ETuningField Field=ETuningField(I);
        Config.SetFloat(TEXT("Tuning"),TuningKey(Field),GetTuning(Field));
    }
    Config.Write(Path);
}
void ABorn2FlapFlightPawn::SetMouseGain(int32 Axis,float Gain)
{
    if(Axis>=0 && Axis<3 && FMath::IsFinite(Gain)) MouseGains[Axis]=FMath::Clamp(Gain,-2.f,2.f);
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;
}
void ABorn2FlapFlightPawn::OpenFlightSettings()
{
    if(SettingsWidget.IsValid() || !GEngine || !GEngine->GameViewport) return;
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC || (RcController && RcController->IsPanelOpen())) return;
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;
    SettingsWidget=SNew(SFlightSettings).Bird(this);
    GEngine->GameViewport->AddViewportWidgetContent(SettingsWidget.ToSharedRef(),100);
    PC->SetPause(true);
    PC->bShowMouseCursor=true;
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(SettingsWidget);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(SettingsWidget);
}
void ABorn2FlapFlightPawn::CloseFlightSettings()
{
    if(!SettingsWidget.IsValid()) return;
    SaveFlightPreferences();
    if(GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(SettingsWidget.ToSharedRef());
    SettingsWidget.Reset();
    if(auto* PC=Cast<APlayerController>(GetController()))
    { PC->SetPause(false); PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); PC->FlushPressedKeys(); }
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;
}
void ABorn2FlapFlightPawn::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseFlightSettings();
    Super::EndPlay(Reason);
}