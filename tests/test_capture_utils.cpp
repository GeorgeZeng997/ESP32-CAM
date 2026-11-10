#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CAMERA_MODEL_ESP32S3_EYE
#include "../src/camera_pins.h"
#include "../src/camera_pins.h"
#include "../include/capture_utils.h"

int main() {
  using namespace capture_utils;

  // 1. Interval & JPEG quality clamping
  assert(clampCaptureIntervalMs(0) == kMinCaptureIntervalMs);
  assert(clampCaptureIntervalMs(200) == 200U);
  assert(clampCaptureIntervalMs(9999999U) == kMaxCaptureIntervalMs);

  assert(clampJpegQuality(0) == kMinJpegQuality);
  assert(clampJpegQuality(12) == 12);
  assert(clampJpegQuality(99) == kMaxJpegQuality);

  // 2. Overflow-safe free space check
  const uint64_t reserve = 2ULL * 1024ULL * 1024ULL;
  assert(!hasEnoughSpaceForFrame(0ULL, 1024U, reserve));
  assert(!hasEnoughSpaceForFrame(reserve, 0U, reserve));
  assert(hasEnoughSpaceForFrame(reserve + 4096ULL, 4096U, reserve));
  assert(!hasEnoughSpaceForFrame(reserve + 4095ULL, 4096U, reserve));

  // 3. JPEG buffer validation
  const uint8_t validJpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
  const uint8_t badMagic[] = {0x89, 0x50, 0x4E, 0x47};
  assert(isValidJpegFrame(validJpeg, sizeof(validJpeg)));
  assert(!isValidJpegFrame(validJpeg, 2U));
  assert(!isValidJpegFrame(badMagic, sizeof(badMagic)));
  assert(!isValidJpegFrame(nullptr, 16U));

  // 4. Run directory parsing & formatting
  uint32_t idx = 0;
  assert(parseRunDirIndex("run_0001", &idx) && idx == 1U);
  assert(parseRunDirIndex("/video/run_0042", &idx) && idx == 42U);
  assert(parseRunDirIndex("run_123456", &idx) && idx == 123456U);
  assert(!parseRunDirIndex("run_abcd", &idx));
  assert(!parseRunDirIndex("run_", &idx));
  assert(!parseRunDirIndex("video_0001", &idx));
  assert(!parseRunDirIndex(nullptr, &idx));

  char pathBuf[64];
  assert(formatRunDirPath(pathBuf, sizeof(pathBuf), "/video", 7U));
  assert(strcmp(pathBuf, "/video/run_0007") == 0);
  assert(!formatRunDirPath(pathBuf, 8U, "/video", 7U));

  assert(formatFramePath(pathBuf, sizeof(pathBuf), "/video/run_0007", 15U));
  assert(strcmp(pathBuf, "/video/run_0007/frame_000015.jpg") == 0);
  assert(!formatFramePath(pathBuf, 12U, "/video/run_0007", 15U));

  // 5. Serial CLI parser
  assert(parseSerialCommand("  \r\n").type == SerialCmdType::kNone);
  assert(parseSerialCommand("status").type == SerialCmdType::kStatus);
  assert(parseSerialCommand("STATUS extra").type == SerialCmdType::kInvalid);
  assert(parseSerialCommand("pause").type == SerialCmdType::kPause);
  assert(parseSerialCommand("RESUME\r\n").type == SerialCmdType::kResume);
  assert(parseSerialCommand("snap").type == SerialCmdType::kSnap);
  assert(parseSerialCommand("NEW").type == SerialCmdType::kNewSession);
  assert(parseSerialCommand("reinit").type == SerialCmdType::kReinit);
  assert(parseSerialCommand("?").type == SerialCmdType::kHelp);

  SerialCommand iv = parseSerialCommand("INTERVAL 500");
  assert(iv.type == SerialCmdType::kInterval && iv.value == 500U);

  SerialCommand ivClamp = parseSerialCommand("interval 10");
  assert(ivClamp.type == SerialCmdType::kInterval && ivClamp.value == kMinCaptureIntervalMs);
  assert(parseSerialCommand("INTERVAL abc").type == SerialCmdType::kInvalid);
  assert(parseSerialCommand("INTERVAL").type == SerialCmdType::kInvalid);

  SerialCommand q = parseSerialCommand("quality 14");
  assert(q.type == SerialCmdType::kQuality && q.value == 14U);
  SerialCommand qClamp = parseSerialCommand("QUALITY 1");
  assert(qClamp.type == SerialCmdType::kQuality && qClamp.value == static_cast<uint32_t>(kMinJpegQuality));

  return 0;
}
