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

template<class T>
class JniArray {
public:
    JniArray(JNIEnv* env, jintArray array)
        : m_env(env)
        , m_array(array)
        , m_length(env->GetArrayLength(array))
        , m_data(env->GetIntArrayElements(array, nullptr))
    {}

    JniArray(JniArray&& other) noexcept
    {
        std::swap(m_env, other.m_env);
        std::swap(m_array, other.m_array);
        std::swap(m_length, other.m_length);
        std::swap(m_data, other.m_data);
    }

    ~JniArray()
    {
        if (m_data)
            m_env->ReleaseIntArrayElements(m_array, m_data, 0);
    }

public:
    T operator[](ssize_t idx)
    {
        assert(0 <= idx && idx <= m_length);
        return m_data[idx];
    }

    const T operator[](ssize_t idx) const
    {
        assert(0 <= idx && idx <= m_length);
        return m_data[idx];
    }

    [[nodiscard]] ssize_t getLength() const noexcept
    {
        return m_length;
    }

private:
    JNIEnv * m_env = nullptr;
    jintArray m_array = nullptr;
    ssize_t m_length = 0;
    T* m_data = nullptr;
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
        : m_env(env)
        , m_inputDevice(inputDevice)
        , m_getNameMethod(getNameMethod)
        , m_getVendorIdMethod(getVendorIdMethod)
        , m_getProductIdMethod(getProductIdMethod)
        , m_supportsSourceMethod(supportsSourceMethod)
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

        auto deviceIdsArray = static_cast<jintArray>(
                env->CallStaticObjectMethod(inputDeviceClass, getDeviceIdsMethod));
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

    [[nodiscard]] unsigned getVendorId() const
    {
        return static_cast<unsigned>(
                m_env->CallIntMethod(m_inputDevice, m_getVendorIdMethod));
    }

    [[nodiscard]] unsigned getProductId() const
    {
        return static_cast<unsigned>(
                m_env->CallIntMethod(m_inputDevice, m_getProductIdMethod));
    }

    [[nodiscard]] std::string getName() const
    {
        return javaStringToStd(
            static_cast<jstring>(m_env->CallObjectMethod(
                m_inputDevice,
                m_getNameMethod)));
    }

    [[nodiscard]] bool supportsSource(size_t sourceFlags) const
    {
        return m_env->CallBooleanMethod(
                m_inputDevice, m_supportsSourceMethod, jint(sourceFlags));
    }

private:
    std::string javaStringToStd(jstring str) const
    {
        const char* utfChars = m_env->GetStringUTFChars(str, nullptr);
        std::string result(utfChars);
        m_env->ReleaseStringUTFChars(str, utfChars);
        return result;
    }

private:
    JNIEnv* m_env;
    jobject m_inputDevice;
    jmethodID m_getNameMethod;
    jmethodID m_getVendorIdMethod;
    jmethodID m_getProductIdMethod;
    jmethodID m_supportsSourceMethod;
};

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
    // would just duplicate the logic of open. Furthermore
    // how many physical devices are you going to attach to
    // an android device?
    return index == 0;
}


////////////////////////////////////////////////////////////
bool JoystickImpl::open(unsigned int joyIndex)
{
    if (joyIndex != 0) return false;

    // Retrieve activity states
    ActivityStates&       states = getActivity();
    const std::lock_guard lock(states.mutex);

    // Initializes JNI
    jint lResult = 0;
    
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
}


////////////////////////////////////////////////////////////
JoystickCaps JoystickImpl::getCapabilities() const
{
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

    return {
        true,
        states.joyAxii,
        states.isJoystickButtonPressed
    };
}

} // namespace sf::priv
