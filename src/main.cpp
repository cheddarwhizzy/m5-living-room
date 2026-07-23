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
  MODE_SCENE_SELECT
};

// Home Assistant client
HomeAssistantClient haClient;

// HA Scenes discovered
struct HAScene {
  char entityId[64];
  char name[64];
  char lights[HomeAssistantClient::LIGHTS_LEN];
};

HAScene haScenes[8];
int haSceneCount = 0;

const unsigned long SCENE_REFRESH_MS = 30000;

// Brightness change per encoder count
const int BRIGHTNESS_STEP = 2;

// How long the dial must sit still before a change is sent to HA. Long enough
// that scrolling past scenes doesn't activate each one on the way.
const unsigned long COMMIT_DELAY_MS = 600;
unsigned long scenePendingSince = 0;
unsigned long brightnessPendingSince = 0;
int lastActivatedScene = -1;

// Every HA call does a full TLS handshake and blocks for ~1s, so all network
// work runs on a separate task. The UI task only posts intent to these queues;
// they hold one item and are overwritten, so a fast spin collapses to one call.
// Scene commands carry the entity id, not an index: the list can be re-sorted
// by a refresh between queueing and sending, which would activate the wrong one.
struct SceneCommand {
  char entityId[64];
};

QueueHandle_t sceneQueue;
QueueHandle_t brightnessQueue;
SemaphoreHandle_t sceneMutex;  // guards haScenes / haSceneCount across tasks

#define SCENES_LOCK()   xSemaphoreTake(sceneMutex, portMAX_DELAY)
#define SCENES_UNLOCK() xSemaphoreGive(sceneMutex)

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
  long encoderPosition = 0;  // raw counts (brightness)
  long encoderDetent = 0;    // whole detents (scene selection)
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
void haTask(void* param);
void resyncEncoder();
void handleEncoder();
void handleButton();
void handleTouch();
void enterSceneMode();
void returnToBrightness();
void displayBrightnessMode(M5GFX& disp);
void displaySceneSelectMode(M5GFX& disp);
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

  sceneMutex = xSemaphoreCreateMutex();
  sceneQueue = xQueueCreate(1, sizeof(SceneCommand));
  brightnessQueue = xQueueCreate(1, sizeof(uint8_t));

  // Connect to WiFi and Home Assistant
  if (haClient.connectWiFi()) {
    M5Dial.Display.fillScreen(TFT_BLACK);
    M5Dial.Display.drawString("Fetching scenes...", 120, 120);
    refreshScenes();
  } else {
    Serial.println("[Setup] WiFi connection failed - no scenes available");
  }

  // TLS needs a generous stack; pinned to core 0 to stay off the UI core.
  xTaskCreatePinnedToCore(haTask, "ha", 12288, nullptr, 1, nullptr, 0);

  Serial.println("Hardware initialized - Phase 2 UI ready");
  State::needsRedraw = true;
  updateDisplay();
}

// Re-reads the labelled scene list so newly labelled scenes appear without a reboot.
// Runs on the HA task; the fetch itself is unlocked, only the swap is guarded.
void refreshScenes() {
  static HomeAssistantClient::Scene fetched[8];  // static: too large for the task stack
  int fetchedCount = 0;
  if (!haClient.fetchAreaScenes(fetched, 8, fetchedCount)) return;

  SCENES_LOCK();
  bool changed = (fetchedCount != haSceneCount);
  for (int i = 0; i < fetchedCount && !changed; i++) {
    changed = strcmp(haScenes[i].entityId, fetched[i].entityId) != 0 ||
              strcmp(haScenes[i].name, fetched[i].name) != 0 ||
              strcmp(haScenes[i].lights, fetched[i].lights) != 0;
  }

  if (changed) {
    for (int i = 0; i < fetchedCount; i++) {
      strcpy(haScenes[i].entityId, fetched[i].entityId);
      strcpy(haScenes[i].name, fetched[i].name);
      strcpy(haScenes[i].lights, fetched[i].lights);
    }
    haSceneCount = fetchedCount;
    if (lastActivatedScene >= haSceneCount) lastActivatedScene = -1;
    if (State::sceneIndex >= haSceneCount) State::sceneIndex = 0;
    if (State::sceneShown >= haSceneCount) State::sceneShown = 0;
    State::needsRedraw = true;
    Serial.printf("[Scenes] Updated: %d scenes\n", haSceneCount);
  }
  SCENES_UNLOCK();
}

// All blocking network work lives here so the UI task never stalls.
void haTask(void* param) {
  unsigned long lastRefresh = millis();

  for (;;) {
    SceneCommand cmd;
    if (xQueueReceive(sceneQueue, &cmd, 0) == pdTRUE) {
      // A request queued behind a slow call may already be stale - the user has
      // kept turning. Compare against what the dial is showing right now.
      char selected[64] = {0};
      int selectedIdx = -1;
      SCENES_LOCK();
      if (State::sceneIndex < haSceneCount) {
        strcpy(selected, haScenes[State::sceneIndex].entityId);
        selectedIdx = State::sceneIndex;
      }
      SCENES_UNLOCK();

      if (strcmp(cmd.entityId, selected) != 0) {
        Serial.printf("[Scene] Skipping stale %s (now %s)\n", cmd.entityId, selected);
      } else if (haClient.activateScene(cmd.entityId)) {
        lastActivatedScene = selectedIdx;
        Serial.printf("[Scene] Live: %s\n", cmd.entityId);
      }
    }

    uint8_t brightness;
    if (xQueueReceive(brightnessQueue, &brightness, 0) == pdTRUE) {
      brightness = State::brightness;  // always send the dial's current value
      int idx = (lastActivatedScene >= 0) ? lastActivatedScene : State::sceneIndex;
      static char lights[HomeAssistantClient::LIGHTS_LEN];  // static: keeps the task stack small
      lights[0] = '\0';
      SCENES_LOCK();
      if (idx >= 0 && idx < haSceneCount) strcpy(lights, haScenes[idx].lights);
      SCENES_UNLOCK();

      if (lights[0]) haClient.setBrightness(lights, brightness);
    }

    if (millis() - lastRefresh > SCENE_REFRESH_MS) {
      lastRefresh = millis();
      refreshScenes();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void loop() {
  M5Dial.update();

  handleEncoder();
  handleButton();
  handleTouch();

  // Hand off to the HA task once the dial has settled. Queues are overwritten,
  // so only the final position of a spin is sent.
  if (scenePendingSince && millis() - scenePendingSince > COMMIT_DELAY_MS) {
    scenePendingSince = 0;
    SceneCommand cmd = {};
    SCENES_LOCK();
    if (State::sceneIndex < haSceneCount) strcpy(cmd.entityId, haScenes[State::sceneIndex].entityId);
    SCENES_UNLOCK();
    if (cmd.entityId[0]) xQueueOverwrite(sceneQueue, &cmd);
  }
  if (brightnessPendingSince && millis() - brightnessPendingSince > COMMIT_DELAY_MS) {
    brightnessPendingSince = 0;
    uint8_t b = State::brightness;
    xQueueOverwrite(brightnessQueue, &b);
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

// Scene selection steps once per physical detent (4 encoder counts). Plain
// division truncates toward zero, which makes steps around 0 asymmetric.
long toDetents(long raw) {
  return (raw >= 0) ? (raw / 4) : -((-raw + 3) / 4);
}

// Discards any accumulated motion so a mode change never applies a stale delta.
void resyncEncoder() {
  long raw = M5Dial.Encoder.read();
  State::encoderPosition = raw;
  State::encoderDetent = toDetents(raw);
}

void handleEncoder() {
  long raw = M5Dial.Encoder.read();

  if (State::mode == MODE_BRIGHTNESS) {
    // Brightness tracks raw counts - one step per count, so a small turn moves it.
    int delta = (int)(raw - State::encoderPosition);
    if (delta == 0) return;
    State::encoderPosition = raw;
    State::encoderDetent = toDetents(raw);

    int newBrightness = constrain(State::brightness + delta * BRIGHTNESS_STEP,
                                  Config::BRIGHTNESS_MIN, Config::BRIGHTNESS_MAX);
    if (newBrightness != State::brightness) {
      State::brightness = newBrightness;
      State::needsRedraw = true;
      brightnessPendingSince = millis();
      Serial.printf("[Encoder] Brightness: %d%%\n", State::brightness);
    }
  } else if (State::mode == MODE_SCENE_SELECT && haSceneCount > 0) {
    // Scenes step per detent so one click never skips past a scene.
    long detents = toDetents(raw);
    int delta = (int)(detents - State::encoderDetent);
    if (delta == 0) return;
    State::encoderDetent = detents;
    State::encoderPosition = raw;

    int next = ((int)State::sceneIndex + delta) % haSceneCount;
    if (next < 0) next += haSceneCount;
    State::sceneIndex = next;
    State::sceneShown = State::sceneIndex;
    State::needsRedraw = true;
    scenePendingSince = millis();  // activates once the dial settles
    Serial.printf("[Encoder] Scene: %s (delta: %d)\n", sceneName(State::sceneIndex), delta);
  }
}

void handleButton() {
  if (M5Dial.BtnA.wasPressed()) {
    Serial.printf("[Button] Pressed\n");
    State::needsRedraw = true;

    if (State::mode == MODE_BRIGHTNESS) {
      enterSceneMode();
    } else {
      returnToBrightness();  // scenes are already live; click just goes back
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
  resyncEncoder();
  State::needsRedraw = true;
  Serial.println("[Mode] Switched to SCENE_SELECT");
}

void returnToBrightness() {
  State::mode = MODE_BRIGHTNESS;
  State::modeChangeTime = millis();
  resyncEncoder();
  State::needsRedraw = true;
  Serial.println("[Mode] Switched to BRIGHTNESS");
}

void updateDisplay() {
  auto& disp = M5Dial.Display;

  // Clear screen
  disp.fillScreen(TFT_BLACK);

  if (State::mode == MODE_BRIGHTNESS) {
    displayBrightnessMode(disp);
  } else {
    SCENES_LOCK();  // the HA task can swap the scene list mid-draw
    displaySceneSelectMode(disp);
    SCENES_UNLOCK();
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
