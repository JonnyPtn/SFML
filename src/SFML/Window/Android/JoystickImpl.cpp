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
#include <SFML/Window/JoystickImpl.hpp>
#include <SFML/System/Err.hpp>
#include <SFML/System/Android/Activity.hpp>

static std::string javaStringToStd(JNIEnv* lJNIEnv, jstring str)
{
    const char* utfChars = lJNIEnv->GetStringUTFChars(str, nullptr);
    std::string result(utfChars);
    lJNIEnv->ReleaseStringUTFChars(str, utfChars);
    return result;
}

namespace sf::priv
{
////////////////////////////////////////////////////////////
void JoystickImpl::initialize()
{
    // To implement
}


////////////////////////////////////////////////////////////
void JoystickImpl::cleanup()
{
    // To implement
}


////////////////////////////////////////////////////////////
bool JoystickImpl::isConnected(unsigned int /* index */)
{
    // To implement
    return true;
}


////////////////////////////////////////////////////////////
bool JoystickImpl::open(unsigned int /* index */)
{
    // To implement
    return true;
}


////////////////////////////////////////////////////////////
void JoystickImpl::close()
{
    // To implement
}


////////////////////////////////////////////////////////////
JoystickCaps JoystickImpl::getCapabilities() const
{
    // To implement
    return {};
}


////////////////////////////////////////////////////////////
Joystick::Identification JoystickImpl::getIdentification() const
{
    // Retrieve activity states
    ActivityStates&       states = getActivity();
    const std::lock_guard lock(states.mutex);

    // Initializes JNI
    jint lResult = 0;

    // TODO: extract to common method also used in windowimpl
    JavaVM* lJavaVM = states.activity->vm;
    JNIEnv* lJNIEnv = states.activity->env;

    JavaVMAttachArgs lJavaVMAttachArgs;
    lJavaVMAttachArgs.version = JNI_VERSION_1_6;
    lJavaVMAttachArgs.name    = "NativeThread";
    lJavaVMAttachArgs.group   = nullptr;

    lResult = lJavaVM->AttachCurrentThread(&lJNIEnv, &lJavaVMAttachArgs);

    if (lResult == JNI_ERR)
        err() << "Failed to initialize JNI, couldn't get the Unicode value" << std::endl;

    jclass inputDeviceClass = lJNIEnv->FindClass("android/view/InputDevice");
    if (inputDeviceClass == nullptr) {
        err() << "Failed to find InputDevice class." << std::endl;
        return m_identification;
    }

    // Locate required InputDevice methods
    jmethodID getDeviceIdsMethod = lJNIEnv->GetStaticMethodID(inputDeviceClass, "getDeviceIds", "()[I");
    jmethodID getDeviceMethod = lJNIEnv->GetStaticMethodID(inputDeviceClass, "getDevice", "(I)Landroid/view/InputDevice;");
    jmethodID getNameMethod = lJNIEnv->GetMethodID(inputDeviceClass, "getName", "()Ljava/lang/String;");
    jmethodID getVendorIdMethod = lJNIEnv->GetMethodID(inputDeviceClass, "getVendorId", "()I");
    jmethodID getProductIdMethod = lJNIEnv->GetMethodID(inputDeviceClass, "getProductId", "()I");
    jmethodID supportsSourceMethod = lJNIEnv->GetMethodID(inputDeviceClass, "supportsSource", "(I)Z");

    if (!getDeviceIdsMethod || !getDeviceMethod || !getNameMethod || !getVendorIdMethod || !getProductIdMethod || !supportsSourceMethod) {
        err() << "Failed to locate required methods from android/view/InputDevice" << std::endl;
        return m_identification;
    }

    // Call getDeviceIds() to get the array of device IDs
    jintArray deviceIdsArray = (jintArray)lJNIEnv->CallStaticObjectMethod(inputDeviceClass, getDeviceIdsMethod);
    if (deviceIdsArray == nullptr) {
        err() << "No input devices found." << std::endl;
        return m_identification;
    }

    // Get the array length and elements
    jsize arrayLength = lJNIEnv->GetArrayLength(deviceIdsArray);
    jint* deviceIds = lJNIEnv->GetIntArrayElements(deviceIdsArray, nullptr);

    for (jsize i = 0; i < arrayLength; ++i) {
        jobject inputDevice = lJNIEnv->CallStaticObjectMethod(inputDeviceClass, getDeviceMethod, deviceIds[i]);
        if (!inputDevice) continue;

        jboolean supportsGamepad = lJNIEnv->CallBooleanMethod(inputDevice, supportsSourceMethod, jint(AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK));
        if (!(bool)supportsGamepad) continue;

        m_identification = Joystick::Identification{
            javaStringToStd(lJNIEnv, (jstring)lJNIEnv->CallObjectMethod(inputDevice, getNameMethod)),
            (unsigned)lJNIEnv->CallIntMethod(inputDevice, getVendorIdMethod),
            (unsigned)lJNIEnv->CallIntMethod(inputDevice, getProductIdMethod),
        };
    }

    // Release the array elements
    lJNIEnv->ReleaseIntArrayElements(deviceIdsArray, deviceIds, 0);

    return m_identification;
}


////////////////////////////////////////////////////////////
JoystickState JoystickImpl::update()
{
    // To implement
    return {};
}

} // namespace sf::priv
