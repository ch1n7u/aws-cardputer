# PocketCloud Terminal

## M5Stack Cardputer v1.1 AWS EC2 Pocket SSH Launcher

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
- **Display**: ST7789V2 TFT (240x135)
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

# Run the EC2 proxy Lambda tests
python -m pytest lambda/ec2_proxy/tests/test_handler.py
```

## Project Structure

```text
template.yaml                # Root AWS SAM template for the EC2 proxy Lambda
src/
  main.cpp                 # Main firmware entry point
include/
  hardware_config.h        # Hardware constants and configuration
lambda/
  ec2_proxy/
    handler.py             # EC2 proxy Lambda handler
    tests/                 # Lambda tests
    README.md              # Lambda deployment notes
    deploy.ps1             # Windows deployment helper
lib/                       # Custom libraries (local)
test/                      # Firmware tests (future)
platformio.ini             # PlatformIO build configuration
README.md                  # Project overview and setup
```

## Dependencies

### Firmware Libraries

- **M5Cardputer** v1.1.1 - Cardputer hardware abstraction and keyboard/input
- **M5Unified** v0.2.14 - Unified M5Stack device API
- **M5GFX** v0.2.20 - Graphics and sprite rendering
- **ArduinoJson** v6.21.6 - JSON parsing for EC2 proxy responses

### Built-in (ESP32 Core)

- WiFi - Network connectivity
- WiFiClientSecure - Secure connections
- HTTPClient - HTTP/HTTPS communication
- Preferences - Non-volatile storage
- SD / SPI - optional SD card configuration storage
- WebServer - local config/status web UI
- FreeRTOS - Real-time OS

### Lambda Runtime

- **Python 3.11** - AWS SAM Lambda runtime
- **boto3** - Provided by the AWS Lambda runtime for EC2/DynamoDB access
- **pytest** - Only needed locally for running `lambda/ec2_proxy/tests/test_handler.py`

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

## AWS Deployment

The EC2 proxy backend is defined in the root [template.yaml](template.yaml) and points to `lambda/ec2_proxy/` as the Lambda code directory.

Use the Lambda folder for deployment and tests:

- `lambda/ec2_proxy/deploy.ps1` for Windows deployment
- `lambda/ec2_proxy/tests/test_handler.py` for local test coverage
