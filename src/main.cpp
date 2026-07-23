#include <Arduino.h>
#include <M5Dial.h>

#include "../include/AppConfig.h"
#include "../include/BuildInfo.h"

// Application state
namespace State {
uint8_t brightness = Config::BRIGHTNESS_INITIAL;
unsigned long last_feedback_time = 0;
bool showing_feedback = false;
const char* feedback_text = nullptr;
long encoder_last_position = 0;
}  // namespace State

// Forward declarations
void setup_serial();
void setup_m5_dial();
void update_display();
void handle_encoder();
void handle_button();
void handle_touch();
void show_temporary_feedback(const char* text);

void setup() {
  setup_serial();

  Serial.println("\n=== M5 Dial Lighting Controller ===");
  Serial.print("Firmware: ");
  Serial.println(BuildInfo::FIRMWARE_NAME);
  Serial.print("Version: ");
  Serial.println(BuildInfo::FIRMWARE_VERSION);
  Serial.print("Build Date: ");
  Serial.println(BuildInfo::BUILD_DATE_STR);

  BuildInfo::HardwareInfo hw;
  Serial.print("Chip Model: ");
  Serial.println(hw.chip_model);
  Serial.print("Chip Revision: ");
  Serial.println(hw.chip_revision);
  Serial.print("Flash Size: ");
  Serial.print(hw.flash_size_bytes / 1024 / 1024);
  Serial.println(" MB");
  Serial.print("PSRAM: ");
  if (hw.psram_size_bytes > 0) {
    Serial.print(hw.psram_size_bytes / 1024 / 1024);
    Serial.println(" MB");
  } else {
    Serial.println("None");
  }
  Serial.print("Free Heap: ");
  Serial.print(hw.free_heap_bytes / 1024);
  Serial.println(" KB");
  Serial.println();

  setup_m5_dial();

  Serial.println("Hardware initialized. Starting main loop.");
  Serial.println();
}

void loop() {
  M5Dial.update();

  handle_encoder();
  handle_button();
  handle_touch();

  // Update display if needed
  update_display();

  // Clear temporary feedback after duration
  if (State::showing_feedback) {
    if (millis() - State::last_feedback_time > Config::FEEDBACK_DURATION_MS) {
      State::showing_feedback = false;
      update_display();
    }
  }

  delay(10);
}

void setup_serial() {
  Serial.begin(Config::SERIAL_BAUD);
  delay(100);
}

void setup_m5_dial() {
  auto cfg = M5.config();

  // Initialize M5Dial with encoder enabled
  M5Dial.begin(cfg, true, false);

  // Initialize display
  M5Dial.Display.setTextSize(3);
  M5Dial.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Dial.Display.fillScreen(TFT_BLACK);

  Serial.println("M5 Dial hardware initialized");
  Serial.println("  - Display: 240x240 round LCD");
  Serial.println("  - Encoder: Rotary input for brightness");
  Serial.println("  - Button: Center dial button");
  Serial.println("  - Touch: Capacitive touchscreen");
}

void update_display() {
  auto& disp = M5Dial.Display;

  // Clear screen
  disp.fillScreen(TFT_BLACK);

  // Title
  disp.setTextSize(2);
  disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  disp.setCursor(45, 20);
  disp.printf("M5 DIAL");

  // Main content
  if (State::showing_feedback) {
    // Show feedback text
    disp.setTextSize(2);
    disp.setTextColor(TFT_YELLOW, TFT_BLACK);
    int x = (240 - (strlen(State::feedback_text) * 12)) / 2;
    disp.setCursor(x, 100);
    disp.printf(State::feedback_text);
  } else {
    // Normal brightness display
    disp.setTextSize(1);
    disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    disp.setCursor(65, 65);
    disp.printf("Brightness");

    // Large brightness number
    disp.setTextSize(7);
    disp.setTextColor(TFT_WHITE, TFT_BLACK);
    disp.setCursor(30, 115);
    disp.printf("%3d%%", State::brightness);

    // Simple brightness arc (visual feedback)
    uint16_t radius = 90;
    uint16_t cx = 120;
    uint16_t cy = 140;

    // Background arc (light gray)
    for (int angle = 135; angle <= 405; angle += 5) {
      float rad = angle * PI / 180.0f;
      int x1 = cx + radius * cos(rad);
      int y1 = cy + radius * sin(rad);
      int x2 = cx + (radius - 8) * cos(rad);
      int y2 = cy + (radius - 8) * sin(rad);
      disp.drawLine(x1, y1, x2, y2, TFT_DARKGREY);
    }

    // Brightness arc (orange)
    int arc_angle = 135 + (270 * State::brightness / 100);
    for (int angle = 135; angle <= arc_angle; angle += 5) {
      float rad = angle * PI / 180.0f;
      int x1 = cx + radius * cos(rad);
      int y1 = cy + radius * sin(rad);
      int x2 = cx + (radius - 8) * cos(rad);
      int y2 = cy + (radius - 8) * sin(rad);
      disp.drawLine(x1, y1, x2, y2, TFT_ORANGE);
    }
  }
}

void handle_encoder() {
  // Read current encoder position
  long current_position = M5Dial.Encoder.read();

  if (current_position != State::encoder_last_position) {
    // Calculate change from last position
    long delta = current_position - State::encoder_last_position;

    // Adjust brightness based on encoder movement
    // Each encoder step = 1%
    int new_brightness = State::brightness + delta;
    new_brightness = constrain(new_brightness, Config::BRIGHTNESS_MIN,
                               Config::BRIGHTNESS_MAX);

    if (new_brightness != State::brightness) {
      State::brightness = new_brightness;
      Serial.printf("[Encoder] Brightness: %d%% (delta: %ld)\n",
                    State::brightness, delta);
      update_display();
    }

    State::encoder_last_position = current_position;
  }
}

void handle_button() {
  if (M5Dial.BtnA.wasPressed()) {
    Serial.printf("[Button] Pressed at %lu ms\n", millis());
    show_temporary_feedback("Pressed");
  }
}

void handle_touch() {
  auto touch_detail = M5Dial.Touch.getDetail();

  // Check for touch event (state values: 0=none, 1=touch, 2=touch_end, 3=touch_begin, etc.)
  if (touch_detail.state == 1 || touch_detail.state == 3) {  // touch or touch_begin
    Serial.printf("[Touch] x=%d, y=%d, state=%d at %lu ms\n", touch_detail.x,
                  touch_detail.y, touch_detail.state, millis());
    show_temporary_feedback("Touch");
  }
}

void show_temporary_feedback(const char* text) {
  State::feedback_text = text;
  State::showing_feedback = true;
  State::last_feedback_time = millis();
  update_display();
}
