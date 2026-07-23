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
#include <WiFi.h>
#include "../include/AppConfig.h"
#include "../include/BuildInfo.h"
#include "../include/WiFiConfig.h"
#include "../include/HomeAssistant.h"

// UI Modes
enum UIMode {
  MODE_BRIGHTNESS,
  MODE_SCENE_SELECT,
  MODE_SCENE_APPLY
};

// Home Assistant client
HomeAssistantClient haClient;

// HA Scenes discovered
struct HAScene {
  char entityId[64];
  char name[64];
};

HAScene haScenes[8];
int haSceneCount = 0;

const unsigned long SCENE_REFRESH_MS = 30000;
unsigned long lastSceneRefresh = 0;

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

const uint32_t scenePalette[] = {
  0x9b7cff, 0x4aa8ff, 0xff8a3d, 0xffc24a, 0xff5ecb, 0xff6a2a, 0x4fd08a, 0x7cd8ff
};

const char* sceneName(int i) { return haScenes[i].name; }
uint32_t sceneColor(int i) { return scenePalette[i % 8]; }

// Forward declarations
void updateDisplay();
void refreshScenes();
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

  Serial.println("\n=== M5 Dial Lighting Controller (Phase 2 + HA) ===");
  Serial.print("Firmware: ");
  Serial.println(BuildInfo::FIRMWARE_NAME);
  Serial.print("Version: ");
  Serial.println(BuildInfo::FIRMWARE_VERSION);

  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);

  M5Dial.Display.setTextSize(1);
  M5Dial.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.drawString("Connecting...", 120, 120);

  // Connect to WiFi and Home Assistant
  if (haClient.connectWiFi()) {
    M5Dial.Display.fillScreen(TFT_BLACK);
    M5Dial.Display.drawString("Fetching scenes...", 120, 120);
    refreshScenes();
  } else {
    Serial.println("[Setup] WiFi connection failed - no scenes available");
  }

  Serial.println("Hardware initialized - Phase 2 UI ready");
  State::needsRedraw = true;
  updateDisplay();
}

// Re-reads the labelled scene list so newly labelled scenes appear without a reboot.
void refreshScenes() {
  lastSceneRefresh = millis();

  HomeAssistantClient::Scene fetched[8];
  int fetchedCount = 0;
  if (!haClient.fetchAreaScenes(fetched, 8, fetchedCount)) return;

  bool changed = (fetchedCount != haSceneCount);
  for (int i = 0; i < fetchedCount && !changed; i++) {
    changed = strcmp(haScenes[i].entityId, fetched[i].entityId) != 0 ||
              strcmp(haScenes[i].name, fetched[i].name) != 0;
  }
  if (!changed) return;

  for (int i = 0; i < fetchedCount; i++) {
    strcpy(haScenes[i].entityId, fetched[i].entityId);
    strcpy(haScenes[i].name, fetched[i].name);
  }
  haSceneCount = fetchedCount;

  if (State::sceneIndex >= haSceneCount) State::sceneIndex = 0;
  if (State::sceneShown >= haSceneCount) State::sceneShown = 0;
  State::needsRedraw = true;
  Serial.printf("[Scenes] Updated: %d scenes\n", haSceneCount);
}

void loop() {
  M5Dial.update();

  handleEncoder();
  handleButton();
  handleTouch();

  // Pick up newly labelled scenes without a power cycle
  if (millis() - lastSceneRefresh > SCENE_REFRESH_MS) {
    refreshScenes();
  }

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
      int newBrightness = State::brightness + encoderDelta;
      newBrightness = constrain(newBrightness, Config::BRIGHTNESS_MIN, Config::BRIGHTNESS_MAX);

      if (newBrightness != State::brightness) {
        State::brightness = newBrightness;
        State::needsRedraw = true;
        Serial.printf("[Encoder] Brightness: %d%%\n", State::brightness);
      }
      State::encoderPosition = currentPos;
    }
  } else if (State::mode == MODE_SCENE_SELECT && haSceneCount > 0) {
    // One scene per detent click (divide by 4 for discrete steps)
    int selectorValue = currentPos / 4;
    int lastSelectorValue = State::encoderPosition / 4;

    if (selectorValue != lastSelectorValue) {
      int stepDelta = selectorValue - lastSelectorValue;
      int next = ((int)State::sceneIndex + stepDelta) % haSceneCount;
      if (next < 0) next += haSceneCount;
      State::sceneIndex = next;
      State::sceneShown = State::sceneIndex;
      State::needsRedraw = true;
      Serial.printf("[Encoder] Scene: %s (step delta: %d)\n", sceneName(State::sceneIndex), stepDelta);
      State::encoderPosition = currentPos;
    }
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasPressed()) {
    Serial.printf("[Button] Pressed\n");
    State::needsRedraw = true;

    if (State::mode == MODE_BRIGHTNESS) {
      enterSceneMode();
    } else if (State::mode == MODE_SCENE_SELECT) {
      applyScene();
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
  if (haSceneCount == 0) {
    Serial.println("[Mode] No scenes available");
    return;
  }
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
  Serial.printf("[Scene] Applied: %s\n", sceneName(State::sceneIndex));

  // Activate the corresponding Home Assistant scene
  if (haSceneCount > 0 && State::sceneIndex < haSceneCount) {
    if (haClient.activateScene(haScenes[State::sceneIndex].entityId)) {
      Serial.printf("[Scene] Activated HA scene: %s\n", haScenes[State::sceneIndex].name);
    }
  }

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
  disp.setTextDatum(middle_center);
  disp.drawString("Brightness", 120, 50);

  // Large number - centered
  disp.setTextSize(7);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setTextDatum(middle_center);
  char brightnessStr[10];
  sprintf(brightnessStr, "%d", State::brightness);
  disp.drawString(brightnessStr, 120, 110);

  // Arc indicator
  uint16_t color565 = colorTo565(0xf2a93b);
  drawBrightnessArc(disp, State::brightness, color565);

  // Status dot - centered at bottom
  disp.fillCircle(120, 215, 5, colorTo565(0x4fd08a));
}

void displaySceneSelectMode(M5GFX& disp) {
  if (haSceneCount == 0) return;
  uint16_t accent = colorTo565(sceneColor(State::sceneIndex));

  // Brightness arc indicator (around the perimeter)
  drawBrightnessArc(disp, State::brightness, colorTo565(0xf2a93b));

  // Scene icon (centered)
  disp.setTextSize(5);
  disp.setTextColor(accent, TFT_BLACK);
  disp.setTextDatum(middle_center);
  disp.drawString("*", 120, 75);  // Placeholder for icon

  // Scene name (centered)
  disp.setTextSize(2);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setTextDatum(middle_center);
  disp.drawString(sceneName(State::sceneIndex), 120, 145);

  // Position indicator (centered)
  disp.setTextSize(1);
  disp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  disp.setTextDatum(middle_center);
  char posStr[10];
  sprintf(posStr, "%d/%d", State::sceneIndex + 1, haSceneCount);
  disp.drawString(posStr, 120, 175);

  // Status dots (scene indicators) - centered at bottom
  int dotY = 210;
  int dotSpacing = 18;
  int startX = 120 - (((haSceneCount - 1) * dotSpacing) / 2);

  for (int i = 0; i < haSceneCount; i++) {
    uint16_t dotColor = (i == State::sceneIndex) ? accent : colorTo565(0x333333);
    disp.fillCircle(startX + (i * dotSpacing), dotY, 3, dotColor);
  }
}

void displaySceneApplyMode(M5GFX& disp) {
  if (haSceneCount == 0) return;
  uint16_t accent = colorTo565(sceneColor(State::sceneIndex));

  // Brightness arc indicator (around the perimeter)
  drawBrightnessArc(disp, State::brightness, colorTo565(0xf2a93b));

  // Scene icon (centered)
  disp.setTextSize(5);
  disp.setTextColor(accent, TFT_BLACK);
  disp.setTextDatum(middle_center);
  disp.drawString("*", 120, 70);  // Placeholder for icon

  // Scene name (centered)
  disp.setTextSize(2);
  disp.setTextColor(TFT_WHITE, TFT_BLACK);
  disp.setTextDatum(middle_center);
  disp.drawString(sceneName(State::sceneIndex), 120, 135);

  // Applied indicator (centered)
  disp.setTextSize(1);
  disp.setTextColor(accent, TFT_BLACK);
  disp.setTextDatum(middle_center);
  disp.drawString("Applied", 120, 175);
}

void drawBrightnessArc(M5GFX& disp, uint8_t brightness, uint16_t color) {
  uint16_t outerRadius = 110;
  uint16_t innerRadius = 95;
  uint16_t cx = 120;
  uint16_t cy = 120;
  uint16_t darkGrey = colorTo565(0x222222);

  // Background arc (full ring) - solid
  for (int angle = 135; angle <= 405; angle += 1) {
    float rad = angle * PI / 180.0f;
    int x1 = cx + outerRadius * cos(rad);
    int y1 = cy + outerRadius * sin(rad);
    int x2 = cx + innerRadius * cos(rad);
    int y2 = cy + innerRadius * sin(rad);
    disp.drawLine(x1, y1, x2, y2, darkGrey);
  }

  // Brightness arc (filled portion) - solid
  int arcEnd = 135 + (270 * brightness / 100);
  for (int angle = 135; angle <= arcEnd; angle += 1) {
    float rad = angle * PI / 180.0f;
    int x1 = cx + outerRadius * cos(rad);
    int y1 = cy + outerRadius * sin(rad);
    int x2 = cx + innerRadius * cos(rad);
    int y2 = cy + innerRadius * sin(rad);
    disp.drawLine(x1, y1, x2, y2, color);
  }
}

uint16_t colorTo565(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
