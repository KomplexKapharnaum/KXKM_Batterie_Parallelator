package com.kxkm.bmu.db

import kotlin.Long
import kotlin.String

public data class Battery_history(
  public val id: Long,
  public val timestamp: Long,
  public val battery_index: Long,
  public val voltage_mv: Long,
  public val current_ma: Long,
  public val state: String,
  public val ah_discharge_mah: Long,
  public val ah_charge_mah: Long,
)
