package com.kxkm.bmu.db

import kotlin.Long
import kotlin.String

public data class Diagnostics(
  public val id: Long,
  public val battery_index: Long,
  public val diagnostic_text: String,
  public val severity: String,
  public val generated_at: Long,
)
