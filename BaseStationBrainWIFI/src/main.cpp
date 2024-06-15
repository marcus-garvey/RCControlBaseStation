#include <Arduino.h>
#include <WiFi.h>
#include <Base64.h>
#include <AsyncUDP.h>

#include "RemotePs3.h"

#define MSG_HELLO_NEW_CLIENT 0x01
#define MSG_CLIENT_ACCEPTED 0x02
#define MSG_SELECT_CLIENT 0x03
#define MSG_CTRL_STATE_UPDATE 0x0A

const char* ssid = "RCBaseStation1";
const char* password = "12345678";

#define RXD2 16
#define TXD2 17

int serverport = 12345;
int clientport = 12346;

RemotePs3 rPs3;

#define MAX_DEVICES 10
bool activeDevices[MAX_DEVICES];
int selectedDevice = -1;

AsyncUDP udp;
IPAddress broadcast(192, 168, 4, 255);

void sendBroadcast(uint8_t msgId, byte* data, uint8_t len)
{
    AsyncUDPMessage msg;
    msg.write(msgId);
    msg.write(len);
    msg.write(data,len);

    udp.sendTo(msg,broadcast,clientport);
    // This method doesnt work
    //udp.broadcastTo(dataToSend,2+len,clientport);
}

void selectDevice(byte devNum)
{
    selectedDevice = devNum;

    Serial.printf("Select device %d\n",(devNum+1));
    Serial2.write((char)(devNum+1));
    Serial2.flush();
    byte data[] = {(byte)(devNum+1)};

    sendBroadcast(MSG_SELECT_CLIENT,data,1);
}

void onSelectDown()
{
  Serial.println("Select BTN Pressed");
  int round = 0;
  for (int i = selectedDevice+1; i < MAX_DEVICES; i++)
  {
    if(activeDevices[i])
    {
        selectDevice(i);
        break;
    }

    // We start the loop from begining 
    if(i == (MAX_DEVICES - 1) && round == 0)
    {
        Serial.println("Reset Loop");
        i = -1;
        round++;    
    }
  }
}

void onUDPRecv(AsyncUDPPacket packet)
{
    if(packet.data()[0] == MSG_HELLO_NEW_CLIENT && packet.data()[1] == 0x01)
    {
        byte deviceNum = packet.data()[2];
        Serial.printf("New Device Num %d\n", deviceNum); 
        activeDevices[deviceNum-1] = true;

        AsyncUDPMessage msg;
        msg.write(MSG_CLIENT_ACCEPTED);
        udp.sendTo(msg,packet.remoteIP(),clientport);
    }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("\nCreating AP");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.print("AP Created with IP Gateway ");
  Serial.println(WiFi.softAPIP());

  rPs3.onSelect(onSelectDown);

  if(udp.listen(serverport)) {
    udp.onPacket(onUDPRecv);
    Serial.println("UDP ready.");
  }

  Serial2.write((char)(0x00));
  Serial2.flush();
}

long fpsLastTime =0;
int fpsCounter = 0;
void loop() {
  // put your main code here, to run repeatedly:
  if (Serial2.available())
  {
    
    int d = Serial2.read();
    if(d == 0)
    {
      fpsCounter++;
      if((millis()-fpsLastTime) > 1000)
      {
        Serial.print(fpsCounter,DEC);
        Serial.println("FPS");
        fpsLastTime = millis();
        fpsCounter = 0;
      }
      //Serial.println("NEw Frame");
      byte temp[1];
      Serial2.readBytes(temp,1);
      
      byte size = temp[0];
      char data[size];
      Serial2.readBytes(data,size); 
      //Serial.write(data,size);
      //Serial.println();
      int decodedLength = Base64.decodedLength(data, size);
      char decodedData[decodedLength + 1];
      Base64.decode(decodedData, data, size);

      //Serial.print(decodedLength);
      rPs3.update(decodedData); 
      

      sendBroadcast(MSG_CTRL_STATE_UPDATE,(byte *)decodedData,decodedLength);

    }
  }
    
}

