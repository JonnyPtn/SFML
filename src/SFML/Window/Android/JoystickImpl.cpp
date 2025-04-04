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

static std::string javaStringToStd(JNIEnv* env, jstring str)
{
    const char* utfChars = env->GetStringUTFChars(str, nullptr);
    std::string result(utfChars);
    env->ReleaseStringUTFChars(str, utfChars);
    return result;
}

template<class T>
class JniArray {
public:
    JniArray(JNIEnv* env, jintArray array)
        : env(env)
        , array(array)
        , length(env->GetArrayLength(array))
        , data(env->GetIntArrayElements(array, nullptr))
    {}

    JniArray(JniArray&& other)
    {
        std::swap(env, other.env);
        std::swap(array, other.array);
        std::swap(length, other.length);
        std::swap(data, other.data);
    }

    ~JniArray()
    {
        if (data)
            env->ReleaseIntArrayElements(array, data, 0);
    }

public:
    T operator[](ssize_t idx)
    {
        assert(0 <= idx && idx <= length);
        return data[idx];
    }

    const T operator[](ssize_t idx) const
    {
        assert(0 <= idx && idx <= length);
        return data[idx];
    }

    ssize_t getLength() const noexcept
    {
        return length;
    }

private:
    JNIEnv * env = nullptr;
    jintArray array = nullptr;
    ssize_t length = 0;
    T* data = nullptr;
};

namespace sf::priv
{
class JniInputDevice
{
public:
    JniInputDevice(
        JNIEnv* env,
        jobject inputDevice,
        jmethodID getNameMethod,
        jmethodID getVendorIdMethod,
        jmethodID getProductIdMethod,
        jmethodID supportsSourceMethod)
        : env(env)
        , inputDevice(inputDevice)
        , getNameMethod(getNameMethod)
        , getVendorIdMethod(getVendorIdMethod)
        , getProductIdMethod(getProductIdMethod)
        , supportsSourceMethod(supportsSourceMethod)
    {}

public:
    static std::optional<JniArray<jint>> getDeviceIds(JNIEnv *env, jclass inputDeviceClass)
    {
        jmethodID getDeviceIdsMethod = env->GetStaticMethodID(inputDeviceClass, "getDeviceIds", "()[I");
        if (!getDeviceIdsMethod)
        {
            err() << "Could not locate InputDevice.getDeviceIds method" << std::endl;
            return std::nullopt;
        }

        jintArray deviceIdsArray = (jintArray)env->CallStaticObjectMethod(inputDeviceClass, getDeviceIdsMethod);
        if (deviceIdsArray == nullptr)
        {
            err() << "No input devices found." << std::endl;
            return std::nullopt;
        }

        return JniArray<jint>(env, deviceIdsArray);
    }

    static std::optional<JniInputDevice> getDevice(JNIEnv *env, jclass inputDeviceClass, jint idx)
    {
        jmethodID getDeviceMethod = env->GetStaticMethodID(inputDeviceClass, "getDevice", "(I)Landroid/view/InputDevice;");
        jmethodID getNameMethod = env->GetMethodID(inputDeviceClass, "getName", "()Ljava/lang/String;");
        jmethodID getVendorIdMethod = env->GetMethodID(inputDeviceClass, "getVendorId", "()I");
        jmethodID getProductIdMethod = env->GetMethodID(inputDeviceClass, "getProductId", "()I");
        jmethodID supportsSourceMethod = env->GetMethodID(inputDeviceClass, "supportsSource", "(I)Z");

        if (!getDeviceMethod || !getNameMethod || !getVendorIdMethod || !getProductIdMethod || !supportsSourceMethod)
        {
            err() << "Could not locate required InputDevice methods" << std::endl;
            return std::nullopt;
        }

        jobject inputDevice = env->CallStaticObjectMethod(inputDeviceClass, getDeviceMethod, idx);
        if (!inputDevice)
        {
            // Can happen normally, no log needed
            return std::nullopt;
        }

        return JniInputDevice(
                env,
                inputDevice,
                getNameMethod,
                getVendorIdMethod,
                getProductIdMethod,
                supportsSourceMethod);
    }

    unsigned getVendorId() const
    {
        return (unsigned)env->CallIntMethod(inputDevice, getVendorIdMethod);
    }

    unsigned getProductId() const
    {
        return (unsigned)env->CallIntMethod(inputDevice, getProductIdMethod);
    }

    std::string getName() const
    {
        return javaStringToStd(
            env,
            (jstring)env->CallObjectMethod(
                inputDevice,
                getNameMethod));
    }

    bool supportsSource(size_t sourceFlags) const
    {
        return env->CallBooleanMethod(inputDevice, supportsSourceMethod, jint(sourceFlags));
    }

private:
    JNIEnv* env;
    jobject inputDevice;
    jmethodID getNameMethod;
    jmethodID getVendorIdMethod;
    jmethodID getProductIdMethod;
    jmethodID supportsSourceMethod;
};

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
bool JoystickImpl::isConnected(unsigned int index)
{
    // This is called as a prefilter before open, but it
    // would just duplicate the logic of open. Furthermore
    // how many physical devices are you going to attach to
    // an android device?
    return index == 0;
}


////////////////////////////////////////////////////////////
bool JoystickImpl::open(unsigned int joy_index)
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
        err() << "Failed to initialize JNI" << std::endl;

    jclass inputDeviceClass = lJNIEnv->FindClass("android/view/InputDevice");
    if (inputDeviceClass == nullptr) {
        err() << "Failed to find InputDevice class." << std::endl;
        return false;
    }

    auto deviceIds = JniInputDevice::getDeviceIds(lJNIEnv, inputDeviceClass);
    if (!deviceIds) return false;

    for (jsize i = 0; i < deviceIds->getLength(); ++i) {
        auto inputDevice = JniInputDevice::getDevice(lJNIEnv, inputDeviceClass, (*deviceIds)[i]);
        if (!inputDevice) continue;

        if (!inputDevice->supportsSource(AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) continue;

        // Get state of nth gamepad
        if (joy_index > 0)
        {
            --joy_index;
            continue;
        }

        m_identification = Joystick::Identification{
            inputDevice->getName(),
            inputDevice->getVendorId(),
            inputDevice->getProductId(),
        };

        m_currentDeviceIdx = int(i);

        return true;
    }

    return false;
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
    return JoystickCaps{
        Joystick::ButtonCount,
        EnumArray<Joystick::Axis, bool, Joystick::AxisCount>{ true }
    };
}


////////////////////////////////////////////////////////////
Joystick::Identification JoystickImpl::getIdentification() const
{
    return m_identification;
}


////////////////////////////////////////////////////////////
JoystickState JoystickImpl::update()
{
    // Retrieve activity states
    ActivityStates&       states = getActivity();
    const std::lock_guard lock(states.mutex);

    // To implement
    return {
        true,
        {},
        states.isJoystickButtonPressed[0]
    };
}

} // namespace sf::priv
