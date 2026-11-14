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
        versionCode = 8
        versionName = "0.6.2"

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

    buildFeatures { compose = true }
    composeOptions { kotlinCompilerExtensionVersion = "1.5.10" } // matches Kotlin 1.9.22

    testOptions {
        unitTests.isIncludeAndroidResources = true
        // Robolectric on JDK 17 needs to reach into java.io to manage file descriptors.
        unitTests.all { it.jvmArgs("--add-opens=java.base/java.io=ALL-UNNAMED") }
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

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.02.00")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.core:core:1.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
    testImplementation("junit:junit:4.13.2")
    // UI smoke tests: run the real activity and Compose screens on the JVM.
    testImplementation("org.robolectric:robolectric:4.11.1")
    testImplementation(composeBom)
    testImplementation("androidx.compose.ui:ui-test-junit4")
    testImplementation("androidx.test:core:1.5.0")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
