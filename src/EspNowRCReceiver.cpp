#include "EspNowRCReceiver.h"

// ────────────────────────────────────────────────────────────
// Global ESP-NOW callback (C-style)
// ────────────────────────────────────────────────────────────
static EspNowRCReceiver* g_espNowRCReceiverInstance = nullptr;

void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (g_espNowRCReceiverInstance == nullptr) {
        Serial.println("EspNowRCReceiver: Received message but no instance");
        return;
    }

    const EspNowMsg* msg = (const EspNowMsg*)incomingData;
    
    //Serial.printf("EspNowRCReceiver: onEspNowRecv msgType=%d len=%d from %02X:%02X:%02X:%02X:%02X:%02X\n", msg->msgType, len, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (msg->msgType == MSG_ACK) {
        if (!g_espNowRCReceiverInstance->registered) {
            // Registration successful — store master MAC and add peer for future comms
            memcpy(g_espNowRCReceiverInstance->masterMAC, mac, 6);
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, mac, 6);
            peer.channel = 0;
            peer.encrypt = false;
            esp_err_t result = esp_now_add_peer(&peer);
            Serial.printf("EspNowRCReceiver: Added master peer: result=%d\n", result);

            g_espNowRCReceiverInstance->registered = true;
            g_espNowRCReceiverInstance->_isDeviceActive = false;
            g_espNowRCReceiverInstance->lastHeartbeatAt = millis();
            Serial.println("EspNowRCReceiver: MSG_ACK received, registration complete");
            if (g_espNowRCReceiverInstance->_callback_doneRegistration) {
                g_espNowRCReceiverInstance->_callback_doneRegistration();
            }
        }
    }
    else if (msg->msgType == MSG_HEARTBEAT) {
        if (g_espNowRCReceiverInstance->registered && memcmp(mac, g_espNowRCReceiverInstance->masterMAC, 6) == 0) {
            g_espNowRCReceiverInstance->lastHeartbeatAt = millis();
            Serial.println("EspNowRCReceiver: Heartbeat received");
        }
    }
    else if (msg->msgType == MSG_ACTIVATE) {
        if (g_espNowRCReceiverInstance->registered && memcmp(mac, g_espNowRCReceiverInstance->masterMAC, 6) == 0) {
            g_espNowRCReceiverInstance->lastHeartbeatAt = millis();
            if (!g_espNowRCReceiverInstance->_isDeviceActive) {
                g_espNowRCReceiverInstance->_isDeviceActive = true;
                if (g_espNowRCReceiverInstance->_callback_goingActive) {
                    g_espNowRCReceiverInstance->_callback_goingActive();
                }
            }
            Serial.println("EspNowRCReceiver: ACTIVATE received");
            EspNowMsg ack = makeSimple(MSG_ACTIVATE_ACK);
            esp_now_send(mac, reinterpret_cast<const uint8_t*>(&ack), msgSize(MSG_ACTIVATE_ACK));
        }
    }
    else if (msg->msgType == MSG_DEACTIVATE) {
        if (g_espNowRCReceiverInstance->registered && memcmp(mac, g_espNowRCReceiverInstance->masterMAC, 6) == 0) {
            g_espNowRCReceiverInstance->lastHeartbeatAt = millis();
            if (g_espNowRCReceiverInstance->_isDeviceActive) {
                g_espNowRCReceiverInstance->_isDeviceActive = false;
                if (g_espNowRCReceiverInstance->_callback_goingInactive) {
                    g_espNowRCReceiverInstance->_callback_goingInactive();
                }
            }
            Serial.println("EspNowRCReceiver: DEACTIVATE received");
        }
    }
    else if (msg->msgType == MSG_GAMEPAD_DATA) {
        // Gamepad data received — set flag for processData() in update()
        memcpy(g_espNowRCReceiverInstance->cur_state.data, msg->payload.gamepad.data, sizeof(GamepadState));
        g_espNowRCReceiverInstance->msgRecvPending = true;
    }
}

// ────────────────────────────────────────────────────────────
// Constructor
// ────────────────────────────────────────────────────────────
EspNowRCReceiver::EspNowRCReceiver(const char* friendlyName)
{
    strncpy(deviceName, friendlyName, sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';
    memset(cur_state.data, 0, sizeof(GamepadState));
    memset(last_state.data, 0, sizeof(GamepadState));
    memset(masterMAC, 0, 6);
    
    registered = false;
    _isDeviceActive = false;
    lastRegisterAttempt = 0;
    lastHeartbeatAt = 0;
    msgRecvPending = false;

    fpsCounter = 0;
    fpsLastTime = millis();
    
    // Register global instance for C-style callback
    g_espNowRCReceiverInstance = this;
}

// ────────────────────────────────────────────────────────────
// Initialization
// ────────────────────────────────────────────────────────────
void EspNowRCReceiver::begin()
{
    // Ensure WiFi is in station mode for ESP-NOW
    WiFi.mode(WIFI_STA);
    Serial.printf("EspNowRCReceiver: Slave MAC = %s\n", WiFi.macAddress().c_str());
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("EspNowRCReceiver: ESP-NOW init failed");
        return;
    }
    Serial.println("EspNowRCReceiver: ESP-NOW init successful");
    
    // Register receive callback
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("EspNowRCReceiver: Recv callback registered");
    
    // Register broadcast peer so we can send MSG_REGISTER
    uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, broadcastAddr, 6);
    broadcastPeer.channel = 0;
    broadcastPeer.encrypt = false;
    esp_err_t addPeerResult = esp_now_add_peer(&broadcastPeer);
    Serial.printf("EspNowRCReceiver: Added broadcast peer: result=%d\n", addPeerResult);
    
    // Force first registration attempt immediately
    lastRegisterAttempt = millis() - REGISTER_RETRY_MS;
    
    if (_callback_startRegistration) _callback_startRegistration();
}


// ────────────────────────────────────────────────────────────
// Main loop function — call this every loop iteration
// ────────────────────────────────────────────────────────────
void EspNowRCReceiver::update()
{
    uint32_t now = millis();
    
    // Non-blocking registration retry
    if (!registered) {
        if ((now - lastRegisterAttempt) >= REGISTER_RETRY_MS) {
            sendRegisterMessage();
        }
        return;  // Skip gamepad processing until registered
    }

    // Check heartbeat timeout
    if (lastHeartbeatAt > 0 && (now - lastHeartbeatAt) >= HEARTBEAT_TIMEOUT_MS) {
        Serial.println("EspNowRCReceiver: Heartbeat timeout - resetting registration");
        registered = false;
        _isDeviceActive = false;
        if (_callback_goingInactive) {
            _callback_goingInactive();
        }
        esp_now_del_peer(masterMAC);
        memset(masterMAC, 0, sizeof(masterMAC));
        lastHeartbeatAt = 0;
        return;
    }
    
    // Process pending gamepad data if received
    if (msgRecvPending) {
        processData();
        msgRecvPending = false;
    }
}

// ────────────────────────────────────────────────────────────
// Send MSG_REGISTER broadcast to master
// ────────────────────────────────────────────────────────────
void EspNowRCReceiver::sendRegisterMessage()
{
    EspNowMsg msg = makeRegister(deviceName);
    
    // Broadcast to all peers (0xFF:0xFF:0xFF:0xFF:0xFF:0xFF)
    uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    size_t msgLen = msgSize(MSG_REGISTER);
    Serial.printf("EspNowRCReceiver: Sending MSG_REGISTER (name='%s', len=%d)\n",
                  deviceName, msgLen);
    esp_err_t result = esp_now_send(broadcastAddr, (const uint8_t *)&msg, msgLen);
    Serial.printf("EspNowRCReceiver: esp_now_send result=%d\n", result);
    
    lastRegisterAttempt = millis();
}

// ────────────────────────────────────────────────────────────
// Process gamepad state changes (event-driven callbacks)
// ────────────────────────────────────────────────────────────
void EspNowRCReceiver::processData()
{
    fpsCounter++;
    if((millis() - fpsLastTime) > 1000) {
        fpsLastTime = millis();
        Serial.print(fpsCounter,DEC);
        Serial.println(" FPS");
        fpsCounter = 0;
    }

    

    // Button press events (rising edge: false→true)
    if(last_state.part.btn_system == false && cur_state.part.btn_system == true && _callback_ps) _callback_ps(true);
    if(last_state.part.btn_start == false && cur_state.part.btn_start == true && _callback_start) _callback_start(true);
    if(last_state.part.btn_select == false && cur_state.part.btn_select == true && _callback_select) _callback_select(true);

    if(last_state.part.btn_dpad_up == false && cur_state.part.btn_dpad_up == true && _callback_dpadup) _callback_dpadup(true);
    if(last_state.part.btn_dpad_down == false && cur_state.part.btn_dpad_down == true && _callback_dpaddown) _callback_dpaddown(true);
    if(last_state.part.btn_dpad_left == false && cur_state.part.btn_dpad_left == true && _callback_dpadleft) _callback_dpadleft(true);
    if(last_state.part.btn_dpad_right == false && cur_state.part.btn_dpad_right == true && _callback_dpadright) _callback_dpadright(true);

    if(last_state.part.btn_cross == false && cur_state.part.btn_cross == true && _callback_cross) _callback_cross(true);
    if(last_state.part.btn_circle == false && cur_state.part.btn_circle == true && _callback_circle) _callback_circle(true);
    if(last_state.part.btn_square == false && cur_state.part.btn_square == true && _callback_square) _callback_square(true);
    if(last_state.part.btn_triangle == false && cur_state.part.btn_triangle == true && _callback_triangle) _callback_triangle(true);

    if(last_state.part.btn_thumb_l == false && cur_state.part.btn_thumb_l == true && _callback_leftstick) _callback_leftstick(true);
    if(last_state.part.btn_thumb_r == false && cur_state.part.btn_thumb_r == true && _callback_rightstick) _callback_rightstick(true);

    // Button release events (falling edge: true→false)
    if(last_state.part.btn_system == true && cur_state.part.btn_system == false && _callback_ps) _callback_ps(false);
    if(last_state.part.btn_start == true && cur_state.part.btn_start == false && _callback_start) _callback_start(false);
    if(last_state.part.btn_select == true && cur_state.part.btn_select == false && _callback_select) _callback_select(false);

    if(last_state.part.btn_dpad_up == true && cur_state.part.btn_dpad_up == false && _callback_dpadup) _callback_dpadup(false);
    if(last_state.part.btn_dpad_down == true && cur_state.part.btn_dpad_down == false && _callback_dpaddown) _callback_dpaddown(false);
    if(last_state.part.btn_dpad_left == true && cur_state.part.btn_dpad_left == false && _callback_dpadleft) _callback_dpadleft(false);
    if(last_state.part.btn_dpad_right == true && cur_state.part.btn_dpad_right == false && _callback_dpadright) _callback_dpadright(false);

    if(last_state.part.btn_cross == true && cur_state.part.btn_cross == false && _callback_cross) _callback_cross(false);
    if(last_state.part.btn_circle == true && cur_state.part.btn_circle == false && _callback_circle) _callback_circle(false);
    if(last_state.part.btn_square == true && cur_state.part.btn_square == false && _callback_square) _callback_square(false);
    if(last_state.part.btn_triangle == true && cur_state.part.btn_triangle == false && _callback_triangle) _callback_triangle(false);

    if(last_state.part.btn_thumb_l == true && cur_state.part.btn_thumb_l == false && _callback_leftstick) _callback_leftstick(false);
    if(last_state.part.btn_thumb_r == true && cur_state.part.btn_thumb_r == false && _callback_rightstick) _callback_rightstick(false);

    // Digital shoulder buttons (GamepadState only has digital btn_l1, btn_r1)
    if(last_state.part.btn_l1 == false && cur_state.part.btn_l1 == true && _callback_l1) _callback_l1(true);
    if(last_state.part.btn_l1 == true && cur_state.part.btn_l1 == false && _callback_l1) _callback_l1(false);

    if(last_state.part.btn_r1 == false && cur_state.part.btn_r1 == true && _callback_r1) _callback_r1(true);
    if(last_state.part.btn_r1 == true && cur_state.part.btn_r1 == false && _callback_r1) _callback_r1(false);

    // Analog triggers (threshold-based)
    if(last_state.part.analog_l2 <= analogBtnEventThreshold && cur_state.part.analog_l2 > analogBtnEventThreshold && _callback_l2) _callback_l2(true);
    if(last_state.part.analog_l2 > analogBtnEventThreshold && cur_state.part.analog_l2 <= analogBtnEventThreshold && _callback_l2) _callback_l2(false);

    if(last_state.part.analog_r2 <= analogBtnEventThreshold && cur_state.part.analog_r2 > analogBtnEventThreshold && _callback_r2) _callback_r2(true);
    if(last_state.part.analog_r2 > analogBtnEventThreshold && cur_state.part.analog_r2 <= analogBtnEventThreshold && _callback_r2) _callback_r2(false);

    // Analog stick events (high threshold)
    if(last_state.part.analog_lx <= analogThresholdHighLX && cur_state.part.analog_lx > analogThresholdHighLX && _callback_LX) _callback_LX(1);
    if(last_state.part.analog_lx >= (-1 * analogThresholdHighLX) && cur_state.part.analog_lx < (-1 * analogThresholdHighLX) && _callback_LX) _callback_LX(-1);
    if(((last_state.part.analog_lx > analogThresholdLowLX && cur_state.part.analog_lx <= analogThresholdLowLX) || (last_state.part.analog_lx < (-1 * analogThresholdLowLX) && cur_state.part.analog_lx >= (-1 * analogThresholdLowLX))) && _callback_LX) _callback_LX(0);

    if(last_state.part.analog_ly <= analogThresholdHighLY && cur_state.part.analog_ly > analogThresholdHighLY && _callback_LY) _callback_LY(1);
    if(last_state.part.analog_ly >= (-1 * analogThresholdHighLY) && cur_state.part.analog_ly < (-1 * analogThresholdHighLY) && _callback_LY) _callback_LY(-1);
    if(((last_state.part.analog_ly > analogThresholdLowLY && cur_state.part.analog_ly <= analogThresholdLowLY) || (last_state.part.analog_ly < (-1 * analogThresholdLowLY) && cur_state.part.analog_ly >= (-1 * analogThresholdLowLY))) && _callback_LY) _callback_LY(0);

    if(last_state.part.analog_rx <= analogThresholdHighRX && cur_state.part.analog_rx > analogThresholdHighRX && _callback_RX) _callback_RX(1);
    if(last_state.part.analog_rx >= (-1 * analogThresholdHighRX) && cur_state.part.analog_rx < (-1 * analogThresholdHighRX) && _callback_RX) _callback_RX(-1);
    if(((last_state.part.analog_rx > analogThresholdLowRX && cur_state.part.analog_rx <= analogThresholdLowRX) || (last_state.part.analog_rx < (-1 * analogThresholdLowRX) && cur_state.part.analog_rx >= (-1 * analogThresholdLowRX))) && _callback_RX) _callback_RX(0);

    if(last_state.part.analog_ry <= analogThresholdHighRY && cur_state.part.analog_ry > analogThresholdHighRY && _callback_RY) _callback_RY(1);
    if(last_state.part.analog_ry >= (-1 * analogThresholdHighRY) && cur_state.part.analog_ry < (-1 * analogThresholdHighRY) && _callback_RY) _callback_RY(-1);
    if(((last_state.part.analog_ry > analogThresholdLowRY && cur_state.part.analog_ry <= analogThresholdLowRY) || (last_state.part.analog_ry < (-1 * analogThresholdLowRY) && cur_state.part.analog_ry >= (-1 * analogThresholdLowRY))) && _callback_RY) _callback_RY(0);

    if(_callback_update) _callback_update();

    swap();
}

void EspNowRCReceiver::swap()
{
    memcpy(last_state.data, cur_state.data, sizeof(GamepadState));
}

// ────────────────────────────────────────────────────────────
// Lifecycle callbacks & public API
// ────────────────────────────────────────────────────────────

bool EspNowRCReceiver::isDeviceActive() {
    return registered;
}

bool EspNowRCReceiver::isRegistered() {
    return registered;
}

void EspNowRCReceiver::onUpdate(callbackVoid_t value) { _callback_update = value; }

void EspNowRCReceiver::onSwitchToInactive(callbackVoid_t value) { _callback_goingInactive = value; }
void EspNowRCReceiver::onSwitchToActive(callbackVoid_t value) { _callback_goingActive = value; }

void EspNowRCReceiver::onRegistrationStart(callbackVoid_t value) { _callback_startRegistration = value; }
void EspNowRCReceiver::onRegistrationDone(callbackVoid_t value) { _callback_doneRegistration = value; }

// ────────────────────────────────────────────────────────────
// Button event registration
// ────────────────────────────────────────────────────────────

void EspNowRCReceiver::onBtnDpadUpEvent(callbackPressed_t value) { _callback_dpadup = value; }
void EspNowRCReceiver::onBtnDpadDownEvent(callbackPressed_t value) { _callback_dpaddown = value; }
void EspNowRCReceiver::onBtnDpadLeftEvent(callbackPressed_t value) { _callback_dpadleft = value; }
void EspNowRCReceiver::onBtnDpadRightEvent(callbackPressed_t value) { _callback_dpadright = value; }

void EspNowRCReceiver::onBtnCrossEvent(callbackPressed_t value) { _callback_cross = value; }
void EspNowRCReceiver::onBtnCircleEvent(callbackPressed_t value) { _callback_circle = value; }
void EspNowRCReceiver::onBtnSquareEvent(callbackPressed_t value) { _callback_square = value; }
void EspNowRCReceiver::onBtnTriangleEvent(callbackPressed_t value) { _callback_triangle = value; }

void EspNowRCReceiver::onBtnLeftStickEvent(callbackPressed_t value) { _callback_leftstick = value; }
void EspNowRCReceiver::onBtnRightStickEvent(callbackPressed_t value) { _callback_rightstick = value; }

void EspNowRCReceiver::onBtnL1Event(callbackPressed_t value) { _callback_l1 = value; }
void EspNowRCReceiver::onBtnL2Event(callbackPressed_t value) { _callback_l2 = value; }

void EspNowRCReceiver::onBtnR1Event(callbackPressed_t value) { _callback_r1 = value; }
void EspNowRCReceiver::onBtnR2Event(callbackPressed_t value) { _callback_r2 = value; }

void EspNowRCReceiver::onBtnSelectEvent(callbackPressed_t value) { _callback_select = value; }
void EspNowRCReceiver::onBtnPsEvent(callbackPressed_t value) { _callback_ps = value; }
void EspNowRCReceiver::onBtnStartEvent(callbackPressed_t value) { _callback_start = value; }

// ────────────────────────────────────────────────────────────
// Analog stick event registration (threshold-based)
// ────────────────────────────────────────────────────────────

void EspNowRCReceiver::onStickLXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold) {
    _callback_LX = funcValue;
    analogThresholdHighLX = highThreshold;
    analogThresholdLowLX = lowThreshold;
}

void EspNowRCReceiver::onStickLYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold) {
    _callback_LY = funcValue;
    analogThresholdHighLY = highThreshold;
    analogThresholdLowLY = lowThreshold;
}

void EspNowRCReceiver::onStickRXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold) {
    _callback_RX = funcValue;
    analogThresholdHighRX = highThreshold;
    analogThresholdLowRX = lowThreshold;
}

void EspNowRCReceiver::onStickRYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold) {
    _callback_RY = funcValue;
    analogThresholdHighRY = highThreshold;
    analogThresholdLowRY = lowThreshold;
}

// ────────────────────────────────────────────────────────────
// Getter methods — return current gamepad state
// ────────────────────────────────────────────────────────────

int8_t EspNowRCReceiver::getLeftStickX() { return cur_state.part.analog_lx; }
int8_t EspNowRCReceiver::getLeftStickY() { return cur_state.part.analog_ly; }

int8_t EspNowRCReceiver::getRightStickX() { return cur_state.part.analog_rx; }
int8_t EspNowRCReceiver::getRightStickY() { return cur_state.part.analog_ry; }

uint8_t EspNowRCReceiver::getL2() { return cur_state.part.analog_l2; }
uint8_t EspNowRCReceiver::getR2() { return cur_state.part.analog_r2; }

