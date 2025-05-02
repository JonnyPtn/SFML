////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2013 Jonathan De Wachter (dewachter.jonathan@gmail.com)
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
#include <SFML/Window/Android/JniHelper.hpp>
#include <SFML/Window/JoystickImpl.hpp>

#include <SFML/System/Android/Activity.hpp>
#include <SFML/System/Err.hpp>

namespace sf::priv
{


////////////////////////////////////////////////////////////
void JoystickImpl::initialize()
{
}


////////////////////////////////////////////////////////////
void JoystickImpl::cleanup()
{
}


////////////////////////////////////////////////////////////
bool JoystickImpl::isConnected(unsigned int index)
{
    // This is called as a prefilter before open, but it
    // would just duplicate the logic of open.
    return index == 0;
}


////////////////////////////////////////////////////////////
bool JoystickImpl::open(unsigned int joyIndex)
{
    if (joyIndex != 0)
        return false;

    // Retrieve activity states
    ActivityStates&       states = getActivity();
    const std::lock_guard lock(states.mutex);

    JNIEnv* env = states.activity->env;
    auto    jni = Jni::attachCurrentThread(states.activity->vm, &env);
    if (!jni)
    {
        err() << "Failed to initialize JNI" << std::endl;
        return false;
    }

    auto inputDeviceClass = JniInputDeviceClass::findClass(env);
    if (!inputDeviceClass)
        return false;

    auto deviceIds = inputDeviceClass->getDeviceIds();
    if (!deviceIds)
        return false;

    for (jsize i = 0; i < deviceIds->getLength(); ++i)
    {
        const auto deviceId    = (*deviceIds)[i];
        auto       inputDevice = inputDeviceClass->getDevice(deviceId);
        if (!inputDevice)
            continue;

        if (!inputDevice->supportsSource(AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK))
            continue;

        m_identification = Joystick::Identification{
            inputDevice->getName(),
            inputDevice->getVendorId(),
            inputDevice->getProductId(),
        };

        m_currentDeviceIdx = deviceId;

        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////
void JoystickImpl::close()
{
}


////////////////////////////////////////////////////////////
JoystickCaps JoystickImpl::getCapabilities() const
{
    return JoystickCaps{Joystick::ButtonCount, EnumArray<Joystick::Axis, bool, Joystick::AxisCount>{true}};
}


////////////////////////////////////////////////////////////
Joystick::Identification JoystickImpl::getIdentification() const
{
    return m_identification;
}


////////////////////////////////////////////////////////////
JoystickState JoystickImpl::update() const
{
    // Retrieve activity states
    ActivityStates&       states = getActivity();
    const std::lock_guard lock(states.mutex);

    JNIEnv* env = states.activity->env;
    auto    jni = Jni::attachCurrentThread(states.activity->vm, &env);
    if (!jni)
    {
        err() << "Failed to initialize JNI" << std::endl;
        return {false};
    }

    auto inputDeviceClass = JniInputDeviceClass::findClass(env);
    if (!inputDeviceClass)
        return {false};

    return {inputDeviceClass->getDevice(m_currentDeviceIdx).has_value(), states.joyAxii, states.isJoystickButtonPressed};
}

} // namespace sf::priv
