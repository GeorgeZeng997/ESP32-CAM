#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace capture_utils {

constexpr uint32_t kMinCaptureIntervalMs = 40U;       // Max ~25 fps
constexpr uint32_t kMaxCaptureIntervalMs = 3600000U;  // Max 1 frame/hour
constexpr int kMinJpegQuality = 4;                    // Highest JPEG quality
constexpr int kMaxJpegQuality = 63;                   // Lowest JPEG quality

inline uint32_t clampCaptureIntervalMs(uint32_t intervalMs) {
  if (intervalMs < kMinCaptureIntervalMs) {
    return kMinCaptureIntervalMs;
  }
  if (intervalMs > kMaxCaptureIntervalMs) {
    return kMaxCaptureIntervalMs;
  }
  return intervalMs;
}

inline int clampJpegQuality(int quality) {
  if (quality < kMinJpegQuality) {
    return kMinJpegQuality;
  }
  if (quality > kMaxJpegQuality) {
    return kMaxJpegQuality;
  }
  return quality;
}

// Overflow-safe check ensuring `minReserveBytes` remains free after writing `frameBytes`.
inline bool hasEnoughSpaceForFrame(uint64_t freeBytes, size_t frameBytes, uint64_t minReserveBytes) {
  if (freeBytes <= minReserveBytes) {
    return false;
  }
  return (freeBytes - minReserveBytes) >= static_cast<uint64_t>(frameBytes);
}

// Basic sanity check for non-empty JPEG buffer starting with SOI marker (0xFF 0xD8).
inline bool isValidJpegFrame(const uint8_t *data, size_t len) {
  if (data == nullptr || len < 4U) {
    return false;
  }
  return data[0] == 0xFFU && data[1] == 0xD8U;
}

// Parses a directory entry name like "run_0007" or "/video/run_0042" into `outIndex`.
// Only accepts pure decimal digits after "run_" (1..8 digits) to ignore legacy hex folders if non-decimal.
inline bool parseRunDirIndex(const char *pathOrName, uint32_t *outIndex) {
  if (pathOrName == nullptr || outIndex == nullptr) {
    return false;
  }
  const char *base = pathOrName;
  for (const char *p = pathOrName; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  if (strncmp(base, "run_", 4) != 0) {
    return false;
  }
  const char *digits = base + 4;
  if (*digits == '\0') {
    return false;
  }
  uint64_t acc = 0;
  size_t count = 0;
  for (const char *p = digits; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    acc = acc * 10ULL + static_cast<uint64_t>(*p - '0');
    ++count;
    if (count > 8U || acc > 99999999ULL) {
      return false;
    }
  }
  *outIndex = static_cast<uint32_t>(acc);
  return true;
}

inline bool formatRunDirPath(char *buf, size_t bufSize, const char *baseDir, uint32_t runIndex) {
  if (buf == nullptr || bufSize == 0U || baseDir == nullptr || baseDir[0] == '\0') {
    return false;
  }
  int written = snprintf(buf, bufSize, "%s/run_%04lu", baseDir, static_cast<unsigned long>(runIndex));
  return written > 0 && static_cast<size_t>(written) < bufSize;
}

inline bool formatFramePath(char *buf, size_t bufSize, const char *dirPath, uint32_t frameIndex) {
  if (buf == nullptr || bufSize == 0U || dirPath == nullptr || dirPath[0] == '\0') {
    return false;
  }
  int written = snprintf(buf, bufSize, "%s/frame_%06lu.jpg", dirPath, static_cast<unsigned long>(frameIndex));
  return written > 0 && static_cast<size_t>(written) < bufSize;
}

enum class SerialCmdType : uint8_t {
  kNone = 0,
  kStatus,
  kPause,
  kResume,
  kSnap,
  kInterval,
  kQuality,
  kNewSession,
  kReinit,
  kHelp,
  kInvalid
};

struct SerialCommand {
  SerialCmdType type;
  uint32_t value;
};

inline bool parseUnsignedArg(const char *str, uint32_t *outVal) {
  if (str == nullptr || outVal == nullptr) {
    return false;
  }
  while (*str == ' ' || *str == '\t') {
    ++str;
  }
  if (*str == '\0') {
    return false;
  }
  uint64_t acc = 0;
  size_t digits = 0;
  while (*str >= '0' && *str <= '9') {
    acc = acc * 10ULL + static_cast<uint64_t>(*str - '0');
    if (acc > 0xFFFFFFFFULL) {
      return false;
    }
    ++digits;
    ++str;
  }
  while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
    ++str;
  }
  if (digits == 0U || *str != '\0') {
    return false;
  }
  *outVal = static_cast<uint32_t>(acc);
  return true;
}

inline bool tokenEqualsIgnoreCase(const char *start, size_t len, const char *upperLiteral) {
  size_t litLen = strlen(upperLiteral);
  if (len != litLen) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = static_cast<unsigned char>(start[i]);
    if (toupper(c) != static_cast<unsigned char>(upperLiteral[i])) {
      return false;
    }
  }
  return true;
}

inline SerialCommand parseSerialCommand(const char *line) {
  SerialCommand result{SerialCmdType::kNone, 0U};
  if (line == nullptr) {
    return result;
  }
  while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
    ++line;
  }
  if (*line == '\0') {
    return result;
  }

  const char *cmdStart = line;
  while (*line != '\0' && *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
    ++line;
  }
  size_t cmdLen = static_cast<size_t>(line - cmdStart);

  const char *rest = line;
  while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n') {
    ++rest;
  }

  if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "STATUS")) {
    result.type = (*rest == '\0') ? SerialCmdType::kStatus : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "PAUSE")) {
    result.type = (*rest == '\0') ? SerialCmdType::kPause : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "RESUME")) {
    result.type = (*rest == '\0') ? SerialCmdType::kResume : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "SNAP")) {
    result.type = (*rest == '\0') ? SerialCmdType::kSnap : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "NEW")) {
    result.type = (*rest == '\0') ? SerialCmdType::kNewSession : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "REINIT")) {
    result.type = (*rest == '\0') ? SerialCmdType::kReinit : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "HELP") || tokenEqualsIgnoreCase(cmdStart, cmdLen, "?")) {
    result.type = (*rest == '\0') ? SerialCmdType::kHelp : SerialCmdType::kInvalid;
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "INTERVAL")) {
    uint32_t val = 0;
    if (parseUnsignedArg(line, &val)) {
      result.type = SerialCmdType::kInterval;
      result.value = clampCaptureIntervalMs(val);
    } else {
      result.type = SerialCmdType::kInvalid;
    }
  } else if (tokenEqualsIgnoreCase(cmdStart, cmdLen, "QUALITY")) {
    uint32_t val = 0;
    if (parseUnsignedArg(line, &val)) {
      result.type = SerialCmdType::kQuality;
      result.value = static_cast<uint32_t>(clampJpegQuality(static_cast<int>(val)));
    } else {
      result.type = SerialCmdType::kInvalid;
    }
  } else {
    result.type = SerialCmdType::kInvalid;
  }

  return result;
}

}  // namespace capture_utils
