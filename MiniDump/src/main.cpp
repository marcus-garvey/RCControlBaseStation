#include <Arduino.h>
#include <ESP32Servo.h> // by Kevin Harrington
#include <WiFi.h>
#include <RemotePs3.h>


// defines
const char* ssid = "RCBaseStation1";
const char* password = "12345678";

#define steeringServoPin  23
#define dumpServoPin 22
#define lightPin1 26
#define lightPin2 25

#define MCU_LED 2

// global variables

Servo steeringServo;
Servo dumpServo;

int dumpBedServoValue = 5;
bool moveDumpServoUp = false;
bool moveDumpServoDown = false;
int moveServoInterval = 15;
long lastDumpServoMove = 0;

bool lightsOff = true;

RemotePs3 controller(1);

// min="48" max="112" value="80"
void steeringControl(int steeringServoValue)
{
  steeringServo.write(steeringServoValue);
}

//min="-255" max="255" value="0"
void throttleControl(int throttleValue)
{
  if (throttleValue > 20)
  {
    analogWrite(32, throttleValue);
    analogWrite(33, LOW);
  }
  else if (throttleValue < -20)
  {
    throttleValue = throttleValue * -1;
    analogWrite(33, throttleValue);
    analogWrite(32, LOW);
  }
  else
  {
    analogWrite(33, LOW);
    analogWrite(32, LOW);
  }
}


void switchLights(bool on)
{
  if (on)
  {
    Serial.println("Lights on");
    digitalWrite(lightPin1, HIGH);
    digitalWrite(lightPin2, LOW);
    lightsOff = false;
  }
  else
  {
    Serial.println("Lights off");
    digitalWrite(lightPin1, LOW);
    digitalWrite(lightPin2, LOW);
    lightsOff = true;
  }
}

void setUpPinModes()
{
  steeringServo.attach(steeringServoPin);
  dumpServo.attach(dumpServoPin);
  pinMode(lightPin1, OUTPUT);
  pinMode(lightPin2, OUTPUT); 
  pinMode(MCU_LED, OUTPUT); 
  dumpServo.write(5);
  delay(50);
  steeringControl(80);
}

void devGoingActive()
{
  switchLights(true);
}

void devGoingInactive()
{
  steeringControl(80);
  throttleControl(0);
  switchLights(false);
}

void ctrlDpadUp(bool pressed)
{
  if(pressed)
    switchLights(lightsOff);
}

void onStickRY(int state)
{
  if(state > 0)
  {
    moveDumpServoUp = true;
    moveDumpServoDown = false;
  }
  else if (state < 0)
  {
    moveDumpServoUp = false;
    moveDumpServoDown = true;
  }
  else 
  {
    moveDumpServoUp = false;
    moveDumpServoDown = false;
  }
}

void ctrlUpdate()
{
  int ctrlValThrottle = controller.getR2() - controller.getL2();
  throttleControl(ctrlValThrottle);

  int ctrlValSteering = controller.getLeftStickX();
  int mappedValSteering = map(ctrlValSteering,-127,127,112,48);
  steeringControl(mappedValSteering);

}

void registrationStart()
{
  
}

void registrationDone()
{
  digitalWrite(MCU_LED, HIGH);
}

void setup() {
  setUpPinModes();
  Serial.begin(115200);

  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting WIFI");

  while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(100);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());

  controller.onSwitchToActive(devGoingActive);
  controller.onSwitchToInactive(devGoingInactive);
  controller.onUpdate(ctrlUpdate);
  controller.onBtnDpadUpEvent(ctrlDpadUp);
  controller.onRegistrationStart(registrationStart);
  controller.onRegistrationDone(registrationDone);
  controller.onStickRYEvent(onStickRY, 115, 30);
  controller.begin();

  switchLights(false);
}

void loop() {
  // put your main code here, to run repeatedly:
  if(moveDumpServoUp || moveDumpServoDown)
  {
    if((millis() - lastDumpServoMove) > moveServoInterval)
    { 
      if(moveDumpServoUp && dumpBedServoValue < 185)
      {
        dumpBedServoValue++;
      }
      else if(moveDumpServoDown && dumpBedServoValue > 5)
      {
        dumpBedServoValue--;
      }
      dumpServo.write(dumpBedServoValue);
      lastDumpServoMove = millis();
    }
  }

}

