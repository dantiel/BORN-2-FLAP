#include "Input/Born2FlapRcController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/SWindow.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "born2flap_rc_windows.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

struct FBorn2FlapRcPlatform
{
#if PLATFORM_WINDOWS
    born2flap::RcWindowsDevices Devices;
#endif
};
namespace
{
const TCHAR *ChannelNames[] = {TEXT("GAS"), TEXT("ROLL"), TEXT("PITCH"), TEXT("YAW")};
}
FBorn2FlapRcController::FBorn2FlapRcController()
{
    Platform = MakeUnique<FBorn2FlapRcPlatform>();
    ConfigPath = FPaths::ProjectSavedDir() / TEXT("Config/RcControllers.ini");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
    GConfig->GetString(TEXT("Selection"), TEXT("Device"), DeviceId, ConfigPath);
    GConfig->GetBool(TEXT("Selection"), TEXT("Enabled"), bEnabled, ConfigPath);
    LoadCalibration();
}
FBorn2FlapRcController::~FBorn2FlapRcController() = default;
void FBorn2FlapRcController::LoadCalibration()
{
    Calibration = {};
    LaunchButton = ResetButton = -1;
    for (int32 I = 0; I < 4; ++I)
    {
        auto &C = Calibration.channels[I];
        const FString Prefix = FString::Printf(TEXT("Channel%d"), I);
        GConfig->GetInt(*DeviceId, *(Prefix + TEXT("Axis")), C.axis, ConfigPath);
        GConfig->GetDouble(*DeviceId, *(Prefix + TEXT("Low")), C.low, ConfigPath);
        GConfig->GetDouble(*DeviceId, *(Prefix + TEXT("Centre")), C.centre, ConfigPath);
        GConfig->GetDouble(*DeviceId, *(Prefix + TEXT("High")), C.high, ConfigPath);
        GConfig->GetBool(*DeviceId, *(Prefix + TEXT("Invert")), C.inverted, ConfigPath);
    }
    GConfig->GetInt(*DeviceId, TEXT("LaunchButton"), LaunchButton, ConfigPath);
    GConfig->GetInt(*DeviceId, TEXT("ResetButton"), ResetButton, ConfigPath);
    if (LaunchButton < -1 || LaunchButton >= 128)
        LaunchButton = -1;
    if (ResetButton < -1 || ResetButton >= 128)
        ResetButton = -1;
    Gate.armed = false;
}
void FBorn2FlapRcController::SaveCalibration()
{
    if (!Calibration.Valid())
        return;
    GConfig->SetString(TEXT("Selection"), TEXT("Device"), *DeviceId, ConfigPath);
    GConfig->SetBool(TEXT("Selection"), TEXT("Enabled"), bEnabled, ConfigPath);
    for (int32 I = 0; I < 4; ++I)
    {
        const auto &C = Calibration.channels[I];
        const FString Prefix = FString::Printf(TEXT("Channel%d"), I);
        GConfig->SetInt(*DeviceId, *(Prefix + TEXT("Axis")), C.axis, ConfigPath);
        GConfig->SetDouble(*DeviceId, *(Prefix + TEXT("Low")), C.low, ConfigPath);
        GConfig->SetDouble(*DeviceId, *(Prefix + TEXT("Centre")), C.centre, ConfigPath);
        GConfig->SetDouble(*DeviceId, *(Prefix + TEXT("High")), C.high, ConfigPath);
        GConfig->SetBool(*DeviceId, *(Prefix + TEXT("Invert")), C.inverted, ConfigPath);
    }
    GConfig->SetInt(*DeviceId, TEXT("LaunchButton"), LaunchButton, ConfigPath);
    GConfig->SetInt(*DeviceId, TEXT("ResetButton"), ResetButton, ConfigPath);
    GConfig->Flush(false, ConfigPath);
    UE_LOG(LogTemp, Display, TEXT("RcCalibration saved device=%s enabled=%d"), *DeviceName, bEnabled);
}
void FBorn2FlapRcController::SelectDevice(int32 Index)
{
#if PLATFORM_WINDOWS
    auto &D = Platform->Devices;
    if (Index < 0 || Index >= int32(D.devices.size()) || !GEngine || !GEngine->GameViewport)
        return;
    auto Window = GEngine->GameViewport->GetWindow();
    if (!Window.IsValid() || !Window->GetNativeWindow().IsValid())
        return;
    const auto Info = D.devices[Index];
    DeviceId = Info.id.c_str();
    DeviceName = Info.name.c_str();
    bConnected = D.Open(Info, static_cast<HWND>(Window->GetNativeWindow()->GetOSWindowHandle()));
    Available = D.available;
    PreviousButtons = D.buttons;
    Stage = -1;
    LearnButton = 0;
    LoadCalibration();
    UE_LOG(LogTemp, Display, TEXT("RcDevice selected=%s connected=%d id=%s"), *DeviceName, bConnected, *DeviceId);
#endif
}
void FBorn2FlapRcController::AdvanceCalibration()
{
    if (!bConnected)
        return;
    Notice.Empty();
    if (Stage == 0)
    {
        Rest = Low = High = RawAxes;
        Stage = 1;
    }
    else if (Stage == 1)
    {
        int32 Count = 0;
        for (int32 I = 0; I < 8; ++I)
            Count += Available[I] && High[I] - Low[I] >= .2;
        if (Count < 4)
            Notice = TEXT("Mindestens vier Achsen voll bewegen; dann ENTER.");
        else
            Stage = 2;
    }
    else if (Stage >= 2 && Stage <= 5)
    {
        int32 Best = -1;
        double Peak = .3, Second = 0;
        for (int32 Axis = 0; Axis < 8; ++Axis)
        {
            bool Used = false;
            for (int32 C = 0; C < Stage - 2; ++C)
                Used |= Calibration.channels[C].axis == Axis;
            if (Used || !Available[Axis] || High[Axis] - Low[Axis] < .2)
                continue;
            const double Delta = FMath::Abs(RawAxes[Axis] - Rest[Axis]) / (High[Axis] - Low[Axis]);
            if (Delta > Peak)
            {
                Second = Best >= 0 ? Peak : Second;
                Peak = Delta;
                Best = Axis;
            }
            else
                Second = FMath::Max(Second, Delta);
        }
        if (Best < 0 || Second > Peak * .65)
        {
            Notice = TEXT("Nur den angezeigten Kanal auslenken. Andere Knueppel neutral / Gas unten.");
            return;
        }
        auto &C = Calibration.channels[Stage - 2];
        C = {Best, Low[Best], Rest[Best], High[Best], RawAxes[Best] < Rest[Best], .025};
        if (!C.Valid(Stage == 2))
        {
            Notice = TEXT("Neutralpunkt ungueltig. X, dann C fuer eine neue Kalibrierung.");
            return;
        }
        ++Stage;
        if (Stage == 6 && Calibration.Valid())
        {
            Stage = -1;
            bEnabled = true;
            SaveCalibration();
            Notice = TEXT("Gespeichert. Gas auf Minimum, dann F3 schliessen und SPACE starten.");
        }
    }
}
void FBorn2FlapRcController::Tick(APlayerController *Player, float Dt)
{
    bLaunch = bReset = false;
    if (!Player)
        return;
    if (Player->WasInputKeyJustPressed(EKeys::F3))
    {
        bPanel = !bPanel;
        Gate.armed = false;
    }
#if PLATFORM_WINDOWS
    auto &D = Platform->Devices;
    const bool WasConnected = bConnected;
    bConnected = D.Poll();
    ScanTime += Dt;
    if (ScanTime > 2)
    {
        ScanTime = 0;
        D.Scan();
        if (!bConnected)
            for (int32 I = 0; I < int32(D.devices.size()); ++I)
                if (DeviceId == D.devices[I].id.c_str() || (DeviceId.IsEmpty() && I == 0))
                {
                    SelectDevice(I);
                    break;
                }
    }
    if (bConnected)
    {
        RawAxes = D.axes;
        Available = D.available;
        if (!WasConnected)
            PreviousButtons = D.buttons;
    }
    if (bPanel)
    {
        if (Player->WasInputKeyJustPressed(EKeys::Tab) && !D.devices.empty())
        {
            int32 Next = 0;
            for (int32 I = 0; I < int32(D.devices.size()); ++I)
                if (DeviceId == D.devices[I].id.c_str())
                    Next = (I + 1) % int32(D.devices.size());
            bEnabled = false;
            SelectDevice(Next);
            Notice.Empty();
        }
        if (Player->WasInputKeyJustPressed(EKeys::C) && bConnected)
        {
            Stage = 0;
            Calibration = {};
            Gate.armed = false;
            LearnButton = 0;
            Notice.Empty();
        }
        if (Player->WasInputKeyJustPressed(EKeys::X))
        {
            Stage = -1;
            LearnButton = 0;
            LoadCalibration();
            Notice.Empty();
        }
        if (Player->WasInputKeyJustPressed(EKeys::Enter))
            AdvanceCalibration();
        if (Player->WasInputKeyJustPressed(EKeys::G) && Stage < 0)
        {
            if (Calibration.Valid())
            {
                bEnabled = !bEnabled;
                SaveCalibration();
            }
            else
                Notice = TEXT("Zuerst mit C die vier Kanaele kalibrieren.");
        }
        if (Stage < 0 && Calibration.Valid())
        {
            if (Player->WasInputKeyJustPressed(EKeys::L))
                LearnButton = 1;
            if (Player->WasInputKeyJustPressed(EKeys::K))
                LearnButton = 2;
        }
    }
    if (Stage == 1 && bConnected)
        for (int32 I = 0; I < 8; ++I)
        {
            Low[I] = FMath::Min(Low[I], RawAxes[I]);
            High[I] = FMath::Max(High[I], RawAxes[I]);
        }
    bool MappedAxesPresent = Calibration.Valid();
    if (MappedAxesPresent)
        for (const auto &C : Calibration.channels)
            MappedAxesPresent &= Available[C.axis];
    Channels = Gate.Filter(bEnabled && bConnected && MappedAxesPresent && !bPanel && Stage < 0 && FApp::HasFocus(),
                           Calibration.Map(RawAxes));
    if (bConnected)
        for (int32 I = 0; I < 128; ++I)
        {
            if (!D.buttons[I] || PreviousButtons[I])
                continue;
            if (bPanel && LearnButton)
            {
                if ((LearnButton == 1 && I == ResetButton) || (LearnButton == 2 && I == LaunchButton))
                    Notice = TEXT("Dieser Taster ist schon belegt. Einen anderen Taster druecken.");
                else
                {
                    (LearnButton == 1 ? LaunchButton : ResetButton) = I;
                    LearnButton = 0;
                    SaveCalibration();
                    Notice = TEXT("Taster gespeichert.");
                }
            }
            else if (Gate.armed && !bPanel)
            {
                bLaunch |= I == LaunchButton;
                bReset |= I == ResetButton;
            }
        }
    PreviousButtons = D.buttons;
    if (WasConnected && !bConnected)
        UE_LOG(LogTemp, Warning, TEXT("RcDevice lost: throttle and sticks neutral; reconnect requires low throttle"));
#else
    Notice = TEXT("USB-Sender derzeit unter Windows verfuegbar. Tastatur bleibt aktiv.");
#endif
}
FString FBorn2FlapRcController::GetStatus() const
{
    if (!bEnabled)
        return TEXT("TASTATUR  |  F3: RC-Sender");
    if (!bConnected)
        return TEXT("RC VERBINDUNG FEHLT  |  Gas aus");
    if (!Calibration.Valid())
        return TEXT("RC KALIBRIEREN  |  F3, dann C");
    if (!Gate.armed)
        return TEXT("RC BEREIT  |  Gas auf Minimum, F3 schliessen");
    return TEXT("RC AKTIV  |  ") + DeviceName;
}
FString FBorn2FlapRcController::GetInstruction() const
{
    if (LearnButton)
        return LearnButton == 1 ? TEXT("Jetzt den Sender-Taster fuer HANDSTART druecken.")
                                : TEXT("Jetzt den Sender-Taster fuer RESET druecken.");
    if (Stage == 0)
        return TEXT("1/6  Gas ganz unten, alle anderen Knueppel neutral. ENTER.");
    if (Stage == 1)
        return TEXT("2/6  Alle vier Knueppel bis an beide Anschlaege bewegen. Danach ENTER.");
    if (Stage >= 2)
    {
        const TCHAR *Actions[] = {TEXT("Gas auf VOLL"), TEXT("Roll nach RECHTS"),
                                  TEXT("Hoehenruder ZIEHEN (Nase hoch)"), TEXT("Seitenruder nach RECHTS")};
        return FString::Printf(TEXT("%d/6  Nur %s halten. ENTER."), Stage + 1, Actions[Stage - 2]);
    }
    return TEXT("USB-Sender im Joystick-Modus verbinden; TAB waehlt das Geraet, C kalibriert.");
}
FString FBorn2FlapRcController::GetMapping(int32 Channel) const
{
    const auto &C = Calibration.channels[Channel];
    return C.Valid(Channel == 0) ? FString::Printf(TEXT("%s: Achse %d%s"), ChannelNames[Channel], C.axis + 1,
                                                   C.inverted ? TEXT(" invertiert") : TEXT(""))
                                 : FString::Printf(TEXT("%s: nicht zugeordnet"), ChannelNames[Channel]);
}
FString FBorn2FlapRcController::GetButtons() const
{
    return FString::Printf(TEXT("Sender-Taster: START %s   RESET %s  |  L / K: Taster lernen"),
                           LaunchButton >= 0 ? *FString::FromInt(LaunchButton + 1) : TEXT("--"),
                           ResetButton >= 0 ? *FString::FromInt(ResetButton + 1) : TEXT("--"));
}
