#pragma once

#include <stdint.h>

// ── GamepadState  (12 bytes) ─────────────────────────────────
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
union GamepadState {
    struct __attribute__((packed)) {

        // ── Byte 0: D-Pad + Face buttons ─────────────────────
        bool btn_dpad_up    : 1;
        bool btn_dpad_down  : 1;
        bool btn_dpad_left  : 1;
        bool btn_dpad_right : 1;
        bool btn_triangle   : 1;
        bool btn_cross      : 1;
        bool btn_square     : 1;
        bool btn_circle     : 1;

        // ── Byte 1: Shoulders + Sticks + System buttons ───────
        bool btn_l1         : 1;
        bool btn_r1         : 1;
        bool btn_thumb_l    : 1;
        bool btn_thumb_r    : 1;
        bool btn_select     : 1;
        bool btn_start      : 1;
        bool btn_system     : 1;
        bool btn_back       : 1;

        // ── Byte 2: Capture + reserved ────────────────────────
        bool btn_capture    : 1;
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
        int8_t  analog_lx;
        int8_t  analog_ly;
        int8_t  analog_rx;
        int8_t  analog_ry;

        // ── Bytes 8-9: analog triggers ────────────────────────
        uint8_t analog_l2;
        uint8_t analog_r2;

        // ── Bytes 10-11: padding ──────────────────────────────
        uint8_t _pad1;
        uint8_t _pad2;

    } part;

    uint8_t data[12];
};
static_assert(sizeof(GamepadState) == 12, "GamepadState must be exactly 12 bytes");
