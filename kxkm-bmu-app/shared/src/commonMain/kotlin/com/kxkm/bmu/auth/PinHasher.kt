package com.kxkm.bmu.auth

import kotlin.random.Random

expect object PinHasher {
    fun hash(pin: String, salt: String): String
    fun generateSalt(): String
}
