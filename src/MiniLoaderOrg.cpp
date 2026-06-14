#include <Arduino.h>
#include <ESP32Servo.h>  // by Kevin Harrington
#include <Bluepad32.h>

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

#define LT1 15
#define LT2 27
#define LT3 14

#define steeringServoPin 23
#define attachmentServoPin 22

Servo steeringServo;
Servo attachmentServo;

#define leftMotor0 4    // \ Used for controlling front drive motor movement
#define leftMotor1 2    // /
#define rightMotor0 12  // \ Used for controlling rear drive motor movement.
#define rightMotor1 13  // /

#define leftBladeTilt0 17   
#define leftBladeTilt1 16   
#define rightBladeTilt0 18  
#define rightBladeTilt1 19    
#define attachmentMotor0 26  
#define attachmentMotor1 25  
#define ripperMotor0 32      
#define ripperMotor1 33      

unsigned long lastInputTime = 0;
const unsigned long INPUT_TIMEOUT = 40;  // ms — adjust if needed

int buttonSwitchTime = 0;
int lightSwitchTime = 0;
int lightSwitchButtonTime = 0;
int lightMode = 0;
bool lightsOn = false;
bool auxLightsOn = false;
bool blinkLT = false;
bool hazardLT = false;
bool hazardsOn = false;
bool attachmentOn = false;
int adjustedSteeringValue = 90;
int steeringTrim = 0;
unsigned long lastSteeringServoTime = 0;
bool incrementalSteeringMode = false;


unsigned long servoTimer = 0;
bool servoActive = false;
// Triple-tap tracking
int tapCount = 0;
unsigned long firstTapTime = 0;
const unsigned long tapWindow = 800;  // time window to count taps (ms)
int lastDpadValue = 0;

struct ComboButton {
  int count;
  unsigned long firstPressTime;
  bool lastState;
};

ComboButton bBtn = { 0, 0, false };
ComboButton xBtn = { 0, 0, false };

const unsigned long comboWindow = 600;  // ms

void onConnectedController(ControllerPtr ctl) {
  bool foundEmptySlot = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
      // Additionally, you can get certain gamepad properties like:
      // Model, VID, PID, BTAddr, flags, etc.
      ControllerProperties properties = ctl->getProperties();
      Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                    properties.product_id);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }
  if (!foundEmptySlot) {
    Serial.println("CALLBACK: Controller connected, but could not found empty slot");
  }
}

void onDisconnectedController(ControllerPtr ctl) {
  bool foundController = false;

  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
      myControllers[i] = nullptr;
      foundController = true;
      break;
    }
  }

  if (!foundController) {
    Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
  }
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

void handleCombo(bool currentState, ComboButton &btn, bool dirA, bool dirB) {
  if (currentState && !btn.lastState) {
    unsigned long now = millis();

    // If attachment is ON → single press turns it OFF
    if (attachmentOn) {
      digitalWrite(attachmentMotor0, LOW);
      digitalWrite(attachmentMotor1, LOW);
      attachmentOn = false;

      btn.count = 0;
      btn.lastState = currentState;
      return;
    }

    // Attachment is OFF → count combo presses
    if (btn.count == 0) {
      btn.firstPressTime = now;
    }

    btn.count++;

    if ((now - btn.firstPressTime) > comboWindow) {
      btn.count = 1;
      btn.firstPressTime = now;
    }

    // 3 presses → turn ON
    if (btn.count == 3) {
      digitalWrite(attachmentMotor0, dirA ? HIGH : LOW);
      digitalWrite(attachmentMotor1, dirB ? HIGH : LOW);
      attachmentOn = true;
      btn.count = 0;
    }
  }

  btn.lastState = currentState;
}
void processThrottle(int axisYValue) {
  int adjustedThrottleValue = axisYValue / 2;
  moveMotor(leftMotor0, leftMotor1, adjustedThrottleValue);
  moveMotor(rightMotor0, rightMotor1, adjustedThrottleValue);
}
void processSteering(int axisRXValue) {
  if (incrementalSteeringMode) {
    if (millis() - lastSteeringServoTime >= 20) {
      adjustedSteeringValue = adjustedSteeringValue + axisRXValue / 160;
      if (adjustedSteeringValue > 145) {
        adjustedSteeringValue = 144;
      }
      if (adjustedSteeringValue < 45) {
        adjustedSteeringValue = 46;
      }
      steeringServo.write(adjustedSteeringValue);
      lastSteeringServoTime = millis();
    }
  } else {
    // Serial.println(axisRXValue);
    adjustedSteeringValue = 180 - ((90 - (axisRXValue / 12)) - steeringTrim);
    steeringServo.write(adjustedSteeringValue);

    Serial.print("Steering Value:");
    Serial.println(adjustedSteeringValue);
  }
}
void processSteeringMode(bool buttonValue) {
  if (buttonValue && (millis() - buttonSwitchTime) > 300) {
    if (!incrementalSteeringMode) {
      incrementalSteeringMode = true;
    } else {
      incrementalSteeringMode = false;
    }
    buttonSwitchTime = millis();
  }
}
void processLights(bool buttonValue) {
  if (buttonValue && (millis() - lightSwitchButtonTime) > 300) {
    lightMode++;
    if (lightMode == 1) {
      digitalWrite(LT1, HIGH);
      digitalWrite(LT2, HIGH);
      Serial.println(12);
      delay(10);
      Serial.println(14);
    } else if (lightMode == 2) {
      digitalWrite(LT1, LOW);
      digitalWrite(LT2, LOW);
      delay(100);
      digitalWrite(LT1, HIGH);
      digitalWrite(LT2, HIGH);
      blinkLT = true;
    } else if (lightMode == 3) {
      blinkLT = false;
      hazardLT = true;
    } else if (lightMode == 4) {
      hazardLT = false;
      digitalWrite(LT1, LOW);
      digitalWrite(LT2, LOW);
      Serial.println(11);
      delay(10);
      Serial.println(13);
      lightMode = 0;
      if (!auxLightsOn) {
        digitalWrite(LT3, HIGH);
        Serial.println(16);
        auxLightsOn = true;
      } else {
        digitalWrite(LT3, LOW);
        Serial.println(15);
        auxLightsOn = false;
      }
    }
    lightSwitchButtonTime = millis();
  }
}


void processGamepad(ControllerPtr ctl) {

  // Record that we received fresh input
  lastInputTime = millis();

  //Throttle
  processThrottle(ctl->axisY());

  //Steering
  processSteering(ctl->axisRX());

  processSteeringMode(ctl->thumbL());
  //Lights
  processLights(ctl->thumbR());

  if (ctl->r1() == 1) {
    digitalWrite(rightBladeTilt0, HIGH);
    digitalWrite(rightBladeTilt1, LOW);
  } else if (ctl->r2() == 1) {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, HIGH);
  } else if (ctl->r1() == 0 || ctl->r2() == 0) {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, LOW);
  }
  if (ctl->l1() == 1) {
    digitalWrite(leftBladeTilt0, HIGH);
    digitalWrite(leftBladeTilt1, LOW);
  } else if (ctl->l2() == 1) {
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, HIGH);
  } else if (ctl->l1() == 0 || ctl->r2() == 0) {
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, LOW);
  }
  if (!attachmentOn) {
    if (ctl->a() == 1) {
      digitalWrite(attachmentMotor0, HIGH);
      digitalWrite(attachmentMotor1, LOW);
    } else if (ctl->y() == 1) {
      digitalWrite(attachmentMotor0, LOW);
      digitalWrite(attachmentMotor1, HIGH);
    } else if (ctl->a() == 0 && ctl->y() == 0) {
      digitalWrite(attachmentMotor0, LOW);
      digitalWrite(attachmentMotor1, LOW);
    }
  }
  if (blinkLT && (millis() - lightSwitchTime) > 300) {
    if (!lightsOn) {
      if (adjustedSteeringValue <= 70) {
        digitalWrite(LT1, HIGH);
        Serial.println(12);
      } else if (adjustedSteeringValue >= 110) {
        digitalWrite(LT2, HIGH);
        Serial.println(14);
      }
      lightsOn = true;
    } else {
      if (adjustedSteeringValue <= 70) {
        digitalWrite(LT2, HIGH);
        digitalWrite(LT1, LOW);
        Serial.println(11);
        delay(10);
        Serial.println(14);
      } else if (adjustedSteeringValue >= 110) {
        digitalWrite(LT1, HIGH);
        digitalWrite(LT2, LOW);
        Serial.println(13);
        delay(10);
        Serial.println(12);
      }
      lightsOn = false;
    }
    lightSwitchTime = millis();
  }
  if (blinkLT && adjustedSteeringValue > 70 && adjustedSteeringValue < 110) {
    digitalWrite(LT1, HIGH);
    digitalWrite(LT2, HIGH);
    Serial.println(12);
    delay(10);
    Serial.println(14);
  }
  if (hazardLT && (millis() - lightSwitchTime) > 300) {
    if (!hazardsOn) {
      digitalWrite(LT1, HIGH);
      digitalWrite(LT2, HIGH);
      Serial.println(12);
      delay(10);
      Serial.println(14);
      hazardsOn = true;
    } else {
      digitalWrite(LT1, LOW);
      digitalWrite(LT2, LOW);
      Serial.println(11);
      delay(10);
      Serial.println(13);
      hazardsOn = false;
    }
    lightSwitchTime = millis();
  }

  handleCombo(ctl->b() == 1, bBtn, true, false);  // B: motor0 HIGH
  handleCombo(ctl->x() == 1, xBtn, false, true);  // X: motor1 HIGH

  int dpadValue = ctl->dpad();  // get current D-pad value
  int targetPosition = -1;

  // Detect edge (new press)
  if (dpadValue != lastDpadValue && dpadValue != 0) {
    // Check if tap window expired
    if (millis() - firstTapTime > tapWindow) {
      tapCount = 0;  // reset taps
      firstTapTime = millis();
    }

    tapCount++;                                  // increment tap count
    if (tapCount == 1) firstTapTime = millis();  // start timer on first tap

    // Triple-tap detected
    if (tapCount >= 3) {
      if (dpadValue == 2) targetPosition = 115;
      else if (dpadValue == 1) targetPosition = 10;

      if (targetPosition != -1) {
        attachmentServo.attach(attachmentServoPin);
        attachmentServo.write(targetPosition);
        servoTimer = millis();
        servoActive = true;
      }

      // Reset tap count after activation
      tapCount = 0;
      firstTapTime = 0;
    }
  }

  // Turn off servo after 2 seconds
  if (servoActive && millis() - servoTimer >= 2000) {
    attachmentServo.detach();
    servoActive = false;
  }

  // Save last D-pad value
  lastDpadValue = dpadValue;
}

void processControllers() {
  for (auto myController : myControllers) {
    if (myController && myController->isConnected() && myController->hasData()) {
      if (myController->isGamepad()) {
        processGamepad(myController);
      } else {
        Serial.println("Unsupported controller");
      }
    }
  }
}

// Arduino setup function. Runs in CPU 1
void setup() {
  pinMode(rightBladeTilt0, OUTPUT);
  pinMode(rightBladeTilt1, OUTPUT);
  digitalWrite(rightBladeTilt0, LOW);
  digitalWrite(rightBladeTilt1, LOW);
  Serial.begin(115200);
  //   put your setup code here, to run once:
  Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t *addr = BP32.localBdAddress();
  Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

  // Setup the Bluepad32 callbacks
  BP32.setup(&onConnectedController, &onDisconnectedController);

  BP32.forgetBluetoothKeys();

  BP32.enableVirtualDevice(false);

  pinMode(leftMotor0, OUTPUT);
  pinMode(leftMotor1, OUTPUT);
  pinMode(rightMotor0, OUTPUT);
  pinMode(rightMotor1, OUTPUT);
  pinMode(ripperMotor0, OUTPUT);
  pinMode(ripperMotor1, OUTPUT);
  pinMode(leftBladeTilt0, OUTPUT);
  pinMode(leftBladeTilt1, OUTPUT);
  pinMode(attachmentMotor0, OUTPUT);
  pinMode(attachmentMotor1, OUTPUT);
  pinMode(LT1, OUTPUT);
  pinMode(LT2, OUTPUT);
  pinMode(LT3, OUTPUT);

  digitalWrite(leftMotor0, LOW);
  digitalWrite(leftMotor1, LOW);
  digitalWrite(rightMotor0, LOW);
  digitalWrite(rightMotor1, LOW);
  digitalWrite(ripperMotor0, LOW);
  digitalWrite(ripperMotor1, LOW);
  digitalWrite(leftBladeTilt0, LOW);
  digitalWrite(leftBladeTilt1, LOW);
  digitalWrite(attachmentMotor0, LOW);
  digitalWrite(attachmentMotor1, LOW);
  digitalWrite(LT1, LOW);
  digitalWrite(LT2, LOW);
  digitalWrite(LT3, LOW);

  steeringServo.attach(steeringServoPin);
  steeringServo.write(adjustedSteeringValue);

  attachmentServo.attach(attachmentServoPin);
  attachmentServo.write(10);


  lastInputTime = millis();  // initialize failsafe timer
}



// Arduino loop function. Runs in CPU 1.
void loop() {
  // This call fetches all the controllers' data.
  // Call this function in your main loop.
  bool dataUpdated = BP32.update();
  if (dataUpdated) {
    processControllers();
  }
  // The main loop must have some kind of "yield to lower priority task" event.
  // Otherwise, the watchdog will get triggered.
  // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
  // Detailed info here:
  // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

  //     vTaskDelay(1);
  else { vTaskDelay(1); }
  // Failsafe check: if no input for too long, stop motors
  if (millis() - lastInputTime > INPUT_TIMEOUT) {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, LOW);
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, LOW);
    digitalWrite(leftMotor0, LOW);
    digitalWrite(leftMotor1, LOW);
    digitalWrite(rightMotor0, LOW);
    digitalWrite(rightMotor1, LOW);
    digitalWrite(ripperMotor0, LOW);
    digitalWrite(ripperMotor1, LOW);
    if (!attachmentOn) {
      digitalWrite(attachmentMotor0, LOW);
      digitalWrite(attachmentMotor1, LOW);
    }
  }
}
