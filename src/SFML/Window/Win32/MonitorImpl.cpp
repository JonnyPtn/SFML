////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2025 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////

#include "SFML/Window/JoystickImpl.hpp"

#include <SFML/Window/Monitor.hpp>

namespace sf
{
std::vector<Monitor> Monitor::getAllMonitors()
{
    DISPLAY_DEVICE device{};
    device.cb = sizeof(DISPLAY_DEVICE);
    int                  deviceIndex{};
    std::vector<Monitor> monitors;
    while (EnumDisplayDevices(nullptr, deviceIndex++, &device, EDD_GET_DEVICE_INTERFACE_NAME))
    {
        if (device.StateFlags & DISPLAY_DEVICE_ACTIVE)
        {
            DISPLAY_DEVICE monitorDevice{};
            monitorDevice.cb = sizeof(DISPLAY_DEVICE);
            int monitorIndex{};
            while (EnumDisplayDevices(device.DeviceName, monitorIndex++, &monitorDevice, EDD_GET_DEVICE_INTERFACE_NAME))
            {
                monitors.emplace_back(device.DeviceName, DISPLAY_DEVICE_PRIMARY_DEVICE & device.StateFlags);
            }
        }
    }
    return monitors;
}

VideoMode Monitor::getVideoMode() const
{
    DEVMODE win32Mode;
    win32Mode.dmSize        = sizeof(win32Mode);
    win32Mode.dmDriverExtra = 0;
    EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &win32Mode);

    return VideoMode{{win32Mode.dmPelsWidth, win32Mode.dmPelsHeight}, win32Mode.dmBitsPerPel};
}


std::vector<VideoMode> Monitor::getVideoModes() const
{
    DEVMODE mode{};
    mode.dmSize = sizeof(DEVMODE);
    int     count{};
    std::vector<VideoMode> modes;
    while (EnumDisplaySettings(m_name.toWideString().c_str(), count++, &mode))
    {
        modes.push_back({{mode.dmPelsWidth, mode.dmPelsHeight}, mode.dmBitsPerPel, mode.dmDisplayFrequency});
    }
    return modes;
}
} // namespace sf
