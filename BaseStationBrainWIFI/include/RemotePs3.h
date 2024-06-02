#ifndef REMOTEPS3_H
#define REMOTEPS3_H

#include <inttypes.h>

#include "Arduino.h"

union transmit
{
    struct ps
    {
        bool btn_dpad_up: 1;
        bool btn_dpad_down: 1;
        bool btn_dpad_left: 1;
        bool btn_dpad_right: 1;

        bool btn_triangle: 1;
        bool btn_cross: 1;
        bool btn_square: 1;
        bool btn_circle: 1;
        
        bool btn_select: 1;
        bool btn_start: 1;
        bool btn_ps: 1;

        bool btn_left_stick: 1;
        bool btn_right_stick: 1;

        bool btn_reserved1: 1;
        bool btn_reserved2: 1;
        bool btn_reserved3: 1;

        int8_t analog_lx: 8;
        int8_t analog_ly: 8;

        int8_t analog_rx: 8;
        int8_t analog_ry: 8;
        
        uint8_t analog_l1: 8;
        uint8_t analog_l2: 8;

        uint8_t analog_r1: 8;
        uint8_t analog_r2: 8;
        /* data */
    } part;
    byte data[10];
    /* data */
};


class RemotePs3 {
public:
    typedef void(*callbackPS_t)();
    RemotePs3();
    void onSelect(callbackPS_t value);
    void update(char* data);

    transmit cur_state;
private:
    void swap();
    
    transmit last_state;

    callbackPS_t _callback_select = nullptr;
};

#endif