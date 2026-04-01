package com.kxkm.bmu.auth

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.UserProfile
import com.kxkm.bmu.model.UserRole

class AuthUseCase(private val db: DatabaseHelper) {
    fun hasNoUsers(): Boolean = db.countUsers() == 0L

    fun authenticate(pin: String): UserProfile? {
        val allUsers = db.getAllUsers()
        for (user in allUsers) {
            val hash = PinHasher.hash(pin, user.salt)
            if (hash == user.pinHash) return user
        }
        return null
    }

    fun createUser(name: String, pin: String, role: UserRole) {
        val salt = PinHasher.generateSalt()
        val hash = PinHasher.hash(pin, salt)
        val id = "user_${currentTimeMillis()}"
        db.insertUser(UserProfile(id, name, role, hash, salt))
    }

    fun deleteUser(userId: String) = db.deleteUser(userId)
    fun getAllUsers(): List<UserProfile> = db.getAllUsers()
}

// expect/actual for System.currentTimeMillis equivalent
expect fun currentTimeMillis(): Long
