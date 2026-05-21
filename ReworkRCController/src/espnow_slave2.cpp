// ============================================================
//  ESP-NOW Slave v2 — Using EspNowRCReceiver for simplified integration
//
//  EspNowRCReceiver handles all ESP-NOW registration & message parsing.
//  This sketch focuses only on device-specific logic:
//    - LED control (pins 22, 23)
//    - Servo control (pin 16, mapped from LX axis)
//    - Button/Stick callbacks for local actions
//
//  Set the friendly name here:
#define DEVICE_NAME    "Test Slave"
#define DEVICE_NODE_ID  2           // 0 = no ID
// ============================================================

#include <ESP32Servo.h>
#include "EspNowRCReceiver.h"

// ── Hardware Pins ────────────────────────────────────────────
static constexpr uint8_t LED_CIRCLE_PIN   = 22;
static constexpr uint8_t LED_TRIANGLE_PIN = 23;
static constexpr uint8_t SERVO_PIN        = 16;

// ── Global objects ───────────────────────────────────────────
EspNowRCReceiver gamepad(DEVICE_NODE_ID, DEVICE_NAME);
Servo servo;

// ── FPS counter (debug) ──────────────────────────────────────
static uint32_t fpsCounter = 0;
static uint32_t fpsLastTime = 0;

// ============================================================
// Callback handlers for EspNowRCReceiver
// ============================================================

// Circle button pressed
void onCirclePressed(bool pressed) {
  digitalWrite(LED_CIRCLE_PIN, pressed ? HIGH : LOW);
  Serial.printf("Circle: %s\n", pressed ? "PRESSED" : "released");
}

// Triangle button pressed
void onTrianglePressed(bool pressed) {
  digitalWrite(LED_TRIANGLE_PIN, pressed ? HIGH : LOW);
  Serial.printf("Triangle: %s\n", pressed ? "PRESSED" : "released");
}

// Left stick X-axis: map to servo angle (0..180)
void onLeftStickX(int state) {
  // state: -1 (left), 0 (center), 1 (right) based on thresholds
  // For more precision, use getLeftStickX() directly
  int8_t rawLX = gamepad.getLeftStickX();  // -128..127
  int angle = ((rawLX + 128) * 180) / 255;
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  //servo.write(angle);
  Serial.printf("LX=%d -> servo angle=%d\n", rawLX, angle);
}

// Debug: print gamepad state every 2s
void onGamepadUpdate() {
  fpsCounter++;
  if ((millis() - fpsLastTime) > 2000) {
    fpsLastTime = millis();
    Serial.printf(
        "[%lu FPS] "
        "LX=%4d LY=%4d RX=%4d RY=%4d | "
        "L2=%3d R2=%3d | "
        "Registered=%s\n",
        fpsCounter,
        gamepad.getLeftStickX(),     gamepad.getLeftStickY(),
        gamepad.getRightStickX(),    gamepad.getRightStickY(),
        gamepad.getL2(),             gamepad.getR2(),
        gamepad.isRegistered() ? "YES" : "NO"
    );
    fpsCounter = 0;
  }

  int8_t rawLX = gamepad.getLeftStickX();  // -128..127
  int angle = ((rawLX + 128) * 180) / 255;
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  servo.write(angle);
}

// Called when registration starts
void onRegistrationStart() {
  Serial.println("=== EspNowRCReceiver: Registration started ===");
}

// Called when registration complete
void onRegistrationDone() {
  Serial.println("=== EspNowRCReceiver: Registration complete ===");
  Serial.println("Waiting for gamepad data...");
}

// ============================================================
// Setup & Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("ESP-NOW Slave v2 (EspNowRCReceiver)");
  Serial.println("========================================");

  // WiFi setup
  WiFi.mode(WIFI_STA);
  Serial.printf("Slave MAC: %s\n", WiFi.macAddress().c_str());

  // LED pins
  pinMode(LED_CIRCLE_PIN, OUTPUT);
  pinMode(LED_TRIANGLE_PIN, OUTPUT);
  digitalWrite(LED_CIRCLE_PIN, LOW);
  digitalWrite(LED_TRIANGLE_PIN, LOW);

  // Servo setup
  servo.attach(SERVO_PIN);
  servo.write(90);  // Center position

  // ── EspNowRCReceiver initialization ──────────────────────────────
  Serial.println("\nInitializing EspNowRCReceiver...");

  // Register lifecycle callbacks
  gamepad.onRegistrationStart(onRegistrationStart);
  gamepad.onRegistrationDone(onRegistrationDone);

  // Register button callbacks
  gamepad.onBtnCircleEvent(onCirclePressed);
  gamepad.onBtnTriangleEvent(onTrianglePressed);

  // Register analog stick callback with thresholds
  // (high: ±100, low: ±50)
  gamepad.onStickLXEvent(onLeftStickX, 100, 50);

  // Register gamepad update callback (for FPS counter & debug)
  gamepad.onUpdate(onGamepadUpdate);

  // Start EspNowRCReceiver — broadcasts MSG_REGISTER, waits for MSG_ACK
  gamepad.begin();

  Serial.println("Setup complete. Waiting for master to accept registration...\n");
}

void loop() {
  // ── Main loop: EspNowRCReceiver handles all ESP-NOW communication ──
  // This is the core difference from espnow_slave.cpp:
  // We don't manually send MSG_REGISTER, handle callbacks, etc.
  // EspNowRCReceiver does all of that internally.

  gamepad.update();  // Non-blocking: handles registration retry, processes incoming data

  
}
