#include <Arduino.h>
#include <ESP32Servo.h> // by Kevin Harrington
#include <WiFi.h>
#include <RemotePs3.h>


// defines
const char* ssid = "RCBaseStation1";
const char* password = "12345678";

#define MCU_LED 2


#define rightMotor0 25
#define rightMotor1 26

#define leftMotor0 33
#define leftMotor1 32

#define armMotor0 21
#define armMotor1 19

#define bucketServoPin  23
#define clawServoPin 22

#define auxLights0 18
#define auxLights1 5

Servo bucketServo;
Servo clawServo;

int bucketServoValue = 140;
int clawServoValue = 150;
int moveServoInterval = 20;
long lastClawServoMove = 0;
long lastBucketServoMove = 0;
bool auxLightsOn = false;
bool moveClawServoUp = false;
bool moveClawServoDown = false;
bool moveBucketServoUp = false;
bool moveBucketServoDown = false;


int lxLastState = 0;
int lyLastState = 0;

RemotePs3 controller(3);

void onBtnL1(bool down)
{
  moveClawServoUp = down;
}

void onBtnL2(bool down)
{
  moveBucketServoDown = down;
}

void onBtnR1(bool down)
{
  moveClawServoDown = down; 
}

void onBtnR2(bool down)
{
  moveBucketServoUp = down;
}

void stopTracks()
{
  if(lxLastState == 0 && lyLastState == 0)
  {
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, LOW);
    delay(10);
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, LOW);
  }
}

void onStickLX(int state)
{
  lxLastState = state;
  if(state == 1)
  {
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, HIGH);
    delay(10);
    digitalWrite(leftMotor0, HIGH);
    digitalWrite(leftMotor1, LOW);
  }
  else if(state == -1)
  {
    digitalWrite(rightMotor0, HIGH);
    digitalWrite(rightMotor1, LOW);
    delay(10);
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, HIGH);
  }
  else
  {
    stopTracks();
  }
}

void onStickLY(int state)
{
  lyLastState = state;
  if(state == 1)
  {
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, HIGH);
    delay(10);
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, HIGH);

  }
  else if(state == -1)
  {
    digitalWrite(rightMotor0, HIGH);
    digitalWrite(rightMotor1, LOW);
    delay(10);
    digitalWrite(leftMotor0, HIGH);
    digitalWrite(leftMotor1, LOW);
  }
  else
  {
    stopTracks();
  }
}

void onStickRY(int state)
{
  if(state == 1)
  {
    digitalWrite(armMotor0, LOW);
    digitalWrite(armMotor1, HIGH);
  }
  else if(state == -1)
  {
    digitalWrite(armMotor0, HIGH);
    digitalWrite(armMotor1, LOW);
  }
  else
  {
    digitalWrite(armMotor0, LOW);
    digitalWrite(armMotor1, LOW);
  }
}

void switchLights(bool on)
{
  if (on)
  {
    digitalWrite(auxLights0, HIGH);
    digitalWrite(auxLights1, LOW);
    auxLightsOn = true;
  }
  else
  {
    digitalWrite(auxLights0, LOW);
    digitalWrite(auxLights1, LOW);
    auxLightsOn = false;
  }
}

void onRightStickDown(bool down)
{
  if(down)
    switchLights(!auxLightsOn);
}

void setUpPinModes()
{
  pinMode(MCU_LED, OUTPUT); 
  pinMode(rightMotor0, OUTPUT);
  pinMode(rightMotor1, OUTPUT);
  pinMode(leftMotor0, OUTPUT);
  pinMode(leftMotor1, OUTPUT);
  pinMode(armMotor0, OUTPUT);
  pinMode(armMotor1, OUTPUT);

  pinMode(auxLights0, OUTPUT);
  pinMode(auxLights1, OUTPUT);
  
  bucketServo.attach(bucketServoPin);
  clawServo.attach(clawServoPin);

  bucketServo.write(bucketServoValue);
  clawServo.write(clawServoValue);

}



void devGoingActive()
{
  switchLights(true);
}

void devGoingInactive()
{

  switchLights(false);
}

void ctrlUpdate()
{
  
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
  controller.onRegistrationStart(registrationStart);
  controller.onRegistrationDone(registrationDone);

  controller.onBtnRightStickEvent(onRightStickDown);
  controller.onStickLXEvent(onStickLX, 115, 30);
  controller.onStickLYEvent(onStickLY, 115, 30);
  controller.onStickRYEvent(onStickRY, 115, 30);
  controller.onBtnL1Event(onBtnL1);
  controller.onBtnR1Event(onBtnR1);
  controller.onBtnL2Event(onBtnL2);
  controller.onBtnR2Event(onBtnR2);
  controller.begin();

  switchLights(false);
}


void loop() {
  // put your main code here, to run repeatedly:

  if(moveClawServoUp || moveClawServoDown)
  {
    if((millis() - lastClawServoMove) > moveServoInterval)
    { 
      if(moveClawServoUp && clawServoValue < 170)
      {
        clawServoValue++;
      }
      else if(moveClawServoDown && clawServoValue > 10)
      {
        clawServoValue--;
      }
      clawServo.write(clawServoValue);
      lastClawServoMove = millis();
    }
  }

  if(moveBucketServoUp || moveBucketServoDown)
  {
    if((millis() - lastBucketServoMove) > moveServoInterval)
    { 
      if(moveBucketServoUp && bucketServoValue < 170)
      {
        bucketServoValue++;
      }
      else if(moveBucketServoDown && bucketServoValue > 10)
      {
        bucketServoValue--;
      }
      bucketServo.write(bucketServoValue);
      lastBucketServoMove = millis();
    }
  }

}

