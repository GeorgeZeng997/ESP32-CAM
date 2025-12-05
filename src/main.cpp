#include <Arduino.h>
#include "esp_camera.h"
#include "esp_system.h"

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"
#include "sd_utils.h"

// Capture every N milliseconds to avoid starving the SD bus.
static const uint32_t kCaptureIntervalMs = 200; // ~5 fps
static const uint64_t kMinimumFreeSpace = 2ULL * 1024ULL * 1024ULL; // keep 2MB headroom

static String sessionDir = "/video";

bool initCamera() {
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
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 3;
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.println("PSRAM found and used");
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    Serial.println("PSRAM not found; using DRAM frame buffer");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-S3 camera to SD recorder");

  if (!initSdCard()) {
    Serial.println("SD init failed; halt");
    return;
  }

  if (!ensureDir("/video")) {
    Serial.println("Failed to create /video directory");
    return;
  }

  // Unique folder for this boot session.
  char runDir[48];
  snprintf(runDir, sizeof(runDir), "/video/run_%04lx", (unsigned long)(esp_random() & 0xFFFF));
  if (ensureDir(runDir)) {
    sessionDir = runDir;
  }

  if (!initCamera()) {
    Serial.println("Camera init failed; halt");
    return;
  }

  Serial.printf("Recording frames into %s\n", sessionDir.c_str());
}

void loop() {
  static uint32_t frameId = 0;
  static uint32_t lastCaptureMs = 0;

  uint32_t now = millis();
  if (now - lastCaptureMs < kCaptureIntervalMs) {
    delay(5);
    return;
  }
  lastCaptureMs = now;

  uint64_t freeBytes = sdFreeBytes();
  if (freeBytes < kMinimumFreeSpace) {
    Serial.println("SD space low, waiting...");
    delay(500);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(50);
    return;
  }

  freeBytes = sdFreeBytes();
  if (freeBytes < fb->len + kMinimumFreeSpace) {
    Serial.println("Not enough space for this frame");
    esp_camera_fb_return(fb);
    delay(200);
    return;
  }

  String savedPath;
  if (saveJpegFrame(sessionDir.c_str(), frameId++, fb->buf, fb->len, savedPath)) {
    Serial.printf("Saved %s (%u bytes)\n", savedPath.c_str(), (unsigned)fb->len);
  } else {
    Serial.println("Failed to write frame");
  }

  esp_camera_fb_return(fb);
}
