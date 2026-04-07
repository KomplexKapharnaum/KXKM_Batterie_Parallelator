package com.kxkm.bmu.db

import kotlin.Double
import kotlin.Long

public data class Fleet_health(
  public val id: Long,
  public val fleet_health_score: Double,
  public val outlier_idx: Long,
  public val outlier_score: Double,
  public val imbalance_severity: Double,
  public val timestamp: Long,
)
