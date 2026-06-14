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
//     MSG_ACTIVATE_ACK. If no ACK arrives within 2 s the slave is removed
//     and the master does not automatically activate the next slave.
//   • Gamepad state is forwarded to the active slave as MSG_GAMEPAD_DATA
//     whenever a new PKT_STATE arrives via UART.
//
// GamepadState layout (12 bytes):
//   [0-9]    raw gamepad state bytes
//   [10]     pad_id        controller slot 0-3
//   [11]     event         0=state 1=connect 2=disconnect
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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "serial_protocol.h"         // UART frame types + GamepadState + RxParser
#include "espnow_protocol.h"  // EspNowMsg, MsgType, makeSimple, makeGamepadData, …
#include "debounced_button.h"

// ── OLED ────────────────────────────────────────────────────
#define OLED_WIDTH  128
#define OLED_HEIGHT  64
#define OLED_RESET   -1
#define OLED_ADDR   0x3C
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
static void displayUpdate();

// ── Hardware ─────────────────────────────────────────────────
static constexpr uint8_t  UART2_TX_PIN          = 17;
static constexpr uint8_t  UART2_RX_PIN          = 16;
static constexpr uint8_t  BUTTON_PIN            = GPIO_NUM_5;    // BOOT button
static constexpr uint32_t CONTROLLER_TIMEOUT_MS = 2000;
static constexpr int      NUM_SLOTS             = 4;
static constexpr uint8_t  LED_PIN               = 2;    // on-board LED (adjust if needed)
static constexpr uint32_t LED_BLINK_MS          = 500;  // blink interval

// ── GamepadState data offsets ─────────────────────────────
// Keep in sync with any slave that decodes the payload.
static constexpr uint8_t DP_PAD_ID    = 10;   // 1 byte
static constexpr uint8_t DP_EVENT     = 11;   // 1 byte  0=state 1=connect 2=disconnect
static constexpr uint8_t DP_STATE     = 0;   // 10 bytes start of raw GamepadState

// ── ESP-NOW slave list ────────────────────────────────────────
#define MAX_SLAVES 20

struct Slave {
    uint8_t  mac[6];
    char     name[NAME_LEN];
    uint32_t lastSeen;
    uint8_t  sendFailCount = 0;
};

static Slave    slaves[MAX_SLAVES];
static int      slaveCount = 0;
static int      activeSlot = -1;   // index into slaves[], -1 = none
static bool     ledState = false;
static uint32_t lastLedToggle = 0;

enum SendType : uint8_t {
    SEND_NONE = 0,
    SEND_HEARTBEAT,
    SEND_ACTIVATE,
    SEND_GAMEPAD,
    SEND_ACK,
    SEND_DEACTIVATE,
};
static SendType lastSendType[MAX_SLAVES] = {};
static bool     sendCallbackSuppressed[MAX_SLAVES] = {};

// ── ACTIVATE handshake state ──────────────────────────────────
static bool     waitingForActivateAck = false;
static uint32_t activateSentAt        = 0;
static int      pendingSlot           = -1;

// ── Gamepad controller handshake ───────────────────────────────
static bool     gamepadControllerReady       = false;
static uint32_t readySentAt           = 0;
static constexpr uint32_t READY_RESEND_MS = 500;
static constexpr uint32_t HEARTBEAT_MS = 3000;
static uint32_t lastHeartbeatSend = 0;
static constexpr uint32_t DISPLAY_PAGE_CHANGE_MS = 4000;
static constexpr uint32_t DISPLAY_REFRESH_MS = 75;
static uint32_t lastDisplayPageChange = 0;
static uint8_t displayPage = 0;  // 0=Controller, 1=Gamepad, 2=Slaves (for error mode)
                                 // or 0=Status, 1=Active, 2=Gamepads (for ok mode)

static DebouncedButton button(BUTTON_PIN, 50);

// ── Controller mirror (UART side) ─────────────────────────────
struct GamepadSlot {
    bool         connected     = false;
    uint8_t      player_id     = 0;
    GamepadState state         = {};
    uint32_t     last_state_ms = 0;
};
static GamepadSlot gSlots[NUM_SLOTS];

// ── UART parser ───────────────────────────────────────────────
static RxParser gRxFwd;

// ── Player-ID counter (1-4, wraps) ────────────────────────────
static uint8_t gNextPlayerId = 1;
// =============================================================
// ESP-NOW slave management  (mirrors reference master code)
// =============================================================

static void removeSlave(int slot) {
    Serial.printf("[MASTER] Slave '%s' removed\n", slaves[slot].name);
    esp_now_del_peer(slaves[slot].mac);

    // If removing the currently active slot, clear activeSlot.
    if (slot == activeSlot) activeSlot = -1;

    int last = slaveCount - 1;
    if (slot != last) {
        // Move last entry into the removed slot to keep array compact
        slaves[slot] = slaves[last];
        lastSendType[slot] = lastSendType[last];
        sendCallbackSuppressed[slot] = sendCallbackSuppressed[last];

        // If any index pointed to the last entry, update it to the new slot
        if (activeSlot == last) activeSlot = slot;
        if (pendingSlot == last) pendingSlot = slot;
    }

    // Clear the old last entry
    lastSendType[last] = SEND_NONE;
    sendCallbackSuppressed[last] = false;
    slaveCount--;

    // If we removed the pending slot, cancel the pending activation
    if (pendingSlot == slot) {
        pendingSlot = -1;
        waitingForActivateAck = false;
    }
}

static void addSlave(const uint8_t* mac, const char* name) {
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
    
    slaves[slaveCount].lastSeen = millis();
    slaveCount++;

    Serial.printf("[MASTER] New slave '%s' (total: %d)\n",
                  name, slaveCount);
}

// =============================================================
// ACTIVATE / DEACTIVATE  (mirrors reference master code)
// =============================================================

static void sendActivate(int slot) {
    EspNowMsg msg = makeSimple(MSG_ACTIVATE);

    lastSendType[slot] = SEND_ACTIVATE;
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

    // compute next slot index first (handles activeSlot == -1 correctly)
    int next = (activeSlot + 1 + slaveCount) % slaveCount;

    // deactivate current slave (if any)
    if (activeSlot >= 0 && activeSlot < slaveCount) {
        EspNowMsg deact = makeSimple(MSG_DEACTIVATE);
        esp_now_send(slaves[activeSlot].mac,
                     reinterpret_cast<uint8_t*>(&deact),
                     msgSize(MSG_DEACTIVATE));
        Serial.printf("[MASTER] DEACTIVATE -> '%s'\n", slaves[activeSlot].name);
        activeSlot = -1;
    }

    // activate computed next slot
    sendActivate(next);
}

// =============================================================
// Forward gamepad state / controller events to the active slave
// =============================================================

// Pack pad_id + event into the reserved GamepadState bytes.
static void buildGamepadPayload(uint8_t pad_id, uint8_t event,
                                const GamepadState* state,
                                GamepadState& out) {
    if (state != nullptr) {
        out = *state;
    } else {
        memset(&out, 0, sizeof(out));
    }
    out.data[10] = pad_id;
    out.data[11] = event;
}

static void forwardToSlave(uint8_t pad_id, uint8_t event,
                            const GamepadState* state) {
    if (activeSlot < 0 || activeSlot >= slaveCount) return;

    GamepadState payload;
    buildGamepadPayload(pad_id, event, state, payload);

    EspNowMsg msg = makeGamepadData(payload);
    lastSendType[activeSlot] = SEND_GAMEPAD;
    esp_now_send(slaves[activeSlot].mac,
                 reinterpret_cast<uint8_t*>(&msg),
                 msgSize(MSG_GAMEPAD_DATA));
}

// =============================================================
// ESP-NOW callbacks
// =============================================================

static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) {
    // Find slave by MAC address
    int slot = -1;
    for (int i = 0; i < slaveCount; i++) {
        if (memcmp(slaves[i].mac, mac, 6) == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) return;

    if (status != ESP_NOW_SEND_SUCCESS) {
        if (sendCallbackSuppressed[slot]) {
            sendCallbackSuppressed[slot] = false;
            lastSendType[slot] = SEND_NONE;
            return;
        }

        const int increment = (lastSendType[slot] == SEND_HEARTBEAT) ? 2 : 1;
        slaves[slot].sendFailCount = static_cast<uint8_t>(slaves[slot].sendFailCount + increment);
        Serial.printf("[ESPNOW] Send failed -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Serial.printf("[ESPNOW] Slave '%s' fail count: %d/5\n",
                      slaves[slot].name, slaves[slot].sendFailCount);
        lastSendType[slot] = SEND_NONE;

        if (slaves[slot].sendFailCount >= 5) {
            Serial.printf("[ESPNOW] Removing unresponsive slave '%s'\n",
                          slaves[slot].name);
            removeSlave(slot);
        }
    } else {
        // Reset fail count on successful send
        slaves[slot].sendFailCount = 0;
        lastSendType[slot] = SEND_NONE;
        sendCallbackSuppressed[slot] = false;
    }
}

// Matches ESP_NOW_RECV_CB_ARGS / ESP_NOW_SRC_MAC from espnow_protocol.h
static void espnow_recv_cb(ESP_NOW_RECV_CB_ARGS,
                            const uint8_t* data, int len) {
    EspNowMsg msg;
    if (!parseMsg(data, len, msg)) {
        Serial.printf("[ESPNOW] Failed to parse message (len=%d)\n", len);
        return;
    }

    const uint8_t* srcMAC = ESP_NOW_SRC_MAC;
    Serial.printf("[ESPNOW] Received message type %d from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  msg.msgType, srcMAC[0], srcMAC[1], srcMAC[2], srcMAC[3], srcMAC[4], srcMAC[5]);

    switch (msg.msgType) {

        case MSG_REGISTER: {
            // Slave announced itself — add to list and reply ACK
            msg.payload.reg.name[NAME_LEN - 1] = '\0';
            Serial.printf("[MASTER] Got MSG_REGISTER from '%s'\n",
                          msg.payload.reg.name);
            addSlave(srcMAC, msg.payload.reg.name);

            EspNowMsg ack = makeSimple(MSG_ACK);
            esp_err_t result = esp_now_send(srcMAC,
                         reinterpret_cast<uint8_t*>(&ack),
                         msgSize(MSG_ACK));
            Serial.printf("[MASTER] Sent MSG_ACK: result=%d\n", result);
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

static void sendControllerReady() {
    send_frame(Serial2, PKT_CONTROLLER_READY, 0, nullptr, 0);
    readySentAt = millis();
    Serial.println("[UART] Sent CONTROLLER_READY");
}

static void registerController(uint8_t pad_id) {
    if (pad_id >= NUM_SLOTS) return;

    const uint8_t player_id      = static_cast<uint8_t>(pad_id + 1);
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
    memset(&gSlots[pad_id].state, 0, sizeof(GamepadState));
}

static void displayUpdate();
static void displayErrorStatusPage(uint8_t page, bool slotsPresent);
static void sendHeartbeats();

// =============================================================
// Display implementation
// =============================================================

static void displayErrorStatusPage(uint8_t page, bool slotsPresent) {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    if (page == 0) {
        // Controller status page
        display.setCursor(0, 0);
        display.println("CONTROLLER");
        display.drawLine(0, 18, OLED_WIDTH - 1, 18, SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 24);
        display.println(gamepadControllerReady ? "Ready" : "NOT READY");
        display.setCursor(0, 36);
        display.setTextSize(2);
        display.println(gamepadControllerReady ? "  OK" : "  MISSING");
    } else if (page == 1) {
        // Gamepad status page
        display.setCursor(0, 0);
        display.println("GAMEPAD");
        display.drawLine(0, 18, OLED_WIDTH - 1, 18, SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 24);
        display.println(slotsPresent ? "Connected" : "NO PADS");
        display.setCursor(0, 36);
        display.setTextSize(2);
        display.println(slotsPresent ? "  OK" : "  MISSING");
    } else {
        // Slaves status page
        display.setCursor(0, 0);
        display.println("SLAVES");
        display.drawLine(0, 18, OLED_WIDTH - 1, 18, SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 24);
        display.printf("%d Slaves\n", slaveCount);
        display.setCursor(0, 36);
        display.setTextSize(2);
        display.println(slaveCount > 0 ? "  OK" : "  MISSING");
    }
}

static constexpr uint32_t SCROLL_STEP_MS = 75;
static constexpr int16_t SCROLL_GAP_PIXELS = 16;
static char scrollLineText[64] = {0};
static int16_t scrollLineOffset = 0;
static int16_t scrollLineMaxOffset = 0;
static uint32_t scrollLineLastMove = 0;

static void displayScrollLine(const char* text, int16_t y) {
    display.setTextSize(2);
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

    if (w <= OLED_WIDTH) {
        display.setCursor(0, y);
        display.print(text);
        scrollLineOffset = 0;
        scrollLineMaxOffset = 0;
        scrollLineText[0] = '\0';
        return;
    }

    if (strncmp(scrollLineText, text, sizeof(scrollLineText)) != 0) {
        strncpy(scrollLineText, text, sizeof(scrollLineText) - 1);
        scrollLineText[sizeof(scrollLineText) - 1] = '\0';
        scrollLineOffset = 0;
        scrollLineMaxOffset = static_cast<int16_t>(w - OLED_WIDTH + SCROLL_GAP_PIXELS);
        scrollLineLastMove = millis();
    }

    uint32_t now = millis();
    if (now - scrollLineLastMove >= SCROLL_STEP_MS) {
        scrollLineLastMove = now;
        scrollLineOffset += 1;
        if (scrollLineOffset > scrollLineMaxOffset) {
            scrollLineOffset = 0;
        }
    }

    display.setCursor(-scrollLineOffset, y);
    display.print(text);
}

static void displayUpdate() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    bool slotsPresent = false;
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (gSlots[i].connected) {
            slotsPresent = true;
            break;
        }
    }

    const bool allOk = gamepadControllerReady && slotsPresent && slaveCount > 0;

    // Update display page every DISPLAY_PAGE_CHANGE_MS
    if (millis() - lastDisplayPageChange >= DISPLAY_PAGE_CHANGE_MS) {
        lastDisplayPageChange = millis();
        displayPage = (displayPage + 1) % 3;
    }

    if (!allOk) {
        // Error mode: show rotating error status pages with large text
        displayErrorStatusPage(displayPage, slotsPresent);
    } else {
        // OK mode: show normal operational pages
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.printf("RC Controller (%d)", slaveCount);
        display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);

        if (displayPage == 0) {
            // Status overview page
            display.setTextSize(1);
            display.setCursor(0, 14);
            display.print("Active: ");

            display.setTextSize(2);
            char activeText[64];
            snprintf(activeText, sizeof(activeText), " %s",
                     (activeSlot >= 0) ? slaves[activeSlot].name : "--");
            displayScrollLine(activeText, 26);

            if (waitingForActivateAck) {
                display.setTextSize(1);
                display.setCursor(0, 52);
                display.println("(Activating...)");
            }

        } else if (displayPage == 1) {
            // Connected gamepads page
            display.setTextSize(1);
            display.setCursor(0, 14);
            display.println("Gamepads:");
            int y = 26;
            for (int i = 0; i < NUM_SLOTS && y < 60; i++) {
                if (gSlots[i].connected) {
                    display.setCursor(0, y);
                    display.printf("Pad%d: ID#%d", i, gSlots[i].player_id);
                    y += 10;
                }
            }
        } else {
            // Slaves info page
            display.setTextSize(1);
            display.setCursor(0, 14);
            display.printf("Models: %d\n", slaveCount);
            display.setCursor(0, 24);
            display.printf("Active: %s\n", activeSlot >= 0 ? slaves[activeSlot].name : "--");
            display.setCursor(0, 34);
            display.printf("Fail Cnt: %d\n", activeSlot >= 0 ? slaves[activeSlot].sendFailCount : 0);
        }
    }

    display.display();
}

static void sendHeartbeats() {
    if (slaveCount == 0) return;

    int i = 0;
    while (i < slaveCount) {
        EspNowMsg msg = makeSimple(MSG_HEARTBEAT);
        lastSendType[i] = SEND_HEARTBEAT;
        esp_err_t result = esp_now_send(slaves[i].mac,
                                       reinterpret_cast<uint8_t*>(&msg),
                                       msgSize(MSG_HEARTBEAT));

        if (result != ESP_OK) {
            slaves[i].sendFailCount = static_cast<uint8_t>(slaves[i].sendFailCount + 2);
            sendCallbackSuppressed[i] = true;
            Serial.printf("[ESPNOW] Heartbeat send failed immediately to '%s' count=%d/5\n", slaves[i].name, slaves[i].sendFailCount);

            if (slaves[i].sendFailCount >= 5) {
                Serial.printf("[ESPNOW] Removing unresponsive slave '%s' due heartbeat failures\n", slaves[i].name);
                removeSlave(i);
                if (slaveCount > 0) selectNextSlave();
                continue;
            }
        }

        i++;
    }
}

// =============================================================
// Incoming UART packet handler
// =============================================================

static void handleIncoming(uint8_t type, uint8_t pad_id,
                            uint8_t* data, uint8_t len) {
    if (!gamepadControllerReady && type != PKT_CONTROLLER_READY_ACK) {
        Serial.printf("[UART] Ignoring type 0x%02X until controller ready\n", type);
        return;
    }

    if (pad_id >= NUM_SLOTS) {
        Serial.printf("[UART] pad_id %d out of range\n", pad_id);
        return;
    }

    switch (type) {
        case PKT_CONTROLLER_READY_ACK:
            if (!gamepadControllerReady) {
                gamepadControllerReady = true;
                Serial.println("[UART] Received CONTROLLER_READY_ACK");
            }
            break;

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
            if (len != sizeof(GamepadState)) {
                Serial.printf("[UART] STATE wrong length %d (expected %d)\n",
                              len, static_cast<int>(sizeof(GamepadState)));
                break;
            }
            memcpy(gSlots[pad_id].state.data, data, sizeof(GamepadState));
            gSlots[pad_id].last_state_ms = millis();

            // Forward to active slave as MSG_GAMEPAD_DATA (event=0)
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
    Serial.printf("[STATUS] Slaves:%d  Active:%d  Waiting:%s  Controller:%s\n",
                  slaveCount,
                  activeSlot,
                  waitingForActivateAck ? "yes" : "no",
                  gamepadControllerReady ? "registered" : "not registered");

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

    button.begin();

    // UART2 to ESP32 #1
    Serial2.begin(UART_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

    // Display setup
    Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED not found!");
        while (true) delay(100);
    }
    display.clearDisplay();
    display.display();

    // ESP-NOW init
    WiFi.mode(WIFI_STA);
    Serial.printf("[ESP32#2] MAC: %s\n", WiFi.macAddress().c_str());

    
    // LED setup
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    displayUpdate();
    sendControllerReady();

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init failed — halting");
        while (true) delay(100);
    }
    Serial.println("[ESPNOW] Init successful");

    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);
    Serial.println("[ESPNOW] Callbacks registered");

    // Broadcast peer — needed to send MSG_ACK before a slave is
    // registered as a unicast peer (matches reference master setup)
    uint8_t bcast[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_peer_info_t bcastPeer = {};
    memcpy(bcastPeer.peer_addr, bcast, 6);
    bcastPeer.channel = 0;
    bcastPeer.encrypt = false;
    esp_err_t addPeerResult = esp_now_add_peer(&bcastPeer);
    Serial.printf("[ESPNOW] Added broadcast peer: result=%d\n", addPeerResult);
    
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
    if (button.update() && button.pressed()) {
        selectNextSlave();
    }

    // ── ACTIVATE timeout: remove unresponsive slave ───────────
    if (waitingForActivateAck &&
        (millis() - activateSentAt > 2000)) {
        Serial.printf("[MASTER] Timeout — removing slave '%s'\n",
                      slaves[pendingSlot].name);
        waitingForActivateAck = false;
        removeSlave(pendingSlot);
    }

    // ── Resend ready handshake until controller acknowledges ───
    if (!gamepadControllerReady && millis() - readySentAt >= READY_RESEND_MS) {
        sendControllerReady();
    }

    // ── Heartbeat to slaves every 10 s ─────────────────────────
    if (millis() - lastHeartbeatSend >= HEARTBEAT_MS) {
        sendHeartbeats();
        lastHeartbeatSend = millis();
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

    // ── Display update every DISPLAY_REFRESH_MS ms ────────────
    static uint32_t lastDisplay = 0;
    if (millis() - lastDisplay >= DISPLAY_REFRESH_MS) {
        displayUpdate();
        lastDisplay = millis();
    }

    // ── Blink onboard LED (non-blocking) ─────────────────────
    if (millis() - lastLedToggle >= LED_BLINK_MS) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        lastLedToggle = millis();
    }
}