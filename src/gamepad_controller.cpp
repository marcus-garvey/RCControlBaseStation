// =============================================================
// esp32_bluepad.cpp  —  ESP32 #1: Bluepad32 host + UART sender
//
// Wiring to ESP32 #2:
//   GPIO17 (TX2)  →  ESP32#2 GPIO16 (RX2)
//   GPIO16 (RX2)  ←  ESP32#2 GPIO17 (TX2)
//   GND           —  GND
//
// Board (Arduino IDE):
//   "ESP32 + Bluepad32"  (add board manager URL from Bluepad32 docs)
//   Select: ESP32 Dev Module (Bluepad32)
//
// This node:
//   • Receives gamepad connections via Bluetooth (Bluepad32 / BTstack)
//   • Sends PKT_CONNECT / PKT_DISCONNECT / PKT_STATE / PKT_PING
//     over UART2 to ESP32 #2 at ~50 Hz
//   • Receives PKT_SET_PLAYER from ESP32 #2 and forwards the
//     player-LED command to the corresponding controller
// =============================================================

#include <Arduino.h>
#include <Bluepad32.h>
#include "serial_protocol.h"

// ── Hardware config ───────────────────────────────────────────
static constexpr uint8_t  UART2_TX_PIN = 17;
static constexpr uint8_t  UART2_RX_PIN = 16;
static constexpr uint32_t STATE_HZ     = 50;    // gamepad state send rate
static constexpr uint32_t PING_MS      = 500;   // heartbeat interval

// ── Bluepad32 controller slots ────────────────────────────────
static ControllerPtr gControllers[BP32_MAX_GAMEPADS] = {};

// ── UART parser for the back-channel (ESP32#2 → ESP32#1) ─────
static RxParser gRxBack;

// ── Axis scaling helper ───────────────────────────────────────
// Bluepad32 reports axes in −512…+511; scale to int8_t −128…+127
static inline int8_t scaleAxis(int32_t v) {
    if (v >  511) v =  511;
    if (v < -512) v = -512;
    return static_cast<int8_t>(v >> 2);
}

// =============================================================
// Bluepad32 connection callbacks
// =============================================================

void onConnectedController(ControllerPtr ctl) {
    int8_t idx = -1;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (gControllers[i] == nullptr) { idx = static_cast<int8_t>(i); break; }
    }
    if (idx < 0) {
        Serial.println("[BP32] No free slot for new controller");
        return;
    }
    gControllers[idx] = ctl;

    const ControllerProperties& prop = ctl->getProperties();
    Serial.printf("[BP32] Controller %d connected  model=%s  VID=%04X PID=%04X\n",
                  idx, ctl->getModelName().c_str(),
                  prop.vendor_id, prop.product_id);

    // Notify ESP32 #2 — pad_id carries the slot index, no payload needed
    send_frame(Serial2, PKT_CONNECT, static_cast<uint8_t>(idx), nullptr, 0);
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (gControllers[i] == ctl) {
            Serial.printf("[BP32] Controller %d disconnected\n", i);
            gControllers[i] = nullptr;
            send_frame(Serial2, PKT_DISCONNECT, static_cast<uint8_t>(i), nullptr, 0);
            return;
        }
    }
}

// =============================================================
// Build and send a GamepadState for one controller
// =============================================================

static void sendState(uint8_t idx, ControllerPtr ctl) {
    GamepadState s = {};   // zero-initialise all bits

    const uint32_t btn  = ctl->buttons();
    const uint32_t misc = ctl->miscButtons();
    const uint8_t  dp   = ctl->dpad();

    // ── D-Pad ─────────────────────────────────────────────────
    s.part.btn_dpad_up    = (dp   & DPAD_UP)             != 0;
    s.part.btn_dpad_down  = (dp   & DPAD_DOWN)           != 0;
    s.part.btn_dpad_left  = (dp   & DPAD_LEFT)           != 0;
    s.part.btn_dpad_right = (dp   & DPAD_RIGHT)          != 0;

    // ── Face buttons ──────────────────────────────────────────
    s.part.btn_triangle   = (btn  & BUTTON_Y)            != 0;  // PS:△  Xbox:Y
    s.part.btn_cross      = (btn  & BUTTON_A)            != 0;  // PS:✕  Xbox:A
    s.part.btn_square     = (btn  & BUTTON_X)            != 0;  // PS:□  Xbox:X
    s.part.btn_circle     = (btn  & BUTTON_B)            != 0;  // PS:○  Xbox:B

    // ── Shoulder + stick clicks ────────────────────────────────
    s.part.btn_l1         = (btn  & BUTTON_SHOULDER_L)   != 0;
    s.part.btn_r1         = (btn  & BUTTON_SHOULDER_R)   != 0;
    s.part.btn_thumb_l    = (btn  & BUTTON_THUMB_L)      != 0;  // L3
    s.part.btn_thumb_r    = (btn  & BUTTON_THUMB_R)      != 0;  // R3

    // ── Misc / system buttons ──────────────────────────────────
    s.part.btn_select     = (misc & MISC_BUTTON_SELECT)  != 0;  // Create / View
    s.part.btn_start      = (misc & MISC_BUTTON_START)   != 0;  // Options / Menu
    s.part.btn_system     = (misc & MISC_BUTTON_SYSTEM)  != 0;  // PS / Xbox guide
    s.part.btn_back       = (misc & MISC_BUTTON_BACK)    != 0;  // Share / back
    s.part.btn_capture    = (misc & MISC_BUTTON_CAPTURE) != 0;  // Mute / Capture

    // ── Analog sticks (scaled from −512…+511 to int8_t) ───────
    s.part.analog_lx = scaleAxis(ctl->axisX());
    s.part.analog_ly = scaleAxis(ctl->axisY());
    s.part.analog_rx = scaleAxis(ctl->axisRX());
    s.part.analog_ry = scaleAxis(ctl->axisRY());

    // ── Triggers (Bluepad32 returns 0…255 directly) ────────────
    s.part.analog_l2 = static_cast<uint8_t>(ctl->brake());     // L2
    s.part.analog_r2 = static_cast<uint8_t>(ctl->throttle());  // R2

    send_frame(Serial2, PKT_STATE, idx, s.data, sizeof(s));
}

static void resendConnectedControllers() {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        ControllerPtr ctl = gControllers[i];
        if (ctl == nullptr || !ctl->isConnected()) continue;
        Serial.printf("[BACK] Resend connected controller slot=%d\n", i);
        send_frame(Serial2, PKT_CONNECT, static_cast<uint8_t>(i), nullptr, 0);
        sendState(static_cast<uint8_t>(i), ctl);
    }
}

// =============================================================
// Handle incoming packets from ESP32 #2 (back-channel)
// =============================================================

static void handleBackChannel(uint8_t type, uint8_t pad_id,
                               uint8_t* data, uint8_t len) {
    switch (type) {
        case PKT_CONTROLLER_READY: {
            Serial.println("[BACK] CONTROLLER_READY received");
            send_frame(Serial2, PKT_CONTROLLER_READY_ACK, 0, nullptr, 0);
            resendConnectedControllers();
            break;
        }

        case PKT_SET_PLAYER: {
            if (len < 1) return;
            if (pad_id >= BP32_MAX_GAMEPADS) return;

            ControllerPtr ctl = gControllers[pad_id];
            if (ctl == nullptr || !ctl->isConnected()) {
                Serial.printf("[BACK] SET_PLAYER for slot %d — no controller present\n", pad_id);
                return;
            }
            const uint8_t player_id = data[0];
            Serial.printf("[BACK] SET_PLAYER slot=%d  player=%d\n", pad_id, player_id);
            ctl->setPlayerLEDs(player_id);
            break;
        }
        default:
            Serial.printf("[BACK] Unknown packet type 0x%02X\n", type);
            break;
    }
}

// =============================================================
// Arduino entry points
// =============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("[ESP32#1] Bluepad32 node starting");

    // UART2 to ESP32 #2
    Serial2.begin(UART_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

    BP32.setup(&onConnectedController, &onDisconnectedController);

    // Comment out the next line to keep paired devices across resets
    BP32.forgetBluetoothKeys();

    Serial.println("[ESP32#1] Ready — waiting for controllers");
}

void loop() {
    // Poll Bluepad32; returns true when any controller data changed
    bool updated = BP32.update();

    if (updated) {
        for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
            ControllerPtr ctl = gControllers[i];
            if (ctl == nullptr || !ctl->isConnected()) continue;
            sendState(static_cast<uint8_t>(i), ctl);
        }
    }

    // Heartbeat
    static uint32_t lastPing = 0;
    if (millis() - lastPing >= PING_MS) {
        send_frame(Serial2, PKT_PING, 0, nullptr, 0);
        lastPing = millis();
    }

    // Read back-channel bytes from ESP32 #2
    while (Serial2.available()) {
        if (rx_feed(gRxBack, static_cast<uint8_t>(Serial2.read()))) {
            handleBackChannel(gRxBack.type, gRxBack.pad_id,
                              gRxBack.buf,  gRxBack.len);
        }
    }

    // Maintain ~STATE_HZ update rate (BP32.update() does not block)
    delay(1000 / STATE_HZ);
}