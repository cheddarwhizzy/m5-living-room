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

  struct Scene {
    char entityId[64];
    char name[64];
    char lights[256];  // comma-separated light entity ids belonging to the scene
  };

  // "Master Bedroom Rest" -> "Rest"
  void stripAreaPrefix(const char* fullName, char* shortName, int maxLen) {
    const char* start = fullName;
    size_t prefixLen = strlen(HA_NAME_PREFIX);
    if (strncmp(fullName, HA_NAME_PREFIX, prefixLen) == 0 && fullName[prefixLen] != '\0') {
      start = fullName + prefixLen;
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

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.printf("[HA] JSON parse error: %s\n", error.c_str());
      return false;
    }

    for (JsonObject item : doc.as<JsonArray>()) {
      if (sceneCount >= maxScenes) {
        Serial.printf("[HA] Ignoring extra scenes beyond %d\n", maxScenes);
        break;
      }

      const char* entityId = item["id"];
      const char* fullName = item["name"] | "Scene";
      const char* lights = item["lights"] | "";
      if (!entityId) continue;

      strncpy(scenes[sceneCount].entityId, entityId, 63);
      scenes[sceneCount].entityId[63] = '\0';
      stripAreaPrefix(fullName, scenes[sceneCount].name, 64);
      strncpy(scenes[sceneCount].lights, lights, 255);
      scenes[sceneCount].lights[255] = '\0';
      sceneCount++;

      Serial.printf("[HA] Added scene: %s (%s)\n", scenes[sceneCount - 1].name, entityId);
    }

    Serial.printf("[HA] Total scenes found: %d\n", sceneCount);
    return sceneCount > 0;
  }

  // Sets brightness on a comma-separated list of light entity ids.
  bool setBrightness(const char* lightList, uint8_t brightnessPct) {
    if (!connected || !lightList || lightList[0] == '\0') return false;

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/light/turn_on";

    http.begin(url);
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(1024);
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

  bool activateScene(const char* entityId) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/scene/turn_on";

    http.begin(url);
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(256);
    doc["entity_id"] = entityId;

    String payload;
    serializeJson(doc, payload);

    int httpResponseCode = http.POST(payload);
    bool success = (httpResponseCode == 200);

    if (success) {
      Serial.printf("[HA] Activated scene %s\n", entityId);
    } else {
      Serial.printf("[HA] Failed to activate scene %s: %d\n", entityId, httpResponseCode);
    }

    http.end();
    return success;
  }
};

extern HomeAssistantClient haClient;

#endif
