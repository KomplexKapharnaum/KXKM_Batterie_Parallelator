plugins {
    kotlin("multiplatform")
    kotlin("plugin.serialization")
    id("com.android.library")
    id("app.cash.sqldelight")
    // id("co.touchlab.skie") version "0.10.1" // TODO: re-enable when compatible with Gradle 9.x
}

kotlin {
    androidTarget {
        compilations.all {
            kotlinOptions { jvmTarget = "17" }
        }
    }

    listOf(iosX64(), iosArm64(), iosSimulatorArm64()).forEach {
        it.binaries.framework {
            baseName = "Shared"
            isStatic = true
        }
    }

    sourceSets {
        val commonMain by getting {
            dependencies {
                // BLE
                implementation("com.juul.kable:core:0.31.0")
                // HTTP + WebSocket
                implementation("io.ktor:ktor-client-core:2.3.12")
                implementation("io.ktor:ktor-client-content-negotiation:2.3.12")
                implementation("io.ktor:ktor-serialization-kotlinx-json:2.3.12")
                implementation("io.ktor:ktor-client-websockets:2.3.12")
                // Serialization
                implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
                // Coroutines
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
                // SQLDelight
                implementation("app.cash.sqldelight:coroutines-extensions:2.0.2")
                // DateTime
                implementation("org.jetbrains.kotlinx:kotlinx-datetime:0.6.1")
            }
        }
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
            }
        }
        val androidMain by getting {
            dependencies {
                implementation("io.ktor:ktor-client-okhttp:2.3.12")
                implementation("app.cash.sqldelight:android-driver:2.0.2")
                // MQTT
                implementation("org.eclipse.paho:org.eclipse.paho.client.mqttv3:1.2.5")
            }
        }
        val iosMain by creating {
            dependsOn(commonMain)
            dependencies {
                implementation("app.cash.sqldelight:native-driver:2.0.2")
                implementation("io.ktor:ktor-client-darwin:2.3.12")
            }
        }
        val iosTest by creating {
            dependsOn(commonTest)
        }
        val iosX64Main by getting { dependsOn(iosMain) }
        val iosX64Test by getting { dependsOn(iosTest) }
        val iosArm64Main by getting { dependsOn(iosMain) }
        val iosArm64Test by getting { dependsOn(iosTest) }
        val iosSimulatorArm64Main by getting { dependsOn(iosMain) }
        val iosSimulatorArm64Test by getting { dependsOn(iosTest) }
    }
}

android {
    namespace = "com.kxkm.bmu.shared"
    compileSdk = 34
    defaultConfig { minSdk = 26 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

sqldelight {
    databases {
        create("BmuDatabase") {
            packageName.set("com.kxkm.bmu.db")
        }
    }
}
