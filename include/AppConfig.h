#pragma once

namespace Config {

// Device Identity
constexpr const char* DEVICE_NAME = "M5 Dial";
constexpr const char* ROOM_NAME = "Living Room";

// Serial Configuration
constexpr unsigned long SERIAL_BAUD = 115200;

// Brightness Control
constexpr uint8_t BRIGHTNESS_MIN = 1;
constexpr uint8_t BRIGHTNESS_MAX = 100;
constexpr uint8_t BRIGHTNESS_INITIAL = 50;

// Timing (milliseconds)
constexpr unsigned long FEEDBACK_DURATION_MS = 500;
constexpr unsigned long TOUCH_FEEDBACK_MS = 500;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 20;

// Encoder Configuration
constexpr int8_t ENCODER_INCREMENT = 1;  // percent per step, 1-percent increments for v1

}  // namespace Config
