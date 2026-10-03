#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/Born2FlapFlightSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace
{
FString PreferencesPath()
{
    return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("B2FDesktopInputTest"))
        ? TEXT("Automation/FlightPreferences.ini") : TEXT("Config/FlightPreferences.ini"));
}
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
    Config.GetFloat(TEXT("Flight"),TEXT("FpvCameraAngle"),FpvCameraAngleDeg);
    FpvCameraAngleDeg = FMath::IsFinite(FpvCameraAngleDeg) ? FMath::Clamp(FpvCameraAngleDeg,-45.f,45.f) : 0.f;
    Config.GetBool(TEXT("Flight"),TEXT("RollWingTwist"),bRollWingTwist);
    Config.GetBool(TEXT("Flight"),TEXT("CoupledThrottle"),bCoupledThrottle);
    double SpeedMod = Desktop.speedModifier;
    Config.GetDouble(TEXT("Controls"),TEXT("SpeedModifier"),SpeedMod);
    Desktop.speedModifier = FMath::IsFinite(SpeedMod) ? FMath::Clamp(SpeedMod,0.0,1.0) : 0.5;
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
        Config.GetFloat(TEXT("Tuning"),born2flap::tuning::Key(Field),Value);
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
    Config.SetFloat(TEXT("Flight"),TEXT("FpvCameraAngle"),FpvCameraAngleDeg);
    Config.SetBool(TEXT("Flight"),TEXT("RollWingTwist"),bRollWingTwist);
    Config.SetBool(TEXT("Flight"),TEXT("CoupledThrottle"),bCoupledThrottle);
    Config.SetDouble(TEXT("Controls"),TEXT("SpeedModifier"),Desktop.speedModifier);
    for(int32 Axis=0;Axis<3;++Axis)
        Config.SetDouble(TEXT("Mouse"),*FString::Printf(TEXT("Gain%d"),Axis),MouseGains[Axis]);
    for(uint8 I=0;I<uint8(ETuningField::Count);++I)
    {
        const ETuningField Field=ETuningField(I);
        Config.SetFloat(TEXT("Tuning"),born2flap::tuning::Key(Field),GetTuning(Field));
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
    if(SettingsPanel.IsValid() || !GEngine || !GEngine->GameViewport) return;
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC || (RcController && RcController->IsPanelOpen())) return;
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;

    // The panel is the semantic view-framework actor (react-native-umg), not
    // the old raw-Slate SFlightSettings. It renders through UBorn2FlapUIRenderer
    // and routes interactive controls back via OnComponentAction.
    FActorSpawnParameters Params;
    Params.Owner=this;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABorn2FlapFlightSettings* Panel=GetWorld()->SpawnActor<ABorn2FlapFlightSettings>(
        FVector::ZeroVector,FRotator::ZeroRotator,Params);
    if(!Panel) return;
    Panel->Open(this);
    SettingsPanel=Panel;

    // No SetPause here: the FlightPawn freezes the simulation while the panel
    // is open (it early-returns in Tick), but the world keeps running so F8/Esc
    // are never swallowed by paused-input routing. GameAndUI (not UIOnly) keeps
    // the game viewport listening for keyboard while the UMG tree gets the mouse.
    PC->bShowMouseCursor=true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    PC->FlushPressedKeys();
}
void ABorn2FlapFlightPawn::CloseFlightSettings()
{
    if(!SettingsPanel.IsValid()) return;
    SaveFlightPreferences();
    if(SettingsPanel.IsValid())
    {
        SettingsPanel->Close(); // detach the window now — Destroy() only schedules GC
        SettingsPanel->Destroy();
    }
    SettingsPanel.Reset();
    if(auto* PC=Cast<APlayerController>(GetController()))
    { PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); PC->FlushPressedKeys(); }
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;
}
void ABorn2FlapFlightPawn::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseFlightSettings();
    if (PanelKeyProcessor.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(PanelKeyProcessor);
    PanelKeyProcessor.Reset();
    Super::EndPlay(Reason);
}