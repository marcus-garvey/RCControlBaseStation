#ifndef REMOTEPS3_H
#define REMOTEPS3_H

#include <inttypes.h>

#include "Arduino.h"
#include <AsyncUDP.h>

#define MSG_HELLO_NEW_CLIENT 0x01
#define MSG_CLIENT_ACCEPTED 0x02
#define MSG_SELECT_CLIENT 0x03
#define MSG_CTRL_STATE_UPDATE 0x0A

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
    
    
    RemotePs3(byte devicenum);

    void begin();

    bool isDeviceActive();
    void onBtnDpadUpDown(callbackPS_t value);
    void onBtnDpadDownDown(callbackPS_t value);
    void onBtnDpadLeftDown(callbackPS_t value);
    void onBtnDpadRightDown(callbackPS_t value);

    void onBtnCrossDown(callbackPS_t value);
    void onBtnCircleDown(callbackPS_t value);
    void onBtnSquareDown(callbackPS_t value);
    void onBtnTriangleDown(callbackPS_t value);

    void onBtnLeftStickDown(callbackPS_t value);
    void onBtnRightStickDown(callbackPS_t value);

    void onBtnSelectDown(callbackPS_t value);
    void onBtnPsDown(callbackPS_t value);
    void onBtnStartDown(callbackPS_t value);
   
    // Returns values between -127 and 127
    int8_t getLeftStickX();
    int8_t getLeftStickY();

    int8_t getRightStickX();
    int8_t getRightStickY();

    // Returns values between 0 and 127
    uint8_t getL1();
    uint8_t getR1();
    // Returns values between 0 and 255
    uint8_t getL2();
    uint8_t getR2();
   
    void onUpdate(callbackPS_t value);
    void onSwitchToInactive(callbackPS_t value);
    void onSwitchToActive(callbackPS_t value);
    void onRegistrationStart(callbackPS_t value);
    void onRegistrationDone(callbackPS_t value);


    // dont use it
    void onUDPRecv(AsyncUDPPacket packet);
private:
    void swap();
    void update(byte* data, byte len);

    void sendData(byte msgid, byte* data,byte len);
    
    long fpsCounter;
    long fpsLastTime;

    byte device;
    bool _isDeviceActive;

    AsyncUDP udp;
    transmit cur_state;
    transmit last_state;
    bool register_mode;

    int serverport = 12345;
    int clientport = 12346;
    IPAddress serverAddr;

    callbackPS_t _callback_goingActive = nullptr;
    callbackPS_t _callback_goingInactive = nullptr;
    callbackPS_t _callback_startRegistration = nullptr;
    callbackPS_t _callback_doneRegistration = nullptr;

    callbackPS_t _callback_update = nullptr;

    callbackPS_t _callback_dpadup = nullptr;
    callbackPS_t _callback_dpaddown = nullptr;
    callbackPS_t _callback_dpadleft = nullptr;
    callbackPS_t _callback_dpadright = nullptr;

    callbackPS_t _callback_cross = nullptr;
    callbackPS_t _callback_circle = nullptr;
    callbackPS_t _callback_square = nullptr;
    callbackPS_t _callback_triangle = nullptr;

    callbackPS_t _callback_select = nullptr;
    callbackPS_t _callback_ps = nullptr;
    callbackPS_t _callback_start = nullptr;

    callbackPS_t _callback_leftstick = nullptr;
    callbackPS_t _callback_rightstick = nullptr;
};

#endif