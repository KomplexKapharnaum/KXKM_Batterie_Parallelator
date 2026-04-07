package com.kxkm.bmu.db

import kotlin.Long
import kotlin.String

public data class Sync_queue(
  public val id: Long,
  public val type: String,
  public val payload: String,
  public val created_at: Long,
  public val retry_count: Long,
)
