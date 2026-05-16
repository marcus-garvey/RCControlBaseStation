// ============================================================
//  ESP-NOW Master
//  Requires: espnow_protocol.h in the same folder
//
//  Libraries (Arduino Library Manager):
//    - Adafruit SSD1306
//    - Adafruit GFX Library
// ============================================================

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "espnow_protocol.h"

// ── OLED ────────────────────────────────────────────────────
#define OLED_WIDTH  128
#define OLED_HEIGHT  64
#define OLED_RESET   -1
#define OLED_ADDR  0x3C
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ── Pins ────────────────────────────────────────────────────
#define BUTTON_PIN 0

// ── Slave list ───────────────────────────────────────────────
#define MAX_SLAVES 20
struct Slave {
  uint8_t  mac[6];
  char     name[NAME_LEN];
  uint8_t  nodeId;
  uint32_t lastSeen;
};
Slave slaves[MAX_SLAVES];
int   slaveCount = 0;
int   activeSlot = -1;

// ── ACTIVATE timeout ─────────────────────────────────────────
bool     waitingForActivateAck = false;
uint32_t activateSentAt        = 0;
int      pendingSlot           = -1;

// ── Button debounce ──────────────────────────────────────────
bool     lastButtonState = HIGH;
uint32_t lastDebounce    = 0;

// ── Dummy data ───────────────────────────────────────────────
#define DATA_INTERVAL 500   // ms between data packets
uint32_t lastDataSent = 0;

// ── OLED ─────────────────────────────────────────────────────

void displayUpdate() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ESP-NOW Master");
  display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);

  if (slaveCount == 0) {
    display.setCursor(0, 14);
    display.print("Waiting for slaves...");
  } else {
    display.setCursor(0, 12);
    display.print("Active: ");
    display.print(activeSlot >= 0 ? slaves[activeSlot].name : "--");
    if (waitingForActivateAck) display.print(" ?");
    display.drawLine(0, 23, OLED_WIDTH - 1, 23, SSD1306_WHITE);

    // show up to 4 slaves in the list
    for (int i = 0, y = 26; i < slaveCount && i < 4; i++, y += 10) {
      display.setCursor(0, y);
      display.print(i == activeSlot ? "> " : "  ");
      display.print(slaves[i].name);
      if (slaves[i].nodeId > 0) {
        display.print(" #");
        display.print(slaves[i].nodeId);
      }
    }
    display.setCursor(0, 56);
    display.printf("Slaves:%d", slaveCount);
  }
  display.display();
}

// ── Slave management ─────────────────────────────────────────

void removeSlave(int slot) {
  Serial.printf("Slave '%s' removed\n", slaves[slot].name);
  esp_now_del_peer(slaves[slot].mac);
  if (slot == activeSlot) activeSlot = -1;
  // overwrite with last entry to keep the array compact
  slaves[slot] = slaves[--slaveCount];
  // fix activeSlot if the moved entry was the active one
  if (activeSlot == slaveCount) activeSlot = slot;
  displayUpdate();
}

void addSlave(const uint8_t *mac, const char *name, uint8_t nodeId) {
  // update lastSeen if already known
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

  Serial.printf("New slave '%s' nodeId=%d (total: %d)\n",
                name, nodeId, slaveCount);
  displayUpdate();
}

// ── ACTIVATE / DEACTIVATE ────────────────────────────────────

void sendActivate(int slot) {
  EspNowMsg msg = makeSimple(MSG_ACTIVATE);
  esp_now_send(slaves[slot].mac, (uint8_t *)&msg, msgSize(MSG_ACTIVATE));

  waitingForActivateAck = true;
  activateSentAt        = millis();
  pendingSlot           = slot;

  Serial.printf("ACTIVATE -> '%s'\n", slaves[slot].name);
  displayUpdate();
}

void selectNextSlave() {
  if (slaveCount == 0)       { Serial.println("No slaves known"); return; }
  if (waitingForActivateAck) { Serial.println("Waiting for ACK..."); return; }

  // deactivate current slave before switching (no ACK expected)
  if (activeSlot >= 0 && activeSlot < slaveCount) {
    EspNowMsg deact = makeSimple(MSG_DEACTIVATE);
    esp_now_send(slaves[activeSlot].mac,
                 (uint8_t *)&deact, msgSize(MSG_DEACTIVATE));
    Serial.printf("DEACTIVATE -> '%s'\n", slaves[activeSlot].name);
    activeSlot = -1;
  }

  // advance ring index; +slaveCount keeps modulo positive after reset to -1
  sendActivate((activeSlot + 1 + slaveCount) % slaveCount);
}

// ── Dummy data sender ────────────────────────────────────────

void sendDummyData() {
  if (activeSlot < 0 || activeSlot >= slaveCount) return;
  if (millis() - lastDataSent < DATA_INTERVAL)    return;

  static uint8_t counter = 0;
  uint8_t buf[32];
  buf[0] = counter++;
  for (int i = 1; i < 32; i++) buf[i] = (uint8_t)(i * counter % 256);

  EspNowMsg msg = makeData(buf, 32, (float)counter);
  esp_now_send(slaves[activeSlot].mac, (uint8_t *)&msg, msgSize(MSG_DATA));
  lastDataSent = millis();
}

// ── Receive callback ─────────────────────────────────────────

void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data, int len) {
  EspNowMsg msg;
  if (!parseMsg(data, len, msg)) return;

  // sender MAC comes from the callback — no payload field needed
  const uint8_t *srcMAC = info->src_addr;

  switch (msg.msgType) {

    case MSG_REGISTER: {
      msg.payload.reg.name[NAME_LEN - 1] = '\0'; // ensure null-termination
      addSlave(srcMAC, msg.payload.reg.name, msg.payload.reg.nodeId);

      EspNowMsg ack = makeSimple(MSG_ACK);
      esp_now_send(srcMAC, (uint8_t *)&ack, msgSize(MSG_ACK));
      break;
    }

    case MSG_ACTIVATE_ACK: {
      if (!waitingForActivateAck) break;
      if (pendingSlot < 0 || pendingSlot >= slaveCount) break;
      if (memcmp(srcMAC, slaves[pendingSlot].mac, 6) != 0) break; // wrong sender

      activeSlot            = pendingSlot;
      waitingForActivateAck = false;
      Serial.printf("Slave '%s' active\n", slaves[activeSlot].name);
      displayUpdate();
      break;
    }

    default: break;
  }
}

// ── Setup / Loop ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found!");
    while (true) delay(100);
  }
  display.clearDisplay();
  display.display();

  WiFi.mode(WIFI_STA);
  Serial.printf("Master MAC: %s\n", WiFi.macAddress().c_str());

  esp_now_init();
  esp_now_register_recv_cb(onReceive);

  // broadcast peer needed to send ACKs before a slave is registered as unicast peer
  uint8_t bcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, bcast, 6);
  peer.channel = 0; peer.encrypt = false;
  esp_now_add_peer(&peer);

  displayUpdate();
}

void loop() {
  // ── Button ────────────────────────────────────────────────
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounce = millis();
  if ((millis() - lastDebounce) > 50 &&
       reading == LOW && lastButtonState == HIGH) {
    selectNextSlave();
  }
  lastButtonState = reading;

  // ── ACTIVATE timeout: remove slave if no ACK within 2 s ──
  if (waitingForActivateAck &&
      (millis() - activateSentAt > 2000)) {
    Serial.printf("Timeout: slave '%s' removed\n",
                  slaves[pendingSlot].name);
    waitingForActivateAck = false;
    removeSlave(pendingSlot);
    if (slaveCount > 0) selectNextSlave();
  }

  // ── Send data to active slave ─────────────────────────────
  sendDummyData();
}
