package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*

object GattParser {
    /** Parse 15-byte battery characteristic (little-endian) */
    fun parseBattery(index: Int, bytes: ByteArray): BatteryState {
        require(bytes.size >= 15) { "Battery payload must be >= 15 bytes, got ${bytes.size}" }
        return BatteryState(
            index = index,
            voltageMv = readInt32LE(bytes, 0),
            currentMa = readInt32LE(bytes, 4),
            state = BatteryStatus.entries.getOrElse(bytes[8].toInt()) { BatteryStatus.ERROR },
            ahDischargeMah = readInt32LE(bytes, 9),
            ahChargeMah = readInt32LE(bytes, 13),
            nbSwitch = if (bytes.size > 17) bytes[17].toInt() and 0xFF else 0
        )
    }

    /** Parse 3-byte topology: {nb_ina, nb_tca, valid} */
    fun parseTopology(bytes: ByteArray): Triple<Int, Int, Boolean> {
        require(bytes.size >= 3)
        return Triple(
            bytes[0].toInt() and 0xFF,
            bytes[1].toInt() and 0xFF,
            bytes[2].toInt() != 0
        )
    }

    /** Parse system heap (uint32 LE) */
    fun parseUint32(bytes: ByteArray): Long {
        require(bytes.size >= 4)
        return readInt32LE(bytes, 0).toLong() and 0xFFFFFFFFL
    }

    /** Parse solar data (12 bytes) */
    fun parseSolar(bytes: ByteArray): SolarData {
        require(bytes.size >= 12)
        return SolarData(
            batteryVoltageMv = readInt16LE(bytes, 0),
            batteryCurrentMa = readInt16LE(bytes, 2),
            panelVoltageMv = readUInt16LE(bytes, 4),
            panelPowerW = readUInt16LE(bytes, 6),
            chargeState = bytes[8].toInt() and 0xFF,
            yieldTodayWh = readInt32LE(bytes, 9).toLong() and 0xFFFFFFFFL,
            valid = bytes.size <= 12 || bytes[12].toInt() != 0
        )
    }

    /** Parse WiFi status (50 bytes) */
    fun parseWifiStatus(bytes: ByteArray): WifiStatusInfo {
        val ssid = bytes.copyOfRange(0, 32).takeWhile { it != 0.toByte() }
            .toByteArray().decodeToString()
        val ip = bytes.copyOfRange(32, 48).takeWhile { it != 0.toByte() }
            .toByteArray().decodeToString()
        val rssi = bytes[48].toInt()
        val connected = bytes[49].toInt() != 0
        return WifiStatusInfo(ssid, ip, rssi, connected)
    }

    /** Encode WiFi config: SSID (32B) + password (64B) = 96 bytes */
    fun encodeWifiConfig(ssid: String, password: String): ByteArray {
        val buf = ByteArray(96)
        val ssidBytes = ssid.encodeToByteArray()
        val passBytes = password.encodeToByteArray()
        ssidBytes.copyInto(buf, 0, 0, minOf(ssidBytes.size, 32))
        passBytes.copyInto(buf, 32, 0, minOf(passBytes.size, 64))
        return buf
    }

    /** Encode switch command: {battery_idx, on_off} */
    fun encodeSwitch(batteryIndex: Int, on: Boolean): ByteArray {
        return byteArrayOf(batteryIndex.toByte(), if (on) 1 else 0)
    }

    /** Encode reset command: {battery_idx} */
    fun encodeReset(batteryIndex: Int): ByteArray {
        return byteArrayOf(batteryIndex.toByte())
    }

    /** Encode protection config: 4x uint16 LE */
    fun encodeConfig(config: ProtectionConfig): ByteArray {
        val buf = ByteArray(8)
        writeUInt16LE(buf, 0, config.minMv)
        writeUInt16LE(buf, 2, config.maxMv)
        writeUInt16LE(buf, 4, config.maxMa)
        writeUInt16LE(buf, 6, config.diffMv)
        return buf
    }

    /** Parse command status response: {last_cmd, battery_idx, result} */
    fun parseCommandStatus(bytes: ByteArray): CommandResult {
        require(bytes.size >= 3)
        val result = bytes[2].toInt() and 0xFF
        return if (result == 0) CommandResult.ok()
        else CommandResult.error("code=$result")
    }

    /** Parse single SOH characteristic (7 bytes) from buffer at offset */
    fun parseSohSingle(index: Int, bytes: ByteArray, offset: Int): BatteryHealth {
        val o = offset
        return BatteryHealth(
            index = index,
            sohPercent = bytes[o].toInt() and 0xFF,
            rOhmicMohm = readUInt16LE(bytes, o + 1) / 10.0f,
            rTotalMohm = readUInt16LE(bytes, o + 3) / 10.0f,
            rintValid = bytes[o + 5].toInt() != 0,
            sohConfidence = bytes[o + 6].toInt() and 0xFF
        )
    }

    /** Parse concatenated SOH characteristics (7 bytes per battery) */
    fun parseSohAll(bytes: ByteArray): List<BatteryHealth> {
        val perBattery = 7
        val count = bytes.size / perBattery
        return (0 until count).map { i ->
            parseSohSingle(i, bytes, i * perBattery)
        }
    }

    /** Data class for R_int BLE result (per battery, 11 bytes) */
    data class RintResult(
        val index: Int,
        val rOhmicMohm: Float,
        val rTotalMohm: Float,
        val vLoadMv: Int,
        val vOcvMv: Int,
        val iLoadMa: Int,
        val rintValid: Boolean
    )

    /** Parse single R_int result (11 bytes) from buffer at offset */
    fun parseRintSingle(index: Int, bytes: ByteArray, offset: Int): RintResult {
        val o = offset
        return RintResult(
            index = index,
            rOhmicMohm = readUInt16LE(bytes, o) / 10.0f,
            rTotalMohm = readUInt16LE(bytes, o + 2) / 10.0f,
            vLoadMv = readUInt16LE(bytes, o + 4),
            vOcvMv = readUInt16LE(bytes, o + 6),
            iLoadMa = readInt16LE(bytes, o + 8),
            rintValid = bytes[o + 10].toInt() != 0
        )
    }

    /** Parse concatenated R_int results (11 bytes per battery) */
    fun parseRintAll(bytes: ByteArray): List<RintResult> {
        val perBattery = 11
        val count = bytes.size / perBattery
        return (0 until count).map { i ->
            parseRintSingle(i, bytes, i * perBattery)
        }
    }

    /** Encode R_int trigger command: single byte = battery index (0xFF = all) */
    fun encodeRintTrigger(batteryIndex: Int): ByteArray {
        return byteArrayOf(
            if (batteryIndex < 0) 0xFF.toByte() else batteryIndex.toByte()
        )
    }

    // -- Little-endian helpers --

    private fun readInt32LE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or
        ((b[off + 1].toInt() and 0xFF) shl 8) or
        ((b[off + 2].toInt() and 0xFF) shl 16) or
        ((b[off + 3].toInt() and 0xFF) shl 24)

    private fun readInt16LE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    private fun readUInt16LE(b: ByteArray, off: Int): Int =
        readInt16LE(b, off) and 0xFFFF

    private fun writeUInt16LE(b: ByteArray, off: Int, value: Int) {
        b[off] = (value and 0xFF).toByte()
        b[off + 1] = ((value shr 8) and 0xFF).toByte()
    }
}
