plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseKeystorePath = System.getenv("STREAMCAM_RELEASE_KEYSTORE")
val releaseStorePassword = System.getenv("STREAMCAM_RELEASE_STORE_PASSWORD")
val releaseKeyAlias = System.getenv("STREAMCAM_RELEASE_KEY_ALIAS")
val releaseKeyPassword = System.getenv("STREAMCAM_RELEASE_KEY_PASSWORD")
val hasReleaseSigning = listOf(releaseKeystorePath, releaseStorePassword, releaseKeyAlias, releaseKeyPassword)
    .all { !it.isNullOrBlank() }

android {
    namespace = "com.streamcam.diagnostics"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.streamcam.diagnostics"
        minSdk = 29
        targetSdk = 35
        versionCode = 18
        versionName = "0.3.14"
    }

    buildFeatures {
        buildConfig = true
    }

    if (hasReleaseSigning) {
        signingConfigs {
            create("release") {
                storeFile = file(releaseKeystorePath!!)
                storePassword = releaseStorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
            }
        }
    }
    buildTypes {
        getByName("release") {
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
        }
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
    implementation("androidx.camera:camera-camera2:1.4.2")
    implementation("androidx.camera:camera-lifecycle:1.4.2")
    implementation("androidx.camera:camera-view:1.4.2")
    implementation("com.google.mlkit:barcode-scanning:17.3.0")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("io.github.webrtc-sdk:android:150.7871.04")
    testImplementation("junit:junit:4.13.2")
}
