#include <Arduino.h>
#include <ESP32Servo.h> // by Kevin Harrington
#include <WiFi.h>
#include <RemotePs3.h>


// defines
const char* ssid = "RCBaseStation1";
const char* password = "12345678";

#define steeringServoPin 23
#define auxServoPin 22
#define cabLights 32
#define auxLights 33

#define auxAttach0 25  // Used for controlling auxiliary attachment movement
#define auxAttach1 26  // Used for controlling auxiliary attachment movement
#define auxAttach2 18  // Used for controlling auxiliary attachment movement
#define auxAttach3 17  // Used for controlling auxiliary attachment movement

#define leftMotor0 33   // Used for controlling the left motor movement
#define leftMotor1 32   // Used for controlling the left motor movement
#define rightMotor0 21  // Used for controlling the right motor movement
#define rightMotor1 19  // Used for controlling the right motor movement

#define MCU_LED 2

// global variables
Servo steeringServo;
Servo auxServo;
int lightSwitchTime = 0;
int adjustedSteeringValue = 86;
int auxServoValue = 90;
bool lightsOn = false;


bool lightsOff = true;

RemotePs3 controller(4);

void steeringControl(int steeringServoValue)
{
  steeringServo.write(steeringServoValue);
}


void moveMotor(int motorPin0, int motorPin1, int velocity) {
  if (velocity > 15) {
    analogWrite(motorPin0, velocity);
    analogWrite(motorPin1, LOW);
  } else if (velocity < -15) {
    analogWrite(motorPin0, LOW);
    analogWrite(motorPin1, (-1 * velocity));
  } else {
    analogWrite(motorPin0, 0);
    analogWrite(motorPin1, 0);
  }
}


void switchLights(bool on)
{
  if (on)
  {
    Serial.println("Lights on");
    digitalWrite(auxAttach0, HIGH);
    digitalWrite(auxAttach1, LOW);
    lightsOff = false;
  }
  else
  {
    Serial.println("Lights off");
    digitalWrite(auxAttach0, LOW);
    digitalWrite(auxAttach1, LOW);
    lightsOff = true;
  }
}

void setUpPinModes()
{
  pinMode(auxAttach2, OUTPUT);
  pinMode(auxAttach3, OUTPUT);
  pinMode(auxAttach0, OUTPUT);
  pinMode(auxAttach1, OUTPUT);
  digitalWrite(auxAttach2, LOW);
  digitalWrite(auxAttach3, LOW);
  pinMode(leftMotor0, OUTPUT);
  pinMode(leftMotor1, OUTPUT);
  pinMode(rightMotor0, OUTPUT);
  pinMode(rightMotor1, OUTPUT);


  steeringServo.attach(steeringServoPin);
  steeringServo.write(adjustedSteeringValue);

  pinMode(MCU_LED, OUTPUT); 
}

void devGoingActive()
{
  switchLights(true);
}

void devGoingInactive()
{
  steeringControl(80);
  moveMotor(leftMotor0, leftMotor1, 0);
  moveMotor(rightMotor0, rightMotor1, 0);
  switchLights(false);
}

void ctrlDpadLeft(bool pressed)
{
  if(pressed)
    switchLights(lightsOff);
}

void ctrlDpadUp(bool pressed)
{
  // dumpster up
  if(pressed)
  {
    digitalWrite(auxAttach2, HIGH);
    digitalWrite(auxAttach3, LOW);
  }
  else
  {
    digitalWrite(auxAttach2, LOW);
    digitalWrite(auxAttach3, LOW);
  }
}

void ctrlDpadDown(bool pressed)
{
  // dumpster down
  if(pressed)
  {
    digitalWrite(auxAttach2, LOW);
    digitalWrite(auxAttach3, HIGH);
  }
  else
  {
    digitalWrite(auxAttach2, LOW);
    digitalWrite(auxAttach3, LOW);
  }
}

void ctrlUpdate()
{
  int LYValue = controller.getR2() - controller.getL2();
  int adjustedThrottleValue = LYValue;
  moveMotor(leftMotor1, leftMotor0, adjustedThrottleValue);
  moveMotor(rightMotor1, rightMotor0, adjustedThrottleValue);

  int ctrlValSteering = controller.getLeftStickX();
  adjustedSteeringValue = 90 - (ctrlValSteering / 3);
  steeringControl(adjustedSteeringValue);

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
  controller.onBtnDpadLeftEvent(ctrlDpadLeft);
  controller.onBtnDpadUpEvent(ctrlDpadUp);
  controller.onBtnDpadDownEvent(ctrlDpadDown);
  controller.onRegistrationStart(registrationStart);
  controller.onRegistrationDone(registrationDone);
  

  controller.begin();

  switchLights(false);
}

void loop() {
  // put your main code here, to run repeatedly:

}

