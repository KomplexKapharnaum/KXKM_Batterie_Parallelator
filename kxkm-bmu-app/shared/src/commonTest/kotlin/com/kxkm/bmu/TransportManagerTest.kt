package com.kxkm.bmu

import com.kxkm.bmu.model.TransportChannel
import com.kxkm.bmu.transport.TransportManager
import kotlin.test.Test
import kotlin.test.assertEquals

class TransportManagerTest {
    @Test
    fun fallbackOrder() {
        val priority = TransportManager.PRIORITY_ORDER
        assertEquals(TransportChannel.BLE, priority[0])
        assertEquals(TransportChannel.WIFI, priority[1])
        assertEquals(TransportChannel.MQTT_CLOUD, priority[2])
        assertEquals(TransportChannel.OFFLINE, priority[3])
    }
}
