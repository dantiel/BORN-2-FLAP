#define NOMINMAX
#include "born2flap_rc_windows.h"
#include <iostream>

int main()
{
    born2flap::RcWindowsDevices input;
    input.Scan();
    std::wcout << L"Attached DirectInput controllers: " << input.devices.size() << L"\n";
    for (const auto &device : input.devices)
    {
        std::wcout << device.name << L" " << device.id << L"\n";
        if (!input.Open(device, GetConsoleWindow() ? GetConsoleWindow() : GetDesktopWindow()))
        {
            std::wcout << L"  Cannot acquire/read device\n";
            continue;
        }
        for (int i = 0; i < 8; ++i)
            if (input.available[i])
                std::wcout << L"  Axis " << i + 1 << L" = " << input.axes[i] << L"\n";
    }
}
