#include "UI/Born2FlapMenuSettings.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Flight/Born2FlapTuning.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

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
    case ETuningField::FlapBaseFreq:      return FMath::Clamp(Value, 10.f, 200.f);
    case ETuningField::TailElevatorAngle: return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::GlideAngle:        return FMath::Clamp(Value, -15.f, 15.f);
    case ETuningField::StrokeFerocity:    return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::AileronScale:      return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::ElevatorScale:     return FMath::Clamp(Value, 0.f, 100.f);
    case ETuningField::MountAngle:        return FMath::Clamp(Value, -15.f, 15.f);
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
    case ETuningField::FlapBaseFreq:      return float(S.Tuning.flap_base_freq_dhz);
    case ETuningField::TailElevatorAngle: return float(S.Tuning.tail_elevator_angle_deg);
    case ETuningField::GlideAngle:        return float(S.Tuning.glide_angle_deg);
    case ETuningField::StrokeFerocity:    return float(S.Tuning.stroke_ferocity);
    case ETuningField::AileronScale:      return float(S.Tuning.aileron_scale);
    case ETuningField::ElevatorScale:     return float(S.Tuning.elevator_scale);
    case ETuningField::MountAngle:        return float(S.Tuning.mount_angle_deg);
    default: return 0.f;
    }
}

void MenuSettingsSetTuning(FMenuSettings& S, ETuningField Field, float Value)
{
    Value = MenuSettingsTuningClamp(Field, Value);
    switch (Field)
    {
    case ETuningField::ServoSpeed:        S.Tuning.servo_no_load_speed_deg_s = Value; break;
    case ETuningField::StallTorque:       S.Tuning.servo_stall_torque_nm = Value; break;
    case ETuningField::Backdrive:         S.Tuning.servo_backdrive_deg_s_nm = Value; break;
    case ETuningField::BatteryVoltage:    S.Tuning.battery_voltage = Value; break;
    case ETuningField::BatteryResistance: S.Tuning.battery_resistance_ohm = Value; break;
    case ETuningField::BatteryCapacity:   S.Tuning.battery_capacity_ah = Value; break;
    case ETuningField::FlapBaseFreq:      S.Tuning.flap_base_freq_dhz = Value; break;
    case ETuningField::TailElevatorAngle: S.Tuning.tail_elevator_angle_deg = Value; break;
    case ETuningField::GlideAngle:        S.Tuning.glide_angle_deg = Value; break;
    case ETuningField::StrokeFerocity:    S.Tuning.stroke_ferocity = Value; break;
    case ETuningField::AileronScale:      S.Tuning.aileron_scale = Value; break;
    case ETuningField::ElevatorScale:     S.Tuning.elevator_scale = Value; break;
    case ETuningField::MountAngle:        S.Tuning.mount_angle_deg = Value; break;
    default: return;
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
    Out.BirdModel = (Model >= 0 && Model <= 2) ? Model : -1;

    FString Lang = Out.Language;
    Config.GetString(TEXT("General"), TEXT("Language"), Lang);
    if (!Lang.IsEmpty())
        Out.Language = Lang;
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
    if (In.BirdModel >= 0 && In.BirdModel <= 2)
        Config.SetInt64(TEXT("Flight"), TEXT("BirdModel"), In.BirdModel);
    Config.SetString(TEXT("General"), TEXT("Language"), *In.Language);

    Config.Write(Path);
}

void MenuSettingsFromPawn(const ABorn2FlapFlightPawn* Pawn, FMenuSettings& Out)
{
    if (!Pawn)
        return;
    for (uint8 I = 0; I < uint8(ETuningField::Count); ++I)
        MenuSettingsSetTuning(Out, ETuningField(I), Pawn->GetTuning(ETuningField(I)));
    Out.BirdModel = Pawn->GetBirdModel();
    Out.BodyMassKg = Pawn->GetBodyMassKg();
    Out.CgOffsetMm = Pawn->GetCgOffsetMm();
    Out.bCoupledThrottle = Pawn->IsThrottleCoupled();
    Out.bFpvAirView = Pawn->IsFpvAirView();
    Out.FpvCameraAngleDeg = Pawn->GetFpvCameraAngle();
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
    if (In.BirdModel >= 0 && In.BirdModel <= 2)
        Pawn->SelectBirdModel(In.BirdModel);
    Pawn->SetBodyMassKg(In.BodyMassKg);
    Pawn->SetCgOffsetMm(In.CgOffsetMm);
    Pawn->SetThrottleCoupled(In.bCoupledThrottle);
    if (In.bFpvAirView != Pawn->IsFpvAirView())
        Pawn->ToggleFpvView();
    Pawn->SetFpvCameraAngle(In.FpvCameraAngleDeg);
    for (int32 Axis = 0; Axis < 3; ++Axis)
        Pawn->SetMouseGain(Axis, In.MouseGains[Axis]);
    Pawn->SetControlExpo(In.ControlExpo);
    Pawn->SetSpeedModifier(In.SpeedModifier);
    Pawn->SetReplaySpiritsEnabled(In.bReplaySpirits);
    Pawn->SetFlightSafety(In.FlightSafety);
    Pawn->SetWingbeatVolume(In.WingbeatVolume);
}

}  // namespace born2flap