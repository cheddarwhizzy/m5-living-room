/**
 * M5 Dial Lighting Controller - Phase 2: Polished UI
 *
 * Features:
 * - Brightness control via rotary encoder (1-100%)
 * - 6 scene selection (Relax, Movie, Dinner, Reading, Party, Night)
 * - Touch to enter scene mode
 * - Dial press to apply scene
 * - Smooth animations and visual feedback
 * - Responsive UI with no blocking delays
 */

#include <M5Dial.h>
#include "../include/AppConfig.h"
#include "../include/BuildInfo.h"

// UI Modes
enum UIMode {
  MODE_BRIGHTNESS,
  MODE_SCENE_SELECT,
  MODE_SCENE_APPLY
};

// Scene Definition
struct Scene {
  const char* name;
  const char* icon;
  uint32_t color;
};

// Application State
namespace State {
  UIMode mode = MODE_BRIGHTNESS;
  UIMode lastMode = MODE_BRIGHTNESS;
  uint8_t brightness = Config::BRIGHTNESS_INITIAL;
  uint8_t lastBrightness = Config::BRIGHTNESS_INITIAL;
  uint8_t sceneIndex = 0;
  uint8_t lastSceneIndex = 0;
  uint8_t sceneShown = 0;

  // Animation timing
  unsigned long modeChangeTime = 0;
  unsigned long sceneApplyTime = 0;
  bool showingFeedback = false;

  // Encoder
  long encoderPosition = 0;
  long lastDisplayedEncoder = 0;
  float encoderAccel = 1.0f;
  unsigned long lastEncoderTime = 0;

  // Dirty flag for redraw
  bool needsRedraw = true;
}

// Scene Library
const Scene scenes[] = {
  {"Relax", "weekend", 0x9b7cff},
  {"Movie", "movie", 0x4aa8ff},
  {"Dinner", "restaurant", 0xff8a3d},
  {"Reading", "menu_book", 0xffc24a},
  {"Party", "local_bar", 0xff5ecb},
  {"Night", "bedtime", 0xff6a2a}
};
const uint8_t sceneCount = 6;

// Forward declarations
void updateDisplay();
void handleEncoder();
void handleButton();
void handleTouch();
void enterSceneMode();
void applyScene();
void returnToBrightness();
void displayBrightnessMode(M5GFX& disp);
void displaySceneSelectMode(M5GFX& disp);
void displaySceneApplyMode(M5GFX& disp);
void drawBrightnessArc(M5GFX& disp, uint8_t brightness, uint16_t color);
uint16_t colorTo565(uint32_t color);

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  delay(100);

  Serial.println("\n=== M5 Dial Lighting Controller (Phase 2) ===");
  Serial.print("Firmware: ");
  Serial.println(BuildInfo::FIRMWARE_NAME);
  Serial.print("Version: ");
  Serial.println(BuildInfo::FIRMWARE_VERSION);

  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);

  M5Dial.Display.setTextSize(1);
  M5Dial.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Dial.Display.fillScreen(TFT_BLACK);

  Serial.println("Hardware initialized - Phase 2 UI ready");
  updateDisplay();
}

void loop() {
  M5Dial.update();

  handleEncoder();
  handleButton();
  handleTouch();

  // Check for mode transitions (scene apply timeout)
  if (State::mode == MODE_SCENE_APPLY && (millis() - State::sceneApplyTime > 1000)) {
    returnToBrightness();
  }

  // Only redraw if state changed
  if (State::needsRedraw || State::mode != State::lastMode ||
      State::brightness != State::lastBrightness ||
      State::sceneIndex != State::lastSceneIndex) {
    updateDisplay();
    State::lastMode = State::mode;
    State::lastBrightness = State::brightness;
    State::lastSceneIndex = State::sceneIndex;
    State::needsRedraw = false;
  }

  delay(10);
}

void handleEncoder() {
  long currentPos = M5Dial.Encoder.read();

  if (State::mode == MODE_BRIGHTNESS) {
    // Smooth brightness adjustment
    int encoderDelta = currentPos - State::encoderPosition;
    if (encoderDelta != 0) {
      State::encoderPosition = currentPos;
      int newBrightness = State::brightness + encoderDelta;
      newBrightness = constrain(newBrightness, Config::BRIGHTNESS_MIN, Config::BRIGHTNESS_MAX);

      if (newBrightness != State::brightness) {
        State::brightness = newBrightness;
        State::needsRedraw = true;
        Serial.printf("[Encoder] Brightness: %d%%\n", State::brightness);
      }
    }
  } else if (State::mode == MODE_SCENE_SELECT) {
    // Smooth scene browsing - one scene per encoder tick
    int encoderDelta = currentPos - State::encoderPosition;
    if (encoderDelta > 2) {
      State::sceneIndex = (State::sceneIndex + 1) % sceneCount;
      State::sceneShown = State::sceneIndex;
      State::needsRedraw = true;
      State::encoderPosition = currentPos;
      Serial.printf("[Encoder] Scene: %s\n", scenes[State::sceneIndex].name);
    } else if (encoderDelta < -2) {
      State::sceneIndex = (State::sceneIndex - 1 + sceneCount) % sceneCount;
      State::sceneShown = State::sceneIndex;
      State::needsRedraw = true;
      State::encoderPosition = currentPos;
      Serial.printf("[Encoder] Scene: %s\n", scenes[State::sceneIndex].name);
    }
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasPressed()) {
    Serial.printf("[Button] Pressed\n");
    State::needsRedraw = true;

    if (State::mode == MODE_BRIGHTNESS || State::mode == MODE_SCENE_SELECT) {
      enterSceneMode();
    } else if (State::mode == MODE_SCENE_APPLY) {
      returnToBrightness();
    }
  }
}

void handleTouch() {
  auto touch = M5Dial.Touch.getDetail();

  // Touch to enter scene select (state 1 = touched, state 3 = touch_begin)
  if (touch.state == 1 || touch.state == 3) {
    if (State::mode == MODE_BRIGHTNESS) {
      Serial.printf("[Touch] Entering scene mode at x=%d, y=%d\n", touch.x, touch.y);
      enterSceneMode();
    }
  }
}

void enterSceneMode() {
  State::mode = MODE_SCENE_SELECT;
  State::modeChangeTime = millis();
  State::sceneIndex = State::sceneShown;
  State::needsRedraw = true;
  Serial.println("[Mode] Switched to SCENE_SELECT");
}

void applyScene() {
  State::mode = MODE_SCENE_APPLY;
  State::sceneApplyTime = millis();
  State::needsRedraw = true;
  Serial.printf("[Scene] Applied: %s\n", scenes[State::sceneIndex].name);

  // Auto-return to brightness after 1 second
  if (millis() - State::sceneApplyTime > 1000) {
    returnToBrightness();
  }
}

void returnToBrightness() {
  State::mode = MODE_BRIGHTNESS;
  State::modeChangeTime = millis();
  State::needsRedraw = true;
  Serial.println("[Mode] Switched to BRIGHTNESS");
}

void updateDisplay() {
  auto& disp = M5Dial.Display;

  // Clear screen
  disp.fillScreen(TFT_BLACK);

  // Always show title
  disp.setTextSize(1);
  disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  disp.setCursor(45, 15);
  disp.printf("M5 DIAL");

  if (State::mode == MODE_BRIGHTNESS) {
    displayBrightnessMode(disp);
  } else if (State::mode == MODE_SCENE_SELECT) {
    displaySceneSelectMode(disp);
  } else if (State::mode == MODE_SCENE_APPLY) {
    displaySceneApplyMode(disp);
  }
}

void displayBrightnessMode(M5GFX& disp) {
  // Label
  disp.setTextSize(1);
  disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  disp.setCursor(60, 65);
  disp.printf("Brightness");

  // Large percentage
  disp.setTextSize(7);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setCursor(25, 105);
  disp.printf("%3d%%", State::brightness);

  // Arc indicator
  uint16_t color565 = colorTo565(0xf2a93b);
  drawBrightnessArc(disp, State::brightness, color565);

  // Status dot
  disp.fillCircle(120, 210, 4, colorTo565(0x4fd08a));
}

void displaySceneSelectMode(M5GFX& disp) {
  Scene current = scenes[State::sceneIndex];

  // Scene icon (using emoji/text representation)
  disp.setTextSize(5);
  disp.setTextColor(colorTo565(current.color), TFT_BLACK);
  disp.setCursor(55, 80);
  disp.printf("*");  // Placeholder for icon

  // Scene name
  disp.setTextSize(2);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setCursor(35, 150);
  disp.printf(current.name);

  // Position indicator
  disp.setTextSize(1);
  disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  disp.setCursor(85, 180);
  disp.printf("%d/%d", State::sceneIndex + 1, sceneCount);

  // Status dots (scene indicators)
  int dotY = 205;
  int dotSpacing = 18;
  for (int i = 0; i < sceneCount; i++) {
    uint16_t dotColor = (i == State::sceneIndex)
      ? colorTo565(current.color)
      : colorTo565(0x333333);
    disp.fillCircle(48 + (i * dotSpacing), dotY, 3, dotColor);
  }
}

void displaySceneApplyMode(M5GFX& disp) {
  Scene current = scenes[State::sceneIndex];

  // Scene icon (animated)
  disp.setTextSize(5);
  disp.setTextColor(colorTo565(current.color), TFT_BLACK);
  disp.setCursor(55, 70);
  disp.printf("*");  // Placeholder for icon

  // Scene name
  disp.setTextSize(2);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setCursor(35, 145);
  disp.printf(current.name);

  // Applied indicator
  disp.setTextSize(1);
  disp.setTextColor(colorTo565(current.color), TFT_BLACK);
  disp.setCursor(85, 175);
  disp.printf("Applied");

  // Check mark
  disp.setTextSize(2);
  disp.setTextColor(colorTo565(current.color), TFT_BLACK);
  disp.setCursor(110, 175);
  disp.printf("✓");
}

void drawBrightnessArc(M5GFX& disp, uint8_t brightness, uint16_t color) {
  uint16_t radius = 85;
  uint16_t cx = 120;
  uint16_t cy = 145;

  // Background arc
  for (int angle = 135; angle <= 405; angle += 8) {
    float rad = angle * PI / 180.0f;
    int x1 = cx + radius * cos(rad);
    int y1 = cy + radius * sin(rad);
    int x2 = cx + (radius - 6) * cos(rad);
    int y2 = cy + (radius - 6) * sin(rad);
    disp.drawLine(x1, y1, x2, y2, TFT_DARKGREY);
  }

  // Brightness arc
  int arcEnd = 135 + (270 * brightness / 100);
  for (int angle = 135; angle <= arcEnd; angle += 8) {
    float rad = angle * PI / 180.0f;
    int x1 = cx + radius * cos(rad);
    int y1 = cy + radius * sin(rad);
    int x2 = cx + (radius - 6) * cos(rad);
    int y2 = cy + (radius - 6) * sin(rad);
    disp.drawLine(x1, y1, x2, y2, color);
  }
}

uint16_t colorTo565(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
