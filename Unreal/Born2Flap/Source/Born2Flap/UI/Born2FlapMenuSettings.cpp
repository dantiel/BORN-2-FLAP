#include "UI/Born2FlapMenuSettings.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Avatar/Born2FlapAvatar.h"
#include "Flight/Born2FlapTuning.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"

namespace
{
FString PreferencesPath()
{
    return FPaths::ProjectSavedDir() / (FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest"))
        ? TEXT("Automation/FlightPreferences.ini") : TEXT("Config/FlightPreferences.ini"));
}
}

namespace born2flap
{

float MenuSettingsTuningClamp(ETuningField Field, float Value)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:        return FMath::Clamp(Value, 100.f, 2400.f);
    case ETuningField::StallTorque:       return FMath::Clamp(Value, 0.05f, 20.f);
    case ETuningField::Backdrive:         return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::BatteryVoltage:    return FMath::Clamp(Value, 3.7f, 22.2f);
    case ETuningField::BatteryResistance: return FMath::Clamp(Value, 0.01f, 0.5f);
    case ETuningField::BatteryCapacity:   return FMath::Clamp(Value, 0.1f, 5.f);
    case ETuningField::BatteryCRating:    return FMath::Clamp(Value, 1.f, 200.f);
    case ETuningField::BatteryBurstCRating: return FMath::Clamp(Value, 1.f, 400.f);
    case ETuningField::FlapBaseFreq:      return FMath::Clamp(Value, 10.f, 200.f);
    case ETuningField::TailElevatorAngle: return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::GlideAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::StrokeFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::AileronScale:      return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::ElevatorScale:     return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::FlapCentre:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::VerticalMountAngle: return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::ReturnFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::YawScale:          return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::StrokeSkew:        return FMath::Clamp(Value, -100.f, 100.f);
    case ETuningField::ReturnSkew:        return FMath::Clamp(Value, -100.f, 100.f);
    default: return 0.f;
    }
}

float MenuSettingsGetTuning(const FMenuSettings& S, ETuningField Field)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:        return float(S.Tuning.servo_no_load_speed_deg_s);
    case ETuningField::StallTorque:       return float(S.Tuning.servo_stall_torque_nm);
    case ETuningField::Backdrive:         return float(S.Tuning.servo_backdrive_deg_s_nm);
    case ETuningField::BatteryVoltage:    return float(S.Tuning.battery_voltage);
    case ETuningField::BatteryResistance: return float(S.Tuning.battery_resistance_ohm);
    case ETuningField::BatteryCapacity:   return float(S.Tuning.battery_capacity_ah);
    case ETuningField::BatteryCRating:    return float(S.Tuning.battery_c_rating);
    case ETuningField::BatteryBurstCRating: return float(S.Tuning.battery_burst_c_rating);
    case ETuningField::FlapBaseFreq:      return float(S.Tuning.flap_base_freq_dhz);
    case ETuningField::TailElevatorAngle: return float(S.Tuning.tail_elevator_angle_deg);
    case ETuningField::GlideAngle:        return float(S.Tuning.glide_angle_deg);
    case ETuningField::StrokeFerocity:    return float(S.Tuning.stroke_ferocity);
    case ETuningField::AileronScale:      return float(S.Tuning.aileron_scale);
    case ETuningField::ElevatorScale:     return float(S.Tuning.elevator_scale);
    case ETuningField::FlapCentre:        return float(S.Tuning.flap_centre_deg);
    case ETuningField::VerticalMountAngle: return float(S.Tuning.vertical_mount_angle_deg);
    case ETuningField::ReturnFerocity:    return float(S.Tuning.return_ferocity);
    case ETuningField::YawScale:          return float(S.Tuning.yaw_scale);
    case ETuningField::StrokeSkew:        return float(S.Tuning.stroke_skew_percent);
    case ETuningField::ReturnSkew:        return float(S.Tuning.return_skew_percent);
    default: return 0.f;
    }
}

void MenuSettingsSetTuning(FMenuSettings& S, ETuningField Field, float Value)
{
    if (!FMath::IsFinite(Value)) return;
    Value = MenuSettingsTuningClamp(Field, Value);
    switch (Field)
    {
    case ETuningField::ServoSpeed:        S.Tuning.servo_no_load_speed_deg_s = Value; break;
    case ETuningField::StallTorque:       S.Tuning.servo_stall_torque_nm = Value; break;
    case ETuningField::Backdrive:         S.Tuning.servo_backdrive_deg_s_nm = Value; break;
    case ETuningField::BatteryVoltage:    S.Tuning.battery_voltage = Value; break;
    case ETuningField::BatteryResistance: S.Tuning.battery_resistance_ohm = Value; break;
    case ETuningField::BatteryCapacity:   S.Tuning.battery_capacity_ah = Value; break;
    case ETuningField::BatteryCRating:    S.Tuning.battery_c_rating = Value; break;
    case ETuningField::BatteryBurstCRating: S.Tuning.battery_burst_c_rating = FMath::Max(Value, S.Tuning.battery_c_rating); break;
    case ETuningField::FlapBaseFreq:      S.Tuning.flap_base_freq_dhz = Value; break;
    case ETuningField::TailElevatorAngle: S.Tuning.tail_elevator_angle_deg = Value; break;
    case ETuningField::GlideAngle:        S.Tuning.glide_angle_deg = Value; break;
    case ETuningField::StrokeFerocity:    S.Tuning.stroke_ferocity = Value; break;
    case ETuningField::AileronScale:      S.Tuning.aileron_scale = Value; break;
    case ETuningField::ElevatorScale:     S.Tuning.elevator_scale = Value; break;
    case ETuningField::FlapCentre:        S.Tuning.flap_centre_deg = Value; break;
    case ETuningField::VerticalMountAngle: S.Tuning.vertical_mount_angle_deg = Value; break;
    case ETuningField::ReturnFerocity:    S.Tuning.return_ferocity = Value; break;
    case ETuningField::YawScale:          S.Tuning.yaw_scale = Value; break;
    case ETuningField::StrokeSkew:        S.Tuning.stroke_skew_percent = Value; break;
    case ETuningField::ReturnSkew:        S.Tuning.return_skew_percent = Value; break;
    default: return;
    }
}

FString MenuSettingsModelTuningSection(int32 Model)
{
    return FString::Printf(TEXT("Tuning.Model%d"), FMath::Clamp(Model, 0, 5));
}

void MenuSettingsResetTuningForModel(FMenuSettings& S, int32 Model)
{
    S.Tuning = born2flap::tuning::DefaultTuningForModel(Model);
    S.BodyMassKg = born2flap::tuning::DefaultBodyMassForModel(Model);
    S.CgOffsetMm = born2flap::tuning::DefaultCgOffsetForModel(Model);
}

void MenuSettingsLoadModelProfile(FMenuSettings& S, int32 Model)
{
    if (Model < 0 || Model > 5)
        return;
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    const FString Section = MenuSettingsModelTuningSection(Model);
    if (Config.Contains(Section))
    {
        for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
        {
            const ETuningField Field = ETuningField(I);
            float Value = MenuSettingsGetTuning(S, Field);
            Config.GetFloat(*Section, born2flap::tuning::Key(Field), Value);
            MenuSettingsSetTuning(S, Field, Value);
        }
        float ModelMass = S.BodyMassKg;
        Config.GetFloat(*Section, TEXT("BodyMassKg"), ModelMass);
        S.BodyMassKg = FMath::IsFinite(ModelMass) ? FMath::Clamp(ModelMass, 0.01f, 2.0f) : S.BodyMassKg;
        float ModelCg = S.CgOffsetMm;
        Config.GetFloat(*Section, TEXT("CgOffsetMm"), ModelCg);
        S.CgOffsetMm = FMath::IsFinite(ModelCg) ? FMath::Clamp(ModelCg, -300.f, 300.f) : S.CgOffsetMm;
    }
    else
    {
        S.Tuning = born2flap::tuning::DefaultTuningForModel(Model);
        S.BodyMassKg = born2flap::tuning::DefaultBodyMassForModel(Model);
        S.CgOffsetMm = born2flap::tuning::DefaultCgOffsetForModel(Model);
    }
}

void MenuSettingsLoad(FMenuSettings& Out)
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;

    float Expo = Out.ControlExpo;
    Config.GetFloat(TEXT("Controls"), TEXT("Expo"), Expo);
    Out.ControlExpo = FMath::IsFinite(Expo) ? FMath::Clamp(Expo, 0.f, 1.f) : 0.65f;

    Config.GetBool(TEXT("Flight"), TEXT("FpvAirView"), Out.bFpvAirView);
    float Cam = Out.FpvCameraAngleDeg;
    Config.GetFloat(TEXT("Flight"), TEXT("FpvCameraAngle"), Cam);
    Out.FpvCameraAngleDeg = FMath::IsFinite(Cam) ? FMath::Clamp(Cam, -45.f, 45.f) : 0.f;
    Config.GetBool(TEXT("Flight"), TEXT("FreeLookInvert"), Out.bFreeLookInvert);
    Config.GetBool(TEXT("Mobile"), TEXT("InvertPitch"), Out.bMobileInvertPitch);
    int32 ThrottleStick = Out.MobileThrottleStick;
    Config.GetInt(TEXT("Mobile"), TEXT("ThrottleStick"), ThrottleStick);
    Out.MobileThrottleStick = (ThrottleStick == 0 || ThrottleStick == 1) ? ThrottleStick : 0;
    int32 RollStick = Out.MobileRollStick;
    Config.GetInt(TEXT("Mobile"), TEXT("RollStick"), RollStick);
    Out.MobileRollStick = (RollStick == 0 || RollStick == 1) ? RollStick : 1;
    Config.GetBool(TEXT("Flight"), TEXT("CoupledThrottle"), Out.bCoupledThrottle);

    float Mass = Out.BodyMassKg;
    Config.GetFloat(TEXT("Flight"), TEXT("BodyMassKg"), Mass);
    Out.BodyMassKg = FMath::IsFinite(Mass) ? FMath::Clamp(Mass, 0.01f, 2.0f) : 0.45f;

    float Cg = Out.CgOffsetMm;
    Config.GetFloat(TEXT("Flight"), TEXT("CgOffsetMm"), Cg);
    // Broad sanity bound only; SetCgOffsetMm re-clamps to the model's
    // length-derived range when applied to the pawn.
    Out.CgOffsetMm = FMath::IsFinite(Cg) ? FMath::Clamp(Cg, -300.f, 300.f) : 0.f;

    Config.GetBool(TEXT("Flight"), TEXT("ReplaySpirits"), Out.bReplaySpirits);
    float Safety = Out.FlightSafety;
    Config.GetFloat(TEXT("Flight"), TEXT("Safety"), Safety);
    Out.FlightSafety = FMath::IsFinite(Safety) ? FMath::Clamp(Safety, 0.f, 2.f) : 1.f;
    float Wing = Out.WingbeatVolume;
    Config.GetFloat(TEXT("Audio"), TEXT("WingbeatVolume"), Wing);
    Out.WingbeatVolume = FMath::IsFinite(Wing) ? FMath::Clamp(Wing, 0.f, 4.f) : 1.f;

    double SpeedMod = Out.SpeedModifier;
    Config.GetDouble(TEXT("Controls"), TEXT("SpeedModifier"), SpeedMod);
    Out.SpeedModifier = FMath::IsFinite(SpeedMod) ? FMath::Clamp((float)SpeedMod, 0.f, 1.f) : 0.5f;

    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        double Gain = Out.MouseGains[Axis];
        Config.GetDouble(TEXT("Mouse"), *FString::Printf(TEXT("Gain%d"), Axis), Gain);
        Out.MouseGains[Axis] = FMath::IsFinite(Gain) ? FMath::Clamp((float)Gain, -2.f, 2.f) : (Axis == 1 ? -1.f : 1.f);
    }

    for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
    {
        const ETuningField Field = ETuningField(I);
        float Value = MenuSettingsGetTuning(Out, Field);
        Config.GetFloat(TEXT("Tuning"), born2flap::tuning::Key(Field), Value);
        MenuSettingsSetTuning(Out, Field, Value);
    }

    // Bird model: only persist an explicit choice. -1 means "level default".
    int32 Model = -1;
    Config.GetInt(TEXT("Flight"), TEXT("BirdModel"), Model);
    Out.BirdModel = (Model >= 0 && Model <= 5) ? Model : -1;

    // Per-model profile: a saved override wins; otherwise this model starts from
    // its factory defaults (not another model's edits).
    if (Out.BirdModel >= 0)
    {
        const FString Section = MenuSettingsModelTuningSection(Out.BirdModel);
        if (Config.Contains(Section))
        {
            for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
            {
                const ETuningField Field = ETuningField(I);
                float Value = MenuSettingsGetTuning(Out, Field);
                Config.GetFloat(*Section, born2flap::tuning::Key(Field), Value);
                MenuSettingsSetTuning(Out, Field, Value);
            }
            float ModelMass = Out.BodyMassKg;
            Config.GetFloat(*Section, TEXT("BodyMassKg"), ModelMass);
            Out.BodyMassKg = FMath::IsFinite(ModelMass) ? FMath::Clamp(ModelMass, 0.01f, 2.0f) : Out.BodyMassKg;
            float ModelCg = Out.CgOffsetMm;
            Config.GetFloat(*Section, TEXT("CgOffsetMm"), ModelCg);
            Out.CgOffsetMm = FMath::IsFinite(ModelCg) ? FMath::Clamp(ModelCg, -300.f, 300.f) : Out.CgOffsetMm;
        }
        else
        {
            Out.Tuning = born2flap::tuning::DefaultTuningForModel(Out.BirdModel);
            Out.BodyMassKg = born2flap::tuning::DefaultBodyMassForModel(Out.BirdModel);
            Out.CgOffsetMm = born2flap::tuning::DefaultCgOffsetForModel(Out.BirdModel);
        }
    }

    bool AvatarEnabled = Out.bAvatarEnabled;
    Config.GetBool(TEXT("Avatar"), TEXT("Enabled"), AvatarEnabled);
    Out.bAvatarEnabled = AvatarEnabled;

    int32 Person = Out.AvatarPerson;
    Config.GetInt(TEXT("Avatar"), TEXT("Person"), Person);
    Out.AvatarPerson = FMath::Clamp(Person, 0, ABorn2FlapAvatar::GetPersonCount() - 1);

    FString Lang = Out.Language;
    Config.GetString(TEXT("General"), TEXT("Language"), Lang);
    if (!Lang.IsEmpty())
        Out.Language = Lang;

    float Scale = Out.UIScale;
    Config.GetFloat(TEXT("General"), TEXT("UIScale"), Scale);
    Out.UIScale = FMath::IsFinite(Scale) ? FMath::Clamp(Scale, 0.5f, 2.0f) : 1.0f;

    int32 WM = Out.WindowMode;
    Config.GetInt(TEXT("Display"), TEXT("WindowMode"), WM);
    Out.WindowMode = (WM >= 0 && WM <= 2) ? WM : 1;
    Config.GetBool(TEXT("Display"), TEXT("FollowSystemResolution"), Out.bFollowSystemResolution);
    int32 ResX = Out.Resolution.X;
    int32 ResY = Out.Resolution.Y;
    Config.GetInt(TEXT("Display"), TEXT("ResolutionX"), ResX);
    Config.GetInt(TEXT("Display"), TEXT("ResolutionY"), ResY);
    Out.Resolution = FIntPoint(FMath::Max(0, ResX), FMath::Max(0, ResY));
}

void MenuSettingsSave(const FMenuSettings& In)
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

    Config.SetFloat(TEXT("Controls"), TEXT("Expo"), In.ControlExpo);
    Config.SetBool(TEXT("Flight"), TEXT("FpvAirView"), In.bFpvAirView);
    Config.SetFloat(TEXT("Flight"), TEXT("FpvCameraAngle"), In.FpvCameraAngleDeg);
    Config.SetBool(TEXT("Flight"), TEXT("FreeLookInvert"), In.bFreeLookInvert);
    Config.SetBool(TEXT("Mobile"), TEXT("InvertPitch"), In.bMobileInvertPitch);
    Config.SetInt64(TEXT("Mobile"), TEXT("ThrottleStick"), In.MobileThrottleStick);
    Config.SetInt64(TEXT("Mobile"), TEXT("RollStick"), In.MobileRollStick);
    Config.SetBool(TEXT("Flight"), TEXT("CoupledThrottle"), In.bCoupledThrottle);
    Config.SetFloat(TEXT("Flight"), TEXT("BodyMassKg"), In.BodyMassKg);
    Config.SetFloat(TEXT("Flight"), TEXT("CgOffsetMm"), In.CgOffsetMm);
    Config.SetBool(TEXT("Flight"), TEXT("ReplaySpirits"), In.bReplaySpirits);
    Config.SetFloat(TEXT("Flight"), TEXT("Safety"), In.FlightSafety);
    Config.SetFloat(TEXT("Audio"), TEXT("WingbeatVolume"), In.WingbeatVolume);
    Config.SetDouble(TEXT("Controls"), TEXT("SpeedModifier"), In.SpeedModifier);
    for (int32 Axis = 0; Axis < 3; ++Axis)
        Config.SetDouble(TEXT("Mouse"), *FString::Printf(TEXT("Gain%d"), Axis), In.MouseGains[Axis]);
    for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
    {
        const ETuningField Field = ETuningField(I);
        Config.SetFloat(TEXT("Tuning"), born2flap::tuning::Key(Field), MenuSettingsGetTuning(In, Field));
    }
    if (In.BirdModel >= 0 && In.BirdModel <= 5)
    {
        const FString Section = MenuSettingsModelTuningSection(In.BirdModel);
        for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
        {
            const ETuningField Field = ETuningField(I);
            Config.SetFloat(*Section, born2flap::tuning::Key(Field), MenuSettingsGetTuning(In, Field));
        }
        Config.SetFloat(*Section, TEXT("BodyMassKg"), In.BodyMassKg);
        Config.SetFloat(*Section, TEXT("CgOffsetMm"), In.CgOffsetMm);
    }
    if (In.BirdModel >= 0 && In.BirdModel <= 5)
        Config.SetInt64(TEXT("Flight"), TEXT("BirdModel"), In.BirdModel);
    Config.SetBool(TEXT("Avatar"), TEXT("Enabled"), In.bAvatarEnabled);
    Config.SetInt64(TEXT("Avatar"), TEXT("Person"), In.AvatarPerson);
    Config.SetString(TEXT("General"), TEXT("Language"), *In.Language);
    Config.SetFloat(TEXT("General"), TEXT("UIScale"), In.UIScale);
    Config.SetInt64(TEXT("Display"), TEXT("WindowMode"), In.WindowMode);
    Config.SetBool(TEXT("Display"), TEXT("FollowSystemResolution"), In.bFollowSystemResolution);
    Config.SetInt64(TEXT("Display"), TEXT("ResolutionX"), In.Resolution.X);
    Config.SetInt64(TEXT("Display"), TEXT("ResolutionY"), In.Resolution.Y);

    Config.Write(Path);
}

void MenuSettingsFromPawn(const ABorn2FlapFlightPawn* Pawn, FMenuSettings& Out)
{
    if (!Pawn)
        return;
    for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
        MenuSettingsSetTuning(Out, ETuningField(I), Pawn->GetTuning(ETuningField(I)));
    Out.BirdModel = Pawn->GetBirdModel();
    Out.bAvatarEnabled = Pawn->IsAvatarEnabled();
    Out.AvatarPerson = Pawn->GetAvatarPerson();
    Out.BodyMassKg = Pawn->GetBodyMassKg();
    Out.CgOffsetMm = Pawn->GetCgOffsetMm();
    Out.bCoupledThrottle = Pawn->IsThrottleCoupled();
    Out.bFpvAirView = Pawn->IsFpvAirView();
    Out.FpvCameraAngleDeg = Pawn->GetFpvCameraAngle();
    Out.bFreeLookInvert = Pawn->GetFreeLookInvert();
    Out.bMobileInvertPitch = Pawn->GetMobileInvertPitch();
    Out.MobileThrottleStick = Pawn->GetMobileThrottleStick();
    Out.MobileRollStick = Pawn->GetMobileRollStick();
    Out.MouseGains = Pawn->GetMouseGains();
    Out.ControlExpo = Pawn->GetControlExpo();
    Out.SpeedModifier = Pawn->GetSpeedModifier();
    Out.bReplaySpirits = Pawn->GetReplaySpiritsEnabled();
    Out.FlightSafety = Pawn->GetFlightSafety();
    Out.WingbeatVolume = Pawn->GetWingbeatVolume();
}

void MenuSettingsToPawn(const FMenuSettings& In, ABorn2FlapFlightPawn* Pawn)
{
    if (!Pawn)
        return;
    for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
        Pawn->SetTuning(ETuningField(I), MenuSettingsGetTuning(In, ETuningField(I)));
    // Select the model first so the CG offset is clamped to the correct
    // length-derived range (a larger craft allows a wider CG travel).
    if (In.BirdModel >= 0 && In.BirdModel <= 5)
        Pawn->SelectBirdModel(In.BirdModel);
    Pawn->SetAvatarEnabled(In.bAvatarEnabled);
    Pawn->SetAvatarPerson(In.AvatarPerson);
    Pawn->SetBodyMassKg(In.BodyMassKg);
    Pawn->SetCgOffsetMm(In.CgOffsetMm);
    Pawn->SetThrottleCoupled(In.bCoupledThrottle);
    if (In.bFpvAirView != Pawn->IsFpvAirView())
        Pawn->ToggleFpvView();
    Pawn->SetFpvCameraAngle(In.FpvCameraAngleDeg);
    Pawn->SetFreeLookInvert(In.bFreeLookInvert);
    Pawn->SetMobileInvertPitch(In.bMobileInvertPitch);
    Pawn->SetMobileThrottleStick(In.MobileThrottleStick);
    Pawn->SetMobileRollStick(In.MobileRollStick);
    for (int32 Axis = 0; Axis < 3; ++Axis)
        Pawn->SetMouseGain(Axis, In.MouseGains[Axis]);
    Pawn->SetControlExpo(In.ControlExpo);
    Pawn->SetSpeedModifier(In.SpeedModifier);
    Pawn->SetReplaySpiritsEnabled(In.bReplaySpirits);
    Pawn->SetFlightSafety(In.FlightSafety);
    Pawn->SetWingbeatVolume(In.WingbeatVolume);
}

void ApplyDisplaySettings(const FMenuSettings& Settings)
{
    UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    if (!GUS)
        return;

    EWindowMode::Type Mode = EWindowMode::WindowedFullscreen;  // 1 = borderless (default)
    if (Settings.WindowMode == 0)
        Mode = EWindowMode::Fullscreen;
    else if (Settings.WindowMode == 2)
        Mode = EWindowMode::Windowed;
    GUS->SetFullscreenMode(Mode);

    if (Settings.bFollowSystemResolution)
    {
        const FIntPoint Desktop = GUS->GetDesktopResolution();
        if (Desktop.X > 0 && Desktop.Y > 0)
            GUS->SetScreenResolution(Desktop);
    }
    else if (Settings.Resolution.X > 0 && Settings.Resolution.Y > 0)
    {
        GUS->SetScreenResolution(Settings.Resolution);
    }

    GUS->ApplyResolutionSettings(false);
    GUS->SaveSettings();
}

}  // namespace born2flap