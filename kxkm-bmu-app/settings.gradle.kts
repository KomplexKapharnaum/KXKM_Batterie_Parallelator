pluginManagement {
    repositories {
        google()
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "kxkm-bmu-app"
// include(":androidApp")  // Disabled until Android SDK available
include(":shared")
