// ============================================================
//  MiniLoader2.cpp  —  Updated MiniLoader using EspNowRCReceiver
//
//  Using EspNowRCReceiver for simplified integration
//  Handles all ESP-NOW registration & message parsing.
//  This sketch focuses only on device-specific logic:
//
//  Set the friendly name here:
#define DEVICE_NAME "MiniLoader"
// ============================================================

#include <Arduino.h>
#include <ESP32Servo.h> // by Kevin Harrington
#include <WiFi.h>
#include <EspNowRCReceiver.h>

// ── Hardware Pins ───────────────────────────────────────────
#define LT1 15
#define LT2 27
#define LT3 14

#define steeringServoPin 23
#define attachmentServoPin 22

#define leftMotor0 4   // \ Used for controlling front drive motor movement
#define leftMotor1 2   // /
#define rightMotor0 12 // \ Used for controlling rear drive motor movement.
#define rightMotor1 13 // /

#define leftBladeTilt0 17
#define leftBladeTilt1 16
#define rightBladeTilt0 18
#define rightBladeTilt1 19
#define attachmentMotor0 26
#define attachmentMotor1 25
#define ripperMotor0 32
#define ripperMotor1 33

// ── Global Variables ────────────────────────────────────────
Servo steeringServo;
Servo attachmentServo;

unsigned long lastInputTime = 0;
const unsigned long INPUT_TIMEOUT = 40; // ms — adjust if needed

int lightSwitchTime = 0;
int lightMode = 0;
bool lightsOn = false;
bool auxLightsOn = false;
bool blinkLT = false;
bool hazardLT = false;
bool hazardsOn = false;
bool attachmentOn = false;
int adjustedSteeringValue = 90 + 10;
int steeringTrim = 10;
unsigned long lastSteeringServoTime = 0;
bool incrementalSteeringMode = false;

unsigned long servoTimer = 0;
bool servoActive = false;

// Triple-tap tracking
int tapCount = 0;
unsigned long firstTapTime = 0;
const unsigned long tapWindow = 800; // time window to count taps (ms)
int lastDpadValue = 0;

struct ComboButton
{
    int count;
    unsigned long firstPressTime;
    bool lastState;
};

ComboButton bBtn = {0, 0, false};
ComboButton xBtn = {0, 0, false};

const unsigned long comboWindow = 600; // ms

EspNowRCReceiver controller(DEVICE_NAME);

// ── Motor Control ───────────────────────────────────────────
void moveMotor(int motorPin0, int motorPin1, int velocity)
{
    if (velocity > 15)
    {
        analogWrite(motorPin0, velocity);
        analogWrite(motorPin1, LOW);
    }
    else if (velocity < -15)
    {
        analogWrite(motorPin0, LOW);
        analogWrite(motorPin1, (-1 * velocity));
    }
    else
    {
        analogWrite(motorPin0, 0);
        analogWrite(motorPin1, 0);
    }
}

// ── Steering Control ────────────────────────────────────────
void processSteering(int axisRXValue)
{
    if (incrementalSteeringMode)
    {
        if (millis() - lastSteeringServoTime >= 20)
        {
            adjustedSteeringValue = adjustedSteeringValue + axisRXValue / 60;
            if (adjustedSteeringValue > 145)
            {
                adjustedSteeringValue = 144;
            }
            if (adjustedSteeringValue < 45)
            {
                adjustedSteeringValue = 46;
            }
            steeringServo.write(adjustedSteeringValue);
            lastSteeringServoTime = millis();
        }
    }
    else
    {
        adjustedSteeringValue = 180 - ((90 - (axisRXValue / 3)) - steeringTrim);
        steeringServo.write(adjustedSteeringValue);
        // Serial.print("Steering Value:");
        // Serial.println(adjustedSteeringValue);
    }
}

// ── Throttle Control ────────────────────────────────────────
void processThrottle(int axisYValue)
{
    int adjustedThrottleValue = axisYValue * 2;
    moveMotor(leftMotor0, leftMotor1, adjustedThrottleValue);
    moveMotor(rightMotor0, rightMotor1, adjustedThrottleValue);
}

// ── Combo Button Handler ────────────────────────────────────
void handleCombo(bool currentState, ComboButton &btn, bool dirA, bool dirB)
{
    if (currentState && !btn.lastState)
    {
        unsigned long now = millis();

        // If attachment is ON → single press turns it OFF
        if (attachmentOn)
        {
            digitalWrite(attachmentMotor0, LOW);
            digitalWrite(attachmentMotor1, LOW);
            attachmentOn = false;

            btn.count = 0;
            btn.lastState = currentState;
            return;
        }

        // Attachment is OFF → count combo presses
        if (btn.count == 0)
        {
            btn.firstPressTime = now;
        }

        btn.count++;

        if ((now - btn.firstPressTime) > comboWindow)
        {
            btn.count = 1;
            btn.firstPressTime = now;
        }

        // 3 presses → turn ON
        if (btn.count == 3)
        {
            digitalWrite(attachmentMotor0, dirA ? HIGH : LOW);
            digitalWrite(attachmentMotor1, dirB ? HIGH : LOW);
            attachmentOn = true;
            btn.count = 0;
        }
    }

    btn.lastState = currentState;
}

void stopAll()
{
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, LOW);
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, LOW);
    digitalWrite(ripperMotor0, LOW);
    digitalWrite(ripperMotor1, LOW);
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, LOW);
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, LOW);
    digitalWrite(attachmentMotor0, LOW);
    digitalWrite(attachmentMotor1, LOW);
    digitalWrite(LT1, LOW);
    digitalWrite(LT2, LOW);
    digitalWrite(LT3, LOW);
    hazardLT = false;
    blinkLT = false;
    lightsOn = false;
    auxLightsOn = false;
    hazardsOn = false;  
    lightMode = 0;
}

// ── Setup Pin Modes ─────────────────────────────────────────
void setUpPinModes()
{
    // Light pins
    pinMode(LT1, OUTPUT);
    pinMode(LT2, OUTPUT);
    pinMode(LT3, OUTPUT);

    // Motor pins
    pinMode(leftMotor0, OUTPUT);
    pinMode(leftMotor1, OUTPUT);
    pinMode(rightMotor0, OUTPUT);
    pinMode(rightMotor1, OUTPUT);

    // Blade tilt pins
    pinMode(leftBladeTilt0, OUTPUT);
    pinMode(leftBladeTilt1, OUTPUT);
    pinMode(rightBladeTilt0, OUTPUT);
    pinMode(rightBladeTilt1, OUTPUT);

    // Attachment pins
    pinMode(attachmentMotor0, OUTPUT);
    pinMode(attachmentMotor1, OUTPUT);
    pinMode(ripperMotor0, OUTPUT);
    pinMode(ripperMotor1, OUTPUT);

    stopAll();
    // Servos
    steeringServo.attach(steeringServoPin);
    steeringServo.write(adjustedSteeringValue);
    attachmentServo.attach(attachmentServoPin);
    attachmentServo.write(115);
    servoActive = true;
}

// ── Control Update ──────────────────────────────────────────
void ctrlUpdate()
{
    lastInputTime = millis();

    // Throttle: Y axis
    int axisYValue = controller.getLeftStickY();
    processThrottle(axisYValue);

    // Steering: Right X axis
    int axisRXValue = controller.getRightStickX();
    processSteering(axisRXValue);
}

// ── D-Pad Callbacks ─────────────────────────────────────────

bool checkTapCount()
{
    if (millis() - firstTapTime > tapWindow)
    {
        tapCount = 0;
    }
    tapCount++; 
    Serial.print("Tap Count: ");
    Serial.println(tapCount);

    if (tapCount == 1) firstTapTime = millis();
    return tapCount >= 3;
}

void ctrlDpadUp(bool pressed)
{
    if(!pressed) return;

    if (checkTapCount())
    {
        int targetPosition = 10;

        attachmentServo.attach(attachmentServoPin);
        attachmentServo.write(targetPosition);
        servoTimer = millis();
        servoActive = true;
    

        // Reset tap count after activation
        tapCount = 0;
        firstTapTime = 0;
    }
}

void ctrlDpadDown(bool pressed)
{
    if(!pressed) return;

    if (checkTapCount())
    {
        int targetPosition = 115;

        attachmentServo.attach(attachmentServoPin);
        attachmentServo.write(targetPosition);
        
        servoTimer = millis();
        servoActive = true;
    

        // Reset tap count after activation
        tapCount = 0;
        firstTapTime = 0;
    }
}

void checkAndResetServo()
{
    if (servoActive && millis() - servoTimer >= 4000)
    {
        attachmentServo.detach();
        servoActive = false;
    }
}

void ctrlDpadLeft(bool pressed)
{
    // Not used in MiniLoader
}

void ctrlDpadRight(bool pressed)
{
    // Not used in MiniLoader
}

// ── Button Callbacks ────────────────────────────────────────
void ctrlBtnCross(bool pressed)
{
    if (!attachmentOn)
    {
        if (pressed)
        {
            digitalWrite(attachmentMotor0, HIGH);
            digitalWrite(attachmentMotor1, LOW);
        }
        else
        {
            digitalWrite(attachmentMotor0, LOW);
            digitalWrite(attachmentMotor1, LOW);
        }
    }
}

void ctrlBtnCircle(bool pressed)
{
    if (!attachmentOn)
    {
        if (pressed)
        {
            digitalWrite(attachmentMotor0, LOW);
            digitalWrite(attachmentMotor1, HIGH);
        }
        else
        {
            digitalWrite(attachmentMotor0, LOW);
            digitalWrite(attachmentMotor1, LOW);
        }
    }
}

void ctrlBtnSquare(bool pressed)
{
    handleCombo(pressed, bBtn, true, false);
}

void ctrlBtnTriangle(bool pressed)
{
    handleCombo(pressed, xBtn, false, true);
}

void ctrlBtnL1(bool pressed)
{
    if (pressed)
    {
        digitalWrite(leftBladeTilt0, HIGH);
        digitalWrite(leftBladeTilt1, LOW);
    }
    else
    {
        digitalWrite(leftBladeTilt0, LOW);
        digitalWrite(leftBladeTilt1, LOW);
    }
}

void ctrlBtnL2(bool pressed)
{
    if (pressed)
    {
        digitalWrite(leftBladeTilt0, LOW);
        digitalWrite(leftBladeTilt1, HIGH);
    }
    else
    {
        digitalWrite(leftBladeTilt0, LOW);
        digitalWrite(leftBladeTilt1, LOW);
    }
}

void ctrlBtnR1(bool pressed)
{
    if (pressed)
    {
        digitalWrite(rightBladeTilt0, HIGH);
        digitalWrite(rightBladeTilt1, LOW);
    }
    else
    {
        digitalWrite(rightBladeTilt0, LOW);
        digitalWrite(rightBladeTilt1, LOW);
    }
}

void ctrlBtnR2(bool pressed)
{
    if (pressed)
    {
        digitalWrite(rightBladeTilt0, LOW);
        digitalWrite(rightBladeTilt1, HIGH);
    }
    else
    {
        digitalWrite(rightBladeTilt0, LOW);
        digitalWrite(rightBladeTilt1, LOW);
    }
}

void ctrlBtnLeftStick(bool pressed)
{
    if (pressed)
    {
        if (!incrementalSteeringMode)
        {
            incrementalSteeringMode = true;
        }
        else
        {
            incrementalSteeringMode = false;
        }
    }
}

void updateLigths()
{
    if (blinkLT && (millis() - lightSwitchTime) > 300)
    {
        if (!lightsOn)
        {
            if (adjustedSteeringValue <= 70)
            {
                digitalWrite(LT1, HIGH);
            }
            else if (adjustedSteeringValue >= 110)
            {
                digitalWrite(LT2, HIGH);
            }
            lightsOn = true;
        }
        else
        {
            if (adjustedSteeringValue <= 70)
            {
                digitalWrite(LT2, HIGH);
                digitalWrite(LT1, LOW);
            }
            else if (adjustedSteeringValue >= 110)
            {
                digitalWrite(LT1, HIGH);
                digitalWrite(LT2, LOW);
            }
            lightsOn = false;
        }
        lightSwitchTime = millis();
    }

    if (blinkLT && adjustedSteeringValue > 70 && adjustedSteeringValue < 110)
    {
        digitalWrite(LT1, HIGH);
        digitalWrite(LT2, HIGH);
    }

    if (hazardLT && (millis() - lightSwitchTime) > 300)
    {
        if (!hazardsOn)
        {
            digitalWrite(LT1, HIGH);
            digitalWrite(LT2, HIGH);
            hazardsOn = true;
        }
        else
        {
            digitalWrite(LT1, LOW);
            digitalWrite(LT2, LOW);
            hazardsOn = false;
        }
        lightSwitchTime = millis();
    }
}

void ctrlBtnRightStick(bool pressed)
{
    if (!pressed)
        return;

    lightMode++;
    if (lightMode == 1)
    {
        digitalWrite(LT1, HIGH);
        digitalWrite(LT2, HIGH);
    }
    else if (lightMode == 2)
    {
        digitalWrite(LT1, LOW);
        digitalWrite(LT2, LOW);
        delay(100);
        digitalWrite(LT1, HIGH);
        digitalWrite(LT2, HIGH);
        blinkLT = true;
    }
    else if (lightMode == 3)
    {
        blinkLT = false;
        hazardLT = true;
    }
    else if (lightMode == 4)
    {
        hazardLT = false;
        digitalWrite(LT1, LOW);
        digitalWrite(LT2, LOW);
        lightMode = 0;
        if (!auxLightsOn)
        {
            digitalWrite(LT3, HIGH);
            auxLightsOn = true;
        }
        else
        {
            digitalWrite(LT3, LOW);
            auxLightsOn = false;
        }
    }
}

void ctrlBtnSelect(bool pressed)
{
    // Not used in MiniLoader
}

void ctrlBtnStart(bool pressed)
{
    // Not used in MiniLoader
}

void ctrlBtnPs(bool pressed)
{
    // Not used in MiniLoader
}

// ── Device Status Callbacks ─────────────────────────────────
void devGoingActive()
{
    Serial.println("Device going active");
    lightMode = 3;
    hazardLT = true;
}

void devGoingInactive()
{
    Serial.println("Device going inactive");
    steeringServo.write(90 + steeringTrim);
    stopAll();
}

void registrationStart()
{
    Serial.println("Registration started");
}

void registrationDone()
{
    Serial.println("Registration done");
}

// ── Setup ───────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(500);

    WiFi.mode(WIFI_STA);
    Serial.printf("Slave MAC: %s\n", WiFi.macAddress().c_str());

    setUpPinModes();

    // Register device status callbacks
    controller.onSwitchToActive(devGoingActive);
    controller.onSwitchToInactive(devGoingInactive);
    controller.onRegistrationStart(registrationStart);
    controller.onRegistrationDone(registrationDone);

    // Register update callback
    controller.onUpdate(ctrlUpdate);

    // Register D-Pad callbacks
    controller.onBtnDpadLeftEvent(ctrlDpadLeft);
    controller.onBtnDpadUpEvent(ctrlDpadUp);
    controller.onBtnDpadDownEvent(ctrlDpadDown);
    controller.onBtnDpadRightEvent(ctrlDpadRight);

    // Register button callbacks
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

    // Initialize the controller
    controller.begin();
}

// ── Main Loop ───────────────────────────────────────────────
void loop()
{
    controller.update(); // Non-blocking: handles registration retry, processes incoming data

    updateLigths();
    checkAndResetServo();
}
