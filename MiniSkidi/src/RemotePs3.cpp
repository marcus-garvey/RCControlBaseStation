#include "RemotePs3.h"


RemotePs3::RemotePs3(byte devicenum)
{
    device = devicenum;
    memset(cur_state.data, 0, 10);
    memset(last_state.data, 0, 10);
    register_mode = true;
    serverAddr.fromString("192.168.4.1");
    _isDeviceActive = false;

    fpsCounter = 0;
    fpsLastTime = millis();
}

void RemotePs3::begin()
{
    if(udp.listen(clientport)) {
        udp.onPacket([](void* arg,AsyncUDPPacket packet) {
            ((RemotePs3*)arg)->onUDPRecv(packet);
        }, this);
        Serial.println("UDP ready.");
    }
    if(_callback_startRegistration) _callback_startRegistration();
    while(register_mode)
    {
        Serial.println("Send hello message to server");
        byte temp[1];
        temp[0] = device;
        sendData(MSG_HELLO_NEW_CLIENT,temp,1);
        delay(2000);
    }
    Serial.println("Registration done");
    if(_callback_doneRegistration) _callback_doneRegistration();
}

bool RemotePs3::isDeviceActive()
{
    return _isDeviceActive;
}

void RemotePs3::onUDPRecv(AsyncUDPPacket packet)
{
    if(packet.data()[0] == MSG_CLIENT_ACCEPTED)
    {
        Serial.println("Recv accept client message");
        register_mode = false;
    }
    else if(packet.data()[0] == MSG_SELECT_CLIENT && packet.data()[1] == 0x01)
    {
        byte deviceNum = packet.data()[2];
        if(device == deviceNum)
        {
            Serial.println("This device is active");
            _isDeviceActive = true;
            if(_callback_goingActive) _callback_goingActive();
        }
        else
        {
            Serial.println("This device is inactive");
            _isDeviceActive = false;
            if(_callback_goingInactive) _callback_goingInactive();
        }
    }
    else if(packet.data()[0] == MSG_CTRL_STATE_UPDATE)
    {
        update((byte*)&(packet.data()[2]), packet.data()[1]);
    }
}

void RemotePs3::sendData(byte msgid, byte* data,byte len)
{
    AsyncUDPMessage msg;
    msg.write(msgid);
    msg.write(len);

    if(len > 0)
        msg.write(data,len);

    udp.sendTo(msg,serverAddr,serverport);
}

void RemotePs3::update(byte* data, byte len)
{
    fpsCounter++;
    if((millis() - fpsLastTime) > 1000)
    {
        fpsLastTime = millis();
        Serial.print(fpsCounter,DEC);
        Serial.println("FPS");
        fpsCounter = 0;
    }

    swap();
    memcpy(cur_state.data,data,len);
    
    // Not active so we skip event handling 
    if(!_isDeviceActive) return;

    if(last_state.part.btn_ps == false && cur_state.part.btn_ps == true && _callback_ps) _callback_ps(true);
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

    if(last_state.part.btn_left_stick == false && cur_state.part.btn_left_stick == true && _callback_leftstick) _callback_leftstick(true);
    if(last_state.part.btn_right_stick == false && cur_state.part.btn_right_stick == true && _callback_rightstick) _callback_rightstick(true);

    if(last_state.part.btn_ps == true && cur_state.part.btn_ps == false && _callback_ps) _callback_ps(false);
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

    if(last_state.part.btn_left_stick == true && cur_state.part.btn_left_stick == false && _callback_leftstick) _callback_leftstick(false);
    if(last_state.part.btn_right_stick == true && cur_state.part.btn_right_stick == false && _callback_rightstick) _callback_rightstick(false);

    if(last_state.part.analog_l1 <= analogBtnEventThreshold && cur_state.part.analog_l1 > analogBtnEventThreshold && _callback_l1 ) _callback_l1(true);
    if(last_state.part.analog_l2 <= analogBtnEventThreshold && cur_state.part.analog_l2 > analogBtnEventThreshold && _callback_l2 ) _callback_l2(true);
    if(last_state.part.analog_r1 <= analogBtnEventThreshold && cur_state.part.analog_r1 > analogBtnEventThreshold && _callback_r1 ) _callback_r1(true);
    if(last_state.part.analog_r2 <= analogBtnEventThreshold && cur_state.part.analog_r2 > analogBtnEventThreshold && _callback_r2 ) _callback_r2(true);

    if(last_state.part.analog_l1 > analogBtnEventThreshold && cur_state.part.analog_l1 <= analogBtnEventThreshold && _callback_l1 ) _callback_l1(false);
    if(last_state.part.analog_l2 > analogBtnEventThreshold && cur_state.part.analog_l2 <= analogBtnEventThreshold && _callback_l2 ) _callback_l2(false);
    if(last_state.part.analog_r1 > analogBtnEventThreshold && cur_state.part.analog_r1 <= analogBtnEventThreshold && _callback_r1 ) _callback_r1(false);
    if(last_state.part.analog_r2 > analogBtnEventThreshold && cur_state.part.analog_r2 <= analogBtnEventThreshold && _callback_r2 ) _callback_r2(false);


    if(last_state.part.analog_lx <= analogThresholdHighLX && cur_state.part.analog_lx > analogThresholdHighLX && _callback_LX) _callback_LX(1);
    if(last_state.part.analog_lx >= (-1 * analogThresholdHighLX) && cur_state.part.analog_lx < (-1 * analogThresholdHighLX) && _callback_LX) _callback_LX(-1);

    if(((last_state.part.analog_lx > analogThresholdLowLX && cur_state.part.analog_lx <= analogThresholdLowLX) ||
        (last_state.part.analog_lx < (-1 * analogThresholdLowLX) && cur_state.part.analog_lx >= (-1 * analogThresholdLowLX)))
        && _callback_LX) _callback_LX(0);

    if(last_state.part.analog_ly <= analogThresholdHighLY && cur_state.part.analog_ly > analogThresholdHighLY && _callback_LY) _callback_LY(1);
    if(last_state.part.analog_ly >= (-1 * analogThresholdHighLY) && cur_state.part.analog_ly < (-1 * analogThresholdHighLY) && _callback_LY) _callback_LY(-1);

    if(((last_state.part.analog_ly > analogThresholdLowLY && cur_state.part.analog_ly <= analogThresholdLowLY) ||
        (last_state.part.analog_ly < (-1 * analogThresholdLowLY) && cur_state.part.analog_ly >= (-1 * analogThresholdLowLY)))
        && _callback_LY) _callback_LY(0);

    if(last_state.part.analog_rx <= analogThresholdHighRX && cur_state.part.analog_rx > analogThresholdHighRX && _callback_RX) _callback_RX(1);
    if(last_state.part.analog_rx >= (-1 * analogThresholdHighRX) && cur_state.part.analog_rx < (-1 * analogThresholdHighRX) && _callback_RX) _callback_RX(-1);

    if(((last_state.part.analog_rx > analogThresholdLowRX && cur_state.part.analog_rx <= analogThresholdLowRX) ||
        (last_state.part.analog_rx < (-1 * analogThresholdLowRX) && cur_state.part.analog_rx >= (-1 * analogThresholdLowRX)))
        && _callback_RX) _callback_RX(0);

    if(last_state.part.analog_ry <= analogThresholdHighRY && cur_state.part.analog_ry > analogThresholdHighRY && _callback_RY) _callback_RY(1);
    if(last_state.part.analog_ry >= (-1 * analogThresholdHighRY) && cur_state.part.analog_ry < (-1 * analogThresholdHighRY) && _callback_RY) _callback_RY(-1);

    if(((last_state.part.analog_ry > analogThresholdLowRY && cur_state.part.analog_ry <= analogThresholdLowRY) ||
        (last_state.part.analog_ry < (-1 * analogThresholdLowRY) && cur_state.part.analog_ry >= (-1 * analogThresholdLowRY)))
        && _callback_RY) _callback_RY(0);

    if(_callback_update) _callback_update();
}

void RemotePs3::swap()
{
    memcpy(last_state.data,cur_state.data,10);
}

void RemotePs3::onUpdate(callbackVoid_t value) { _callback_update = value; } 

void RemotePs3::onSwitchToInactive(callbackVoid_t value) { _callback_goingInactive = value; }
void RemotePs3::onSwitchToActive(callbackVoid_t value) { _callback_goingActive = value; }

void RemotePs3::onRegistrationStart(callbackVoid_t value) { _callback_startRegistration = value; }
void RemotePs3::onRegistrationDone(callbackVoid_t value) { _callback_doneRegistration = value; }


void RemotePs3::onBtnDpadUpEvent(callbackPressed_t value) { _callback_dpadup = value; }
void RemotePs3::onBtnDpadDownEvent(callbackPressed_t value) { _callback_dpaddown = value; }
void RemotePs3::onBtnDpadLeftEvent(callbackPressed_t value) { _callback_dpadleft = value; }
void RemotePs3::onBtnDpadRightEvent(callbackPressed_t value) { _callback_dpadright = value; }

void RemotePs3::onBtnCrossEvent(callbackPressed_t value) { _callback_cross = value; }
void RemotePs3::onBtnCircleEvent(callbackPressed_t value) { _callback_circle = value; }
void RemotePs3::onBtnSquareEvent(callbackPressed_t value) { _callback_square = value; }
void RemotePs3::onBtnTriangleEvent(callbackPressed_t value) { _callback_triangle = value; }

void RemotePs3::onBtnLeftStickEvent(callbackPressed_t value) { _callback_leftstick = value; }
void RemotePs3::onBtnRightStickEvent(callbackPressed_t value) { _callback_rightstick = value; }

void RemotePs3::onBtnL1Event(callbackPressed_t value) { _callback_l1 = value; }
void RemotePs3::onBtnL2Event(callbackPressed_t value) { _callback_l2 = value; }

void RemotePs3::onBtnR1Event(callbackPressed_t value) { _callback_r1 = value; }
void RemotePs3::onBtnR2Event(callbackPressed_t value) { _callback_r2 = value; }

void RemotePs3::onBtnSelectEvent(callbackPressed_t value) { _callback_select = value; }
void RemotePs3::onBtnPsEvent(callbackPressed_t value) { _callback_ps = value; }
void RemotePs3::onBtnStartEvent(callbackPressed_t value) { _callback_start = value; }

int8_t RemotePs3::getLeftStickX() { return cur_state.part.analog_lx; }
int8_t RemotePs3::getLeftStickY() { return cur_state.part.analog_ly; }

int8_t RemotePs3::getRightStickX() { return cur_state.part.analog_rx; }
int8_t RemotePs3::getRightStickY() { return cur_state.part.analog_ry; }

uint8_t RemotePs3::getL1() { return cur_state.part.analog_l1; }
uint8_t RemotePs3::getL2() { return cur_state.part.analog_l2; }

uint8_t RemotePs3::getR1() { return cur_state.part.analog_r1; }
uint8_t RemotePs3::getR2() { return cur_state.part.analog_r2; }

void RemotePs3::onStickLXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold)
{
    _callback_LX = funcValue;
    analogThresholdHighLX = highThreshold;
    analogThresholdLowLX = lowThreshold;
}
void RemotePs3::onStickLYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold)
{
    _callback_LY = funcValue;
    analogThresholdHighLY = highThreshold;
    analogThresholdLowLY = lowThreshold;
}
void RemotePs3::onStickRXEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold)
{
    _callback_RX = funcValue;
    analogThresholdHighRX = highThreshold;
    analogThresholdLowRX = lowThreshold;
}
void RemotePs3::onStickRYEvent(callbackAnalogStick_t funcValue, uint8_t highThreshold, uint8_t lowThreshold)
{
    _callback_RY = funcValue;
    analogThresholdHighRY = highThreshold;
    analogThresholdLowRY = lowThreshold;
}

