# KMP Shared Module

Kotlin Multiplatform — code partagé pour iOS et Android. **Pas encore intégré dans le projet Xcode** (l'app iOS utilise des stubs locaux dans `iosApp/KXKMBmu/Stubs/`).

## Build

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export PATH="$JAVA_HOME/bin:$PATH"
./gradlew :shared:linkDebugFrameworkIosArm64
```

Output : `shared/build/bin/iosArm64/debugFramework/Shared.framework`

## Constraints

- **JDK 17 obligatoire** (pas JDK 21+)
- **Gradle wrapper 8.5** (pas 9.x — incompatible avec AGP 8.2.0)
- **SKIE désactivé** (Gradle 9.x incompatible) — utiliser des callbacks manuels au lieu de Flow → AsyncSequence

## Architecture

- `shared/src/commonMain/kotlin/com/kxkm/bmu/`
  - `model/` : data classes (BatteryState, SystemInfo, etc.)
  - `transport/` : Transport interface, BleTransport, WifiTransport, MqttTransport, OfflineTransport
  - `domain/` : MonitoringUseCase, ControlUseCase, ConfigUseCase, AuditUseCase, SohUseCase
  - `db/` : SQLDelight queries (BmuDatabase.sq)
  - `auth/` : AuthUseCase + PinHasher (expect/actual)
- `shared/src/iosMain/` : actual impls iOS (CC_SHA256, NSDate, NativeSqliteDriver)
- `shared/src/androidMain/` : actual impls Android (MessageDigest, AndroidSqliteDriver)

## SQLDelight Gotchas

- `nullable params` (`WHERE (? IS NULL OR field = ?)`) ne fonctionnent pas — séparer en queries distinctes
- `IN ?` non supporté — utiliser un loop avec `WHERE id = ?`

## Integration Status

Le framework Shared.framework compile mais n'est **pas** linké dans le projet Xcode `iosApp/`. L'app utilise `iosApp/KXKMBmu/Stubs/SharedStubs.swift` qui mock les types KMP. Pour intégrer :
1. Ajouter `Shared.framework` aux Frameworks Xcode
2. Bridge layer : les types KMP sont préfixés `Shared*` (ex: `SharedBatteryState`)
3. Adapter `SharedBridge.swift` pour mapper Kotlin enums → Swift enums

## Anti-Patterns

- Ne pas utiliser `String.format()` en commonMain — pas multiplateforme, utiliser `padStart`
- Ne pas exposer `Flow<T>` aux ViewModels iOS sans SKIE — utiliser callback wrappers
- Ne pas oublier `expect/actual` pour `currentTimeMillis()` (différent JVM/iOS)
