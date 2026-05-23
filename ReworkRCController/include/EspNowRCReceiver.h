#ifndef ESPNOWRCRECEIVER_H
#define ESPNOWRCRECEIVER_H

#include <inttypes.h>
#include "Arduino.h"
#include <esp_now.h>
#include <WiFi.h>
#include "espnow_protocol.h"
#include "gamepad_state.h"

// ────────────────────────────────────────────────────────────
// EspNowRCReceiver — ESP-NOW Slave Gamepad Receiver
// ────────────────────────────────────────────────────────────
// Usage:
//   EspNowRCReceiver remote(0);
//   remote.begin();  // Broadcasts MSG_REGISTER, waits for MSG_ACK
//   // in loop:
//   remote.update(); // Receives MSG_GAMEPAD_DATA, fires callbacks
// ────────────────────────────────────────────────────────────

// Forward declaration for friend
void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

class EspNowRCReceiver {
public:
    typedef void(*callbackVoid_t)();
    typedef void(*callbackPressed_t)(bool pressed);
    // state returns -1 or 1 if the high threshold was triggered and 0 if the low threshold was triggered
    typedef void(*callbackAnalogStick_t)(int state);

    friend void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

    EspNowRCReceiver(const char* friendlyName = "EspNowRCReceiver");

    void begin();
    void update();  // Call this in loop — handles non-blocking registration retry

    bool isDeviceActive();      // Returns true if MSG_ACK received (registered with master)
    bool isRegistered();        // Alias for isDeviceActive()

    // ── Button event registration ────────────────────────────
    void onBtnDpadUpEvent(callbackPressed_t value);
    void onBtnDpadDownEvent(callbackPressed_t value);
    void onBtnDpadLeftEvent(callbackPressed_t value);
    void onBtnDpadRightEvent(callbackPressed_t value);

    void onBtnCrossEvent(callbackPressed_t value);
    void onBtnCircleEvent(callbackPressed_t value);
    void onBtnSquareEvent(callbackPressed_t value);
    void onBtnTriangleEvent(callbackPressed_t value);

    void onBtnLeftStickEvent(callbackPressed_t value);   // btn_thumb_l in GamepadState
    void onBtnRightStickEvent(callbackPressed_t value);  // btn_thumb_r in GamepadState

    void onBtnL1Event(callbackPressed_t value);
    void onBtnL2Event(callbackPressed_t value);

    void onBtnR1Event(callbackPressed_t value);
    void onBtnR2Event(callbackPressed_t value);

    void onBtnSelectEvent(callbackPressed_t value);
    void onBtnPsEvent(callbackPressed_t value);         // btn_system in GamepadState
    void onBtnStartEvent(callbackPressed_t value);

    // ── Analog stick event registration (threshold-based) ────
    void onStickLXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickLYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickRXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);
    void onStickRYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold);

    // ── Getter methods ───────────────────────────────────────
    // Returns values between -127 and 127
    int8_t getLeftStickX();
    int8_t getLeftStickY();

    int8_t getRightStickX();
    int8_t getRightStickY();

    // Returns values between 0 and 255 (analog_l2, analog_r2 from GamepadState)
    uint8_t getL2();
    uint8_t getR2();

    // L1, R1 no longer available as analog in GamepadState (only btn_l1, btn_r1 digital)
    uint8_t getL1() { return 0; }
    uint8_t getR1() { return 0; }

    // ── Lifecycle callbacks ──────────────────────────────────
    void onUpdate(callbackVoid_t value);
    void onSwitchToInactive(callbackVoid_t value);
    void onSwitchToActive(callbackVoid_t value);
    void onRegistrationStart(callbackVoid_t value);
    void onRegistrationDone(callbackVoid_t value);

private:
    // ── ESP-NOW & registration ───────────────────────────────
    uint8_t masterMAC[6];           // Master peer MAC (set by MSG_ACK)
    bool registered;                // true after MSG_ACK received
    uint32_t lastRegisterAttempt;   // millis() of last MSG_REGISTER broadcast
    static const uint32_t REGISTER_RETRY_MS = 2000;  // Retry every 2s
    uint32_t lastHeartbeatAt;       // millis() of last heartbeat/ACK from master
    static const uint32_t HEARTBEAT_TIMEOUT_MS = 6000; // timeout if no heartbeat received

    bool msgRecvPending;            // true when MSG_GAMEPAD_DATA received, awaiting processData()

    // ── Gamepad state ────────────────────────────────────────
    GamepadState cur_state;
    GamepadState last_state;

    // ── Device tracking ──────────────────────────────────────
    char deviceName[32];
    bool _isDeviceActive;

    // ── FPS counter (debug) ──────────────────────────────────
    long fpsCounter;
    long fpsLastTime;

    // ── Button/analog event thresholds ───────────────────────
    uint8_t analogBtnEventThreshold = 40;

    // ── Lifecycle callbacks ──────────────────────────────────
    callbackVoid_t _callback_goingActive = nullptr;
    callbackVoid_t _callback_goingInactive = nullptr;
    callbackVoid_t _callback_startRegistration = nullptr;
    callbackVoid_t _callback_doneRegistration = nullptr;

    callbackVoid_t _callback_update = nullptr;

    // ── Button event callbacks ───────────────────────────────
    callbackPressed_t _callback_dpadup = nullptr;
    callbackPressed_t _callback_dpaddown = nullptr;
    callbackPressed_t _callback_dpadleft = nullptr;
    callbackPressed_t _callback_dpadright = nullptr;

    callbackPressed_t _callback_cross = nullptr;
    callbackPressed_t _callback_circle = nullptr;
    callbackPressed_t _callback_square = nullptr;
    callbackPressed_t _callback_triangle = nullptr;

    callbackPressed_t _callback_select = nullptr;
    callbackPressed_t _callback_ps = nullptr;           // Mapped to btn_system
    callbackPressed_t _callback_start = nullptr;

    callbackPressed_t _callback_leftstick = nullptr;    // Mapped to btn_thumb_l
    callbackPressed_t _callback_rightstick = nullptr;   // Mapped to btn_thumb_r

    callbackPressed_t _callback_l1 = nullptr;
    callbackPressed_t _callback_l2 = nullptr;
    callbackPressed_t _callback_r1 = nullptr;
    callbackPressed_t _callback_r2 = nullptr;

    // ── Analog stick event thresholds & callbacks ────────────
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

    // ── Private helper methods ───────────────────────────────
    void swap();                               // Swap cur_state -> last_state
    void sendRegisterMessage();                // Broadcast MSG_REGISTER
    void processData();                        // Event-driven callback logic (swap, compare, fire)
};

#endif