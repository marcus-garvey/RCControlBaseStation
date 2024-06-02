
#include <Ps3Controller.h>
#include "TransmitData.h"
#include <Base64.h>


#define RXD2 16
#define TXD2 17

transmit controller_state;
int update_interval = 40;

int encodedLen = 0;
int dataLen = 10;
unsigned long lastTime;
void updateState(transmit& state)
{
    state.part.btn_circle = Ps3.data.button.circle;
    state.part.btn_cross = Ps3.data.button.cross;
    state.part.btn_square = Ps3.data.button.square;
    state.part.btn_triangle = Ps3.data.button.triangle;

    state.part.btn_dpad_down = Ps3.data.button.down;
    state.part.btn_dpad_up = Ps3.data.button.up;
    state.part.btn_dpad_left = Ps3.data.button.left;
    state.part.btn_dpad_right = Ps3.data.button.right;

    state.part.btn_select = Ps3.data.button.select;
    state.part.btn_start = Ps3.data.button.start;
    state.part.btn_ps = Ps3.data.button.ps;

    state.part.btn_left_stick = Ps3.data.button.l3;
    state.part.btn_right_stick = Ps3.data.button.r3;

    state.part.btn_reserved1 = false;
    state.part.btn_reserved2 = false;
    state.part.btn_reserved3 = false;

    state.part.analog_lx = Ps3.data.analog.stick.lx;
    state.part.analog_ly = Ps3.data.analog.stick.ly;

    state.part.analog_rx = Ps3.data.analog.stick.rx;
    state.part.analog_ry = Ps3.data.analog.stick.ry;

    state.part.analog_l1 = Ps3.data.analog.button.l1;
    state.part.analog_l2 = Ps3.data.analog.button.l2;
    state.part.analog_r1 = Ps3.data.analog.button.r1;
    state.part.analog_r2 = Ps3.data.analog.button.r2;

}

void onPS3Notify()
{
    updateState(controller_state);
}

void onPS3Connect(){
    Serial.println("PS3 Controller Connected.");
    delay(2000);
    Ps3.setPlayer(0);
}

void onPS3Disconnect()
{
    Serial.println("PS3 Controller Disconnected.");    
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(38400, SERIAL_8N1, RXD2, TXD2);
    Ps3.attach(onPS3Notify);
    Ps3.attachOnConnect(onPS3Connect);
    Ps3.attachOnDisconnect(onPS3Disconnect);
    Ps3.begin("1a:2b:3c:01:01:01");

    Serial.println("Ready.");
    String address = Ps3.getAddress();
    Serial.println(address);

    encodedLen = Base64.encodedLength(dataLen);
    
    Serial.printf("%d %d",dataLen,encodedLen);
}

void loop()
{
    if(!Ps3.isConnected())
        return;

    if (Serial2.available() > 0) {
        // read the incoming byte:
        char temp[1];
        Serial2.readBytes(temp,1);
        
        Ps3.setPlayer(temp[0]);
         // say what you got:
        Serial.print("I received: ");
        Serial.println(temp[0], DEC);
    }


    if((millis() - lastTime) > update_interval)
    {
        char encodedString[encodedLen + 1];
        Base64.encode(encodedString, controller_state.data, dataLen);
    
        Serial2.write(0);
        Serial2.write(encodedLen);
        Serial2.write(encodedString,encodedLen);
        Serial2.flush();
        //Serial.write(encodedString,encodedLen);
        //Serial.println();
        lastTime = millis();
    }
    
}


