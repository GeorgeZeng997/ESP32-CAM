#include <Arduino.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"
#include "capture_utils.h"
#include "sd_utils.h"

namespace {

constexpr const char *kBaseVideoDir = "/video";
constexpr uint32_t kDefaultCaptureIntervalMs = 200U;            // ~5 fps
constexpr uint64_t kMinimumFreeSpace = 2ULL * 1024ULL * 1024ULL; // Keep 2 MB reserve
constexpr uint32_t kRecoveryRetryIntervalMs = 5000U;            // Retry failed hardware every 5 s

bool gSdReady = false;
bool gCameraReady = false;
bool gPaused = false;
bool gSnapRequested = false;

uint32_t gCaptureIntervalMs = kDefaultCaptureIntervalMs;
int gJpegQuality = 10;
uint32_t gFrameId = 0;
uint32_t gSavedFrames = 0;
uint32_t gDroppedFrames = 0;
uint32_t gConsecutiveWriteErrors = 0;
String gSessionDir = "";

char gCmdBuf[64];
size_t gCmdLen = 0;
bool gCmdOverflow = false;

bool initCamera(int jpegQuality) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = capture_utils::clampJpegQuality(jpegQuality);
    config.fb_count = 3;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.println("PSRAM found and used");
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = capture_utils::clampJpegQuality(jpegQuality < 12 ? 12 : jpegQuality);
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    Serial.println("PSRAM not found; using DRAM frame buffer");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  gJpegQuality = config.jpeg_quality;
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    Serial.println("Camera sensor configured");
  }

  return true;
}

bool startNewSession() {
  if (!gSdReady) {
    return false;
  }
  String allocated;
  if (!allocateNextRunDir(kBaseVideoDir, allocated)) {
    Serial.println("Failed to allocate session directory under /video");
    return false;
  }
  gSessionDir = allocated;
  gFrameId = 0;
  gConsecutiveWriteErrors = 0;
  Serial.printf("Recording session ready: %s\n", gSessionDir.c_str());
  return true;
}

void printStatusJson() {
  uint64_t freeBytes = gSdReady ? sdFreeBytes() : 0ULL;
  uint32_t freePsram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf(
      "{\"sd_ready\":%s,\"camera_ready\":%s,\"paused\":%s,\"session_dir\":\"%s\","
      "\"next_frame_id\":%lu,\"frames_saved\":%lu,\"frames_dropped\":%lu,"
      "\"interval_ms\":%lu,\"jpeg_quality\":%d,\"free_mb\":%llu,"
      "\"free_heap\":%lu,\"free_psram\":%lu,\"uptime_ms\":%lu}\n",
      gSdReady ? "true" : "false",
      gCameraReady ? "true" : "false",
      gPaused ? "true" : "false",
      gSessionDir.c_str(),
      static_cast<unsigned long>(gFrameId),
      static_cast<unsigned long>(gSavedFrames),
      static_cast<unsigned long>(gDroppedFrames),
      static_cast<unsigned long>(gCaptureIntervalMs),
      gJpegQuality,
      freeBytes / (1024ULL * 1024ULL),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(freePsram),
      static_cast<unsigned long>(millis()));
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  STATUS          - Print JSON recorder telemetry");
  Serial.println("  PAUSE           - Pause continuous frame recording");
  Serial.println("  RESUME          - Resume continuous frame recording");
  Serial.println("  SNAP            - Capture a single JPEG frame immediately");
  Serial.println("  INTERVAL <ms>   - Set capture interval (40..3600000 ms)");
  Serial.println("  QUALITY <4..63> - Set JPEG quality (lower = higher quality)");
  Serial.println("  NEW             - Allocate a fresh /video/run_XXXX folder");
  Serial.println("  REINIT          - Re-initialize SD card and camera");
  Serial.println("  HELP            - Show this command reference");
}

void handleSerialLine(const char *line) {
  capture_utils::SerialCommand cmd = capture_utils::parseSerialCommand(line);
  switch (cmd.type) {
    case capture_utils::SerialCmdType::kNone:
      break;
    case capture_utils::SerialCmdType::kStatus:
      printStatusJson();
      break;
    case capture_utils::SerialCmdType::kPause:
      gPaused = true;
      Serial.println("Recording paused");
      break;
    case capture_utils::SerialCmdType::kResume:
      gPaused = false;
      Serial.println("Recording resumed");
      break;
    case capture_utils::SerialCmdType::kSnap:
      gSnapRequested = true;
      Serial.println("Single-frame snapshot queued");
      break;
    case capture_utils::SerialCmdType::kInterval:
      gCaptureIntervalMs = cmd.value;
      Serial.printf("Capture interval updated to %lu ms\n", static_cast<unsigned long>(gCaptureIntervalMs));
      break;
    case capture_utils::SerialCmdType::kQuality: {
      gJpegQuality = static_cast<int>(cmd.value);
      sensor_t *s = gCameraReady ? esp_camera_sensor_get() : nullptr;
      if (s) {
        s->set_quality(s, gJpegQuality);
      }
      Serial.printf("JPEG quality updated to %d\n", gJpegQuality);
      break;
    }
    case capture_utils::SerialCmdType::kNewSession:
      if (!startNewSession()) {
        Serial.println("ERROR: Could not create new session directory");
      }
      break;
    case capture_utils::SerialCmdType::kReinit:
      Serial.println("Re-initializing storage and camera...");
      gSdReady = initSdCard() && startNewSession();
      if (!gCameraReady) {
        gCameraReady = initCamera(gJpegQuality);
      }
      printStatusJson();
      break;
    case capture_utils::SerialCmdType::kHelp:
      printHelp();
      break;
    case capture_utils::SerialCmdType::kInvalid:
      Serial.println("Unknown or invalid command. Type HELP for usage.");
      break;
  }
}

void pollSerialCli() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (gCmdOverflow) {
        gCmdOverflow = false;
        gCmdLen = 0;
        Serial.println("Command too long (discarded)");
        continue;
      }
      if (gCmdLen > 0) {
        gCmdBuf[gCmdLen] = '\0';
        handleSerialLine(gCmdBuf);
        gCmdLen = 0;
      }
    } else if (!gCmdOverflow) {
      if (gCmdLen + 1U < sizeof(gCmdBuf)) {
        gCmdBuf[gCmdLen++] = c;
      } else {
        gCmdOverflow = true;
      }
    }
  }
}

void captureSingleFrame(uint32_t nowMs) {
  uint64_t freeBytes = sdFreeBytes();
  if (!capture_utils::hasEnoughSpaceForFrame(freeBytes, 0U, kMinimumFreeSpace)) {
    ++gDroppedFrames;
    Serial.println("SD free space below 2 MB reserve, skipping capture");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    ++gDroppedFrames;
    Serial.println("Camera capture failed");
    return;
  }

  freeBytes = sdFreeBytes();
  if (!capture_utils::hasEnoughSpaceForFrame(freeBytes, fb->len, kMinimumFreeSpace)) {
    ++gDroppedFrames;
    Serial.println("Not enough SD space for captured frame");
    esp_camera_fb_return(fb);
    return;
  }

  String savedPath;
  uint32_t currentIndex = gFrameId;
  bool ok = saveJpegFrame(gSessionDir.c_str(), currentIndex, fb->buf, fb->len, savedPath);
  size_t frameLen = fb->len;
  size_t frameWidth = fb->width;
  size_t frameHeight = fb->height;
  esp_camera_fb_return(fb);

  if (ok) {
    appendManifestRow(gSessionDir.c_str(), currentIndex, nowMs, frameLen, frameWidth, frameHeight);
    ++gFrameId;
    ++gSavedFrames;
    gConsecutiveWriteErrors = 0;
    Serial.printf("Saved %s (%u bytes)\n", savedPath.c_str(), static_cast<unsigned>(frameLen));
  } else {
    ++gDroppedFrames;
    ++gConsecutiveWriteErrors;
    Serial.println("Failed to write frame");
    if (gConsecutiveWriteErrors >= 5U) {
      Serial.println("Multiple consecutive SD write failures; marking SD unready for auto-recovery");
      gSdReady = false;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-S3 camera to SD recorder (type HELP for serial commands)");

  gSdReady = initSdCard();
  if (gSdReady) {
    gSdReady = startNewSession();
  } else {
    Serial.println("SD init failed; will retry periodically in background");
  }

  gCameraReady = initCamera(gJpegQuality);
  if (!gCameraReady) {
    Serial.println("Camera init failed; will retry periodically in background");
  }
}

void loop() {
  static uint32_t lastCaptureMs = 0;
  static uint32_t lastRecoveryMs = 0;

  pollSerialCli();

  uint32_t now = millis();
  if (!gSdReady || !gCameraReady) {
    if (now - lastRecoveryMs >= kRecoveryRetryIntervalMs) {
      lastRecoveryMs = now;
      if (!gSdReady && initSdCard()) {
        gSdReady = startNewSession();
      }
      if (!gCameraReady) {
        gCameraReady = initCamera(gJpegQuality);
      }
    }
    delay(10);
    return;
  }

  if (gSnapRequested) {
    gSnapRequested = false;
    lastCaptureMs = now;
    captureSingleFrame(now);
    return;
  }

  if (gPaused) {
    delay(10);
    return;
  }

  if (now - lastCaptureMs < gCaptureIntervalMs) {
    delay(2);
    return;
  }
  lastCaptureMs = now;

  captureSingleFrame(now);
}
