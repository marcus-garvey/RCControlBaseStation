#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <EspNowRCReceiver.h>

// ============================================================
// MiniDozer — converted from MiniDozerOrg to EspNowRCReceiver
// ============================================================

#define DEVICE_NAME "MiniDozer"

#define LT1 15
#define LT2 27
#define LT3 14

#define ripperServoPin 23

Servo ripperServo;

#define leftMotor0 4
#define leftMotor1 2
#define rightMotor0 12
#define rightMotor1 13

#define leftBladeTilt0 16
#define leftBladeTilt1 17
#define rightBladeTilt0 19
#define rightBladeTilt1 18
#define bladeTilt0 25
#define bladeTilt1 26
#define ripperMotor0 33
#define ripperMotor1 32

unsigned long lastInputTime = 0;
const unsigned long INPUT_TIMEOUT = 40;


int ripperServoValue = 90;
int servoDelay = 2;
bool lightMode = false;
bool lightsOn = false;
bool moveRipperServoUp = false;
bool moveRipperServoDown = false;

bool bladeTiltForward = false;
bool bladeTiltBackward = false;
bool ripperForward = false;
bool ripperBackward = false;

EspNowRCReceiver controller(DEVICE_NAME);

void moveMotor(int motorPin0, int motorPin1, int velocity) {
    if (velocity > 15) {
        analogWrite(motorPin0, velocity);
        analogWrite(motorPin1, LOW);
    } else if (velocity < -15) {
        analogWrite(motorPin0, LOW);
        analogWrite(motorPin1, -velocity);
    } else {
        analogWrite(motorPin0, 0);
        analogWrite(motorPin1, 0);
    }
}

void stopAll() {
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, LOW);
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, LOW);
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, LOW);
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, LOW);
    digitalWrite(bladeTilt0, LOW);
    digitalWrite(bladeTilt1, LOW);
    digitalWrite(ripperMotor0, LOW);
    digitalWrite(ripperMotor1, LOW);
    digitalWrite(LT1, LOW);
    digitalWrite(LT2, LOW);
    digitalWrite(LT3, LOW);
}

void setUpPinModes() {
    pinMode(leftMotor0, OUTPUT);
    pinMode(leftMotor1, OUTPUT);
    pinMode(rightMotor0, OUTPUT);
    pinMode(rightMotor1, OUTPUT);
    pinMode(leftBladeTilt0, OUTPUT);
    pinMode(leftBladeTilt1, OUTPUT);
    pinMode(rightBladeTilt0, OUTPUT);
    pinMode(rightBladeTilt1, OUTPUT);
    pinMode(bladeTilt0, OUTPUT);
    pinMode(bladeTilt1, OUTPUT);
    pinMode(ripperMotor0, OUTPUT);
    pinMode(ripperMotor1, OUTPUT);
    pinMode(LT1, OUTPUT);
    pinMode(LT2, OUTPUT);
    pinMode(LT3, OUTPUT);

    stopAll();

    ripperServo.attach(ripperServoPin);
    ripperServo.write(ripperServoValue);
}

void switchLights(bool on)
{
    if (on) {
        digitalWrite(LT1, HIGH);
        lightMode = true;
    } else {
        digitalWrite(LT1, LOW);
        lightMode = false;
    }
}

void processThrottle() {
    int leftY = controller.getLeftStickY();
    int rightY = controller.getRightStickY();
    moveMotor(leftMotor0, leftMotor1, leftY * 2);
    moveMotor(rightMotor0, rightMotor1, rightY * 2);
}

void processAuxMotors() {
    if (bladeTiltForward) {
        moveMotor(bladeTilt0, bladeTilt1, 255);
    } else if (bladeTiltBackward) {
        moveMotor(bladeTilt0, bladeTilt1, -255);
    } else {
        moveMotor(bladeTilt0, bladeTilt1, 0);
    }

    if (ripperForward) {
        moveMotor(ripperMotor0, ripperMotor1, 255);
    } else if (ripperBackward) {
        moveMotor(ripperMotor0, ripperMotor1, -255);
    } else {
        moveMotor(ripperMotor0, ripperMotor1, 0);
    }
}

void updateRipperServo() {
    if (moveRipperServoUp) {
        if (servoDelay == 2) {
            if (ripperServoValue >= 10 && ripperServoValue < 170) {
                ripperServoValue += 2;
                ripperServo.write(ripperServoValue);
            }
            servoDelay = 0;
        }
        servoDelay++;
    } else if (moveRipperServoDown) {
        if (servoDelay == 2) {
            if (ripperServoValue <= 170 && ripperServoValue > 10) {
                ripperServoValue -= 2;
                ripperServo.write(ripperServoValue);
            }
            servoDelay = 0;
        }
        servoDelay++;
    }
}

void ctrlUpdate() {
    lastInputTime = millis();

    processThrottle();
    processAuxMotors();
}

void ctrlDpadUp(bool pressed) {
    bladeTiltForward = pressed;
}

void ctrlDpadDown(bool pressed) {
    bladeTiltBackward = pressed;
}

void ctrlDpadLeft(bool pressed) {
    ripperForward = pressed;
}

void ctrlDpadRight(bool pressed) {
    ripperBackward = pressed;
}

void ctrlBtnCross(bool pressed) {
    moveRipperServoDown = pressed;
}

void ctrlBtnCircle(bool pressed) {
    moveRipperServoUp = pressed;
}

void ctrlBtnSquare(bool pressed) {

}

void ctrlBtnTriangle(bool pressed) {

}

void ctrlBtnL1(bool pressed) {
    digitalWrite(leftBladeTilt0, pressed ? HIGH : LOW);
    digitalWrite(leftBladeTilt1, LOW);
}

void ctrlBtnL2(bool pressed) {
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, pressed ? HIGH : LOW);
}

void ctrlBtnR1(bool pressed) {
    digitalWrite(rightBladeTilt0, pressed ? HIGH : LOW);
    digitalWrite(rightBladeTilt1, LOW);
}

void ctrlBtnR2(bool pressed) {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, pressed ? HIGH : LOW);
}

void ctrlBtnLeftStick(bool pressed) {

}

void ctrlBtnRightStick(bool pressed) {
    if(pressed) {
        switchLights(!lightMode);
    }
}

void ctrlBtnSelect(bool pressed) {

}

void ctrlBtnStart(bool pressed) {

}

void ctrlBtnPs(bool pressed) {

}

void devGoingActive() {
    Serial.println("Device going active");
    switchLights(true);
}

void devGoingInactive() {
    Serial.println("Device going inactive");
    stopAll();
    ripperServo.write(90);
    switchLights(false);
}

void registrationStart() {
    Serial.println("Registration started");
}

void registrationDone() {
    Serial.println("Registration done");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    WiFi.mode(WIFI_STA);
    Serial.printf("Slave MAC: %s\n", WiFi.macAddress().c_str());

    setUpPinModes();

    controller.onSwitchToActive(devGoingActive);
    controller.onSwitchToInactive(devGoingInactive);
    controller.onRegistrationStart(registrationStart);
    controller.onRegistrationDone(registrationDone);

    controller.onUpdate(ctrlUpdate);

    controller.onBtnDpadUpEvent(ctrlDpadUp);
    controller.onBtnDpadDownEvent(ctrlDpadDown);
    controller.onBtnDpadLeftEvent(ctrlDpadLeft);
    controller.onBtnDpadRightEvent(ctrlDpadRight);

    controller.onBtnCrossEvent(ctrlBtnCross);
    controller.onBtnCircleEvent(ctrlBtnCircle);
    controller.onBtnSquareEvent(ctrlBtnSquare);
    controller.onBtnTriangleEvent(ctrlBtnTriangle);
    controller.onBtnL1Event(ctrlBtnL1);
    controller.onBtnL2Event(ctrlBtnL2);
    controller.onBtnR1Event(ctrlBtnR1);
    controller.onBtnR2Event(ctrlBtnR2);
    controller.onBtnLeftStickEvent(ctrlBtnLeftStick);
    controller.onBtnRightStickEvent(ctrlBtnRightStick);
    controller.onBtnSelectEvent(ctrlBtnSelect);
    controller.onBtnStartEvent(ctrlBtnStart);
    controller.onBtnPsEvent(ctrlBtnPs);

    controller.begin();
    lastInputTime = millis();
}

void loop() {
    controller.update();
    updateRipperServo();
}
