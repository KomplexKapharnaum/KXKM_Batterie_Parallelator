package com.kxkm.bmu.db

import kotlin.Double
import kotlin.Long

public data class Ml_scores(
  public val id: Long,
  public val battery_index: Long,
  public val soh_score: Double,
  public val rul_days: Long,
  public val anomaly_score: Double,
  public val r_int_trend: Double,
  public val timestamp: Long,
)
