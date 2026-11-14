plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "org.ghostnet.phase0"
    compileSdk = 35
    // Pinned explicitly here (not just via gradle.properties' android.ndkVersion)
    // so the module fails loudly instead of silently resolving a different NDK
    // if gradle.properties is ever edited or overridden. Phase 0 hit a real
    // mismatch between the NDK this was tested with and what the (now-replaced)
    // JS8 decoder candidate's own README named; see PHASE1_PLAN.md WS1.
    ndkVersion = "25.1.8937393"

    defaultConfig {
        applicationId = "org.ghostnet.phase0"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-phase0"

        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti", "-O2")
                arguments += listOf("-DEIGEN3_INCLUDE_DIR=${rootProject.projectDir.parentFile}/third_party")
            }
        }
    }

    externalNativeBuild {
        cmake { path = file("CMakeLists.txt"); version = "3.22.1" }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
