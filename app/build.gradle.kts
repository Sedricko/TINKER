plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.sedricko.tinker"
    compileSdk = 37
    defaultConfig {
        applicationId = "com.sedricko.tinker"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    // chave fixa: sem ela cada build do CI assina diferente e o Android não deixa atualizar por cima
    // shortcut: chave de teste no repositório público, trocar por uma em GitHub Secrets antes de publicar em loja
    signingConfigs {
        getByName("debug") {
            storeFile = file("tinker-dev.keystore")
            storePassword = "android"
            keyAlias = "tinker"
            keyPassword = "android"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    // o daemon vai como lib*.so para ser extraído em nativeLibraryDir, o único lugar onde o Android deixa executar
    packaging {
        jniLibs { useLegacyPackaging = true }
    }
    buildFeatures { compose = true }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.09.00"))
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")
    testImplementation("junit:junit:4.13.2")
}
