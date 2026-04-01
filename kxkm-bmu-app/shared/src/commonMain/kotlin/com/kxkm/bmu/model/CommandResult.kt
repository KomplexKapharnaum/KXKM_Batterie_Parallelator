package com.kxkm.bmu.model

data class CommandResult(
    val isSuccess: Boolean,
    val errorMessage: String? = null
) {
    companion object {
        fun ok() = CommandResult(true)
        fun error(msg: String) = CommandResult(false, msg)
    }
}
