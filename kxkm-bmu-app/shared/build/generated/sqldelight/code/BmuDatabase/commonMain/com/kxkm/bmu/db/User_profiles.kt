package com.kxkm.bmu.db

import kotlin.String

public data class User_profiles(
  public val id: String,
  public val name: String,
  public val role: String,
  public val pin_hash: String,
  public val salt: String,
)
