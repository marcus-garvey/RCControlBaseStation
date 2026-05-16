// ============================================================
//  ESP-NOW Slave
//  Requires: espnow_protocol.h in the same folder
//
//  Set the friendly name and optional node ID here:
#define DEVICE_NAME    "Sensor-01"
#define DEVICE_NODE_ID  1           // 0 = no ID
// ============================================================

#include <esp_now.h>
#include <WiFi.h>
#include "espnow_protocol.h"

// ── State ────────────────────────────────────────────────────
uint8_t broadcastMAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t masterMAC[6];
bool    registered = false;
bool    active     = false;

// ── Receive buffer ───────────────────────────────────────────
DataPayload rxData;
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

    case MSG_DATA: {
      if (len >= (int)(1 + sizeof(DataPayload))) {
        memcpy(&rxData, &msg.payload.data, sizeof(DataPayload));
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

  Serial.printf("[DATA] counter=%d  value=%.1f  [1]=%d [2]=%d\n",
                rxData.data[0], rxData.value,
                rxData.data[1], rxData.data[2]);

  // ── Insert application logic here ────────────────────────
  // e.g. set PWM, toggle relay, update local display, etc.
}

// ── Setup / Loop ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.printf("Slave MAC: %s\n", WiFi.macAddress().c_str());

  esp_now_init();
  esp_now_register_recv_cb(onReceive);

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
  delay(10);
}
