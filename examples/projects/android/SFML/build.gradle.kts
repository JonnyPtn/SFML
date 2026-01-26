plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.sfml.sfml"

    defaultConfig {
        minSdk = (properties["sfml.minSdk"] as String).toInt()
        compileSdk = 36
        ndk {
            abiFilters += properties["sfml.archAbi"] as String
        }
        externalNativeBuild {
            cmake {
                arguments += "-DSFML_BUILD_EXAMPLES=ON"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../../../CMakeLists.txt")
            version = "3.28.0+"
        }
    }
    ndkVersion = "27.0.12077973"
}
