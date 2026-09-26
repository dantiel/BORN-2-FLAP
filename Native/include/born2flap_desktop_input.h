#pragma once
#include "born2flap_rc_input.h"

namespace born2flap
{
// Desktop transmitter: independent keyboard and mouse steering, one throttle owner.
struct DesktopInput
{
    static constexpr double MouseTravel = 1800.0; // pixels from centre to full stick
    RcKeyboard keyboard;
    double wheelThrottle = 0;
    bool wheelOwnsThrottle = false;
    double mouseRoll = 0, mousePitch = 0, mouseYaw = 0;
    bool previousMuteYaw = false, previousMuteRoll = false;
    double throttle = 0, roll = 0, pitch = 0, yaw = 0;

    static double KeyboardThrottle(bool w, bool control, bool shift)
    {
        return !w ? 0 : control ? .32 : shift ? 1 : .72;
    }

    void Step(double dt, double keyThrottle, bool wDown, double keyRoll, double keyPitch, double keyYaw,
              double mouseX, double mouseY, double wheel, bool muteYaw, bool muteRoll, bool resetMouse = false)
    {
        if (dt <= 0) return;
        // Retain fractional wheel events. Keyboard never changes the remembered wheel value.
        if (wheel != 0)
        {
            wheelThrottle = std::clamp(wheelThrottle + wheel * .02, 0.0, 1.0);
            wheelOwnsThrottle = true;
        }
        if (wDown) wheelOwnsThrottle = false;
        keyboard.Step(dt, wheelOwnsThrottle ? wheelThrottle : keyThrottle, keyRoll, keyPitch, keyYaw);

        // Relative displacement moves a persistent virtual stick; stopping never centres it.
        // Per-frame mouse deltas already integrate motion, so do not scale them by dt.
        const bool clicked = resetMouse || (muteYaw && !previousMuteYaw) || (muteRoll && !previousMuteRoll);
        previousMuteYaw = muteYaw;
        previousMuteRoll = muteRoll;
        if (clicked)
            mouseRoll = mousePitch = mouseYaw = 0;
        else
        {
            mouseRoll = muteRoll ? 0 : std::clamp(mouseRoll + mouseX / MouseTravel, -1.0, 1.0);
            mouseYaw = muteYaw ? 0 : std::clamp(mouseYaw + mouseX / MouseTravel, -1.0, 1.0);
            mousePitch = std::clamp(mousePitch + mouseY / MouseTravel, -1.0, 1.0);
        }
        throttle = keyboard.throttle;
        // Expo each device before summing: opposing inputs cancel and either can add authority.
        roll = std::clamp(RcKeyboard::Expo(keyboard.roll) + RcKeyboard::Expo(mouseRoll), -1.0, 1.0);
        pitch = std::clamp(RcKeyboard::Expo(keyboard.pitch) + RcKeyboard::Expo(mousePitch), -1.0, 1.0);
        yaw = std::clamp(RcKeyboard::Expo(keyboard.yaw) + RcKeyboard::Expo(mouseYaw), -1.0, 1.0);
    }
};
}
