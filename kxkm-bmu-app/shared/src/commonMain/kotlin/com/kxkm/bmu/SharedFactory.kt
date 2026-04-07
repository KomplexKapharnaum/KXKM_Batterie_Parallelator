package com.kxkm.bmu

import com.kxkm.bmu.auth.AuthUseCase
import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.db.DriverFactory
import com.kxkm.bmu.domain.*
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.sync.SyncManager
import com.kxkm.bmu.transport.*

class SharedFactory(driverFactory: DriverFactory) {
    private val db = DatabaseHelper(driverFactory)
    private val ble = BleTransport()
    private val offline = OfflineTransport(db)

    private var wifi: WifiTransport? = null
    private var mqtt: MqttTransport? = null
    private var restClient: CloudRestClient? = null

    val transportManager = TransportManager(ble, wifi, mqtt, offline)
    val authUseCase = AuthUseCase(db)
    val auditUseCase = AuditUseCase(db)

    private var currentUserId: String = "unknown"

    fun setCurrentUser(userId: String) { currentUserId = userId }

    fun configureWifi(baseUrl: String, token: String) {
        wifi = WifiTransport(baseUrl, token)
        transportManager.setWifi(wifi)
    }

    fun configureCloud(apiUrl: String, apiKey: String, mqttBroker: String,
                       mqttUser: String, mqttPass: String) {
        restClient = CloudRestClient(apiUrl, apiKey)
        mqtt = MqttTransport(mqttBroker, mqttUser, mqttPass)
        transportManager.setMqtt(mqtt)
    }

    val sohRestClient = SohRestClient(
        baseUrl = "http://kxkm-ai:8400",
        apiKey = ""
    )

    val sohUseCase = SohUseCase(
        restClient = sohRestClient,
        db = db
    )

    fun createMonitoringUseCase() = MonitoringUseCase(transportManager, db)
    fun createControlUseCase() = ControlUseCase(transportManager, auditUseCase) { currentUserId }
    fun createConfigUseCase() = ConfigUseCase(transportManager, auditUseCase) { currentUserId }
    fun createSyncManager() = SyncManager(db, restClient)

    /** Bridge for platform-specific MQTT clients (iOS NWConnection, Android Paho) */
    fun onCloudMqttMessage(topic: String, payload: String) {
        mqtt?.onMqttMessage(topic, payload)
    }

    fun setMqttConnected(connected: Boolean) {
        mqtt?.setConnected(connected)
    }

    fun close() {
        transportManager.close()
    }

    companion object {
        // iOS needs a companion object factory since constructors with expect/actual are complex
        fun create(driverFactory: DriverFactory): SharedFactory = SharedFactory(driverFactory)
    }
}
