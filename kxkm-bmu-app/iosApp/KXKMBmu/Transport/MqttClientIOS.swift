import Foundation
import Network

/// Client MQTT 3.1.1 minimal sur TCP (Network framework).
/// Implémente uniquement CONNECT, SUBSCRIBE, PUBLISH (réception), PING.
final class MqttClientIOS {
    private let host: String
    private let port: UInt16
    private let clientId: String
    private let username: String?
    private let password: String?
    private let topic: String

    var onMessage: ((String, String) -> Void)?
    var onConnectionChange: ((Bool) -> Void)?

    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "mqtt.client", qos: .utility)
    private var pingTimer: DispatchSourceTimer?
    private var packetId: UInt16 = 1
    private var isConnected = false
    private var shouldReconnect = true

    init(host: String, port: UInt16 = 1883, clientId: String = "kxkm-bmu-ios",
         username: String? = nil, password: String? = nil,
         topic: String = "bmu/+/battery/#") {
        self.host = host
        self.port = port
        self.clientId = clientId
        self.username = username
        self.password = password
        self.topic = topic
    }

    // MARK: - Public API

    func connect() {
        shouldReconnect = true
        startConnection()
    }

    func disconnect() {
        shouldReconnect = false
        stopPing()
        connection?.cancel()
        connection = nil
        setConnected(false)
    }

    // MARK: - Connection

    private func startConnection() {
        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host),
            port: NWEndpoint.Port(rawValue: port)!
        )
        let conn = NWConnection(to: endpoint, using: .tcp)
        self.connection = conn

        conn.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                self?.sendConnect()
            case .failed, .cancelled:
                self?.handleDisconnect()
            default:
                break
            }
        }
        conn.start(queue: queue)
    }

    private func handleDisconnect() {
        stopPing()
        setConnected(false)
        guard shouldReconnect else { return }
        queue.asyncAfter(deadline: .now() + 5) { [weak self] in
            self?.startConnection()
        }
    }

    private func setConnected(_ connected: Bool) {
        guard isConnected != connected else { return }
        isConnected = connected
        DispatchQueue.main.async { [weak self] in
            self?.onConnectionChange?(connected)
        }
    }

    // MARK: - MQTT Protocol: CONNECT (packet type 1)

    private func sendConnect() {
        var payload = Data()
        // Variable header: protocol name + version + flags + keepalive
        payload.appendMqttString("MQTT")
        payload.append(4) // Protocol level 3.1.1
        var flags: UInt8 = 0x02 // Clean session
        if username != nil { flags |= 0x80 }
        if password != nil { flags |= 0x40 }
        payload.append(flags)
        payload.appendUInt16(60) // Keepalive 60s
        // Payload: client ID, username, password
        payload.appendMqttString(clientId)
        if let u = username { payload.appendMqttString(u) }
        if let p = password { payload.appendMqttString(p) }

        send(packetType: 0x10, payload: payload)
        receiveLoop()
    }

    // MARK: - MQTT Protocol: SUBSCRIBE (packet type 8)

    private func sendSubscribe() {
        var payload = Data()
        payload.appendUInt16(nextPacketId())
        payload.appendMqttString(topic)
        payload.append(0) // QoS 0
        send(packetType: 0x82, payload: payload) // 0x82 = SUBSCRIBE + reserved bit 1
    }

    // MARK: - MQTT Protocol: PINGREQ (packet type 12)

    private func startPing() {
        stopPing()
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + 45, repeating: 45)
        timer.setEventHandler { [weak self] in
            self?.send(packetType: 0xC0, payload: Data()) // PINGREQ
        }
        timer.resume()
        pingTimer = timer
    }

    private func stopPing() {
        pingTimer?.cancel()
        pingTimer = nil
    }

    // MARK: - Receive loop

    private func receiveLoop() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 8192) {
            [weak self] data, _, isComplete, error in
            guard let self, let data, !data.isEmpty else {
                if isComplete || error != nil { self?.handleDisconnect() }
                return
            }
            self.processData(data)
            if self.isConnected || !data.isEmpty {
                self.receiveLoop()
            }
        }
    }

    private var rxBuffer = Data()

    private func processData(_ data: Data) {
        rxBuffer.append(data)
        while let (packet, consumed) = extractPacket(from: rxBuffer) {
            rxBuffer.removeFirst(consumed)
            handlePacket(packet)
        }
    }

    /// Extraie un paquet MQTT complet du buffer. Retourne (fixedHeader+payload, bytesConsumed).
    private func extractPacket(from buf: Data) -> (Data, Int)? {
        guard buf.count >= 2 else { return nil }
        var idx = 1
        var remaining = 0
        var multiplier = 1
        repeat {
            guard idx < buf.count else { return nil }
            let byte = buf[idx]
            remaining += Int(byte & 0x7F) * multiplier
            multiplier *= 128
            idx += 1
        } while buf[idx - 1] & 0x80 != 0
        let total = idx + remaining
        guard buf.count >= total else { return nil }
        return (buf.prefix(total), total)
    }

    private func handlePacket(_ packet: Data) {
        let type = packet[0] & 0xF0
        switch type {
        case 0x20: // CONNACK
            if packet.count >= 4 && packet[3] == 0 {
                setConnected(true)
                sendSubscribe()
                startPing()
            } else {
                handleDisconnect()
            }
        case 0x30: // PUBLISH
            parsePUBLISH(packet)
        case 0x90: // SUBACK
            break // Subscription confirmed
        case 0xD0: // PINGRESP
            break
        default:
            break
        }
    }

    // MARK: - PUBLISH parsing

    private func parsePUBLISH(_ packet: Data) {
        // Skip fixed header (type byte + remaining length bytes)
        var idx = 1
        while idx < packet.count && packet[idx] & 0x80 != 0 { idx += 1 }
        idx += 1 // past last remaining length byte

        guard idx + 2 <= packet.count else { return }
        let topicLen = Int(packet[idx]) << 8 | Int(packet[idx + 1])
        idx += 2
        guard idx + topicLen <= packet.count else { return }
        let topicData = packet[idx..<(idx + topicLen)]
        idx += topicLen

        // QoS 0 — pas de packet ID
        let payloadData = packet[idx...]

        guard let topic = String(data: topicData, encoding: .utf8),
              let payload = String(data: payloadData, encoding: .utf8) else { return }

        DispatchQueue.main.async { [weak self] in
            self?.onMessage?(topic, payload)
        }
    }

    // MARK: - Send helpers

    private func send(packetType: UInt8, payload: Data) {
        var packet = Data()
        packet.append(packetType)
        packet.appendRemainingLength(payload.count)
        packet.append(payload)
        connection?.send(content: packet, completion: .contentProcessed { _ in })
    }

    private func nextPacketId() -> UInt16 {
        packetId = packetId &+ 1
        if packetId == 0 { packetId = 1 }
        return packetId
    }
}

// MARK: - Data helpers for MQTT binary encoding

private extension Data {
    mutating func appendMqttString(_ s: String) {
        let bytes = Array(s.utf8)
        appendUInt16(UInt16(bytes.count))
        append(contentsOf: bytes)
    }

    mutating func appendUInt16(_ v: UInt16) {
        append(UInt8(v >> 8))
        append(UInt8(v & 0xFF))
    }

    mutating func appendRemainingLength(_ length: Int) {
        var x = length
        repeat {
            var byte = UInt8(x % 128)
            x /= 128
            if x > 0 { byte |= 0x80 }
            append(byte)
        } while x > 0
    }
}
