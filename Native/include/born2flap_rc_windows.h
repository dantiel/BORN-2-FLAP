#pragma once
// Small DirectInput backend shared by the game and the Windows device probe.
// No global hooks, emulated key presses, drivers or device reconfiguration.
#ifdef _WIN32
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <array>
#include <string>
#include <vector>

namespace born2flap
{
struct RcDeviceInfo
{
    GUID guid{};
    std::wstring id, name;
};
class RcWindowsDevices
{
    IDirectInput8W *input = nullptr;
    IDirectInputDevice8W *device = nullptr;
    static BOOL CALLBACK Enumerate(const DIDEVICEINSTANCEW *info, void *context)
    {
        auto *self = static_cast<RcWindowsDevices *>(context);
        wchar_t guid[40]{};
        StringFromGUID2(info->guidInstance, guid, 40);
        self->devices.push_back({info->guidInstance, guid, info->tszProductName});
        return DIENUM_CONTINUE;
    }
    static BOOL CALLBACK Axis(const DIDEVICEOBJECTINSTANCEW *info, void *context)
    {
        auto *self = static_cast<RcWindowsDevices *>(context);
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(range);
        range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        range.diph.dwObj = info->dwType;
        range.diph.dwHow = DIPH_BYID;
        range.lMin = -32768;
        range.lMax = 32767;
        if (FAILED(self->device->SetProperty(DIPROP_RANGE, &range.diph)))
            return DIENUM_CONTINUE;
        const GUID *types[] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis};
        for (int i = 0; i < 6; ++i)
            if (IsEqualGUID(info->guidType, *types[i]))
                self->available[i] = true;
        if (IsEqualGUID(info->guidType, GUID_Slider))
            self->available[self->available[6] ? 7 : 6] = true;
        return DIENUM_CONTINUE;
    }

  public:
    std::vector<RcDeviceInfo> devices;
    std::array<bool, 8> available{};
    std::array<double, 8> axes{};
    std::array<bool, 128> buttons{};
    RcWindowsDevices()
    {
        DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                           reinterpret_cast<void **>(&input), nullptr);
    }
    ~RcWindowsDevices()
    {
        Close();
        if (input)
            input->Release();
    }
    RcWindowsDevices(const RcWindowsDevices &) = delete;
    RcWindowsDevices &operator=(const RcWindowsDevices &) = delete;
    void Scan()
    {
        devices.clear();
        if (input)
            input->EnumDevices(DI8DEVCLASS_GAMECTRL, Enumerate, this, DIEDFL_ATTACHEDONLY);
    }
    void Close()
    {
        if (device)
        {
            device->Unacquire();
            device->Release();
            device = nullptr;
        }
        axes = {};
        buttons = {};
        available = {};
    }
    bool Open(const RcDeviceInfo &info, HWND window)
    {
        Close();
        if (!input || FAILED(input->CreateDevice(info.guid, &device, nullptr)))
            return false;
        if (FAILED(device->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(device->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)))
        {
            Close();
            return false;
        }
        device->EnumObjects(Axis, this, DIDFT_AXIS);
        device->Acquire();
        return Poll();
    }
    bool Poll()
    {
        if (!device)
            return false;
        if (FAILED(device->Poll()))
        {
            if (FAILED(device->Acquire()))
                return false;
            device->Poll();
        }
        DIJOYSTATE2 state{};
        if (FAILED(device->GetDeviceState(sizeof(state), &state)))
            return false;
        const LONG raw[] = {state.lX,  state.lY,  state.lZ,           state.lRx,
                            state.lRy, state.lRz, state.rglSlider[0], state.rglSlider[1]};
        for (int i = 0; i < 8; ++i)
            axes[i] = available[i] ? (raw[i] + 32768.0) / 65535.0 : .5;
        for (int i = 0; i < 128; ++i)
            buttons[i] = (state.rgbButtons[i] & 0x80) != 0;
        return true;
    }
};
} // namespace born2flap
#endif
