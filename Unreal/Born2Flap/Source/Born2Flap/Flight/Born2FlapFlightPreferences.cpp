#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Game/Born2FlapGameMode.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/Born2FlapFlightSettings.h"
#include "UI/Born2FlapPoiOverlay.h"
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
    Config.GetFloat(TEXT("Flight"),TEXT("Safety"),FlightSafety);
    FlightSafety=FMath::IsFinite(FlightSafety) ? FMath::Clamp(FlightSafety,0.f,2.f) : 1.f;
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
    Config.SetFloat(TEXT("Flight"),TEXT("Safety"),FlightSafety);
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

    // Brain path: the settings panel is authored by the Ruby Brain; just flag it
    // open and hand the mouse to UMG (no native actor to spawn).
    if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        if (GM->IsBrainActive())
        {
            bBrainSettingsOpen = true;
            PC->bShowMouseCursor = true;
            FInputModeUIOnly Mode;
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
            PC->FlushPressedKeys();
            return;
        }
    }

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
    // is open (it early-returns in Tick). UIOnly hands the mouse fully to the
    // UMG tree so buttons, sliders and the scroll box are clickable/scrollable;
    // F8/Esc are captured by the Slate input pre-processor (FPanelKeyInputProcessor),
    // so closing stays reliable even though UIOnly routes keys away from PlayerInput.
    PC->bShowMouseCursor=true;
    FInputModeUIOnly Mode;
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    PC->FlushPressedKeys();
}
void ABorn2FlapFlightPawn::CloseFlightSettings()
{
    if (bBrainSettingsOpen)
    {
        bBrainSettingsOpen = false;
        SaveFlightPreferences();
        if (auto* PC = Cast<APlayerController>(GetController()))
        { PC->bShowMouseCursor = false; PC->SetInputMode(FInputModeGameOnly()); PC->FlushPressedKeys(); }
        return;
    }
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
void ABorn2FlapFlightPawn::OpenPoiOverlay()
{
    if(PoiOverlay.IsValid() || !GEngine || !GEngine->GameViewport) return;
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;

    // Brain path: the POI overlay is authored by the Ruby Brain (F8 flag); no
    // native actor to spawn.
    if (ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        if (GM->IsBrainActive())
        {
            bBrainPoiOpen = true;
            PC->bShowMouseCursor = true;
            FInputModeUIOnly Mode;
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
            PC->FlushPressedKeys();
            return;
        }
    }

    FActorSpawnParameters Params;
    Params.Owner=this;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABorn2FlapPoiOverlay* Overlay=GetWorld()->SpawnActor<ABorn2FlapPoiOverlay>(
        FVector::ZeroVector,FRotator::ZeroRotator,Params);
    if(!Overlay) return;
    Overlay->Open(this);
    PoiOverlay=Overlay;
    PC->bShowMouseCursor=true;
    FInputModeUIOnly Mode;
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    PC->FlushPressedKeys();
}
void ABorn2FlapFlightPawn::ClosePoiOverlay()
{
    if (bBrainPoiOpen)
    {
        bBrainPoiOpen = false;
        if (auto* PC = Cast<APlayerController>(GetController()))
        { PC->bShowMouseCursor = false; PC->SetInputMode(FInputModeGameOnly()); PC->FlushPressedKeys(); }
        return;
    }
    if(!PoiOverlay.IsValid()) return;
    if(PoiOverlay.IsValid())
    {
        PoiOverlay->Close();
        PoiOverlay->Destroy();
    }
    PoiOverlay.Reset();
    if(auto* PC=Cast<APlayerController>(GetController()))
    { PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); PC->FlushPressedKeys(); }
    Desktop.mouseRoll=Desktop.mousePitch=Desktop.mouseYaw=0;
}
void ABorn2FlapFlightPawn::SelectPoi(int32 Index)
{
    if(POIs.IsEmpty()) return;
    SelectedPoi=FMath::Clamp(Index,0,POIs.Num()-1);
    ResetFlight();
}
void ABorn2FlapFlightPawn::EndPlay(const EEndPlayReason::Type Reason)
{
    CloseFlightSettings();
    ClosePoiOverlay();
    if (PanelKeyProcessor.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(PanelKeyProcessor);
    PanelKeyProcessor.Reset();
    Super::EndPlay(Reason);
}