#pragma once

#include <Arduino.h>

struct DebouncedButton {
    uint8_t  pin;
    bool     stableState = HIGH;
    bool     lastRaw     = HIGH;
    uint32_t lastChange  = 0;
    uint32_t debounceMs;

    DebouncedButton(uint8_t pin_, uint32_t debounceMs_ = 50)
      : pin(pin_), debounceMs(debounceMs_) {}

    void begin() const {
        pinMode(pin, INPUT_PULLUP);
    }

    bool update() {
        bool reading = digitalRead(pin);
        if (reading != lastRaw) {
            lastRaw = reading;
            lastChange = millis();
        }
        if ((millis() - lastChange) > debounceMs && stableState != reading) {
            stableState = reading;
            return true;
        }
        return false;
    }

    bool pressed() const {
        return stableState == LOW;
    }
};
