package com.kxkm.bmu.domain

import com.kxkm.bmu.db.BatteryHistoryPoint
import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.*
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.datetime.Clock

class MonitoringUseCase(
    private val transport: TransportManager,
    private val db: DatabaseHelper
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    /** Start recording history snapshots every 10s */
    fun startRecording() {
        scope.launch {
            transport.observeBatteries().collect { batteries ->
                batteries.forEach { db.insertHistory(it) }
                delay(10_000)
            }
        }
        // Purge old history daily
        scope.launch {
            while (true) {
                val sevenDaysAgo = Clock.System.now().toEpochMilliseconds() - 7 * 86400 * 1000L
                db.purgeOldHistory(sevenDaysAgo)
                delay(86400_000) // daily
            }
        }
    }

    fun observeBatteries(): Flow<List<BatteryState>> = transport.observeBatteries()

    fun observeBattery(index: Int): Flow<BatteryState?> =
        transport.observeBatteries().map { it.firstOrNull { b -> b.index == index } }

    fun observeSystem(): Flow<SystemInfo?> = transport.observeSystem()
    fun observeSolar(): Flow<SolarData?> = transport.observeSolar()

    fun getHistory(batteryIndex: Int, hours: Int): List<BatteryHistoryPoint> {
        val sinceMs = Clock.System.now().toEpochMilliseconds() - hours * 3600 * 1000L
        return db.getHistory(batteryIndex, sinceMs)
    }

    /** Callback-based API for iOS/Android ViewModels */
    fun observeBatteries(callback: (List<BatteryState>) -> Unit) {
        scope.launch {
            observeBatteries().collect { callback(it) }
        }
    }

    fun observeBattery(index: Int, callback: (BatteryState?) -> Unit) {
        scope.launch {
            observeBattery(index).collect { callback(it) }
        }
    }

    fun observeSystem(callback: (SystemInfo?) -> Unit) {
        scope.launch {
            observeSystem().collect { callback(it) }
        }
    }

    fun observeSolar(callback: (SolarData?) -> Unit) {
        scope.launch {
            observeSolar().collect { callback(it) }
        }
    }

    fun getHistory(batteryIndex: Int, hours: Int, callback: (List<BatteryHistoryPoint>) -> Unit) {
        scope.launch {
            callback(getHistory(batteryIndex, hours))
        }
    }
}
