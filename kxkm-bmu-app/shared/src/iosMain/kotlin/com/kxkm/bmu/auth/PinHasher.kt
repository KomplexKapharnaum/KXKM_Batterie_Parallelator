package com.kxkm.bmu.auth

import kotlinx.cinterop.*
import platform.CoreCrypto.CC_SHA256
import platform.CoreCrypto.CC_SHA256_DIGEST_LENGTH
import kotlin.random.Random

actual object PinHasher {
    @OptIn(ExperimentalForeignApi::class)
    actual fun hash(pin: String, salt: String): String {
        val input = "$salt:$pin".encodeToByteArray()
        val digest = UByteArray(CC_SHA256_DIGEST_LENGTH)
        input.usePinned { pinned ->
            digest.usePinned { digestPinned ->
                CC_SHA256(pinned.addressOf(0), input.size.toUInt(), digestPinned.addressOf(0))
            }
        }
        return digest.joinToString("") { it.toString(16).padStart(2, '0') }
    }

    actual fun generateSalt(): String {
        return Random.nextBytes(16).joinToString("") { "%02x".format(it) }
    }
}
