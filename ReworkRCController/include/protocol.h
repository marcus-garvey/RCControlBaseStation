#pragma once
// =============================================================
// protocol.h  —  Shared UART frame definitions
// Include this file on BOTH ESP32 nodes.
//
// Wire format:
//   [0xAA] [TYPE 1B] [PAD_ID 1B] [LEN 1B] [PAYLOAD LEN-B] [CRC8 1B]
//
// CRC-8 (poly 0x07) is computed over ALL preceding bytes incl. 0xAA.
//
// Bluepad32 button  →  struct field
// ─────────────────────────────────────────────────────────────
//  BUTTON_A           btn_cross      PS:✕   Xbox:A
//  BUTTON_B           btn_circle     PS:○   Xbox:B
//  BUTTON_X           btn_square     PS:□   Xbox:X
//  BUTTON_Y           btn_triangle   PS:△   Xbox:Y
//  BUTTON_SHOULDER_L  btn_l1         digital shoulder left
//  BUTTON_SHOULDER_R  btn_r1         digital shoulder right
//  BUTTON_THUMB_L     btn_thumb_l    L3 / left  stick click
//  BUTTON_THUMB_R     btn_thumb_r    R3 / right stick click
//  dpad() DPAD_UP     btn_dpad_up
//  dpad() DPAD_DOWN   btn_dpad_down
//  dpad() DPAD_LEFT   btn_dpad_left
//  dpad() DPAD_RIGHT  btn_dpad_right
//  MISC_BUTTON_SELECT btn_select     PS:Create/Share  Xbox:View
//  MISC_BUTTON_START  btn_start      PS:Options       Xbox:Menu
//  MISC_BUTTON_SYSTEM btn_system     PS:PS-button     Xbox:Xbox-button
//  MISC_BUTTON_BACK   btn_back       Xbox:Share / extra back
//  MISC_BUTTON_CAPTURE btn_capture   PS:Mute  Switch:Capture
//
//  axisX()    → analog_lx   scaled −512…+511  →  −128…+127
//  axisY()    → analog_ly
//  axisRX()   → analog_rx
//  axisRY()   → analog_ry
//  brake()    → analog_l2   0…255  (L2 trigger)
//  throttle() → analog_r2   0…255  (R2 trigger)
// =============================================================

#include <stdint.h>
#include <string.h>

// ── Constants ────────────────────────────────────────────────
#define UART_START_BYTE  0xAA
#define MAX_PAYLOAD      16      // >= sizeof(StatePayload) = 12
#define UART_BAUD        115200

// ── Packet types ─────────────────────────────────────────────
typedef enum : uint8_t {
    PKT_CONNECT    = 0x01,   // ESP32#1 → #2  controller connected   (no payload)
    PKT_DISCONNECT = 0x02,   // ESP32#1 → #2  controller disconnected (no payload)
    PKT_STATE      = 0x03,   // ESP32#1 → #2  full gamepad state      (StatePayload)
    PKT_PING       = 0x04,   // ESP32#1 → #2  heartbeat               (no payload)
    PKT_SET_PLAYER = 0x05,   // ESP32#2 → #1  set player LED          (SetPlayerPayload)
} PktType;

// ── StatePayload  (12 bytes) ──────────────────────────────────
//
//  Byte  0  — D-Pad (bits 0-3) + Face buttons (bits 4-7)
//  Byte  1  — L1/R1 + thumb clicks + select/start/system/back
//  Byte  2  — capture (bit 0) + 7 reserved bits
//  Byte  3  — padding (alignment)
//  Bytes 4-7— analog axes (int8_t each)
//  Bytes 8-9— analog triggers (uint8_t each)
//  Bytes10-11 padding
//
// Bit order within each byte: LSB = bit 0 = first field declared.
union StatePayload {
    struct __attribute__((packed)) {

        // ── Byte 0: D-Pad + Face buttons ─────────────────────
        bool btn_dpad_up    : 1;  // dpad() & DPAD_UP
        bool btn_dpad_down  : 1;  // dpad() & DPAD_DOWN
        bool btn_dpad_left  : 1;  // dpad() & DPAD_LEFT
        bool btn_dpad_right : 1;  // dpad() & DPAD_RIGHT
        bool btn_triangle   : 1;  // BUTTON_Y   (PS:△  Xbox:Y)
        bool btn_cross      : 1;  // BUTTON_A   (PS:✕  Xbox:A)
        bool btn_square     : 1;  // BUTTON_X   (PS:□  Xbox:X)
        bool btn_circle     : 1;  // BUTTON_B   (PS:○  Xbox:B)

        // ── Byte 1: Shoulders + Sticks + System buttons ───────
        bool btn_l1         : 1;  // BUTTON_SHOULDER_L  (digital bump)
        bool btn_r1         : 1;  // BUTTON_SHOULDER_R  (digital bump)
        bool btn_thumb_l    : 1;  // BUTTON_THUMB_L     (L3 click)
        bool btn_thumb_r    : 1;  // BUTTON_THUMB_R     (R3 click)
        bool btn_select     : 1;  // MISC_BUTTON_SELECT (Create / View)
        bool btn_start      : 1;  // MISC_BUTTON_START  (Options / Menu)
        bool btn_system     : 1;  // MISC_BUTTON_SYSTEM (PS / Xbox guide)
        bool btn_back       : 1;  // MISC_BUTTON_BACK   (Share extra / back)

        // ── Byte 2: Capture + reserved ────────────────────────
        bool btn_capture    : 1;  // MISC_BUTTON_CAPTURE (PS:Mute  Switch:Capture)
        bool _res0          : 1;
        bool _res1          : 1;
        bool _res2          : 1;
        bool _res3          : 1;
        bool _res4          : 1;
        bool _res5          : 1;
        bool _res6          : 1;

        // ── Byte 3: padding ───────────────────────────────────
        uint8_t _pad0;

        // ── Bytes 4-7: analog sticks ──────────────────────────
        int8_t  analog_lx;        // left  stick X  (−128…+127)
        int8_t  analog_ly;        // left  stick Y  (−128…+127)
        int8_t  analog_rx;        // right stick X  (−128…+127)
        int8_t  analog_ry;        // right stick Y  (−128…+127)

        // ── Bytes 8-9: analog triggers ────────────────────────
        uint8_t analog_l2;        // L2 / brake     (0…255)
        uint8_t analog_r2;        // R2 / throttle  (0…255)

        // ── Bytes 10-11: padding ──────────────────────────────
        uint8_t _pad1;
        uint8_t _pad2;

    } part;

    uint8_t data[12];   // raw byte access for send_frame / memcpy
};
static_assert(sizeof(StatePayload) == 12, "StatePayload must be exactly 12 bytes");

// ── SET_PLAYER payload (1 byte) ───────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t player_id;   // 1…4  →  ctl->setPlayerLEDs()
} SetPlayerPayload;

// ── CRC-8 (polynomial 0x07) ───────────────────────────────────
static inline uint8_t crc8_calc(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07u : (crc << 1);
    }
    return crc;
}

// ── Frame transmit helper ─────────────────────────────────────
static inline void send_frame(Stream& stream,
                               uint8_t  type,
                               uint8_t  pad_id,
                               const void* payload,
                               uint8_t  plen) {
    uint8_t buf[4 + MAX_PAYLOAD + 1];
    buf[0] = UART_START_BYTE;
    buf[1] = type;
    buf[2] = pad_id;
    buf[3] = plen;
    if (plen > 0 && payload != nullptr)
        memcpy(&buf[4], payload, plen);
    buf[4 + plen] = crc8_calc(buf, 4 + plen);
    stream.write(buf, 5 + plen);
}

// ── Parser state machine ──────────────────────────────────────
// Instantiate one RxParser per UART direction.
typedef enum : uint8_t {
    RX_WAIT_START,
    RX_WAIT_TYPE,
    RX_WAIT_ID,
    RX_WAIT_LEN,
    RX_WAIT_PAYLOAD,
    RX_WAIT_CRC,
} RxState;

typedef struct {
    RxState state     = RX_WAIT_START;
    uint8_t type      = 0;
    uint8_t pad_id    = 0;
    uint8_t len       = 0;
    uint8_t collected = 0;
    uint8_t buf[MAX_PAYLOAD];
} RxParser;

// Feed one received byte. Returns true on a complete, CRC-valid packet.
// On true: p.type, p.pad_id and p.buf[0..p.len-1] hold the packet data.
static inline bool rx_feed(RxParser& p, uint8_t byte) {
    switch (p.state) {
        case RX_WAIT_START:
            if (byte == UART_START_BYTE) p.state = RX_WAIT_TYPE;
            break;
        case RX_WAIT_TYPE:
            p.type  = byte; p.state = RX_WAIT_ID;
            break;
        case RX_WAIT_ID:
            p.pad_id = byte; p.state = RX_WAIT_LEN;
            break;
        case RX_WAIT_LEN:
            p.len       = byte;
            p.collected = 0;
            p.state     = (byte > 0) ? RX_WAIT_PAYLOAD : RX_WAIT_CRC;
            break;
        case RX_WAIT_PAYLOAD:
            if (p.collected < MAX_PAYLOAD)
                p.buf[p.collected] = byte;
            if (++p.collected >= p.len)
                p.state = RX_WAIT_CRC;
            break;
        case RX_WAIT_CRC: {
            uint8_t chk[4 + MAX_PAYLOAD];
            chk[0] = UART_START_BYTE;
            chk[1] = p.type;
            chk[2] = p.pad_id;
            chk[3] = p.len;
            memcpy(&chk[4], p.buf, p.len);
            p.state = RX_WAIT_START;
            if (byte == crc8_calc(chk, 4 + p.len))
                return true;   // valid — read p.type / p.pad_id / p.buf
            // CRC error → discard silently; auto-resync on next 0xAA
            break;
        }
    }
    return false;
}