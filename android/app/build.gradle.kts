plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "org.ghostnet.monitor"
    compileSdk = 35
    // Pinned explicitly here (not just via gradle.properties) so the module
    // fails loudly instead of silently resolving a different NDK.
    ndkVersion = "25.1.8937393"

    defaultConfig {
        applicationId = "org.ghostnet.monitor"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "0.2-m1"

        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti")
            }
        }
    }

    externalNativeBuild {
        cmake { path = file("CMakeLists.txt"); version = "3.22.1" }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
