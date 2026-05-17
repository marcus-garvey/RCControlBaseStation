#include <Bluepad32.h>
#include <Base64.h>
#include "TransmitData.h"

transmit controller_state;
GamepadPtr myGamepad = nullptr;

void updateState(transmit& state, GamepadPtr gp)
{
    // Face buttons
    state.part.btn_circle   = (gp->buttons() & BUTTON_B) != 0;        // Circle → B
    state.part.btn_cross    = (gp->buttons() & BUTTON_A) != 0;        // Cross  → A
    state.part.btn_square   = (gp->buttons() & BUTTON_X) != 0;        // Square → X
    state.part.btn_triangle = (gp->buttons() & BUTTON_Y) != 0;        // Triangle → Y

    // D-Pad
    state.part.btn_dpad_down  = (gp->dpad() & DPAD_DOWN)  != 0;
    state.part.btn_dpad_up    = (gp->dpad() & DPAD_UP)    != 0;
    state.part.btn_dpad_left  = (gp->dpad() & DPAD_LEFT)  != 0;
    state.part.btn_dpad_right = (gp->dpad() & DPAD_RIGHT) != 0;

    // System buttons
    state.part.btn_select = (gp->miscButtons() & MISC_BUTTON_SELECT) != 0;
    state.part.btn_start  = (gp->miscButtons() & MISC_BUTTON_START)  != 0;
    state.part.btn_ps     = (gp->miscButtons() & MISC_BUTTON_HOME)   != 0;

    // Stick clicks
    state.part.btn_left_stick  = (gp->buttons() & BUTTON_THUMB_L) != 0;
    state.part.btn_right_stick = (gp->buttons() & BUTTON_THUMB_R) != 0;

    state.part.btn_reserved1 = false;
    state.part.btn_reserved2 = false;
    state.part.btn_reserved3 = false;

    // Analog sticks — Bluepad32 range: -512..511, map to -128..127 (int8)
    state.part.analog_lx = (int8_t)(gp->axisX()  >> 2);
    state.part.analog_ly = (int8_t)(gp->axisY()  >> 2);
    state.part.analog_rx = (int8_t)(gp->axisRX() >> 2);
    state.part.analog_ry = (int8_t)(gp->axisRY() >> 2);

    // Triggers — Bluepad32 range: 0..1023, map to 0..255 (uint8)
    state.part.analog_l1 = (gp->buttons() & BUTTON_SHOULDER_L) ? 255 : 0;
    state.part.analog_l2 = (uint8_t)(gp->brake()    >> 2);
    state.part.analog_r1 = (gp->buttons() & BUTTON_SHOULDER_R) ? 255 : 0;
    state.part.analog_r2 = (uint8_t)(gp->throttle() >> 2);
}


// --- Bluepad32 callbacks ---
void onConnectedGamepad(GamepadPtr gp)
{
    myGamepad = gp;
    Serial.println("Controller Connected.");

    // Equivalent to Ps3.setPlayer(0) — set LED player index
    GamepadProperties props = gp->getProperties();
    Serial.printf("Model: %s, VID=0x%04x, PID=0x%04x\n", gp->getModelName().c_str(), props.vendor_id, props.product_id);
    gp->setPlayerLEDs(1);  // Player 1 LED
}

void onDisconnectedGamepad(GamepadPtr gp)
{
    Serial.println("Controller Disconnected.");
    myGamepad = nullptr;
}

void setup() {
    Serial.begin(115200);

    BP32.setup(&onConnectedGamepad, &onDisconnectedGamepad);

}


void loop() {
    // Must be called every loop — updates gamepad state & fires callbacks
    bool updated = BP32.update();

    if (myGamepad == nullptr || !myGamepad->isConnected())
        return;

    // Send state whenever Bluepad32 reports new data
    if (updated) {
        updateState(controller_state, myGamepad);
    }

}

