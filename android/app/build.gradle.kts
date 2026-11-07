plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.streamcam.diagnostics"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.streamcam.diagnostics"
        minSdk = 29
        targetSdk = 35
        versionCode = 2
        versionName = "0.2.1"
    }

    buildFeatures {
        buildConfig = true
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation("com.google.android.material:material:1.12.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("io.github.webrtc-sdk:android:150.7871.04")
}
