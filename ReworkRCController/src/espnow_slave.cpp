// ============================================================
//  ESP-NOW Slave
//  Requires: espnow_protocol.h in the same folder
//
//  Set the friendly name and optional node ID here:
#define DEVICE_NAME    "First Slave"
#define DEVICE_NODE_ID  1           // 0 = no ID
// ============================================================

#include <esp_now.h>
#include <WiFi.h>
#include "espnow_protocol.h"
#include <ESP32Servo.h> 

// ── State ────────────────────────────────────────────────────
static constexpr uint8_t LED_CIRCLE_PIN   = 22;
static constexpr uint8_t LED_TRIANGLE_PIN = 23;
static constexpr uint8_t SERVO_PIN        = 16;

Servo servo;

uint8_t broadcastMAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t masterMAC[6];
bool    registered = false;
bool    active     = false;

// ── Receive buffer ───────────────────────────────────────────
GamepadState rxData;
bool        newDataAvailable = false;

// ── Receive callback ─────────────────────────────────────────

void onReceive(ESP_NOW_RECV_CB_ARGS,
               const uint8_t *data, int len) {
  EspNowMsg msg;
  if (!parseMsg(data, len, msg)) return;

  // sender MAC comes from the callback — no payload field needed
  const uint8_t *srcMAC = ESP_NOW_SRC_MAC;

  switch (msg.msgType) {

    case MSG_ACK: {
      if (registered) break;
      // store master MAC once from the first ACK received
      memcpy(masterMAC, srcMAC, 6);

      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, masterMAC, 6);
      peer.channel = 0; peer.encrypt = false;
      esp_now_add_peer(&peer);

      registered = true;
      Serial.println("Registered with master as '" DEVICE_NAME "'");
      break;
    }

    case MSG_ACTIVATE: {
      active = true;
      Serial.println("ACTIVATE — now active");

      EspNowMsg ack = makeSimple(MSG_ACTIVATE_ACK);
      esp_now_send(masterMAC, (uint8_t *)&ack, msgSize(MSG_ACTIVATE_ACK));
      break;
    }

    case MSG_DEACTIVATE: {
      active = false;
      Serial.println("DEACTIVATE — now inactive");
      // no ACK — fire and forget from master side
      break;
    }

    case MSG_GAMEPAD_DATA: {
      if (len >= (int)(1 + sizeof(GamepadState))) {
        memcpy(&rxData, &msg.payload.gamepad, sizeof(GamepadState));
        newDataAvailable = true;
      }
      break;
    }

    default: break;
  }
}

// ── Process received data (called from loop, not from callback) ──

void processData() {
  if (!newDataAvailable) return;
  newDataAvailable = false;

  digitalWrite(LED_CIRCLE_PIN,   rxData.part.btn_circle   ? HIGH : LOW);
  digitalWrite(LED_TRIANGLE_PIN, rxData.part.btn_triangle ? HIGH : LOW);
  // Map LX (-128..127) to servo angle 0..180
  {
    int lx = (int)rxData.part.analog_lx; // -128..127
    int angle = ((lx + 128) * 180) / 255;
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    servo.write(angle);
    Serial.printf("LX=%d -> angle=%d\n", lx, angle);
  }
}

// =============================================================
// Debug dump
// =============================================================

static void printStatus() {    
  const auto& p = rxData.part;
  Serial.printf(
      "UP=%d DN=%d LT=%d RT=%d | "
      "TRI=%d CRS=%d SQR=%d CIR=%d | "
      "L1=%d R1=%d L3=%d R3=%d | "
      "SEL=%d STA=%d SYS=%d BCK=%d CAP=%d | "
      "LX=%4d LY=%4d RX=%4d RY=%4d | "
      "L2=%3d R2=%3d\n",
      p.btn_dpad_up,  p.btn_dpad_down, p.btn_dpad_left, p.btn_dpad_right,
      p.btn_triangle, p.btn_cross,     p.btn_square,    p.btn_circle,
      p.btn_l1,       p.btn_r1,        p.btn_thumb_l,   p.btn_thumb_r,
      p.btn_select,   p.btn_start,     p.btn_system,    p.btn_back,
      p.btn_capture,
      p.analog_lx,  p.analog_ly,  p.analog_rx,  p.analog_ry,
      p.analog_l2,  p.analog_r2
  );

}

// ── Setup / Loop ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.printf("Slave MAC: %s\n", WiFi.macAddress().c_str());

  esp_now_init();
  esp_now_register_recv_cb(onReceive);

  pinMode(LED_CIRCLE_PIN,   OUTPUT);
  pinMode(LED_TRIANGLE_PIN, OUTPUT);
  digitalWrite(LED_CIRCLE_PIN,   LOW);
  digitalWrite(LED_TRIANGLE_PIN, LOW);

  // Servo setup
  servo.attach(SERVO_PIN);
  // initialize to centre
  servo.write(90);

  // broadcast peer required to receive REGISTER responses before master is known
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMAC, 6);
  peer.channel = 0; peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void loop() {
  // ── Keep sending REGISTER until the master responds ───────
  if (!registered) {
    EspNowMsg msg = makeRegister(DEVICE_NAME, DEVICE_NODE_ID);
    esp_now_send(broadcastMAC, (uint8_t *)&msg, msgSize(MSG_REGISTER));
    Serial.println("Searching for master...");
    delay(2000);
    return;
  }

  // ── Process any received data outside the callback ────────
  processData();
  
  // ── Debug print every 2 s ────────────────────────────────
    static uint32_t lastDbg = 0;
    if (millis() - lastDbg >= 2000) {
        printStatus();
        lastDbg = millis();
    }
}
