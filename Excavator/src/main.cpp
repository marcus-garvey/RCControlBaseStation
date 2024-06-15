#include <Arduino.h>
#include <ESP32Servo.h> // by Kevin Harrington
#include <WiFi.h>
#include <RemotePs3.h>
#include "Adafruit_MCP23X17.h"

// defines
const char* ssid = "RCBaseStation1";
const char* password = "12345678";


// defines
#define clawServoPin 5
#define auxServoPin 18
#define cabLights 32
#define auxLights 33

#define pivot0 15
#define pivot1 14
#define mainBoom0 9
#define mainBoom1 8
#define secondBoom0 0
#define secondBoom1 1
#define tiltAttach0 3
#define tiltAttach1 2
#define thumb0 11
#define thumb1 10
#define auxAttach0 12
#define auxAttach1 13

#define leftMotor0 7
#define leftMotor1 6
#define rightMotor0 4
#define rightMotor1 5


Adafruit_MCP23X17 mcp;
Servo clawServo;
Servo auxServo;
int dly = 250;
int clawServoValue = 90;
int auxServoValue = 90;
int player = 0;
int battery = 0;
int servoDelay = 0;

bool cabLightsOn = false;
bool auxLightsOn = false;
bool moveClawServoUp = false;
bool moveClawServoDown = false;
bool moveAuxServoUp = false;
bool moveAuxServoDown = false;

#define MCU_LED 2
#define DEVICE_NUM 2
RemotePs3 controller(DEVICE_NUM);




void onBtnDpadUp(bool down)
{
  if(down)
  {
    mcp.digitalWrite(thumb0, HIGH);
    mcp.digitalWrite(thumb1, LOW);
    Serial.println("Started pressing the up button");
  }
  else 
  {
    mcp.digitalWrite(thumb0, LOW);
    mcp.digitalWrite(thumb1, LOW);
    Serial.println("Released the up button");
  }
}

void onBtnDpadDown(bool down)
{
  if(down)
  {
    Serial.println("Started pressing the down button");
    mcp.digitalWrite(thumb0, LOW);
    mcp.digitalWrite(thumb1, HIGH);
  }
  else 
  {
    Serial.println("Released the down button");
    mcp.digitalWrite(thumb0, LOW);
    mcp.digitalWrite(thumb1, LOW);
  }
}

void onBtnDpadLeft(bool down)
{
  if(down)
  {
    mcp.digitalWrite(auxAttach0, LOW);
    mcp.digitalWrite(auxAttach1, HIGH);
    Serial.println("Started pressing the left button");
  }
  else 
  {
    mcp.digitalWrite(auxAttach0, LOW);
    mcp.digitalWrite(auxAttach1, LOW);
    Serial.println("Released the left button");
  }
}

void onBtnDpadRight(bool down)
{
  if(down)
  {
    mcp.digitalWrite(auxAttach0, HIGH);
    mcp.digitalWrite(auxAttach1, LOW);
    Serial.println("Started pressing the right button");
  }
  else 
  {
    mcp.digitalWrite(auxAttach0, LOW);
    mcp.digitalWrite(auxAttach1, LOW);
    Serial.println("Released the right button");
  }
}

void onBtnTriangle(bool down)
{
  if(down)
  {
    moveClawServoDown = true;
    Serial.println("Started pressing the triangle button");
  }
  else 
  {
    moveClawServoDown = false;
  }
}

void onBtnCross(bool down)
{
  if(down)
  {
    moveClawServoUp = true;
    Serial.println("Started pressing the cross button");
  }
  else 
  {
    moveClawServoUp = false;
    Serial.println("Released the cross button");
  }
}

void onBtnSquare(bool down)
{
  if(down)
  {
    moveAuxServoUp = true;
    Serial.println("Started pressing the square button"); 
  }
  else 
  {
    moveAuxServoUp = false;
    Serial.println("Released the square button");
  }
}

void onBtnCircle(bool down)
{
  moveAuxServoDown = down;
  if(down)
  {
    Serial.println("Started pressing the circle button");
  }
  else 
  {
    Serial.println("Released the circle button");
  }
  
}

void switchLightsCab(bool on)
{
  if (on) {
    digitalWrite(cabLights, HIGH);
    cabLightsOn = true;
  } else {
    digitalWrite(cabLights, LOW);
    cabLightsOn = false;
  }
}

void switchLightsAux(bool on)
{
  if (on) {
    digitalWrite(auxLights, HIGH);
    auxLightsOn = true;
  } else {
    digitalWrite(auxLights, LOW);
    auxLightsOn = false;
  }
}

void onBtnLeftStick(bool pressed)
{
  if(pressed)
    switchLightsCab(!cabLightsOn);
}

void onBtnRightStick(bool pressed)
{
  if(pressed)
    switchLightsAux(!auxLightsOn);
}

void onBtnL1(bool down)
{
  if(down) 
  {
    mcp.digitalWrite(leftMotor0, HIGH);
    mcp.digitalWrite(leftMotor1, LOW);
    
    Serial.println("Started pressing the left shoulder button");

  }
  else
  {
    mcp.digitalWrite(leftMotor0, LOW);
    mcp.digitalWrite(leftMotor1, LOW);
    
    Serial.println("Released the left shoulder button");
 
  }
}

void onBtnL2(bool down)
{
  if(down) 
  {
    mcp.digitalWrite(leftMotor0, LOW);
    mcp.digitalWrite(leftMotor1, HIGH);
    
    Serial.println("Started pressing the left trigger button");
  }
  else
  {
    mcp.digitalWrite(leftMotor0, LOW);
    mcp.digitalWrite(leftMotor1, LOW);
    
    Serial.println("Released the left trigger button");
  }
}

void onBtnR1(bool down)
{
  if(down) 
  {
    mcp.digitalWrite(rightMotor0, HIGH);
    mcp.digitalWrite(rightMotor1, LOW);
    
    Serial.println("Started pressing the right shoulder button");

  }
  else
  {
    mcp.digitalWrite(rightMotor0, LOW);
    mcp.digitalWrite(rightMotor1, LOW);
    Serial.println("Released the right shoulder button");
  }  
}

void onBtnR2(bool down)
{
  if(down) 
  {
    mcp.digitalWrite(rightMotor0, LOW);
    mcp.digitalWrite(rightMotor1, HIGH);
    Serial.println("Started pressing the right trigger button");
  }
  else
  {
    mcp.digitalWrite(rightMotor0, LOW);
    mcp.digitalWrite(rightMotor1, LOW);
    Serial.println("Released the right trigger button");
  }  
}

void onStickLX(int state)
{
  if(state == 1)
  {
    mcp.digitalWrite(pivot0, HIGH);
    mcp.digitalWrite(pivot1, LOW);

    Serial.print("LX Made to into Positive");
  }
  else if(state == -1)
  {
    mcp.digitalWrite(pivot0, LOW);
    mcp.digitalWrite(pivot1, HIGH);

    Serial.print("LX Made to into negative");
  }
  else
  {
    mcp.digitalWrite(pivot0, LOW);
    mcp.digitalWrite(pivot1, LOW);
  }
}

void onStickLY(int state)
{
  if(state == 1)
  {
    mcp.digitalWrite(secondBoom0, HIGH);
    mcp.digitalWrite(secondBoom1, LOW);

  }
  else if(state == -1)
  {
    mcp.digitalWrite(secondBoom0, LOW);
    mcp.digitalWrite(secondBoom1, HIGH);

  }
  else
  {
    mcp.digitalWrite(secondBoom0, LOW);
    mcp.digitalWrite(secondBoom1, LOW);
  }
}

void onStickRX(int state)
{
  if(state == 1)
  {
    mcp.digitalWrite(tiltAttach0, HIGH);
    mcp.digitalWrite(tiltAttach1, LOW);

    Serial.print("RX Made to into Positive");
  }
  else if(state == -1)
  {
    mcp.digitalWrite(tiltAttach0, LOW);
    mcp.digitalWrite(tiltAttach1, HIGH);

    Serial.print("RX Made to into negative");
  }
  else
  {
    mcp.digitalWrite(tiltAttach0, LOW);
    mcp.digitalWrite(tiltAttach1, LOW);
  }
}

void onStickRY(int state)
{
  Serial.println("RY Event");
  if(state == 1)
  {
    mcp.digitalWrite(mainBoom0, HIGH);
    mcp.digitalWrite(mainBoom1, LOW);
    
  }
  else if(state == -1)
  {
    mcp.digitalWrite(mainBoom0, LOW);
    mcp.digitalWrite(mainBoom1, HIGH);

  }
  else
  {
    mcp.digitalWrite(mainBoom0, LOW);
    mcp.digitalWrite(mainBoom1, LOW);
  }
}

void ctrlUpdate()
{
if (moveClawServoUp) {
    if (servoDelay == 3) {
      if (clawServoValue >= 10 && clawServoValue < 170) {
        clawServoValue = clawServoValue + 1;
        clawServo.write(clawServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveClawServoDown) {
    if (servoDelay == 3) {
      if (clawServoValue <= 170 && clawServoValue > 10) {
        clawServoValue = clawServoValue - 1;
        clawServo.write(clawServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoUp) {
    if (servoDelay == 3) {
      if (auxServoValue >= 10 && auxServoValue < 170) {
        auxServoValue = auxServoValue + 1;
        auxServo.write(auxServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoDown) {
    if (servoDelay == 3) {
      if (auxServoValue <= 170 && auxServoValue > 10) {
        auxServoValue = auxServoValue - 1;
        auxServo.write(auxServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }

}

void devGoingActive()
{
  switchLightsCab(true);
}

void devGoingInactive()
{
  switchLightsCab(false);
}

void registrationStart()
{
  
}

void registrationDone()
{
  digitalWrite(MCU_LED, HIGH);
}

void setup() {
  Serial.begin(115200);
  mcp.begin_I2C();

  for (int i = 0; i <= 15; i++) {
    mcp.pinMode(i, OUTPUT);
  }

  pinMode(clawServoPin, OUTPUT);
  pinMode(auxServoPin, OUTPUT);

  pinMode(cabLights, OUTPUT);
  pinMode(auxLights, OUTPUT);

  clawServo.attach(clawServoPin);
  auxServo.attach(auxServoPin);
  clawServo.write(clawServoValue);
  auxServo.write(auxServoValue);

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
  controller.onRegistrationStart(registrationStart);
  controller.onRegistrationDone(registrationDone);

  // Register all event btn here
  controller.onBtnDpadUpEvent(onBtnDpadUp);
  controller.onBtnDpadDownEvent(onBtnDpadDown);
  controller.onBtnDpadLeftEvent(onBtnDpadLeft);
  controller.onBtnDpadRightEvent(onBtnDpadRight);
  
  controller.onBtnCrossEvent(onBtnCross);
  controller.onBtnTriangleEvent(onBtnTriangle);
  controller.onBtnSquareEvent(onBtnSquare);
  controller.onBtnCircleEvent(onBtnCircle);

  controller.onBtnL1Event(onBtnL1);
  controller.onBtnR1Event(onBtnR1);
  controller.onBtnL2Event(onBtnL2);
  controller.onBtnR2Event(onBtnR2);

  controller.onBtnLeftStickEvent(onBtnLeftStick);
  controller.onBtnRightStickEvent(onBtnRightStick);

  controller.onStickLXEvent(onStickLX, 115, 30);
  controller.onStickLYEvent(onStickLY, 115, 30);
  controller.onStickRXEvent(onStickRX, 115, 30);
  controller.onStickRYEvent(onStickRY, 115, 30);

  controller.begin();

  switchLightsCab(false);
}

void loop() {
  // put your main code here, to run repeatedly:

  
}

