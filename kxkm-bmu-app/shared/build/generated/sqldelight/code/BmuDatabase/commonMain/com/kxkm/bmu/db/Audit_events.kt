package com.kxkm.bmu.db

import kotlin.Long
import kotlin.String

public data class Audit_events(
  public val id: Long,
  public val timestamp: Long,
  public val user_id: String,
  public val action: String,
  public val target: Long?,
  public val detail: String?,
  public val synced: Long,
)
