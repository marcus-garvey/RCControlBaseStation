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

    if(last_state.part.btn_ps == false && cur_state.part.btn_ps == true && _callback_ps) _callback_ps();
    if(last_state.part.btn_start == false && cur_state.part.btn_start == true && _callback_start) _callback_start();
    if(last_state.part.btn_select == false && cur_state.part.btn_select == true && _callback_select) _callback_select();

    if(last_state.part.btn_dpad_up == false && cur_state.part.btn_dpad_up == true && _callback_dpadup) _callback_dpadup();
    if(last_state.part.btn_dpad_down == false && cur_state.part.btn_dpad_down == true && _callback_dpaddown) _callback_dpaddown();
    if(last_state.part.btn_dpad_left == false && cur_state.part.btn_dpad_left == true && _callback_dpadleft) _callback_dpadleft();
    if(last_state.part.btn_dpad_right == false && cur_state.part.btn_dpad_right == true && _callback_dpadright) _callback_dpadright();

    if(last_state.part.btn_cross == false && cur_state.part.btn_cross == true && _callback_cross) _callback_cross();
    if(last_state.part.btn_circle == false && cur_state.part.btn_circle == true && _callback_circle) _callback_circle();
    if(last_state.part.btn_square == false && cur_state.part.btn_square == true && _callback_square) _callback_square();
    if(last_state.part.btn_triangle == false && cur_state.part.btn_triangle == true && _callback_triangle) _callback_triangle();

    if(last_state.part.btn_left_stick == false && cur_state.part.btn_left_stick == true && _callback_leftstick) _callback_leftstick();
    if(last_state.part.btn_right_stick == false && cur_state.part.btn_right_stick == true && _callback_rightstick) _callback_rightstick();

    if(_callback_update) _callback_update();
}

void RemotePs3::swap()
{
    memcpy(last_state.data,cur_state.data,10);
}

void RemotePs3::onUpdate(callbackPS_t value) { _callback_update = value; } 

void RemotePs3::onSwitchToInactive(callbackPS_t value) { _callback_goingInactive = value; }
void RemotePs3::onSwitchToActive(callbackPS_t value) { _callback_goingActive = value; }

void RemotePs3::onRegistrationStart(callbackPS_t value) { _callback_startRegistration = value; }
void RemotePs3::onRegistrationDone(callbackPS_t value) { _callback_doneRegistration = value; }


void RemotePs3::onBtnDpadUpDown(callbackPS_t value) { _callback_dpadup = value; }
void RemotePs3::onBtnDpadDownDown(callbackPS_t value) { _callback_dpaddown = value; }
void RemotePs3::onBtnDpadLeftDown(callbackPS_t value) { _callback_dpadleft = value; }
void RemotePs3::onBtnDpadRightDown(callbackPS_t value) { _callback_dpadright = value; }

void RemotePs3::onBtnCrossDown(callbackPS_t value) { _callback_cross = value; }
void RemotePs3::onBtnCircleDown(callbackPS_t value) { _callback_circle = value; }
void RemotePs3::onBtnSquareDown(callbackPS_t value) { _callback_square = value; }
void RemotePs3::onBtnTriangleDown(callbackPS_t value) { _callback_triangle = value; }

void RemotePs3::onBtnLeftStickDown(callbackPS_t value) { _callback_leftstick = value; }
void RemotePs3::onBtnRightStickDown(callbackPS_t value) { _callback_rightstick = value; }

void RemotePs3::onBtnSelectDown(callbackPS_t value) { _callback_select = value; }
void RemotePs3::onBtnPsDown(callbackPS_t value) { _callback_ps = value; }
void RemotePs3::onBtnStartDown(callbackPS_t value) { _callback_start = value; }

int8_t RemotePs3::getLeftStickX() { return cur_state.part.analog_lx; }
int8_t RemotePs3::getLeftStickY() { return cur_state.part.analog_ly; }

int8_t RemotePs3::getRightStickX() { return cur_state.part.analog_rx; }
int8_t RemotePs3::getRightStickY() { return cur_state.part.analog_ry; }

uint8_t RemotePs3::getL1() { return cur_state.part.analog_l1; }
uint8_t RemotePs3::getL2() { return cur_state.part.analog_l2; }

uint8_t RemotePs3::getR1() { return cur_state.part.analog_r1; }
uint8_t RemotePs3::getR2() { return cur_state.part.analog_r2; }