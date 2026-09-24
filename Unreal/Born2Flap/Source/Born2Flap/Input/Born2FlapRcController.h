#pragma once
#include "CoreMinimal.h"
#include "born2flap_rc_calibration.h"

class APlayerController;
struct FBorn2FlapRcPlatform;

// Owns device discovery, per-device calibration and a transmitter-loss gate.
class FBorn2FlapRcController
{
  public:
    FBorn2FlapRcController();
    ~FBorn2FlapRcController();
    void Tick(APlayerController *Player, float Dt);
    bool IsPanelOpen() const { return bPanel; }
    bool IsEnabled() const { return bEnabled; }
    bool IsConnected() const { return bConnected; }
    bool IsArmed() const { return Gate.armed; }
    bool LaunchPressed() const { return bLaunch; }
    bool ResetPressed() const { return bReset; }
    const std::array<double, 4> &GetChannels() const { return Channels; }
    const std::array<double, 8> &GetRawAxes() const { return RawAxes; }
    const std::array<bool, 8> &GetAvailableAxes() const { return Available; }
    FString GetDeviceName() const { return DeviceName; }
    FString GetStatus() const;
    FString GetInstruction() const;
    FString GetMapping(int32 Channel) const;
    FString GetButtons() const;
    FString Notice;

  private:
    TUniquePtr<FBorn2FlapRcPlatform> Platform;
    born2flap::RcCalibration Calibration;
    born2flap::RcSignalGate Gate;
    std::array<double, 4> Channels{};
    std::array<double, 8> RawAxes{}, Rest{}, Low{}, High{};
    std::array<bool, 8> Available{};
    std::array<bool, 128> PreviousButtons{};
    FString DeviceId, DeviceName = TEXT("Kein Sender ausgewaehlt"), ConfigPath;
    float ScanTime = 3;
    int32 Stage = -1, LearnButton = 0, LaunchButton = -1, ResetButton = -1;
    bool bPanel = false, bEnabled = false, bConnected = false, bLaunch = false, bReset = false;
    void SelectDevice(int32 Index);
    void LoadCalibration();
    void SaveCalibration();
    void AdvanceCalibration();
};
