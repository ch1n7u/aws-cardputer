#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// M5Stack Cardputer v1.1 Hardware Configuration
// STAMP-S3A module with ESP32-S3FN8 MCU

// Display Configuration
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 136
#define DISPLAY_TFT_TYPE TFT_ST7789V2

// Memory Constraints
#define MAX_BUFFER_SIZE 4096  // Respect 8MB flash, constrained RAM
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_API_RESPONSE 2048

// Battery
#define BATTERY_CHECK_INTERVAL_MS 10000  // Check battery every 10 seconds

// Keyboard
#define KEYBOARD_SCAN_INTERVAL_MS 50  // Scan keyboard every 50ms

// WiFi
#define WIFI_CONNECT_TIMEOUT_MS 30000  // 30 second timeout for WiFi connection

// EC2 proxy API (set these to your deployed API and token)
#define EC2_PROXY_URL "https://REPLACE_WITH_API.execute-api.REGION.amazonaws.com/Prod"
#define EC2_PROXY_TOKEN "" // set device token or store in Preferences/SD and load at runtime
#define MAX_INSTANCES 12

// Feature Flags
#define ENABLE_BATTERY_MONITORING 1
#define ENABLE_KEYBOARD_INPUT 1
#define ENABLE_DISPLAY_OUTPUT 1

#endif // HARDWARE_CONFIG_H
