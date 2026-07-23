#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <WiFi.h>
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
  };

  // Strip area prefix from scene name (e.g., "Master Bedroom Mushroom" -> "Mushroom")
  void stripAreaPrefix(const char* fullName, char* shortName, int maxLen) {
    const char* ptr = fullName;
    int spaceCount = 0;

    // Skip words until we find the scene name (usually after 2+ spaces)
    while (*ptr && spaceCount < 2) {
      if (*ptr == ' ') {
        spaceCount++;
        ptr++;
        // If next part looks like the scene name (capitalized), use it
        if (spaceCount >= 2 && *ptr >= 'A' && *ptr <= 'Z') {
          break;
        }
      } else {
        ptr++;
      }
    }

    // If we found a scene name, copy it; otherwise use full name
    if (spaceCount >= 2 && *ptr) {
      strncpy(shortName, ptr, maxLen - 1);
      shortName[maxLen - 1] = '\0';
    } else {
      strncpy(shortName, fullName, maxLen - 1);
      shortName[maxLen - 1] = '\0';
    }
  }

  bool fetchAreaScenes(Scene* scenes, int maxScenes, int& sceneCount) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/states";

    http.begin(url);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
      Serial.printf("[HA] Failed to fetch states: %d\n", httpResponseCode);
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON to find scenes in the area
    sceneCount = 0;
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.printf("[HA] JSON parse error: %s\n", error.c_str());
      return false;
    }

    // Filter scenes by area
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject item : arr) {
      if (sceneCount >= maxScenes) break;

      const char* entityId = item["entity_id"];
      if (entityId && strncmp(entityId, "scene.", 6) == 0) {
        // Check if this scene is in our area
        JsonObject attr = item["attributes"];
        const char* areaId = attr["area_id"];

        if (areaId && strcmp(areaId, HA_AREA) == 0) {
          strncpy(scenes[sceneCount].entityId, entityId, 63);
          const char* fullName = attr["friendly_name"] | "Unknown";
          stripAreaPrefix(fullName, scenes[sceneCount].name, 64);
          sceneCount++;

          Serial.printf("[HA] Found scene: %s (%s)\n", scenes[sceneCount-1].name, entityId);
        }
      }
    }

    return true;
  }

  bool activateScene(const char* entityId) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/scene/turn_on";

    http.begin(url);
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
