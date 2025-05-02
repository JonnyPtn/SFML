#include <SFML/Window/Android/JniHelper.hpp>
#include <SFML/System/Err.hpp>
#include <ostream>

std::optional<JniInputDeviceClass> JniInputDeviceClass::findClass(JNIEnv* env)
{
    assert(env);
    jclass inputDeviceClass = env->FindClass("android/view/InputDevice");
    if (inputDeviceClass == nullptr) return std::nullopt;

    jmethodID getDeviceIdsMethod = env->GetStaticMethodID(inputDeviceClass, "getDeviceIds", "()[I");
    jmethodID getDeviceMethod = env->GetStaticMethodID(inputDeviceClass, "getDevice", "(I)Landroid/view/InputDevice;");
    if (!getDeviceIdsMethod || !getDeviceMethod)
    {
        sf::err() << "Could not locate required InputDevice methods" << std::endl;
        return std::nullopt;
    }

    return JniInputDeviceClass(env, inputDeviceClass, getDeviceIdsMethod, getDeviceMethod);
}

std::optional<JniArray<jint>> JniInputDeviceClass::getDeviceIds()
{
    auto deviceIdsArray = static_cast<jintArray>(
        m_env->CallStaticObjectMethod(m_inputDeviceClass, m_getDeviceIdsMethod));
    if (deviceIdsArray == nullptr)
    {
        sf::err() << "No input devices found." << std::endl;
        return std::nullopt;
    }

    return JniArray<jint>(m_env, deviceIdsArray);
}

std::optional<JniInputDevice> JniInputDeviceClass::getDevice(jint idx)
{
    jmethodID getNameMethod = m_env->GetMethodID(m_inputDeviceClass, "getName", "()Ljava/lang/String;");
    jmethodID getVendorIdMethod = m_env->GetMethodID(m_inputDeviceClass, "getVendorId", "()I");
    jmethodID getProductIdMethod = m_env->GetMethodID(m_inputDeviceClass, "getProductId", "()I");
    jmethodID supportsSourceMethod = m_env->GetMethodID(m_inputDeviceClass, "supportsSource", "(I)Z");

    if (!getNameMethod || !getVendorIdMethod || !getProductIdMethod || !supportsSourceMethod)
    {
        sf::err() << "Could not locate required InputDevice methods" << std::endl;
        return std::nullopt;
    }

    jobject inputDevice = m_env->CallStaticObjectMethod(m_inputDeviceClass, m_getDeviceMethod, idx);
    if (!inputDevice)
    {
        // Can happen normally, no log needed
        return std::nullopt;
    }

    return JniInputDevice(
            m_env,
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

bool Jni::attachCurrentThread(JavaVM* vm, JNIEnv** env)
{
    assert(vm && env);

    JavaVMAttachArgs lJavaVMAttachArgs;
    lJavaVMAttachArgs.version = JNI_VERSION_1_6;
    lJavaVMAttachArgs.name    = "NativeThread";
    lJavaVMAttachArgs.group   = nullptr;

    jint lResult = 0;
    lResult = vm->AttachCurrentThread(env, &lJavaVMAttachArgs);

    return lResult != JNI_ERR;
}
