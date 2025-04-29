#include <SFML/Window/Android/JniHelper.hpp>
#include <SFML/System/Err.hpp>
#include <ostream>

std::optional<JniArray<jint>> JniInputDevice::getDeviceIds(JNIEnv *env, jclass inputDeviceClass)
{
    jmethodID getDeviceIdsMethod = env->GetStaticMethodID(inputDeviceClass, "getDeviceIds", "()[I");
    if (!getDeviceIdsMethod)
    {
        sf::err() << "Could not locate InputDevice.getDeviceIds method" << std::endl;
        return std::nullopt;
    }

    auto deviceIdsArray = static_cast<jintArray>(
            env->CallStaticObjectMethod(inputDeviceClass, getDeviceIdsMethod));
    if (deviceIdsArray == nullptr)
    {
        sf::err() << "No input devices found." << std::endl;
        return std::nullopt;
    }

    return JniArray<jint>(env, deviceIdsArray);
}

std::optional<JniInputDevice> JniInputDevice::getDevice(JNIEnv *env, jclass inputDeviceClass, jint idx)
{
    jmethodID getDeviceMethod = env->GetStaticMethodID(inputDeviceClass, "getDevice", "(I)Landroid/view/InputDevice;");
    jmethodID getNameMethod = env->GetMethodID(inputDeviceClass, "getName", "()Ljava/lang/String;");
    jmethodID getVendorIdMethod = env->GetMethodID(inputDeviceClass, "getVendorId", "()I");
    jmethodID getProductIdMethod = env->GetMethodID(inputDeviceClass, "getProductId", "()I");
    jmethodID supportsSourceMethod = env->GetMethodID(inputDeviceClass, "supportsSource", "(I)Z");

    if (!getDeviceMethod || !getNameMethod || !getVendorIdMethod || !getProductIdMethod || !supportsSourceMethod)
    {
        sf::err() << "Could not locate required InputDevice methods" << std::endl;
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

std::string JniInputDevice::javaStringToStd(jstring str) const
{
    const char* utfChars = m_env->GetStringUTFChars(str, nullptr);
    std::string result(utfChars);
    m_env->ReleaseStringUTFChars(str, utfChars);
    return result;
}
