package com.kxkm.bmu.transport

import com.juul.kable.*
import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class BleTransport : Transport {
    override val channel = TransportChannel.BLE
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected

    private var peripheral: Peripheral? = null
    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    companion object {
        private const val BMU_DEVICE_NAME = "KXKM-BMU"
        // UUID base: 4b584b4d-xxxx-4b4d-424d-55424c450000
        private fun svcUuid(id: Int) = "4b584b4d-%04x-4b4d-424d-55424c450000".format(id)
        private fun chrUuid(id: Int) = svcUuid(id)

        val BATTERY_SVC = svcUuid(0x0001)
        val SYSTEM_SVC  = svcUuid(0x0002)
        val CONTROL_SVC = svcUuid(0x0003)
    }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        // Scan for KXKM-BMU device
        val advertisement = Scanner {
            filters { match { name = BMU_DEVICE_NAME } }
        }.advertisements.first()

        peripheral = scope.peripheral(advertisement) {
            onServicesDiscovered {
                subscribeToNotifications()
            }
        }
        peripheral?.connect()
        _isConnected.value = true
    }

    override suspend fun disconnect() {
        peripheral?.disconnect()
        _isConnected.value = false
    }

    private suspend fun subscribeToNotifications() {
        val p = peripheral ?: return

        // Subscribe to battery characteristics (0x0010–0x001F)
        for (i in 0..15) {
            val uuid = chrUuid(0x0010 + i)
            scope.launch {
                try {
                    p.observe(characteristicOf(BATTERY_SVC, uuid)).collect { bytes ->
                        val state = GattParser.parseBattery(i, bytes)
                        val current = _batteries.value.toMutableList()
                        val idx = current.indexOfFirst { it.index == i }
                        if (idx >= 0) current[idx] = state else current.add(state)
                        _batteries.value = current.sortedBy { it.index }
                    }
                } catch (_: Exception) { /* characteristic may not exist for unused batteries */ }
            }
        }

        // Subscribe to system notifications
        scope.launch {
            p.observe(characteristicOf(SYSTEM_SVC, chrUuid(0x0021))).collect { bytes ->
                val heap = GattParser.parseUint32(bytes)
                _system.value = _system.value?.copy(heapFree = heap) ?: SystemInfo(
                    firmwareVersion = "", heapFree = heap, uptimeSeconds = 0,
                    wifiIp = null, nbIna = 0, nbTca = 0, topologyValid = false
                )
            }
        }

        // Read static system info once
        scope.launch {
            val fw = p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0020))).decodeToString()
            val uptime = GattParser.parseUint32(p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0022))))
            val ip = p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0023))).decodeToString()
            val (nbIna, nbTca, valid) = GattParser.parseTopology(
                p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0024)))
            )
            _system.value = SystemInfo(fw, _system.value?.heapFree ?: 0, uptime,
                ip.ifEmpty { null }, nbIna, nbTca, valid)
        }

        // Subscribe to solar
        scope.launch {
            p.observe(characteristicOf(SYSTEM_SVC, chrUuid(0x0025))).collect { bytes ->
                _solar.value = GattParser.parseSolar(bytes)
            }
        }
    }

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0030)), GattParser.encodeSwitch(index, on))
        delay(200) // Wait for status notification
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun resetSwitchCount(index: Int): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0031)), GattParser.encodeReset(index))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0032)), GattParser.encodeConfig(config))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0034)), GattParser.encodeWifiConfig(ssid, password))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }
}
