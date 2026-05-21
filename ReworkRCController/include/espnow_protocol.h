// ============================================================
//  espnow_protocol.h  —  shared between master and slave
//
//  Structure:
//    Header (1 byte, always present)
//      uint8_t  msgType
//    Payload (union, only as large as needed)
//      RegisterPayload  : name(16)  -> 16 bytes
//      GamepadState     : raw gamepad state      -> 12 bytes
//      (ACK / ACTIVATE / DEACTIVATE / ACTIVATE_ACK: no payload)
//
//  Sender MAC: NOT in payload — read from info->src_addr in callback.
//  Always pass msgSize(type) to esp_now_send(), not sizeof(EspNowMsg).
// ============================================================

#pragma once
#include <stdint.h>
#include <string.h>
#include "gamepad_state.h"

// ── Message types ────────────────────────────────────────────
enum MsgType : uint8_t {
  MSG_REGISTER      = 0,   // Slave -> Master  (broadcast)
  MSG_ACK           = 10,   // Master -> Slave  (unicast, no payload)
  MSG_ACTIVATE      = 2,   // Master -> Slave  (unicast, no payload)
  MSG_ACTIVATE_ACK  = 3,   // Slave  -> Master (unicast, no payload)
  MSG_GAMEPAD_DATA  = 4,   // Master -> Slave  (unicast, raw gamepad state)
  MSG_DEACTIVATE    = 5,   // Master -> Slave  (unicast, no payload, no ACK)
  MSG_HEARTBEAT     = 6,   // Master -> Slave  (unicast, no payload)
};

// ── Payload definitions ──────────────────────────────────────
#define NAME_LEN 16

#pragma pack(push, 1)   // no padding — required for transmission

struct RegisterPayload {
  char    name[NAME_LEN]; // friendly name, null-terminated
};

// ── Full message (union) ─────────────────────────────────────
struct EspNowMsg {
  uint8_t msgType;          // 1-byte header
  union {
    RegisterPayload reg;    // 16 bytes  -> MSG_REGISTER
    GamepadState    gamepad; // 12 bytes  -> MSG_GAMEPAD_DATA
    // all other types: no payload
  } payload;
};

#pragma pack(pop)

// ── Message size per type ────────────────────────────────────
// Always pass msgSize(type) to esp_now_send() — never sizeof(EspNowMsg)!
inline size_t msgSize(uint8_t type) {
  switch (type) {
    case MSG_REGISTER:      return 1 + sizeof(RegisterPayload); // 17 bytes
    case MSG_GAMEPAD_DATA:  return 1 + sizeof(GamepadState);     // 13 bytes
    case MSG_ACK:
    case MSG_ACTIVATE:
    case MSG_ACTIVATE_ACK:
    case MSG_DEACTIVATE:
    default:                return 1;                           //  1 byte
  }
}

// ── Builder helpers ───────────────────────────────────────────

inline EspNowMsg makeSimple(uint8_t type) {
  EspNowMsg m = {};
  m.msgType = type;
  return m;
}

inline EspNowMsg makeRegister(const char *name) {
  EspNowMsg m = {};
  m.msgType = MSG_REGISTER;
  strncpy(m.payload.reg.name, name, NAME_LEN - 1);
  return m;
}

inline EspNowMsg makeGamepadData(const GamepadState &state) {
  EspNowMsg m = {};
  m.msgType = MSG_GAMEPAD_DATA;
  m.payload.gamepad = state;
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