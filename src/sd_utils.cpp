#include "sd_utils.h"

#include "capture_utils.h"

static const int kSdClkPin = 39;
static const int kSdCmdPin = 38;
static const int kSdData0Pin = 40;

bool initSdCard() {
  SD_MMC.end();
  if (!SD_MMC.setPins(kSdClkPin, kSdCmdPin, kSdData0Pin)) {
    Serial.println("SD_MMC pin configuration failed");
    return false;
  }
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("Card mount failed");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    SD_MMC.end();
    return false;
  }

  Serial.print("SD_MMC Card Type: ");
  switch (cardType) {
    case CARD_MMC: Serial.println("MMC"); break;
    case CARD_SD: Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default: Serial.println("UNKNOWN"); break;
  }

  Serial.printf("Card size: %lluMB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024ULL * 1024ULL));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024ULL * 1024ULL));
  return true;
}

bool ensureDir(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (SD_MMC.exists(path)) {
    return true;
  }
  bool created = SD_MMC.mkdir(path);
  if (!created) {
    Serial.printf("Failed to create dir: %s\n", path);
  }
  return created;
}

bool initManifestFile(const char *dirPath) {
  if (dirPath == nullptr || dirPath[0] == '\0') {
    return false;
  }
  char manifestPath[112];
  int n = snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.csv", dirPath);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(manifestPath)) {
    return false;
  }
  if (SD_MMC.exists(manifestPath)) {
    return true;
  }
  File file = SD_MMC.open(manifestPath, FILE_WRITE);
  if (!file) {
    return false;
  }
  static const char kHeader[] = "frame_index,filename,uptime_ms,bytes,width,height\n";
  size_t written = file.write(reinterpret_cast<const uint8_t *>(kHeader), sizeof(kHeader) - 1U);
  file.close();
  return written == (sizeof(kHeader) - 1U);
}

bool appendManifestRow(
    const char *dirPath,
    uint32_t frameIndex,
    uint32_t uptimeMs,
    size_t len,
    size_t width,
    size_t height) {
  if (dirPath == nullptr || dirPath[0] == '\0') {
    return false;
  }
  char manifestPath[112];
  int n = snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.csv", dirPath);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(manifestPath)) {
    return false;
  }
  File file = SD_MMC.open(manifestPath, FILE_APPEND);
  if (!file) {
    return false;
  }
  char row[128];
  int rowLen = snprintf(
      row,
      sizeof(row),
      "%lu,frame_%06lu.jpg,%lu,%u,%u,%u\n",
      static_cast<unsigned long>(frameIndex),
      static_cast<unsigned long>(frameIndex),
      static_cast<unsigned long>(uptimeMs),
      static_cast<unsigned>(len),
      static_cast<unsigned>(width),
      static_cast<unsigned>(height));
  if (rowLen <= 0 || static_cast<size_t>(rowLen) >= sizeof(row)) {
    file.close();
    return false;
  }
  size_t written = file.write(reinterpret_cast<const uint8_t *>(row), static_cast<size_t>(rowLen));
  file.close();
  return written == static_cast<size_t>(rowLen);
}

bool allocateNextRunDir(const char *baseDir, String &outRunDir) {
  if (!ensureDir(baseDir)) {
    return false;
  }

  uint32_t maxIndex = 0;
  File root = SD_MMC.open(baseDir);
  if (root && root.isDirectory()) {
    File entry = root.openNextFile();
    while (entry) {
      if (entry.isDirectory()) {
        uint32_t parsed = 0;
        if (capture_utils::parseRunDirIndex(entry.name(), &parsed) && parsed > maxIndex) {
          maxIndex = parsed;
        }
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
  } else if (root) {
    root.close();
  }

  for (uint32_t offset = 1; offset <= 1024U; ++offset) {
    uint32_t candidateIdx = maxIndex + offset;
    char candidate[96];
    if (!capture_utils::formatRunDirPath(candidate, sizeof(candidate), baseDir, candidateIdx)) {
      return false;
    }
    if (!SD_MMC.exists(candidate) && SD_MMC.mkdir(candidate)) {
      initManifestFile(candidate);
      outRunDir = candidate;
      return true;
    }
  }

  return false;
}

uint64_t sdFreeBytes() {
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  return (total > used) ? (total - used) : 0;
}

bool saveJpegFrame(const char *dirPath, uint32_t frameIndex, const uint8_t *data, size_t len, String &savedPath) {
  if (!capture_utils::isValidJpegFrame(data, len)) {
    Serial.printf("Rejected invalid JPEG buffer (%u bytes)\n", static_cast<unsigned>(len));
    return false;
  }

  char path[96];
  if (!capture_utils::formatFramePath(path, sizeof(path), dirPath, frameIndex)) {
    Serial.println("Frame path buffer overflow");
    return false;
  }

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open %s for write\n", path);
    return false;
  }

  size_t written = file.write(data, len);
  file.close();

  if (written != len) {
    Serial.printf("Write incomplete (%u/%u), removing partial file\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(len));
    SD_MMC.remove(path);
    return false;
  }

  savedPath = path;
  return true;
}
