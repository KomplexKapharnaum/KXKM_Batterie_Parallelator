import Foundation
import Security

/// Stores and retrieves WiFi passwords in the iOS Keychain, keyed by SSID.
enum WifiKeychain {

    private static let service = "com.kxkm.bmu.wifi"

    /// Save a WiFi password for the given SSID.
    static func save(ssid: String, password: String) {
        guard !ssid.isEmpty, !password.isEmpty else { return }
        let account = ssid
        let data = Data(password.utf8)

        // Delete any existing entry first
        let deleteQuery: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        SecItemDelete(deleteQuery as CFDictionary)

        // Add new entry
        let addQuery: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlocked,
        ]
        SecItemAdd(addQuery as CFDictionary, nil)
    }

    /// Load the saved WiFi password for the given SSID, if any.
    static func load(ssid: String) -> String? {
        guard !ssid.isEmpty else { return nil }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: ssid,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }
}
