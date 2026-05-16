// ============================================================
//  espnow_protocol.h  —  shared between master and slave
//
//  Structure:
//    Header (1 byte, always present)
//      uint8_t  msgType
//    Payload (union, only as large as needed)
//      RegisterPayload  : name(16) + nodeId(1)  -> 17 bytes
//      DataPayload      : data(32) + value(4)   -> 36 bytes
//      (ACK / ACTIVATE / DEACTIVATE / ACTIVATE_ACK: no payload)
//
//  Sender MAC: NOT in payload — read from info->src_addr in callback.
//  Always pass msgSize(type) to esp_now_send(), not sizeof(EspNowMsg).
// ============================================================

#pragma once
#include <stdint.h>
#include <string.h>

// ── Message types ────────────────────────────────────────────
enum MsgType : uint8_t {
  MSG_REGISTER     = 0,   // Slave -> Master  (broadcast)
  MSG_ACK          = 1,   // Master -> Slave  (unicast, no payload)
  MSG_ACTIVATE     = 2,   // Master -> Slave  (unicast, no payload)
  MSG_ACTIVATE_ACK = 3,   // Slave  -> Master (unicast, no payload)
  MSG_DATA         = 4,   // Master -> Slave  (unicast, no ACK expected)
  MSG_DEACTIVATE   = 5,   // Master -> Slave  (unicast, no payload, no ACK)
};

// ── Payload definitions ──────────────────────────────────────
#define NAME_LEN 16

#pragma pack(push, 1)   // no padding — required for transmission

struct RegisterPayload {
  char    name[NAME_LEN]; // friendly name, null-terminated
  uint8_t nodeId;         // optional fixed node ID (0 = none)
};

struct DataPayload {
  uint8_t data[32];
  float   value;
};

// ── Full message (union) ─────────────────────────────────────
struct EspNowMsg {
  uint8_t msgType;          // 1-byte header
  union {
    RegisterPayload reg;    // 17 bytes  -> MSG_REGISTER
    DataPayload     data;   // 36 bytes  -> MSG_DATA
    // all other types: no payload
  } payload;
};

#pragma pack(pop)

// ── Message size per type ────────────────────────────────────
// Always pass msgSize(type) to esp_now_send() — never sizeof(EspNowMsg)!
inline size_t msgSize(uint8_t type) {
  switch (type) {
    case MSG_REGISTER:     return 1 + sizeof(RegisterPayload); // 18 bytes
    case MSG_DATA:         return 1 + sizeof(DataPayload);     // 37 bytes
    case MSG_ACK:
    case MSG_ACTIVATE:
    case MSG_ACTIVATE_ACK:
    case MSG_DEACTIVATE:
    default:               return 1;                           //  1 byte
  }
}

// ── Builder helpers ───────────────────────────────────────────

inline EspNowMsg makeSimple(uint8_t type) {
  EspNowMsg m = {};
  m.msgType = type;
  return m;
}

inline EspNowMsg makeRegister(const char *name, uint8_t nodeId = 0) {
  EspNowMsg m = {};
  m.msgType = MSG_REGISTER;
  strncpy(m.payload.reg.name, name, NAME_LEN - 1);
  m.payload.reg.nodeId = nodeId;
  return m;
}

inline EspNowMsg makeData(const uint8_t *buf, size_t len, float value = 0.f) {
  EspNowMsg m = {};
  m.msgType = MSG_DATA;
  memcpy(m.payload.data.data, buf, len < 32 ? len : 32);
  m.payload.data.value = value;
  return m;
}

// ── Receive callback compatibility ───────────────────────────
// Hardcoded for Core 2.x (bluepad32 framework).
// If you switch to a stock Core 3.x framework, change these to:
//   #define ESP_NOW_RECV_CB_ARGS const esp_now_recv_info_t *info
//   #define ESP_NOW_SRC_MAC      info->src_addr
#define ESP_NOW_RECV_CB_ARGS const uint8_t *info
#define ESP_NOW_SRC_MAC      info
// Returns false if the buffer is too short to contain a valid message.
inline bool parseMsg(const uint8_t *raw, int len, EspNowMsg &out) {
  if (len < 1) return false;
  memset(&out, 0, sizeof(out));
  memcpy(&out, raw, len < (int)sizeof(out) ? len : sizeof(out));
  return true;
}