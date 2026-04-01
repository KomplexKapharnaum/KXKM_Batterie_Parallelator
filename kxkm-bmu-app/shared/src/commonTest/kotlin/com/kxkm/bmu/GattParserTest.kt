package com.kxkm.bmu

import com.kxkm.bmu.model.BatteryStatus
import com.kxkm.bmu.transport.GattParser
import kotlin.test.Test
import kotlin.test.assertEquals

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
}
