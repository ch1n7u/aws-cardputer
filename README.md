# PocketCloud Terminal

**M5Stack Cardputer v1.1 AWS EC2 Pocket SSH Launcher**

A lightweight handheld cloud terminal firmware for the M5Stack Cardputer v1.1 that enables:
- Wi-Fi connectivity
- AWS API authentication
- EC2 instance management (start/stop)
- SSH shell access
- Keyboard input and display output

## Hardware

- **Device**: M5Stack Cardputer v1.1
- **MCU**: ESP32-S3 (STAMP-S3A)
- **RAM**: Constrained (8MB Flash)
- **Display**: ST7789V2 TFT (160x136)
- **Input**: 56-key keyboard
- **Power**: Battery with monitoring API

## Build System

**PlatformIO** with Arduino framework targeting `m5stack-stamps3`

### Build Commands

```bash
# Install dependencies
platformio lib install

# Build firmware
platformio run

# Build and upload to device
platformio run --target upload

# Monitor serial output
platformio run --target monitor
```

## Project Structure

```
src/
  main.cpp                 # Main firmware entry point
include/
  hardware_config.h        # Hardware constants and configuration
lib/                       # Custom libraries (local)
test/                      # Unit tests (future)
platformio.ini             # PlatformIO build configuration
```

## Dependencies

### Required (Milestone 1)
- **M5Unified** v0.1+ - Unified M5Stack device API
- **M5GFX** v0.1+ - Graphics library for M5Stack
- **ArduinoJson** v6.21+ - JSON parsing

### Built-in (ESP32 Core)
- WiFi - Network connectivity
- WiFiClientSecure - Secure connections
- HTTPClient - HTTP/HTTPS communication
- Preferences - Non-volatile storage
- FreeRTOS - Real-time OS

### Conditional (Future)
- wolfSSH - Lightweight SSH (only if memory stable)

## Milestone 1: Project Initialization ✓

- [x] Initialize PlatformIO project for Cardputer v1.1
- [x] Create correct platformio.ini with dependencies
- [x] Add M5Unified, M5GFX libraries
- [x] Create project folder structure
- [x] Implement minimal compilable firmware:
  - [x] Initialize M5Cardputer
  - [x] Initialize display (ST7789V2)
  - [x] Initialize keyboard (56-key)
  - [x] Initialize battery API
  - [x] Display "PocketCloud Terminal"
- [x] Ensure clean compilation

## Next Steps

- Milestone 2: Wi-Fi connectivity and SSID scanning
- Milestone 3: Backend API authentication
- Milestone 4: EC2 control and polling
- Milestone 5: SSH terminal implementation
