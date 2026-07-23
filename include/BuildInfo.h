#pragma once

#include <Arduino.h>

namespace BuildInfo {

constexpr const char* FIRMWARE_NAME = "M5 Dial Lighting Controller";
constexpr const char* FIRMWARE_VERSION = "0.1.0";

// Build date - automatically set at compile time
#ifndef BUILD_DATE
  #define BUILD_DATE __DATE__
#endif

constexpr const char* BUILD_DATE_STR = BUILD_DATE;

// Git commit - optional, set via build flag if available
#ifndef GIT_COMMIT
  #define GIT_COMMIT "unknown"
#endif

constexpr const char* GIT_COMMIT_STR = GIT_COMMIT;

// Hardware info (populated at runtime)
struct HardwareInfo {
  const char* chip_model;
  uint32_t chip_revision;
  uint32_t flash_size_bytes;
  uint32_t psram_size_bytes;
  uint32_t free_heap_bytes;

  HardwareInfo()
    : chip_model(ESP.getChipModel()),
      chip_revision(ESP.getChipRevision()),
      flash_size_bytes(ESP.getFlashChipSize()),
      psram_size_bytes(ESP.getPsramSize()),
      free_heap_bytes(ESP.getFreeHeap()) {}
};

}  // namespace BuildInfo
