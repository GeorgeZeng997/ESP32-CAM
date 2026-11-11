# ESP32-CAM SD Recorder

PlatformIO / Arduino firmware for **ESP32-S3 EYE** (and pin-compatible Freenove ESP32-S3 WROOM CAM boards) that continuously captures JPEG frames from the onboard camera sensor and writes sequential frames plus timing metadata to a microSD card over 1-bit `SD_MMC`.

## Key Features

- **Collision-Free Session Folders**: Scans `/video` on boot and allocates the next monotonic session directory (`/video/run_0001`, `/video/run_0002`, ...) instead of risking 16-bit random folder collisions or overwriting existing captures.
- **Per-Frame Timing Manifest (`manifest.csv`)**: Appends `frame_index,filename,uptime_ms,bytes,width,height` for every verified JPEG frame so variable-cadence recordings convert cleanly to real-time MP4 video.
- **Atomic / Validated Frame Writes**: Verifies JPEG SOI headers (`0xFF 0xD8`), enforces a 2 MB free-space reserve on the SD card, and removes any partially written file if an SD write error occurs.
- **Background Auto-Recovery**: Keeps the serial console responsive and retries SD card or camera initialization every 5 seconds instead of entering an uninitialized capture loop when hardware is missing at boot.
- **Interactive 115200-Baud Serial CLI**: Live JSON status telemetry, pause/resume, single-shot snapshot, runtime interval/quality tuning, and new session rollover.
- **Host MP4 Converter (`tools/frames_to_mp4.py`)**: Converts any `/video/run_XXXX` directory into an H.264 `.mp4` using exact `manifest.csv` frame durations via `ffmpeg`.

## Hardware Pinout (`CAMERA_MODEL_ESP32S3_EYE`)

| Peripheral | Signal | ESP32-S3 GPIO |
| :--- | :--- | :--- |
| **SD_MMC (1-bit)** | `CLK` / `CMD` / `D0` | `GPIO39` / `GPIO38` / `GPIO40` |
| **Camera Clock & SCCB** | `XCLK` / `SIOD` (`SDA`) / `SIOC` (`SCL`) | `GPIO15` / `GPIO4` / `GPIO5` |
| **Camera Sync** | `VSYNC` / `HREF` / `PCLK` | `GPIO6` / `GPIO7` / `GPIO13` |
| **Camera Data (`D0..D7`)** | `Y2..Y9` | `11, 9, 8, 10, 12, 18, 17, 16` |

> **PSRAM Note**: `platformio.ini` defaults to `board_build.arduino.memory_type = qio_opi` (`8MB` flash) for ESP32-S3-EYE V2.2 / Freenove ESP32-S3 N8R8 / N16R8 Octal PSRAM modules (enabling `SVGA` 800x600 triple-buffered PSRAM capture). Change to `qio_qspi` if targeting a Quad PSRAM module, or run without PSRAM in automatic `VGA` DRAM fallback mode.

## Build & Flash

```bash
# Build firmware
pio run -e esp32s3_save_to_sd

# Flash and open serial monitor (115200 baud)
pio run -e esp32s3_save_to_sd --target upload
pio device monitor --baud 115200
```

## Serial CLI Commands (`115200 8N1`)

| Command | Description |
| :--- | :--- |
| `STATUS` | Print single-line JSON status (`sd_ready`, `camera_ready`, `paused`, `session_dir`, `frames_saved`, `frames_dropped`, `interval_ms`, `jpeg_quality`, `free_mb`, `free_psram`, `uptime_ms`) |
| `PAUSE` | Pause continuous JPEG recording |
| `RESUME` | Resume continuous JPEG recording |
| `SNAP` | Trigger an immediate single-frame capture (works even while paused) |
| `INTERVAL <ms>` | Set capture interval in milliseconds (clamped to `40..3600000` ms) |
| `QUALITY <4..63>` | Adjust sensor JPEG compression quality (`4` = highest quality, `63` = smallest size) |
| `NEW` | Start a new `/video/run_XXXX` session folder with a fresh `manifest.csv` |
| `REINIT` | Re-mount `SD_MMC` and re-probe camera hardware |
| `HELP` | Print command summary |

## SD Card Directory Layout

```text
/video/
  run_0001/
    manifest.csv
    frame_000000.jpg
    frame_000001.jpg
    ...
```

## Exporting a Session to MP4 (`tools/frames_to_mp4.py`)

Requires `ffmpeg` in `PATH` and Python 3.9+:

```bash
# Encode using exact per-frame timestamps from manifest.csv
python tools/frames_to_mp4.py /path/to/sdcard/video/run_0001 -o run_0001.mp4

# Override with a fixed constant frame rate (e.g. 10 fps timelapse)
python tools/frames_to_mp4.py /path/to/sdcard/video/run_0001 --fps 10 -o timelapse.mp4
```

## Running Host Unit Tests

```bash
g++ -std=c++17 -Wall -Wextra -Werror tests/test_capture_utils.cpp -o test_capture_utils && ./test_capture_utils
python -m unittest discover -s tests -v
```
