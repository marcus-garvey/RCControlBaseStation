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
    typedef void(*callbackVoid_t)();
    typedef void(*callbackPressed_t)(bool pressed);
    // state returns -1 or 1 if the high threshold was triggered and 0 if the low threshold was triggered
    typedef void(*callbackAnalogStick_t)(int state);
    
    
    RemotePs3(byte devicenum);

    void begin();

    bool isDeviceActive();
    void onBtnDpadUpEvent(callbackPressed_t value);
    void onBtnDpadDownEvent(callbackPressed_t value);
    void onBtnDpadLeftEvent(callbackPressed_t value);
    void onBtnDpadRightEvent(callbackPressed_t value);

    void onBtnCrossEvent(callbackPressed_t value);
    void onBtnCircleEvent(callbackPressed_t value);
    void onBtnSquareEvent(callbackPressed_t value);
    void onBtnTriangleEvent(callbackPressed_t value);

    void onBtnLeftStickEvent(callbackPressed_t value);
    void onBtnRightStickEvent(callbackPressed_t value);

    void onBtnL1Event(callbackPressed_t value);
    void onBtnL2Event(callbackPressed_t value);

    void onBtnR1Event(callbackPressed_t value);
    void onBtnR2Event(callbackPressed_t value);

    void onBtnSelectEvent(callbackPressed_t value);
    void onBtnPsEvent(callbackPressed_t value);
    void onBtnStartEvent(callbackPressed_t value);
   
    void onStickLXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickLYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickRXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickRYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);

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
   
    void onUpdate(callbackVoid_t value);
    void onSwitchToInactive(callbackVoid_t value);
    void onSwitchToActive(callbackVoid_t value);
    void onRegistrationStart(callbackVoid_t value);
    void onRegistrationDone(callbackVoid_t value);


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

    uint8_t analogBtnEventThreshold = 40;
    callbackVoid_t _callback_goingActive = nullptr;
    callbackVoid_t _callback_goingInactive = nullptr;
    callbackVoid_t _callback_startRegistration = nullptr;
    callbackVoid_t _callback_doneRegistration = nullptr;

    callbackVoid_t _callback_update = nullptr;

    callbackPressed_t _callback_dpadup = nullptr;
    callbackPressed_t _callback_dpaddown = nullptr;
    callbackPressed_t _callback_dpadleft = nullptr;
    callbackPressed_t _callback_dpadright = nullptr;

    callbackPressed_t _callback_cross = nullptr;
    callbackPressed_t _callback_circle = nullptr;
    callbackPressed_t _callback_square = nullptr;
    callbackPressed_t _callback_triangle = nullptr;

    callbackPressed_t _callback_select = nullptr;
    callbackPressed_t _callback_ps = nullptr;
    callbackPressed_t _callback_start = nullptr;

    callbackPressed_t _callback_leftstick = nullptr;
    callbackPressed_t _callback_rightstick = nullptr;

    callbackPressed_t _callback_l1= nullptr;
    callbackPressed_t _callback_l2 = nullptr;
    callbackPressed_t _callback_r1= nullptr;
    callbackPressed_t _callback_r2 = nullptr;

    uint8_t analogThresholdHighLX = 255;
    uint8_t analogThresholdLowLX = 0;
    callbackAnalogStick_t _callback_LX = nullptr;
    uint8_t analogThresholdHighLY = 255;
    uint8_t analogThresholdLowLY = 0;
    callbackAnalogStick_t _callback_LY = nullptr;
    uint8_t analogThresholdHighRX = 255;
    uint8_t analogThresholdLowRX = 0;
    callbackAnalogStick_t _callback_RX = nullptr;
    uint8_t analogThresholdHighRY = 255;
    uint8_t analogThresholdLowRY = 0;
    callbackAnalogStick_t _callback_RY = nullptr;
};

#endif