#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapWingMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// Feed real Unreal key/axis events through PlayerInput, then observe the pawn's
// resulting channels. This also detects missing mouse axes and wheel scaling.
void ABorn2FlapFlightPawn::CheckDesktopInputTest(float DeltaSeconds)
{
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->PlayerInput) return;
    const int32 Stage = FMath::FloorToInt(DesktopTestTime / 2.0);
    auto Key = [&](FKey K, bool Down) {
        PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };
    auto Axis = [&](FKey K, float Value) {
        PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Axis, Value, 1));
    };
    auto Near = [](double A, double B) { return FMath::Abs(A-B) < .005; };
    if (Stage != DesktopTestStage)
    {
        if(DesktopTestStage<0 && FParse::Param(FCommandLine::Get(),TEXT("B2FWingPaintPreview")))
        {
            auto* Paint=UTexture2D::CreateTransient(64,64,PF_B8G8R8A8);
            auto& Mip=Paint->GetPlatformData()->Mips[0];
            auto* Pixels=static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
            for(int32 Y=0; Y<64; ++Y)
                for(int32 X=0; X<64; ++X)
                    Pixels[Y*64+X]=((X/8+Y/8)%2) ? FColor(230,190,65) : FColor(30,80,135);
            Mip.BulkData.Unlock();
            Paint->UpdateResource();
            SetWingPaint(Paint);
        }
        bool Pass = bHealthy;
        switch (DesktopTestStage)
        {
        case 0: Pass &= Near(Throttle,0); break;
        case 1:
            Pass &= Near(Throttle,.32) && GEngine->GameViewport->ViewModeIndex==VMI_Lit;
            Pass &= !PC->PlayerInput->DebugExecBindings.ContainsByPredicate([](const FKeyBind& B) {
                return B.Key==EKeys::F2 && B.Command.Contains(TEXT("viewmode"));
            });
            break;
        case 2: Pass &= Near(Throttle,.72); break;
        case 3: Pass &= Near(Throttle,1); break;
        case 4: Pass &= Near(Throttle,0); break;
        case 5: Pass &= Near(Throttle,.5) && Near(Desktop.wheelThrottle,.5) && Desktop.wheelOwnsThrottle; break;
        case 6: Pass &= Near(Throttle,.72) && Near(Desktop.wheelThrottle,.5) && !Desktop.wheelOwnsThrottle; break;
        case 7: Pass &= Near(Throttle,0) && Near(Desktop.wheelThrottle,.5) && !Desktop.wheelOwnsThrottle; break;
        case 8: Pass &= Near(Throttle,.52) && Near(Desktop.wheelThrottle,.52) && Desktop.wheelOwnsThrottle; break;
        case 9: Pass &= Near(Throttle,0); break;
        case 10: Pass &= RollInput > .9 && YawInput > .9 && PitchInput > .1; break;
        case 11: Pass &= RollInput > .9 && Near(YawInput,0) && PitchInput > .1; break;
        case 12: Pass &= Near(RollInput,0) && YawInput > .9 && PitchInput > .1; break;
        case 13: Pass &= Near(RollInput,0) && Near(YawInput,0) && PitchInput > .1; break;
        case 14: Pass &= Near(RollInput,0) && Near(YawInput,0) && Near(PitchInput,0); break;
        case 15: case 18: Pass &= RollInput > .99 && YawInput > .99 && PitchInput > .99; break;
        case 16: case 19: case 20: Pass &= Near(RollInput,0) && Near(YawInput,0) && Near(PitchInput,0); break;
        case 17: Pass &= RollInput > .99 && YawInput > .99 && PitchInput > .99; break;
        case 21: Pass &= Near(RollInput,0) && Near(PitchInput,0) && Near(YawInput,0); break;
        case 22: case 23: Pass &= RollInput < -.98 && PitchInput < -.98 && YawInput < -.98; break;
        case 24: Pass &= RollInput > .98 && PitchInput > .98 && YawInput > .98; break;
        case 25: Pass &= PitchInput < -.98 && RollInput > .98 && YawInput > .98; break;
        case 26: Pass &= RollInput < -.98 && PitchInput > .98 && Near(YawInput,0) && BirdModel==1; break;
        case 27: Pass &= RollInput < -.98 && PitchInput > .98 && Near(YawInput,0) && BirdModel==0; break;
        case 28: Pass &= RollInput > .98 && PitchInput < -.98 && YawInput < -.98 && BirdModel==2; break;
        }
        if (DesktopTestStage >= 0)
        {
            bDesktopTestPass &= Pass;
            UE_LOG(LogTemp, Display, TEXT("DesktopInputStage %d %s throttle=%.3f memory=%.3f wheel=%d roll=%.3f pitch=%.3f yaw=%.3f"),
                   DesktopTestStage, Pass ? TEXT("PASS") : TEXT("FAIL"), Throttle, Desktop.wheelThrottle,
                   Desktop.wheelOwnsThrottle, RollInput, PitchInput, YawInput);
            if (DesktopTestStage == 8 && FParse::Param(FCommandLine::Get(),TEXT("B2FChannelCapture")))
                FScreenshotRequest::RequestScreenshot(TEXT("RAVENSTONEFIELD_CHANNELS.png"),true,false);
            if (DesktopTestStage == 0 && FParse::Param(FCommandLine::Get(),TEXT("B2FBirdPreview")))
                FScreenshotRequest::RequestScreenshot(TEXT("RAVENCROW_MODEL.png"),true,false);
            if (DesktopTestStage == 25 && FParse::Param(FCommandLine::Get(),TEXT("B2FBirdPreview")))
                FScreenshotRequest::RequestScreenshot(TEXT("PROTOTYPE_MEMBRANE.png"),true,false);
        }
        // Only edges are injected for buttons; the engine maintains their held state.
        for (FKey K : {EKeys::W,EKeys::LeftControl,EKeys::LeftShift,EKeys::R,EKeys::LeftMouseButton,
                      EKeys::RightMouseButton,EKeys::Left,EKeys::Down,EKeys::A,EKeys::F2})
            if (PC->IsInputKeyDown(K)) Key(K,false);
        DesktopTestStage = Stage;
        switch (Stage)
        {
        case 1: Key(EKeys::F2,true); Key(EKeys::W,true); Key(EKeys::LeftControl,true); break;
        case 2: case 6: Key(EKeys::W,true); break;
        case 3: Key(EKeys::W,true); Key(EKeys::LeftShift,true); break;
        case 5: Axis(EKeys::MouseWheelAxis,25); break;
        case 8: Axis(EKeys::MouseWheelAxis,1); break;
        case 9: Key(EKeys::R,true); break;
        case 11: Key(EKeys::LeftMouseButton,true); break;
        case 12: Key(EKeys::RightMouseButton,true); break;
        case 13: Key(EKeys::LeftMouseButton,true); Key(EKeys::RightMouseButton,true); break;
        case 14: Key(EKeys::Left,true); Key(EKeys::Down,true); Key(EKeys::A,true); break;
        case 16: Key(EKeys::LeftMouseButton,true); break;
        case 19: Key(EKeys::RightMouseButton,true); break;
        case 21: Key(EKeys::LeftMouseButton,true); break;
        case 26: SetMouseGain(0,-1); SetMouseGain(1,1); SetMouseGain(2,0); SelectBirdModel(1); break;
        case 27: SelectBirdModel(0); break;
        case 28:
            if(FParse::Param(FCommandLine::Get(),TEXT("B2FBirdPreview")))
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[](float){FScreenshotRequest::RequestScreenshot(TEXT("PEREGRINE_FALCON.png"),true,false);return false;}),.3f);
            SelectBirdModel(2); SetMouseGain(0,1); SetMouseGain(1,-1); SetMouseGain(2,-1); break;
        }
        if (Stage >= 29)
        {
            bDesktopTestPass &= CheckFlightCameras();
            TInlineComponentArray<UBorn2FlapWingMesh*> Wings(this);
            bool WingsPass=Wings.Num()==6;
            for(auto* Wing : Wings) WingsPass &= Wing->HasValidDeformation();
            UE_LOG(LogTemp,Display,TEXT("WingVisualTest %s: all three models receive solver bending, finite normals and stable UVs"),WingsPass ? TEXT("PASS") : TEXT("FAIL"));
            bDesktopTestPass &= WingsPass;
            if (FParse::Param(FCommandLine::Get(),TEXT("B2FSettingsCapture")))
            {
                // Core ticker keeps running while gameplay is paused by the actual Slate panel.
                SetControlExpo(.35f);
                SetMouseGain(0,-.6f); SetMouseGain(1,-1.2f); SetMouseGain(2,0);
                OpenFlightSettings();
                bDesktopTestPass &= IsFlightSettingsOpen() && PC->IsPaused() && PC->bShowMouseCursor;
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float) {
                    FScreenshotRequest::RequestScreenshot(TEXT("RAVENCROW_SETTINGS.png"),true,false);
                    return false;
                }),1.f);
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float) {
                    // Offscreen windows have no OS keyboard focus; exercise the
                    // same Slate key handler directly and keep it alive through close.
                    const auto Panel=SettingsWidget;
                    Panel->OnKeyDown(Panel->GetCachedGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
                    auto* Player=Cast<APlayerController>(GetController());
                    double SavedRoll=99, SavedExpo=99;
                    FConfigFile Saved; Saved.Read(FPaths::ProjectSavedDir()/TEXT("Automation/FlightPreferences.ini"));
                    Saved.GetDouble(TEXT("Mouse"),TEXT("Gain0"),SavedRoll);
                    Saved.GetDouble(TEXT("Controls"),TEXT("Expo"),SavedExpo);
                    const bool Pass=bDesktopTestPass && !IsFlightSettingsOpen() && Player && !Player->IsPaused() &&
                                    !Player->bShowMouseCursor && FMath::Abs(SavedRoll+.6)<.001 && FMath::Abs(SavedExpo-.35)<.001;
                    UE_LOG(LogTemp,Display,TEXT("DesktopInputTest %s: signed gains, model selection, Slate pause/Escape/resume and saved preferences"),Pass ? TEXT("PASS") : TEXT("FAIL"));
                    FPlatformMisc::RequestExitWithStatus(false,Pass ? 0 : 1);
                    return false;
                }),3.f);
                return;
            }
            UE_LOG(LogTemp, Display, TEXT("DesktopInputTest %s: Unreal input, throttle memory, sticky mouse axes, click reset, additive keyboard, button mutes"),
                   bDesktopTestPass ? TEXT("PASS") : TEXT("FAIL"));
            FPlatformMisc::RequestExitWithStatus(false,bDesktopTestPass ? 0 : 1);
        }
    }
    if ((Stage >= 10 && Stage < 15) || Stage == 17)
    {
        Axis(EKeys::MouseX,born2flap::DesktopInput::MouseTravel * DeltaSeconds);
        Axis(EKeys::MouseY,born2flap::DesktopInput::MouseTravel * .5 * DeltaSeconds);
    }
    if (Stage == 22 || Stage == 24)
    {
        const float Direction = Stage == 22 ? -1.f : 1.f;
        Axis(EKeys::MouseX,Direction * born2flap::DesktopInput::MouseTravel * DeltaSeconds);
        Axis(EKeys::MouseY,Direction * born2flap::DesktopInput::MouseTravel * DeltaSeconds);
    }
    if (Stage == 25) Axis(EKeys::MouseY,-born2flap::DesktopInput::MouseTravel * DeltaSeconds);
    if (Stage == 26 || Stage == 28)
    {
        Axis(EKeys::MouseX,born2flap::DesktopInput::MouseTravel * DeltaSeconds);
        Axis(EKeys::MouseY,born2flap::DesktopInput::MouseTravel * DeltaSeconds);
    }
    DesktopTestTime += DeltaSeconds;
}