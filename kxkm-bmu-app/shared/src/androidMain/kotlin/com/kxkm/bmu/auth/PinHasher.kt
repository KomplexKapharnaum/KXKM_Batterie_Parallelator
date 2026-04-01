package com.kxkm.bmu.auth

import java.security.MessageDigest
import kotlin.random.Random

actual object PinHasher {
    actual fun hash(pin: String, salt: String): String {
        val md = MessageDigest.getInstance("SHA-256")
        val bytes = md.digest("$salt:$pin".toByteArray(Charsets.UTF_8))
        return bytes.joinToString("") { "%02x".format(it) }
    }

    actual fun generateSalt(): String {
        return Random.nextBytes(16).joinToString("") { "%02x".format(it) }
    }
}
