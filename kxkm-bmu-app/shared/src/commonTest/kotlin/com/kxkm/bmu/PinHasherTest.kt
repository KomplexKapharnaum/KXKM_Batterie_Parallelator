package com.kxkm.bmu

import com.kxkm.bmu.auth.PinHasher
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals

class PinHasherTest {
    @Test
    fun hashIsDeterministic() {
        val salt = "test-salt-123"
        val hash1 = PinHasher.hash("123456", salt)
        val hash2 = PinHasher.hash("123456", salt)
        assertEquals(hash1, hash2)
    }

    @Test
    fun differentPinsDifferentHashes() {
        val salt = "test-salt-123"
        val hash1 = PinHasher.hash("123456", salt)
        val hash2 = PinHasher.hash("654321", salt)
        assertNotEquals(hash1, hash2)
    }

    @Test
    fun differentSaltsDifferentHashes() {
        val hash1 = PinHasher.hash("123456", "salt-a")
        val hash2 = PinHasher.hash("123456", "salt-b")
        assertNotEquals(hash1, hash2)
    }
}
