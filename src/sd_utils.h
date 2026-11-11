#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

// Initializes SD_MMC in 1-bit mode using the ESP32-S3 EYE pinout. Returns true on success.
bool initSdCard();

// Ensures the directory exists (creates it if missing).
bool ensureDir(const char *path);

// Allocates a collision-free monotonic session directory (e.g. "/video/run_0001") and initializes manifest.csv.
bool allocateNextRunDir(const char *baseDir, String &outRunDir);

// Initializes manifest.csv header inside `dirPath` if not already present.
bool initManifestFile(const char *dirPath);

// Appends frame timing & geometry metadata to `<dirPath>/manifest.csv`.
bool appendManifestRow(
    const char *dirPath,
    uint32_t frameIndex,
    uint32_t uptimeMs,
    size_t len,
    size_t width,
    size_t height);

// Remaining usable space on the card in bytes.
uint64_t sdFreeBytes();

// Saves a validated JPEG frame into `dirPath` using an incremental filename (`frame_%06lu.jpg`).
// Unlinks any partial file if writing fails. Returns true on success and populates `savedPath`.
bool saveJpegFrame(const char *dirPath, uint32_t frameIndex, const uint8_t *data, size_t len, String &savedPath);
