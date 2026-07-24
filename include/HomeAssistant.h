#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "WiFiConfig.h"

class HomeAssistantClient {
public:
  bool connected = false;

  bool connectWiFi() {
    Serial.println("[WiFi] Connecting to " WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      connected = true;
      return true;
    } else {
      Serial.println("\n[WiFi] Failed to connect");
      connected = false;
      return false;
    }
  }

  // Scenes built from individual lights rather than groups list many more
  // entities, so this is sized for a room's worth of them.
  static const int LIGHTS_LEN = 512;

  // Limits are per area, not global - switching areas swaps the scene list.
  static const int MAX_SCENES_PER_AREA = 8;
  static const int MAX_AREAS = 6;
  static const int MAX_SCENES = MAX_SCENES_PER_AREA * MAX_AREAS;

  struct Scene {
    char entityId[64];
    char name[64];
    char area[48];
    char lights[LIGHTS_LEN];  // comma-separated light entity ids in the scene
  };

  // "Master Bedroom Rest" in area "Master Bedroom" -> "Rest". The area is shown
  // separately on screen, so repeating it in every scene name wastes the display.
  void stripAreaPrefix(const char* fullName, const char* area, char* shortName, int maxLen) {
    const char* start = fullName;
    size_t areaLen = strlen(area);
    if (areaLen && strncasecmp(fullName, area, areaLen) == 0) {
      const char* rest = fullName + areaLen;
      while (*rest == ' ') rest++;
      if (*rest) start = rest;
    }
    strncpy(shortName, start, maxLen - 1);
    shortName[maxLen - 1] = '\0';
  }

  // Asks HA to render just the labelled scenes, so the ESP32 gets ~200 bytes
  // instead of the ~200KB /api/states dump (which the WiFi stack truncates).
  bool fetchAreaScenes(Scene* scenes, int maxScenes, int& sceneCount) {
    sceneCount = 0;

    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/template";

    http.begin(url);
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument reqDoc(1024);
    reqDoc["template"] =
      "[{% for e in label_entities('" HA_SCENE_LABEL "') %}"
      "{\"id\":\"{{ e }}\",\"name\":\"{{ state_attr(e,'friendly_name') }}\","
      "\"area\":\"{{ area_name(e) }}\","
      "\"lights\":\"{{ state_attr(e,'entity_id') | select('match','light\\\\.') | join(',') }}\"}"
      "{{ ',' if not loop.last }}"
      "{% endfor %}]";

    String request;
    serializeJson(reqDoc, request);

    Serial.printf("[HA] Fetching scenes labelled '%s'\n", HA_SCENE_LABEL);
    int httpResponseCode = http.POST(request);

    if (httpResponseCode != 200) {
      Serial.printf("[HA] Template request failed: %d\n", httpResponseCode);
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[HA] Payload (%d bytes): %s\n", payload.length(), payload.c_str());

    // Sized for MAX_SCENES entries each listing a room's worth of lights.
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.printf("[HA] JSON parse error: %s\n", error.c_str());
      return false;
    }

    for (JsonObject item : doc.as<JsonArray>()) {
      if (sceneCount >= maxScenes) {
        Serial.printf("[HA] Ignoring scenes beyond %d total\n", maxScenes);
        break;
      }

      const char* entityId = item["id"];
      const char* fullName = item["name"] | "Scene";
      const char* lights = item["lights"] | "";
      const char* area = item["area"] | "";
      if (!entityId) continue;

      // Scenes with no area would be unreachable once the UI is area-scoped
      if (area[0] == '\0' || strcmp(area, "None") == 0) {
        Serial.printf("[HA] Skipping %s - not assigned to an area\n", entityId);
        continue;
      }

      strncpy(scenes[sceneCount].entityId, entityId, 63);
      scenes[sceneCount].entityId[63] = '\0';
      strncpy(scenes[sceneCount].area, area, 47);
      scenes[sceneCount].area[47] = '\0';
      stripAreaPrefix(fullName, area, scenes[sceneCount].name, 64);
      strncpy(scenes[sceneCount].lights, lights, LIGHTS_LEN - 1);
      scenes[sceneCount].lights[LIGHTS_LEN - 1] = '\0';
      if (strlen(lights) >= LIGHTS_LEN) {
        Serial.printf("[HA] WARNING: light list truncated for %s\n", entityId);
      }
      sceneCount++;

      Serial.printf("[HA] Added scene: %s (%s)\n", scenes[sceneCount - 1].name, entityId);
    }

    // label_entities() has no defined order, so sort by (area, name) to keep the
    // dial's scene positions stable and to group each area contiguously.
    for (int i = 1; i < sceneCount; i++) {
      Scene key = scenes[i];
      int j = i - 1;
      while (j >= 0) {
        int cmp = strcasecmp(scenes[j].area, key.area);
        if (cmp == 0) cmp = strcasecmp(scenes[j].name, key.name);
        if (cmp <= 0) break;
        scenes[j + 1] = scenes[j];
        j--;
      }
      scenes[j + 1] = key;
    }

    // Enforce the per-area cap now that areas are contiguous, so one busy room
    // can't crowd out another. Done after sorting so the kept scenes are stable.
    int kept = 0, inArea = 0;
    for (int i = 0; i < sceneCount; i++) {
      bool newArea = (i == 0) || strcasecmp(scenes[i].area, scenes[i - 1].area) != 0;
      if (newArea) inArea = 0;

      if (inArea < MAX_SCENES_PER_AREA) {
        if (kept != i) scenes[kept] = scenes[i];
        kept++;
        inArea++;
      } else {
        Serial.printf("[HA] Dropping %s - over %d scenes in %s\n",
                      scenes[i].entityId, MAX_SCENES_PER_AREA, scenes[i].area);
      }
    }
    sceneCount = kept;

    Serial.printf("[HA] Total scenes found: %d\n", sceneCount);
    return sceneCount > 0;
  }

  // Sets brightness on a comma-separated list of light entity ids.
  bool setBrightness(const char* lightList, uint8_t brightnessPct) {
    if (!connected || !lightList || lightList[0] == '\0') return false;

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/light/turn_on";

    http.begin(url);
    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(2048);
    JsonArray targets = doc.createNestedArray("entity_id");

    // Split the comma-separated list into individual entity ids
    String list(lightList);
    int start = 0;
    while (start < list.length()) {
      int comma = list.indexOf(',', start);
      if (comma < 0) comma = list.length();
      String one = list.substring(start, comma);
      one.trim();
      if (one.length()) targets.add(one);
      start = comma + 1;
    }
    doc["brightness_pct"] = brightnessPct;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    http.end();

    if (code == 200) {
      Serial.printf("[HA] Brightness %d%% on %d lights\n", brightnessPct, targets.size());
      return true;
    }
    Serial.printf("[HA] Brightness call failed: %d\n", code);
    return false;
  }

  // Each call sets up a fresh TLS connection, which occasionally overruns the
  // timeout (-11 READ_TIMEOUT), so a failed attempt is retried once.
  bool activateScene(const char* entityId) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    DynamicJsonDocument doc(256);
    doc["entity_id"] = entityId;
    String payload;
    serializeJson(doc, payload);

    for (int attempt = 1; attempt <= 2; attempt++) {
      HTTPClient http;
      http.begin(String(HA_URL) + "/api/services/scene/turn_on");
      http.setConnectTimeout(8000);
      http.setTimeout(12000);
      http.addHeader("Authorization", "Bearer " HA_TOKEN);
      http.addHeader("Content-Type", "application/json");

      int code = http.POST(payload);
      http.end();

      if (code == 200) {
        Serial.printf("[HA] Activated scene %s\n", entityId);
        return true;
      }
      Serial.printf("[HA] Activate %s failed (attempt %d): %d\n", entityId, attempt, code);
      if (attempt == 1) delay(150);
    }
    return false;
  }
};

extern HomeAssistantClient haClient;

#endif
