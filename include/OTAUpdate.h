#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <ArduinoOTA.h>
#include <M5Dial.h>
#include "WiFiConfig.h"

// Over-the-air firmware updates. Only useful once WiFi is up, so call this
// after connectWiFi() succeeds; ArduinoOTA.handle() then runs from loop().
inline void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    // The HA task keeps hitting TLS while the flash is being rewritten; the
    // screen is the only feedback available once the USB cable is gone.
    M5Dial.Display.fillScreen(TFT_BLACK);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Dial.Display.drawString("OTA Update", 120, 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int lastPct = -1;
    int pct = total ? (progress * 100) / total : 0;
    if (pct == lastPct) return;
    lastPct = pct;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    M5Dial.Display.fillRect(60, 120, 120, 30, TFT_BLACK);
    M5Dial.Display.drawString(buf, 120, 135);
  });

  ArduinoOTA.onEnd([]() {
    M5Dial.Display.fillScreen(TFT_BLACK);
    M5Dial.Display.drawString("Rebooting...", 120, 120);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    M5Dial.Display.fillScreen(TFT_BLACK);
    M5Dial.Display.drawString("OTA failed", 120, 120);
    Serial.printf("[OTA] Error %u\n", error);
  });

  ArduinoOTA.begin();
  Serial.print("[OTA] Ready at ");
  Serial.print(WiFi.localIP());
  Serial.println(" (" OTA_HOSTNAME ".local)");
}

#endif
