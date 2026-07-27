#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_TFT_CS = 5;
constexpr uint8_t PIN_SD_CS = 22;
constexpr uint8_t PIN_BTN_UP = 2;
constexpr uint8_t PIN_BTN_DOWN = 13;
constexpr uint8_t PIN_BTN_LEFT = 27;
constexpr uint8_t PIN_BTN_RIGHT = 35;
constexpr uint8_t PIN_BTN_A = 34;
constexpr uint8_t PIN_BTN_B = 12;

struct Buttons {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
};

inline void initHardwarePins() {
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
}

inline Buttons readButtons() {
    return {
        digitalRead(PIN_BTN_UP) == LOW,
        digitalRead(PIN_BTN_DOWN) == LOW,
        digitalRead(PIN_BTN_LEFT) == LOW,
        digitalRead(PIN_BTN_RIGHT) == LOW,
        digitalRead(PIN_BTN_A) == LOW,
        digitalRead(PIN_BTN_B) == LOW,
    };
}

