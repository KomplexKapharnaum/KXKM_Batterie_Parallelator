package com.kxkm.bmu

import com.kxkm.bmu.model.BatteryStatus
import com.kxkm.bmu.transport.GattParser
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.test.assertFalse

class GattParserTest {
    @Test
    fun parseBatteryCharacteristic() {
        // 15 bytes: voltage_mv(i32) + current_ma(i32) + state(u8) + ah_dis(i32) + ah_ch(i32) + nb_sw(u8)
        // voltage = 26500 (0x00006784 LE = 84 67 00 00)
        // current = 5200  (0x00001450 LE = 50 14 00 00)
        // state = 0 (CONNECTED)
        // ah_dis = 1500  (0x000005DC LE = DC 05 00 00)
        // ah_ch = 200    (0x000000C8 LE = C8 00 00 00)
        // nb_sw = 3
        val bytes = byteArrayOf(
            0x84.toByte(), 0x67, 0x00, 0x00,  // voltage 26500
            0x50, 0x14, 0x00, 0x00,            // current 5200
            0x00,                               // state CONNECTED
            0xDC.toByte(), 0x05, 0x00, 0x00,   // ah_dis 1500
            0xC8.toByte(), 0x00, 0x00, 0x00,   // ah_ch 200
            0x03                                // nb_switch 3
        )

        val result = GattParser.parseBattery(0, bytes)
        assertEquals(26500, result.voltageMv)
        assertEquals(5200, result.currentMa)
        assertEquals(BatteryStatus.CONNECTED, result.state)
        assertEquals(1500, result.ahDischargeMah)
        assertEquals(200, result.ahChargeMah)
        assertEquals(3, result.nbSwitch)
    }

    @Test
    fun parseTopology() {
        val bytes = byteArrayOf(4, 1, 1) // nb_ina=4, nb_tca=1, valid=1
        val (nbIna, nbTca, valid) = GattParser.parseTopology(bytes)
        assertEquals(4, nbIna)
        assertEquals(1, nbTca)
        assertEquals(true, valid)
    }

    @Test
    fun encodeWifiConfig() {
        val bytes = GattParser.encodeWifiConfig("MySSID", "MyPassword123")
        assertEquals(96, bytes.size) // 32 + 64
        assertEquals('M'.code.toByte(), bytes[0])
        assertEquals('y'.code.toByte(), bytes[1])
        assertEquals(0, bytes[6].toInt()) // null terminator within 32 bytes
        assertEquals('M'.code.toByte(), bytes[32]) // password starts at offset 32
    }

    @Test
    fun parseSohCharacteristic() {
        // 7 bytes: soh_pct(u8) + r_ohmic_x10(u16 LE) + r_total_x10(u16 LE) + valid(u8) + confidence(u8)
        // soh = 92, r_ohmic = 15.2 mOhm (x10 = 152 = 0x0098 LE = 98 00)
        // r_total = 18.5 mOhm (x10 = 185 = 0x00B9 LE = B9 00), valid = 1, confidence = 100
        val bytes = byteArrayOf(
            92.toByte(),                   // soh_pct
            0x98.toByte(), 0x00,           // r_ohmic_mohm_x10 = 152
            0xB9.toByte(), 0x00,           // r_total_mohm_x10 = 185
            0x01,                          // rint_valid
            100.toByte()                   // soh_confidence
        )

        val health = GattParser.parseSohSingle(0, bytes, 0)
        assertEquals(92, health.sohPercent)
        assertEquals(15.2f, health.rOhmicMohm, 0.15f)
        assertEquals(18.5f, health.rTotalMohm, 0.15f)
        assertTrue(health.rintValid)
        assertEquals(100, health.sohConfidence)
    }

    @Test
    fun parseSohMultipleBatteries() {
        // 2 batteries x 7 bytes = 14 bytes
        val bytes = byteArrayOf(
            // Battery 0: SOH 92, r_ohm 15.2, r_tot 18.5, valid, confidence 100
            92.toByte(), 0x98.toByte(), 0x00, 0xB9.toByte(), 0x00, 0x01, 100.toByte(),
            // Battery 1: SOH 85, r_ohm 22.0, r_tot 30.0, valid, confidence 80
            85.toByte(), 0xDC.toByte(), 0x00, 0x2C, 0x01, 0x01, 80.toByte()
        )

        val list = GattParser.parseSohAll(bytes)
        assertEquals(2, list.size)
        assertEquals(92, list[0].sohPercent)
        assertEquals(85, list[1].sohPercent)
        assertEquals(22.0f, list[1].rOhmicMohm, 0.15f)
        assertEquals(30.0f, list[1].rTotalMohm, 0.15f)
        assertEquals(80, list[1].sohConfidence)
    }

    @Test
    fun parseRintResult() {
        // 11 bytes per battery: r_ohmic_x10(u16) + r_total_x10(u16) + v_load(u16) + v_ocv(u16) + i_load(i16) + valid(u8)
        val bytes = byteArrayOf(
            0x98.toByte(), 0x00,           // r_ohmic_mohm_x10
            0xB9.toByte(), 0x00,           // r_total_mohm_x10
            0x74, 0x67,                    // v_load_mv = 26484
            0x78, 0x69,                    // v_ocv_mv = 26999 (0x6978 LE)
            0x50, 0x14,                    // i_load_ma = 5200
            0x01                           // valid
        )

        val result = GattParser.parseRintSingle(0, bytes, 0)
        assertEquals(15.2f, result.rOhmicMohm, 0.15f)
        assertEquals(18.5f, result.rTotalMohm, 0.15f)
        assertTrue(result.rintValid)
    }
}
