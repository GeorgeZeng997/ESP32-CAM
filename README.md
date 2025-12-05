# ESP32-CAM SD Recorder

PlatformIO/Arduino project for ESP32-S3 EYE that continually grabs frames from the onboard OV2640 camera and stores sequential JPEGs to an SD card (SD_MMC).

## What it does
- Mounts SD_MMC using the ESP32-S3 EYE pinout (CLK 39, CMD 38, D0 40).
- Sets up the camera, enabling PSRAM for higher resolution and extra frame buffers when available.
- Captures a frame roughly every 200 ms, writes it as `frame_XXXXXX.jpg`, and creates a new `/video/run_<id>` folder on each boot.
- Skips captures when remaining space is below 2 MB to avoid filling the card.

## Hardware
- Board: ESP32-S3 EYE (also matches Freenove ESP32-S3 CAM SD wiring).
- SD card: formatted FAT32, wired to CLK 39, CMD 38, D0 40 (1-bit SD_MMC).
- PSRAM is recommended for SVGA frame size and smoother capture.

## Prerequisites
- PlatformIO Core (`platformio` CLI) or VS Code with the PlatformIO extension.
- USB cable for flashing, microSD card for storage.

## Build and flash
```bash
platformio run --target upload
platformio device monitor --baud 115200
```

## SD card layout
Each boot creates a unique folder and fills it with sequential JPEGs:
```
/video/
  run_abcd/
    frame_000000.jpg
    frame_000001.jpg
    ...
```

## Configuration
- Capture cadence and free-space guard: `kCaptureIntervalMs` and `kMinimumFreeSpace` in `src/main.cpp`.
- Image size/quality and buffer count: camera `config` in `initCamera()` within `src/main.cpp`.
- Camera/SD pin map: `CAMERA_MODEL_ESP32S3_EYE` in `src/camera_pins.h`.
- Flash layout: `partitions.csv`.

## Publish to GitHub
```bash
git init -b main
git add .
git commit -m "Initial commit"
git remote add origin git@github.com:GeorgeZeng997/ESP32-CAM.git
git push -u origin main
```
