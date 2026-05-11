# PROJECT: PocketCloud Terminal
## M5Stack Cardputer v1.1 AWS EC2 Pocket SSH Launcher

---

# Objective

Build a production-grade handheld cloud terminal for the M5Stack Cardputer v1.1 that can:

- Connect to Wi-Fi
- Authenticate securely to a backend API
- Start a stopped AWS EC2 instance
- Poll EC2 until running
- Retrieve public IP
- Establish SSH connection
- Provide lightweight terminal shell access
- Accept keyboard input from Cardputer
- Stop EC2 instance from device
- Recover from network/API/SSH failures

This project MUST be implemented incrementally.

Every milestone must:

- compile successfully
- be testable independently
- avoid placeholder logic
- avoid pseudo-code
- avoid skipping dependencies

DO NOT proceed to the next milestone until the current milestone is fully working.

---

# Hardware Target

Primary hardware:

M5Stack Cardputer v1.1

Core module:

STAMP-S3A

MCU:

ESP32-S3FN8

Specifications:

- 8MB Flash
- constrained RAM environment
- integrated Wi-Fi
- ST7789V2 TFT display
- 56-key keyboard
- speaker
- microSD support
- battery-powered portable device

IMPORTANT:

This is a resource-constrained embedded device.

All architecture decisions MUST respect memory limitations.

---

# Design Philosophy

This is NOT a desktop terminal emulator.

The implementation MUST be lightweight.

Rules:

- minimize heap usage
- avoid large buffers
- avoid full ANSI parser
- avoid unnecessary dynamic allocations
- prefer ring buffers
- prefer fixed-size structs
- non-blocking workflows where practical

---

# Architecture

System flow:

Cardputer
→ Wi-Fi
→ HTTPS backend API
→ AWS Lambda
→ AWS EC2 APIs
→ target EC2 instance
→ SSH shell session

Security principle:

AWS credentials MUST NEVER be stored on the Cardputer.

---

# Preferred Runtime Strategy

Primary mode:

Lightweight SSH terminal

Fallback mode:

Command relay backend mode

If memory profiling shows SSH instability:

Switch architecture to:

Cardputer
→ HTTPS backend
→ backend executes commands on EC2
→ returns stdout

GitHub Copilot MUST support fallback architecture.

---

# Tech Stack

## Firmware

Language:

C++

Framework:

Arduino

Build system:

PlatformIO

Board target:

m5stack-stamps3

Platform:

espressif32

Required libraries:

- M5Unified
- M5GFX
- WiFi
- WiFiClientSecure
- HTTPClient
- ArduinoJson
- Preferences
- FreeRTOS

Conditional library:

- wolfSSH

wolfSSH MUST only be used if memory usage remains stable.

---

# Device APIs

Use exact Cardputer APIs.

Initialization:

```cpp
M5Cardputer.begin();