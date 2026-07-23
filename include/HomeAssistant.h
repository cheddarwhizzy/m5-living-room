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

  struct Entity {
    char entityId[64];
    char name[64];
    char state[32];
  };

  bool fetchAreaLights(Entity* lights, int maxLights, int& lightCount) {
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

    // Parse JSON to find lights in the area
    lightCount = 0;
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.printf("[HA] JSON parse error: %s\n", error.c_str());
      return false;
    }

    // Filter lights by area
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject item : arr) {
      if (lightCount >= maxLights) break;

      const char* entityId = item["entity_id"];
      if (entityId && strncmp(entityId, "light.", 6) == 0) {
        // Check if this light is in our area
        JsonObject attr = item["attributes"];
        const char* areaId = attr["area_id"];

        if (areaId && strcmp(areaId, HA_AREA) == 0) {
          strncpy(lights[lightCount].entityId, entityId, 63);
          strncpy(lights[lightCount].name, attr["friendly_name"] | "Unknown", 63);
          strncpy(lights[lightCount].state, item["state"], 31);
          lightCount++;

          Serial.printf("[HA] Found light: %s (%s)\n", lights[lightCount-1].name, entityId);
        }
      }
    }

    return true;
  }

  bool toggleLight(const char* entityId) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/light/toggle";

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
      Serial.printf("[HA] Toggled %s\n", entityId);
    } else {
      Serial.printf("[HA] Failed to toggle %s: %d\n", entityId, httpResponseCode);
    }

    http.end();
    return success;
  }

  bool setBrightness(const char* entityId, uint8_t brightness) {
    if (!connected) {
      Serial.println("[HA] Not connected to WiFi");
      return false;
    }

    HTTPClient http;
    String url = String(HA_URL) + "/api/services/light/turn_on";

    http.begin(url);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(256);
    doc["entity_id"] = entityId;
    doc["brightness"] = (int)(brightness * 2.55); // Convert 0-100 to 0-255

    String payload;
    serializeJson(doc, payload);

    int httpResponseCode = http.POST(payload);
    bool success = (httpResponseCode == 200);

    if (success) {
      Serial.printf("[HA] Set %s brightness to %d\n", entityId, brightness);
    } else {
      Serial.printf("[HA] Failed to set brightness: %d\n", httpResponseCode);
    }

    http.end();
    return success;
  }
};

extern HomeAssistantClient haClient;

#endif
