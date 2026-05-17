// =============================================================
// esp32_espnow.cpp  —  ESP32 #2: UART receiver + ESP-NOW Master
//
// Wiring to ESP32 #1:
//   GPIO16 (RX2)  ←  ESP32#1 GPIO17 (TX2)
//   GPIO17 (TX2)  →  ESP32#1 GPIO16 (RX2)
//   GND           —  GND
//
// Board (Arduino IDE):
//   Standard "ESP32 Dev Module" — Espressif Arduino-ESP32 package
//   No Bluepad32 dependency on this node.
//
// Role in the system:
//   ┌──────────┐  UART  ┌───────────────┐  ESP-NOW  ┌────────────┐
//   │ ESP32 #1 ├───────►│   ESP32 #2    ├──────────►│   Slave(s) │
//   │ Bluepad32│◄───────│  (this file)  │◄──────────│            │
//   └──────────┘        │   ESP-NOW     │           └────────────┘
//                       │   MASTER      │
//                       └───────────────┘
//
// ESP-NOW master behaviour (mirrors master reference code):
//   • Maintains a list of up to MAX_SLAVES known slaves.
//   • Slaves register via MSG_REGISTER (broadcast); master replies MSG_ACK.
//   • Button on BUTTON_PIN cycles the active slave (ACTIVATE / DEACTIVATE).
//   • Master sends MSG_ACTIVATE to one slave at a time and waits for
//     MSG_ACTIVATE_ACK. If no ACK arrives within 2 s the slave is removed.
//   • Gamepad state is encoded into DataPayload.data[32] and forwarded to
//     the active slave as MSG_DATA whenever a new PKT_STATE arrives via UART.
//
// DataPayload.data layout (14 of 32 bytes used):
//   [0]      pad_id          uint8_t   controller slot 0-3
//   [1]      event           uint8_t   0=state 1=connect 2=disconnect
//   [2-13]   StatePayload    12 bytes  raw gamepad state (see protocol.h)
//   [14-31]  reserved / zero
//   DataPayload.value = millis() cast to float (send timestamp)
//
// On PKT_CONNECT from ESP32 #1:
//   • A player-ID (1-4) is assigned and sent back as PKT_SET_PLAYER so
//     Bluepad32 lights the correct LED on the physical controller.
//
// OLED is NOT used here (master reference has one; omit to keep this
// file self-contained). Add it back following the reference master code
// if your hardware has an SSD1306 display.
// =============================================================

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "protocol.h"         // UART frame types + StatePayload + RxParser
#include "espnow_protocol.h"  // EspNowMsg, MsgType, makeSimple, makeData, …

// ── Hardware ─────────────────────────────────────────────────
static constexpr uint8_t  UART2_TX_PIN          = 17;
static constexpr uint8_t  UART2_RX_PIN          = 16;
static constexpr uint8_t  BUTTON_PIN            = 0;    // BOOT button
static constexpr uint32_t CONTROLLER_TIMEOUT_MS = 2000;
static constexpr int      NUM_SLOTS             = 4;

// ── DataPayload.data byte offsets ─────────────────────────────
// Keep in sync with any slave that decodes the payload.
static constexpr uint8_t DP_PAD_ID    = 0;   // 1 byte
static constexpr uint8_t DP_EVENT     = 1;   // 1 byte  0=state 1=connect 2=disconnect
static constexpr uint8_t DP_STATE     = 2;   // 12 bytes (StatePayload)

// ── ESP-NOW slave list ────────────────────────────────────────
#define MAX_SLAVES 20

struct Slave {
    uint8_t  mac[6];
    char     name[NAME_LEN];
    uint8_t  nodeId;
    uint32_t lastSeen;
};

static Slave    slaves[MAX_SLAVES];
static int      slaveCount = 0;
static int      activeSlot = -1;   // index into slaves[], -1 = none

// ── ACTIVATE handshake state ──────────────────────────────────
static bool     waitingForActivateAck = false;
static uint32_t activateSentAt        = 0;
static int      pendingSlot           = -1;

// ── Button debounce ───────────────────────────────────────────
static bool     lastButtonState = HIGH;
static uint32_t lastDebounce    = 0;

// ── Controller mirror (UART side) ─────────────────────────────
struct GamepadSlot {
    bool         connected     = false;
    uint8_t      player_id     = 0;
    StatePayload state         = {};
    uint32_t     last_state_ms = 0;
};
static GamepadSlot gSlots[NUM_SLOTS];

// ── UART parser ───────────────────────────────────────────────
static RxParser gRxFwd;

// ── Player-ID counter (1-4, wraps) ────────────────────────────
static uint8_t gNextPlayerId = 1;
static inline uint8_t assignPlayerId() {
    uint8_t id = gNextPlayerId;
    gNextPlayerId = static_cast<uint8_t>((gNextPlayerId % 4) + 1);
    return id;
}

// =============================================================
// ESP-NOW slave management  (mirrors reference master code)
// =============================================================

static void removeSlave(int slot) {
    Serial.printf("[MASTER] Slave '%s' removed\n", slaves[slot].name);
    esp_now_del_peer(slaves[slot].mac);
    if (slot == activeSlot) activeSlot = -1;
    // compact array: overwrite with last entry
    slaves[slot] = slaves[--slaveCount];
    if (activeSlot == slaveCount) activeSlot = slot;
}

static void addSlave(const uint8_t* mac, const char* name, uint8_t nodeId) {
    // refresh lastSeen if already known
    for (int i = 0; i < slaveCount; i++) {
        if (memcmp(slaves[i].mac, mac, 6) == 0) {
            slaves[i].lastSeen = millis();
            return;
        }
    }
    if (slaveCount >= MAX_SLAVES) return;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) return;

    memcpy(slaves[slaveCount].mac, mac, 6);
    strncpy(slaves[slaveCount].name,
            (strlen(name) > 0) ? name : "Unknown",
            NAME_LEN - 1);
    slaves[slaveCount].nodeId   = nodeId;
    slaves[slaveCount].lastSeen = millis();
    slaveCount++;

    Serial.printf("[MASTER] New slave '%s' nodeId=%d (total: %d)\n",
                  name, nodeId, slaveCount);
}

// =============================================================
// ACTIVATE / DEACTIVATE  (mirrors reference master code)
// =============================================================

static void sendActivate(int slot) {
    EspNowMsg msg = makeSimple(MSG_ACTIVATE);
    esp_now_send(slaves[slot].mac,
                 reinterpret_cast<uint8_t*>(&msg),
                 msgSize(MSG_ACTIVATE));

    waitingForActivateAck = true;
    activateSentAt        = millis();
    pendingSlot           = slot;

    Serial.printf("[MASTER] ACTIVATE -> '%s'\n", slaves[slot].name);
}

static void selectNextSlave() {
    if (slaveCount == 0)       { Serial.println("[MASTER] No slaves"); return; }
    if (waitingForActivateAck) { Serial.println("[MASTER] Waiting for ACK"); return; }

    // deactivate current slave
    if (activeSlot >= 0 && activeSlot < slaveCount) {
        EspNowMsg deact = makeSimple(MSG_DEACTIVATE);
        esp_now_send(slaves[activeSlot].mac,
                     reinterpret_cast<uint8_t*>(&deact),
                     msgSize(MSG_DEACTIVATE));
        Serial.printf("[MASTER] DEACTIVATE -> '%s'\n", slaves[activeSlot].name);
        activeSlot = -1;
    }

    // advance ring: +slaveCount keeps modulo positive when activeSlot == -1
    sendActivate((activeSlot + 1 + slaveCount) % slaveCount);
}

// =============================================================
// Forward gamepad state to the active slave via MSG_DATA
// =============================================================

// Encode pad_id + event + StatePayload into DataPayload.data[32]
static void buildDataPayload(uint8_t pad_id, uint8_t event,
                              const StatePayload* state,
                              uint8_t out[32]) {
    memset(out, 0, 32);
    out[DP_PAD_ID] = pad_id;
    out[DP_EVENT]  = event;
    if (state != nullptr)
        memcpy(&out[DP_STATE], state->data, sizeof(StatePayload)); // 12 bytes
}

static void forwardToSlave(uint8_t pad_id, uint8_t event,
                            const StatePayload* state) {
    if (activeSlot < 0 || activeSlot >= slaveCount) return;

    uint8_t buf[32];
    buildDataPayload(pad_id, event, state, buf);

    // value carries the send timestamp so the slave can measure latency
    EspNowMsg msg = makeData(buf, 32, static_cast<float>(millis()));
    esp_now_send(slaves[activeSlot].mac,
                 reinterpret_cast<uint8_t*>(&msg),
                 msgSize(MSG_DATA));
}

// =============================================================
// ESP-NOW callbacks
// =============================================================

static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS)
        Serial.printf("[ESPNOW] Send failed -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Matches ESP_NOW_RECV_CB_ARGS / ESP_NOW_SRC_MAC from espnow_protocol.h
static void espnow_recv_cb(ESP_NOW_RECV_CB_ARGS,
                            const uint8_t* data, int len) {
    EspNowMsg msg;
    if (!parseMsg(data, len, msg)) return;

    const uint8_t* srcMAC = ESP_NOW_SRC_MAC;

    switch (msg.msgType) {

        case MSG_REGISTER: {
            // Slave announced itself — add to list and reply ACK
            msg.payload.reg.name[NAME_LEN - 1] = '\0';
            addSlave(srcMAC, msg.payload.reg.name, msg.payload.reg.nodeId);

            EspNowMsg ack = makeSimple(MSG_ACK);
            esp_now_send(srcMAC,
                         reinterpret_cast<uint8_t*>(&ack),
                         msgSize(MSG_ACK));
            break;
        }

        case MSG_ACTIVATE_ACK: {
            // Slave confirmed activation
            if (!waitingForActivateAck) break;
            if (pendingSlot < 0 || pendingSlot >= slaveCount) break;
            if (memcmp(srcMAC, slaves[pendingSlot].mac, 6) != 0) break;

            activeSlot            = pendingSlot;
            waitingForActivateAck = false;
            Serial.printf("[MASTER] Slave '%s' is now active\n",
                          slaves[activeSlot].name);
            break;
        }

        default:
            break;
    }
}

// =============================================================
// Controller registration (UART side)
// =============================================================

static void registerController(uint8_t pad_id) {
    if (pad_id >= NUM_SLOTS) return;

    const uint8_t player_id      = assignPlayerId();
    gSlots[pad_id].player_id     = player_id;
    gSlots[pad_id].last_state_ms = millis();

    Serial.printf("[REG] Slot %d -> player-ID %d\n", pad_id, player_id);

    // Notify active slave of new controller connection (event=1)
    forwardToSlave(pad_id, 1, nullptr);

    // Tell ESP32 #1 which LED to light on the physical controller
    SetPlayerPayload sp = { player_id };
    send_frame(Serial2, PKT_SET_PLAYER, pad_id, &sp, sizeof(sp));
}

static void unregisterController(uint8_t pad_id) {
    if (pad_id >= NUM_SLOTS)        return;
    if (!gSlots[pad_id].connected)  return;

    Serial.printf("[REG] Slot %d disconnected (was player %d)\n",
                  pad_id, gSlots[pad_id].player_id);

    // Notify active slave of disconnect (event=2)
    forwardToSlave(pad_id, 2, nullptr);

    gSlots[pad_id].connected = false;
    gSlots[pad_id].player_id = 0;
    memset(&gSlots[pad_id].state, 0, sizeof(StatePayload));
}

// =============================================================
// Incoming UART packet handler
// =============================================================

static void handleIncoming(uint8_t type, uint8_t pad_id,
                            uint8_t* data, uint8_t len) {
    if (pad_id >= NUM_SLOTS) {
        Serial.printf("[UART] pad_id %d out of range\n", pad_id);
        return;
    }

    switch (type) {

        case PKT_CONNECT:
            Serial.printf("[UART] CONNECT  slot=%d\n", pad_id);
            gSlots[pad_id].connected     = true;
            gSlots[pad_id].last_state_ms = millis();
            registerController(pad_id);
            break;

        case PKT_DISCONNECT:
            Serial.printf("[UART] DISCONNECT slot=%d\n", pad_id);
            unregisterController(pad_id);
            break;

        case PKT_STATE: {
            if (len != sizeof(StatePayload)) {
                Serial.printf("[UART] STATE wrong length %d (expected %d)\n",
                              len, static_cast<int>(sizeof(StatePayload)));
                break;
            }
            memcpy(gSlots[pad_id].state.data, data, sizeof(StatePayload));
            gSlots[pad_id].last_state_ms = millis();

            // Forward to active slave as MSG_DATA (event=0)
            forwardToSlave(pad_id, 0, &gSlots[pad_id].state);
            break;
        }

        case PKT_PING:
            // Heartbeat — UART link to ESP32 #1 is alive
            break;

        default:
            Serial.printf("[UART] Unknown packet type 0x%02X\n", type);
            break;
    }
}

// =============================================================
// Watchdog: drop controllers that stop sending STATE
// =============================================================

static void checkTimeouts() {
    const uint32_t now = millis();
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (!gSlots[i].connected) continue;
        if (now - gSlots[i].last_state_ms > CONTROLLER_TIMEOUT_MS) {
            Serial.printf("[WDG] Timeout on slot %d\n", i);
            unregisterController(i);
        }
    }
}

// =============================================================
// Debug dump
// =============================================================

static void printStatus() {
    Serial.printf("[STATUS] Slaves:%d  Active:%d  Waiting:%s\n",
                  slaveCount,
                  activeSlot,
                  waitingForActivateAck ? "yes" : "no");

    for (int i = 0; i < NUM_SLOTS; i++) {
        if (!gSlots[i].connected) continue;
        const auto& p = gSlots[i].state.part;
        Serial.printf(
            "  Slot%d P%d | "
            "UP=%d DN=%d LT=%d RT=%d | "
            "TRI=%d CRS=%d SQR=%d CIR=%d | "
            "L1=%d R1=%d L3=%d R3=%d | "
            "SEL=%d STA=%d SYS=%d BCK=%d CAP=%d | "
            "LX=%4d LY=%4d RX=%4d RY=%4d | "
            "L2=%3d R2=%3d\n",
            i, gSlots[i].player_id,
            p.btn_dpad_up,  p.btn_dpad_down, p.btn_dpad_left, p.btn_dpad_right,
            p.btn_triangle, p.btn_cross,     p.btn_square,    p.btn_circle,
            p.btn_l1,       p.btn_r1,        p.btn_thumb_l,   p.btn_thumb_r,
            p.btn_select,   p.btn_start,     p.btn_system,    p.btn_back,
            p.btn_capture,
            p.analog_lx,  p.analog_ly,  p.analog_rx,  p.analog_ry,
            p.analog_l2,  p.analog_r2
        );
    }
}

// =============================================================
// Arduino entry points
// =============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("[ESP32#2] ESP-NOW Master node starting");

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // UART2 to ESP32 #1
    Serial2.begin(UART_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

    // ESP-NOW init
    WiFi.mode(WIFI_STA);
    Serial.printf("[ESP32#2] MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init failed — halting");
        while (true) delay(100);
    }
    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // Broadcast peer — needed to send MSG_ACK before a slave is
    // registered as a unicast peer (matches reference master setup)
    uint8_t bcast[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_peer_info_t bcastPeer = {};
    memcpy(bcastPeer.peer_addr, bcast, 6);
    bcastPeer.channel = 0;
    bcastPeer.encrypt = false;
    esp_now_add_peer(&bcastPeer);

    Serial.println("[ESP32#2] Ready — waiting for slaves and UART packets");
}

void loop() {
    // ── UART: drain RX and handle packets ────────────────────
    while (Serial2.available()) {
        if (rx_feed(gRxFwd, static_cast<uint8_t>(Serial2.read()))) {
            handleIncoming(gRxFwd.type, gRxFwd.pad_id,
                           gRxFwd.buf,  gRxFwd.len);
        }
    }

    // ── Button: cycle active slave ────────────────────────────
    bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastButtonState) lastDebounce = millis();
    if ((millis() - lastDebounce) > 50 &&
         reading == LOW && lastButtonState == HIGH) {
        selectNextSlave();
    }
    lastButtonState = reading;

    // ── ACTIVATE timeout: remove unresponsive slave ───────────
    if (waitingForActivateAck &&
        (millis() - activateSentAt > 2000)) {
        Serial.printf("[MASTER] Timeout — removing slave '%s'\n",
                      slaves[pendingSlot].name);
        waitingForActivateAck = false;
        removeSlave(pendingSlot);
        if (slaveCount > 0) selectNextSlave();
    }

    // ── Watchdog: detect dead controllers ────────────────────
    static uint32_t lastWdg = 0;
    if (millis() - lastWdg >= 250) {
        checkTimeouts();
        lastWdg = millis();
    }

    // ── Debug print every 2 s ────────────────────────────────
    static uint32_t lastDbg = 0;
    if (millis() - lastDbg >= 2000) {
        printStatus();
        lastDbg = millis();
    }
}