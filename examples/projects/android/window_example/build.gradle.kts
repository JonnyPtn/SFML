plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.sfml.window_example"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.sfml.window_example"
        minSdk = (properties["sfml.minSdk"] as String).toInt()
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }
}

dependencies {
    implementation(project(":SFML"))
}
