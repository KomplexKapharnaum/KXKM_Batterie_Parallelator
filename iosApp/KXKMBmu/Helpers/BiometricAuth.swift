import LocalAuthentication

enum BiometricAuth {
    static func authenticate(completion: @escaping (Bool) -> Void) {
        let context = LAContext()
        var error: NSError?

        guard context.canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: &error) else {
            completion(false)
            return
        }

        context.evaluatePolicy(.deviceOwnerAuthenticationWithBiometrics,
                               localizedReason: "Déverrouiller KXKM BMU") { success, _ in
            DispatchQueue.main.async { completion(success) }
        }
    }

    static func getStoredPin() -> String? {
        // Read from Keychain
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.kxkm.bmu.pin",
            kSecReturnData as String: true
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data,
              let pin = String(data: data, encoding: .utf8) else { return nil }
        return pin
    }

    static func storePin(_ pin: String) {
        let data = pin.data(using: .utf8)!
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.kxkm.bmu.pin",
            kSecValueData as String: data
        ]
        SecItemDelete(query as CFDictionary) // Remove old
        SecItemAdd(query as CFDictionary, nil)
    }
}
