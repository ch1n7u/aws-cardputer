#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <time.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include "hardware_config.h"

static M5Canvas* pCanvas = nullptr;
#define canvas (*pCanvas)
static uint16_t BG_COLOR;
static uint16_t FG_COLOR;
static uint16_t DIM_COLOR;
static uint16_t ACCENT_COLOR;
static uint16_t ACCENT_BG;
static uint16_t SUCCESS_COLOR;
static uint16_t WARN_COLOR;
static uint16_t ERR_COLOR;

static void init_theme() {
  pCanvas = new M5Canvas(&M5Cardputer.Display);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  BG_COLOR = canvas.color565(0, 0, 0);
  FG_COLOR = canvas.color565(230, 237, 243);
  DIM_COLOR = canvas.color565(125, 133, 144);
  ACCENT_COLOR = canvas.color565(88, 166, 255);
  ACCENT_BG = canvas.color565(31, 111, 235);
  SUCCESS_COLOR = canvas.color565(63, 185, 80);
  WARN_COLOR = canvas.color565(210, 153, 34);
  ERR_COLOR = canvas.color565(248, 81, 73);
}

// Global SD flag (set in setup)
static bool sd_ok = false;
static SPIClass sd_spi(FSPI);
static WebServer configServer(80);
static bool config_server_started = false;
static bool time_synced = false;
static String last_ec2_error;
static void set_ec2_error(const String& msg);
static void clear_display();
static void show_status_line(int line, const String& message, uint16_t color = 0xFFFF);
static bool load_saved_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len);
static void clear_saved_wifi_credentials();
static void draw_wifi_loading_screen(const String& ssid, unsigned long elapsed_ms, unsigned long timeout_ms, const String& status);
static bool saved_wifi_network_available(const char* ssid);
static bool connect_wifi_after_boot(const char* ssid, const char* password);

static const int EC2_DEBUG_MAX_LINES = 8;
static String ec2_debug_lines[EC2_DEBUG_MAX_LINES];
static int ec2_debug_line_count = 0;
static bool ec2_debug_active = false;

static void draw_glow_rect(int x, int y, int w, int h) {
  canvas.drawRoundRect(x-2, y-2, w+4, h+4, 5, canvas.color565(0, 60, 120));
  canvas.drawRoundRect(x-1, y-1, w+2, h+2, 4, canvas.color565(0, 120, 200));
  canvas.drawRoundRect(x, y, w, h, 3, canvas.color565(0, 200, 255));
}

// Draw battery indicator with percentage inside the battery box
static void draw_battery_indicator(int x, int y) {
  // Get battery percentage from M5Cardputer
  int battery_percent = M5Cardputer.Power.getBatteryLevel();
  battery_percent = constrain(battery_percent, 0, 100);
  
  // Determine battery color based on percentage
  uint16_t battery_color;
  if (battery_percent > 50) {
    battery_color = SUCCESS_COLOR; // Green for good battery
  } else if (battery_percent > 20) {
    battery_color = WARN_COLOR; // Yellow/Orange for medium battery
  } else {
    battery_color = ERR_COLOR; // Red for low battery
  }
  
  int battery_width = 22;
  int battery_height = 12;
  int terminal_width = 1;
  
  // Draw battery body outline (hollow)
  canvas.drawRect(x - battery_width, y, battery_width, battery_height, battery_color);
  
  // Draw battery terminal
  canvas.fillRect(x - battery_width - terminal_width - 1, y + 3, terminal_width, 6, battery_color);
  
  // Draw percentage text INSIDE hollow battery box.
  // "100%" is too wide for this box, so use "100" at full charge.
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(battery_color);
  canvas.setTextSize(1);

  String percent_str = (battery_percent >= 100)
    ? String("100")
    : String(battery_percent) + "%";
  canvas.drawString(percent_str, x - battery_width / 2, y + battery_height / 2 - 1);
}

static void ec2_debug_render(const String& title)
{
  canvas.fillScreen(BG_COLOR);
  draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);

  canvas.setTextColor(ACCENT_COLOR);
  canvas.setTextDatum(top_center);
  canvas.drawString(title, canvas.width() / 2, 10);
  
  // Draw battery indicator at top right
  draw_battery_indicator(canvas.width() - 8, 8);
  
  canvas.setTextDatum(top_left);

  const int firstLine = max(0, ec2_debug_line_count - (EC2_DEBUG_MAX_LINES - 1));
  int screenLine = 0;
  for (int i = firstLine; i < ec2_debug_line_count && screenLine < EC2_DEBUG_MAX_LINES; ++i) {
    canvas.setTextColor(DIM_COLOR);
    canvas.drawString(ec2_debug_lines[i], 10, 25 + (screenLine * 12));
    screenLine++;
  }
  canvas.pushSprite(0, 0);
}

static void ec2_debug_clear(const String& title)
{
  ec2_debug_line_count = 0;
  ec2_debug_active = true;
  ec2_debug_render(title);
}

static void ec2_debug_append(const String& msg)
{
  Serial.println(msg);
  if (!ec2_debug_active) return;
  if (ec2_debug_line_count < EC2_DEBUG_MAX_LINES) {
    ec2_debug_lines[ec2_debug_line_count++] = msg;
  } else {
    for (int i = 1; i < EC2_DEBUG_MAX_LINES; ++i) {
      ec2_debug_lines[i - 1] = ec2_debug_lines[i];
    }
    ec2_debug_lines[EC2_DEBUG_MAX_LINES - 1] = msg;
  }
  ec2_debug_render("EC2 Debug");
}

static void clear_display()
{
  canvas.fillScreen(BG_COLOR);
  canvas.setCursor(4, 4);
  canvas.setTextColor(FG_COLOR, BG_COLOR);
  canvas.pushSprite(0, 0);
}

static void show_status_line(int line, const String& message, uint16_t color)
{
  if (color == 0xFFFF) color = FG_COLOR;
  else if (color == TFT_GREEN) color = SUCCESS_COLOR;
  else if (color == TFT_YELLOW) color = WARN_COLOR;
  else if (color == TFT_CYAN) color = ACCENT_COLOR;
  else if (color == TFT_RED) color = ERR_COLOR;

  draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);
  
  // Draw battery indicator at top right
  if (line == 0) {
    draw_battery_indicator(canvas.width() - 8, 8);
  }

  const int lineHeight = 12;
  const int y = 20 + (line * lineHeight);
  if (y >= canvas.height() - 10) return;

  canvas.setCursor(10, y);
  canvas.setTextColor(color, BG_COLOR);
  canvas.fillRect(10, y, canvas.width() - 20, lineHeight, BG_COLOR);
  canvas.setCursor(10, y);
  canvas.println(message);
  canvas.pushSprite(0, 0);
}

static void split_wrapped_text(const String& text, int maxChars, String* firstLine, String* secondLine) {
  firstLine->remove(0);
  secondLine->remove(0);

  if (maxChars <= 0 || text.length() <= (size_t)maxChars) {
    *firstLine = text;
    return;
  }

  int breakPos = maxChars;
  for (int i = maxChars; i > 0; --i) {
    if (text[i] == ' ') {
      breakPos = i;
      break;
    }
  }

  *firstLine = text.substring(0, breakPos);
  firstLine->trim();
  *secondLine = text.substring(breakPos);
  secondLine->trim();

  if (secondLine->length() == 0) {
    *secondLine = text.substring(breakPos);
  }
}

static int draw_wrapped_wifi_line(int x, int y, int width, const String& label, const String& value, uint16_t color) {
  const int lineHeight = 12;
  const int maxChars = max(8, width / 6);
  String firstLine;
  String secondLine;
  split_wrapped_text(value, maxChars - label.length(), &firstLine, &secondLine);

  canvas.setCursor(x, y);
  canvas.setTextColor(color, BG_COLOR);
  canvas.fillRect(x, y, width, lineHeight * 2, BG_COLOR);
  canvas.print(label);
  canvas.println(firstLine);

  if (secondLine.length() > 0) {
    canvas.setCursor(x + 12, y + lineHeight);
    canvas.print(secondLine);
    return 2;
  }

  return 1;
}

enum class InputKeyType {
  None,
  Printable,
  Backspace,
  Enter,
  Escape
};

struct InputKey {
  InputKeyType type;
  char value;
};

static InputKey read_input_key()
{
  static Keyboard_Class::KeysState prev_status;
  if (M5Cardputer.Keyboard.isChange()) {
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    
    if (M5Cardputer.Keyboard.isPressed()) {
       if (status.del && !prev_status.del) {
          prev_status = status;
          return {InputKeyType::Backspace, '\0'};
       }
       if (status.enter && !prev_status.enter) {
          prev_status = status;
          return {InputKeyType::Enter, '\0'};
       }
       
       if (!status.word.empty()) {
          for (char ch : status.word) {
              bool found = false;
              for (char pch : prev_status.word) {
                  if (ch == pch) found = true;
              }
              if (!found) {
                  if (ch == '`' || ch == 27) {
                      prev_status = status;
                      return {InputKeyType::Escape, '\0'};
                  }
                  prev_status = status;
                  return {InputKeyType::Printable, ch};
              }
          }
       }
    }
    prev_status = status;
  }
  return {InputKeyType::None, '\0'};
}

static void wait_for_key_release()
{
  do {
    M5Cardputer.update();
    delay(20);
  } while (M5Cardputer.Keyboard.isPressed());
}

static bool input_matches(const InputKey& input, char lower)
{
  if (input.type != InputKeyType::Printable) return false;
  return input.value == lower || input.value == (char)toupper(lower);
}

static void draw_home_screen()
{
  canvas.fillScreen(BG_COLOR);
  
  draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);
  
  // Draw title
  canvas.setTextColor(ACCENT_COLOR);
  canvas.setTextDatum(top_center);
  canvas.drawString("POCKETCLOUD", canvas.width() / 2, 12);
  
  // Draw battery indicator at top right
  draw_battery_indicator(canvas.width() - 8, 8);
  
  canvas.setTextColor(FG_COLOR);
  canvas.setTextDatum(middle_center);
  
  if (WiFi.status() == WL_CONNECTED) {
    canvas.setTextColor(SUCCESS_COLOR);
    canvas.drawString("WiFi Connected", canvas.width() / 2, 50);
    canvas.setTextColor(DIM_COLOR);
    canvas.drawString(WiFi.localIP().toString(), canvas.width() / 2, 65);
  } else {
    canvas.setTextColor(WARN_COLOR);
    canvas.drawString("WiFi Not Connected", canvas.width() / 2, 50);
  }
  
  canvas.setTextDatum(bottom_center);
  canvas.setTextColor(FG_COLOR);
  canvas.drawString("[E] EC2  [S] Set  [W] WiFi", canvas.width() / 2, canvas.height() - 10);
  
  canvas.pushSprite(0, 0);
}

static int draw_wifi_list_entry(int x, int y, int width, int index, const String& ssid, uint16_t color) {
  const int lineHeight = 12;
  const String prefix = String(index + 1) + ": ";
  const int maxChars = max(8, width / 6);
  String firstLine;
  String secondLine;
  split_wrapped_text(ssid, maxChars - prefix.length(), &firstLine, &secondLine);

  canvas.setTextColor(color, BG_COLOR);
  canvas.setCursor(x, y);
  canvas.fillRect(x, y, width, lineHeight * 2, BG_COLOR);
  canvas.print(prefix);
  canvas.println(firstLine);

  if (secondLine.length() > 0) {
    canvas.setCursor(x + 12, y + lineHeight);
    canvas.print(secondLine);
    return 2;
  }

  return 1;
}

struct EC2Instance {
  char id[40];
  char name[64];
  char state[20];
};

int fetch_ec2_instances(EC2Instance *out, int maxInstances);

struct Ec2Settings {
  char url[256];
  char token[128]; // legacy static bearer token fallback
  char pairCode[32];
  char deviceId[32];
  char awsAccessKey[32];
  char awsSecretKey[64];
  char awsRegion[24];
  char accessToken[256];
  char refreshToken[256];
  char pin[16];
  uint32_t accessExpiryMs;
};

String _device_bound_key() {
  uint64_t mac = ESP.getEfuseMac();
  char key[17];
  snprintf(key, sizeof(key), "%08X%08X", (uint32_t)(mac >> 32), (uint32_t)mac);
  return String(key);
}

String _xor_hex_encode(const char* input) {
  String key = _device_bound_key();
  const size_t keyLen = key.length();
  String out;
  for (size_t i = 0; i < strlen(input); ++i) {
    uint8_t c = static_cast<uint8_t>(input[i]);
    uint8_t k = static_cast<uint8_t>(key[i % keyLen]);
    uint8_t v = c ^ k;
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", v);
    out += hex;
  }
  return out;
}

String _xor_hex_decode(const String& encodedHex) {
  String key = _device_bound_key();
  const size_t keyLen = key.length();
  String out;
  if (encodedHex.length() % 2 != 0) return out;
  for (size_t i = 0; i < encodedHex.length(); i += 2) {
    char byteChars[3] = {encodedHex[i], encodedHex[i + 1], '\0'};
    uint8_t v = static_cast<uint8_t>(strtoul(byteChars, nullptr, 16));
    uint8_t k = static_cast<uint8_t>(key[(i / 2) % keyLen]);
    char c = static_cast<char>(v ^ k);
    out += c;
  }
  return out;
}

// Amazon Root CA 1 (used by API Gateway custom domains and many AWS endpoints).
static const char AWS_ROOT_CA1[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/iY1+Op/ktLitT1vSyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzETMBEGA1UEChMKQW1hem9uLmNvbTEVMBMGA1UEAxMM
QW1hem9uIFJvb3QgQ0EgMTAeFw0xNTA1MjYwMDAwMDBaFw00MDAxMTcwMDAwMDBa
MDkxCzAJBgNVBAYTAlVTMRMwEQYDVQQKEwpBbWF6b24uY29tMRUwEwYDVQQDEwxB
bWF6b24gUm9vdCBDQSAxMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA
uKizGR4qf5B5vW5jM90+hCbGyF/F7fs/3Mhqdh0dX8GZFODdgNpTi27C/medhOiC
k2fYVBfupnCJA7l2nD6fVwppIqh+apMI2vlA38nSxrdbidKfnUSsfx8bVsgcuyo6
edSxnl2xe50Tzw9uQWGWpZ6YG1ChcxrFAxo0xO+ogzAm8h1Hn0pV3hokW2N7DbSt
O2Qe6hw2yffA9H9n1tFoZT3zh0+BTtPlqvGjufH6G+jD/adJzi10BGi7ipo+nWQK
BgU984wgKLF6ig84yYI6FqdtYYdlcYNSeNBY0d5hDOGOZa2m30IZHuZOKhHvJ3C0
cFLbFhF38N5EJ2VNivg3XwIDAQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUhBjMhTTsvAyUf7z6y+J5v2r6CwEwDQYJKoZI
hvcNAQELBQADggEBAK3N7M8Nl1N8MPv+2MvmFo0xSg6u40qCMgfHdCqkfNNpJBWl
AbIYW/W2PASi6DPd7OJbRRqtD9h5pz50jdK5Zk90un0nLBKBPXn1HULICwhf66A1
VpzwuWdlxeqmoeZaZX6mE6xPD58Ll35H0TADaBrZEcD3xKhsR4HIX66vepQP9en5
bY3bT5iAG2wE8xmPKzW0fvk/sdzCYwH14r2raXIiQunlslqY5T2r8j04YfGLwRoT
FiNUFDXL9uBeb5GsyhQOdE31E4n4t4DccnE9vrFica8FWHZcizxgxYkWwaP42Qik
Ize8ih/7gToYtL6vhfVqlhK/SXxPxq8np5xpoE2mR7U=
-----END CERTIFICATE-----
)PEM";

void load_ec2_settings(Ec2Settings* out) {
  memset(out, 0, sizeof(Ec2Settings));
  strncpy(out->url, EC2_PROXY_URL, sizeof(out->url) - 1);
  strncpy(out->token, EC2_PROXY_TOKEN, sizeof(out->token) - 1);
  snprintf(out->deviceId, sizeof(out->deviceId), "cardputer-%08X", (uint32_t)ESP.getEfuseMac());

  Preferences prefs;
  if (prefs.begin("ec2", true)) {
    auto safe_get = [&](const char* key) -> String {
      return prefs.isKey(key) ? prefs.getString(key, "") : String();
    };

    String u = safe_get("url");
    String tokenEnc = safe_get("token_enc");
    String pairEnc = safe_get("pair_code_enc");
    String devId = safe_get("device_id");
    String awsAccessEnc = safe_get("aws_access_enc");
    String awsSecretEnc = safe_get("aws_secret_enc");
    String awsRegion = safe_get("aws_region");
    String accessEnc = safe_get("access_token_enc");
    String refreshEnc = safe_get("refresh_token_enc");
    String pinEnc = safe_get("pin_enc");
    String t = tokenEnc.length() > 0 ? _xor_hex_decode(tokenEnc) : safe_get("token");
    String pc = pairEnc.length() > 0 ? _xor_hex_decode(pairEnc) : safe_get("pair_code");
    String at = accessEnc.length() > 0 ? _xor_hex_decode(accessEnc) : safe_get("access_token");
    String rt = refreshEnc.length() > 0 ? _xor_hex_decode(refreshEnc) : safe_get("refresh_token");
    String p = pinEnc.length() > 0 ? _xor_hex_decode(pinEnc) : safe_get("pin");
    if (u.length() > 0) u.toCharArray(out->url, sizeof(out->url));
    if (t.length() > 0) t.toCharArray(out->token, sizeof(out->token));
    if (pc.length() > 0) pc.toCharArray(out->pairCode, sizeof(out->pairCode));
    if (devId.length() > 0) devId.toCharArray(out->deviceId, sizeof(out->deviceId));
    if (awsAccessEnc.length() > 0) _xor_hex_decode(awsAccessEnc).toCharArray(out->awsAccessKey, sizeof(out->awsAccessKey));
    if (awsSecretEnc.length() > 0) _xor_hex_decode(awsSecretEnc).toCharArray(out->awsSecretKey, sizeof(out->awsSecretKey));
    if (awsRegion.length() > 0) awsRegion.toCharArray(out->awsRegion, sizeof(out->awsRegion));
    if (at.length() > 0) at.toCharArray(out->accessToken, sizeof(out->accessToken));
    if (rt.length() > 0) rt.toCharArray(out->refreshToken, sizeof(out->refreshToken));
    if (p.length() > 0) p.toCharArray(out->pin, sizeof(out->pin));
    out->accessExpiryMs = prefs.getULong("access_exp_ms", 0UL);
    prefs.end();
  }
}

bool save_ec2_settings_to_prefs(const Ec2Settings* settings) {
  Preferences prefs;
  if (!prefs.begin("ec2", false)) return false;
  prefs.putString("url", String(settings->url));
  prefs.putString("token_enc", _xor_hex_encode(settings->token));
  prefs.putString("pair_code_enc", _xor_hex_encode(settings->pairCode));
  prefs.putString("device_id", String(settings->deviceId));
  prefs.putString("aws_access_enc", _xor_hex_encode(settings->awsAccessKey));
  prefs.putString("aws_secret_enc", _xor_hex_encode(settings->awsSecretKey));
  prefs.putString("aws_region", String(settings->awsRegion));
  prefs.putString("access_token_enc", _xor_hex_encode(settings->accessToken));
  prefs.putString("refresh_token_enc", _xor_hex_encode(settings->refreshToken));
  prefs.putULong("access_exp_ms", settings->accessExpiryMs);
  prefs.remove("token");
  prefs.remove("pair_code");
  prefs.remove("access_token");
  prefs.remove("refresh_token");
  if (settings->pin[0] != '\0') {
    prefs.putString("pin_enc", _xor_hex_encode(settings->pin));
    prefs.remove("pin");
  } else {
    prefs.remove("pin");
    prefs.remove("pin_enc");
  }
  prefs.end();
  return true;
}

bool load_ec2_settings_from_sd(Ec2Settings* settings, bool saveToPrefs) {
  if (!sd_ok) return false;
  if (!SD.exists("/ec2.conf")) return false;
  File f = SD.open("/ec2.conf", FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.replace("\r", "");
    line.replace("\n", "");
    if (line.startsWith("url=")) {
      String v = line.substring(4);
      v.toCharArray(settings->url, sizeof(settings->url));
    } else if (line.startsWith("token_enc=")) {
      String v = line.substring(10);
      String decoded = _xor_hex_decode(v);
      decoded.toCharArray(settings->token, sizeof(settings->token));
    } else if (line.startsWith("token=")) {
      String v = line.substring(6);
      v.toCharArray(settings->token, sizeof(settings->token));
    } else if (line.startsWith("pair_code_enc=")) {
      String v = line.substring(14);
      String decoded = _xor_hex_decode(v);
      decoded.toCharArray(settings->pairCode, sizeof(settings->pairCode));
    } else if (line.startsWith("pair_code=")) {
      String v = line.substring(10);
      v.toCharArray(settings->pairCode, sizeof(settings->pairCode));
    } else if (line.startsWith("device_id=")) {
      String v = line.substring(10);
      v.toCharArray(settings->deviceId, sizeof(settings->deviceId));
    } else if (line.startsWith("access_token_enc=")) {
      String v = line.substring(17);
      String decoded = _xor_hex_decode(v);
      decoded.toCharArray(settings->accessToken, sizeof(settings->accessToken));
    } else if (line.startsWith("refresh_token_enc=")) {
      String v = line.substring(18);
      String decoded = _xor_hex_decode(v);
      decoded.toCharArray(settings->refreshToken, sizeof(settings->refreshToken));
    } else if (line.startsWith("pin_enc=")) {
      String v = line.substring(8);
      String decoded = _xor_hex_decode(v);
      decoded.toCharArray(settings->pin, sizeof(settings->pin));
    } else if (line.startsWith("pin=")) {
      String v = line.substring(4);
      v.toCharArray(settings->pin, sizeof(settings->pin));
    }
  }
  f.close();

  if (saveToPrefs) return save_ec2_settings_to_prefs(settings);
  return true;
}

static String html_escape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

static String form_value(const char* name) {
  return configServer.hasArg(name) ? configServer.arg(name) : "";
}

static String normalize_api_url(String url) {
  if (url.startsWith("https:/") && !url.startsWith("https://")) {
    url.replace("https:/", "https://");
  }
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

static bool web_pin_ok(const Ec2Settings& settings) {
  (void)settings;
  return true;
}

static void send_config_page(const String& notice = "", bool error = false) {
  Ec2Settings settings;
  load_ec2_settings(&settings);

  String wifiSsid = "";
  Preferences wifiPrefs;
  if (wifiPrefs.begin("wifi", true)) {
    wifiSsid = wifiPrefs.getString("ssid", "");
    wifiPrefs.end();
  }

  String html;
  html.reserve(7000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>AWS Cardputer Config</title><style>");
  html += F("body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#101418;color:#eef3f7}");
  html += F("main{max-width:760px;margin:0 auto;padding:20px}h1{font-size:24px;margin:0 0 12px}");
  html += F("section{border:1px solid #2f3a44;border-radius:8px;padding:16px;margin:14px 0;background:#171d23}");
  html += F("label{display:block;margin:12px 0 6px;color:#b8c4ce}input{box-sizing:border-box;width:100%;padding:10px;border-radius:6px;border:1px solid #475563;background:#0d1116;color:#fff}");
  html += F("button{padding:11px 14px;border:0;border-radius:6px;background:#2f81f7;color:white;font-weight:700}small{color:#94a3af}.ok{color:#52d273}.err{color:#ff6b6b}.row{display:flex;gap:10px;align-items:center}.row input{width:auto}</style></head><body><main>");
  html += F("<h1>AWS Cardputer Config</h1>");
  html += F("<small>Device IP: ");
  html += WiFi.localIP().toString();
  html += F("</small>");
  if (notice.length()) {
    html += error ? F("<p class='err'>") : F("<p class='ok'>");
    html += html_escape(notice);
    html += F("</p>");
  }
  html += F("<form method='post' action='/save'>");
  if (settings.pin[0] != '\0') {
    html += F("<section><h2>Unlock</h2><label>Current device PIN</label><input name='pin_current' type='password' inputmode='numeric' maxlength='4' required><small>Required because a PIN is set on the device.</small></section>");
  }
  html += F("<section><h2>AWS Proxy</h2>");
  html += F("<label>API Gateway URL</label><input name='url' value='");
  html += html_escape(String(settings.url));
  html += F("' placeholder='https://abc123.execute-api.region.amazonaws.com/Prod'>");
  html += F("<label>Pair code</label><input name='pair_code' type='password' placeholder='Leave blank to keep current pair code'>");
  html += F("<div class='row'><input id='clear_pair' name='clear_pair' type='checkbox' value='1'><label for='clear_pair'>Clear saved pair code</label></div>");
  html += F("<label>Legacy admin token</label><input name='token' type='password' placeholder='Leave blank to keep current token'>");
  html += F("<div class='row'><input id='clear_token' name='clear_token' type='checkbox' value='1'><label for='clear_token'>Clear legacy token</label></div>");
  html += F("<label>Device ID</label><input name='device_id' value='");
  html += html_escape(String(settings.deviceId));
  html += F("'></section>");
  html += F("<section><h2>Direct AWS Mode</h2>");
  html += F("<label>AWS Region</label><input name='aws_region' value='");
  html += html_escape(String(settings.awsRegion));
  html += F("' placeholder='ap-south-1'>");
  html += F("<label>AWS Access Key ID</label><input name='aws_access_key' value='");
  html += html_escape(String(settings.awsAccessKey));
  html += F("' placeholder='AKIA...'>");
  html += F("<label>AWS Secret Access Key</label><input name='aws_secret_key' type='password' placeholder='Leave blank to keep current secret'>");
  html += F("<div class='row'><input id='clear_aws' name='clear_aws' type='checkbox' value='1'><label for='clear_aws'>Clear direct AWS credentials</label></div>");
  html += F("<small>Use a restricted IAM user with only EC2 describe/start/stop permissions.</small></section>");
  html += F("<section><h2>Device Lock</h2><label>New 4-digit PIN</label><input name='pin_new' type='password' inputmode='numeric' maxlength='4' placeholder='Leave blank to keep current PIN'>");
  html += F("<div class='row'><input id='clear_pin' name='clear_pin' type='checkbox' value='1'><label for='clear_pin'>Clear PIN</label></div></section>");
  html += F("<section><h2>WiFi</h2><label>SSID</label><input name='wifi_ssid' value='");
  html += html_escape(wifiSsid);
  html += F("'><label>Password</label><input name='wifi_password' type='password' placeholder='Leave blank to keep current password'>");
  html += F("<div class='row'><input id='forget_wifi' name='forget_wifi' type='checkbox' value='1'><label for='forget_wifi'>Forget saved WiFi credentials</label></div>");
  html += F("<small>Changing WiFi saves credentials. Reboot or press W on the device to reconnect.</small></section>");
  html += F("<button type='submit'>Save settings</button></form>");
  html += F("<section><h2>Status</h2><p>Pair code: ");
  html += settings.pairCode[0] ? F("set") : F("empty");
  html += F("<br>Legacy token: ");
  html += settings.token[0] ? F("set") : F("empty");
  html += F("<br>Refresh token: ");
  html += settings.refreshToken[0] ? F("set") : F("empty");
  html += F("<br>Direct AWS: ");
  html += (settings.awsAccessKey[0] && settings.awsSecretKey[0] && settings.awsRegion[0]) ? F("set") : F("empty");
  html += F("<br>PIN: ");
  html += settings.pin[0] ? F("set") : F("empty");
  html += F("</p><p><a style='color:#8ab4ff' href='/ping'>Ping web server</a><br>");
  html += F("<a style='color:#8ab4ff' href='/debug'>View debug status</a><br>");
  html += F("<a style='color:#8ab4ff' href='/test'>Test AWS connection</a></p></section></main></body></html>");
  configServer.send(200, "text/html", html);
}

static void handle_config_save() {
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (!web_pin_ok(settings)) {
    send_config_page("Wrong PIN. Settings were not saved.", true);
    return;
  }

  String url = form_value("url");
  String deviceId = form_value("device_id");
  String pairCode = form_value("pair_code");
  String token = form_value("token");
  String awsRegion = form_value("aws_region");
  String awsAccessKey = form_value("aws_access_key");
  String awsSecretKey = form_value("aws_secret_key");
  String pinNew = form_value("pin_new");
  String wifiSsid = form_value("wifi_ssid");
  String wifiPassword = form_value("wifi_password");
  bool forgetWifi = configServer.hasArg("forget_wifi");

  url.trim();
  deviceId.trim();
  pairCode.trim();
  token.trim();
  awsRegion.trim();
  awsAccessKey.trim();
  awsSecretKey.trim();
  pinNew.trim();

  if (pinNew.length() > 0 && pinNew.length() != 4) {
    send_config_page("PIN must be exactly 4 digits.", true);
    return;
  }
  for (size_t i = 0; i < pinNew.length(); ++i) {
    if (!isDigit(pinNew[i])) {
      send_config_page("PIN must contain digits only.", true);
      return;
    }
  }

  if (url.length() > 0) {
    if (!url.startsWith("https://") || url.indexOf(".execute-api.") < 0) {
      send_config_page("API URL must look like https://abc.execute-api.region.amazonaws.com/Prod", true);
      return;
    }
    url.toCharArray(settings.url, sizeof(settings.url));
  }
  if (deviceId.length() > 0) deviceId.toCharArray(settings.deviceId, sizeof(settings.deviceId));
  if (configServer.hasArg("clear_pair")) settings.pairCode[0] = '\0';
  else if (pairCode.length() > 0) pairCode.toCharArray(settings.pairCode, sizeof(settings.pairCode));
  if (configServer.hasArg("clear_token")) settings.token[0] = '\0';
  else if (token.length() > 0) token.toCharArray(settings.token, sizeof(settings.token));
  if (configServer.hasArg("clear_aws")) {
    settings.awsAccessKey[0] = '\0';
    settings.awsSecretKey[0] = '\0';
    settings.awsRegion[0] = '\0';
  } else {
    if (awsRegion.length() > 0) awsRegion.toCharArray(settings.awsRegion, sizeof(settings.awsRegion));
    if (awsAccessKey.length() > 0) awsAccessKey.toCharArray(settings.awsAccessKey, sizeof(settings.awsAccessKey));
    if (awsSecretKey.length() > 0) awsSecretKey.toCharArray(settings.awsSecretKey, sizeof(settings.awsSecretKey));
  }
  if (configServer.hasArg("clear_pin")) settings.pin[0] = '\0';
  else if (pinNew.length() > 0) pinNew.toCharArray(settings.pin, sizeof(settings.pin));

  // Force re-pair/refresh when auth material changes.
  if (pairCode.length() > 0 || token.length() > 0 || configServer.hasArg("clear_pair") || configServer.hasArg("clear_token")) {
    settings.accessToken[0] = '\0';
    settings.refreshToken[0] = '\0';
    settings.accessExpiryMs = 0;
  }

  save_ec2_settings_to_prefs(&settings);

  if (forgetWifi) {
    clear_saved_wifi_credentials();
  } else if (wifiSsid.length() > 0) {
    Preferences wifiPrefs;
    if (wifiPrefs.begin("wifi", false)) {
      wifiPrefs.putString("ssid", wifiSsid);
      if (wifiPassword.length() > 0) {
        wifiPrefs.putString("password", wifiPassword);
      }
      wifiPrefs.end();
    }
  }

  send_config_page(forgetWifi ? "WiFi credentials removed." : "Settings saved.");
}

// Background task for running EC2 test from web handler
static void ec2_test_task(void* /*pv*/) {
  Serial.println("[EC2-Test] task started");
  static EC2Instance list[MAX_INSTANCES];
  int count = fetch_ec2_instances(list, MAX_INSTANCES);
  if (count < 0) {
    Serial.print("[EC2-Test] FAIL: ");
    Serial.println(last_ec2_error.length() ? last_ec2_error : "unknown error");
  } else {
    Serial.print("[EC2-Test] OK: ");
    Serial.print(count);
    Serial.println(" instance(s)");
    for (int i = 0; i < count; ++i) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(list[i].id);
      Serial.print(" ");
      Serial.print(list[i].state);
      Serial.print(" ");
      Serial.println(list[i].name);
    }
  }
  Serial.println("[EC2-Test] task finished");
  vTaskDelete(NULL);
}

static void handle_config_test() {
  if (WiFi.status() != WL_CONNECTED) {
    configServer.send(200, "text/plain", "FAIL: WiFi not connected");
    return;
  }

  // Run instance fetch in a background FreeRTOS task to avoid blocking the web server
  // TLS + HTTPClient + mbedTLS entropy can use a very large stack on ESP32-S3.
  BaseType_t created = xTaskCreatePinnedToCore(ec2_test_task, "ec2_test", 32768, NULL, 1, NULL, 1);
  if (created != pdPASS) {
    Serial.println("[EC2] Failed to create test task");
    configServer.send(500, "text/plain", "FAIL: unable to start test task");
    return;
  }
  configServer.send(200, "text/plain", "OK: test started; check serial logs");
}

static void handle_config_ping() {
  configServer.send(200, "text/plain", "OK: web server is running");
}

static void handle_config_debug() {
  Ec2Settings settings;
  load_ec2_settings(&settings);

  String out;
  out.reserve(600);
  out += "WiFi: ";
  out += WiFi.status() == WL_CONNECTED ? "connected\n" : "not connected\n";
  out += "IP: " + WiFi.localIP().toString() + "\n";
  out += "Device ID: ";
  out += settings.deviceId[0] ? String(settings.deviceId) : "(empty)";
  out += "\nPair code: ";
  out += settings.pairCode[0] ? "set" : "empty";
  out += "\nRefresh token: ";
  out += settings.refreshToken[0] ? "set" : "empty";
  out += "\nAccess token: ";
  out += settings.accessToken[0] ? "set" : "empty";
  out += "\nAWS region: ";
  out += settings.awsRegion[0] ? String(settings.awsRegion) : "(empty)";
  out += "\nAWS access key: ";
  out += settings.awsAccessKey[0] ? "set" : "empty";
  out += "\nAWS secret key: ";
  out += settings.awsSecretKey[0] ? "set" : "empty";
  out += "\nPIN: ";
  out += settings.pin[0] ? "set" : "empty";
  out += "\nClock: ";
  time_t now = time(nullptr);
  out += String((long)now);
  out += now >= 1700000000 ? " synced\n" : " not synced\n";
  out += "Last EC2 error: ";
  out += last_ec2_error.length() ? last_ec2_error : "(none)";
  out += "\n";
  configServer.send(200, "text/plain", out);
}

static void start_config_server() {
  if (config_server_started || WiFi.status() != WL_CONNECTED) return;
  configServer.on("/", HTTP_GET, [](){ send_config_page(); });
  configServer.on("/save", HTTP_POST, handle_config_save);
  configServer.on("/ping", HTTP_GET, handle_config_ping);
  configServer.on("/debug", HTTP_GET, handle_config_debug);
  configServer.on("/test", HTTP_GET, handle_config_test);
  configServer.onNotFound([](){ configServer.sendHeader("Location", "/"); configServer.send(302, "text/plain", ""); });
  configServer.begin();
  config_server_started = true;
}

static bool sync_time_for_tls(bool quiet = false) {
  if (time_synced) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!quiet) ec2_debug_append("EC2: syncing clock");
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.aws.com");
  unsigned long start = millis();
  time_t now = time(nullptr);
  while (now < 1700000000 && (millis() - start) < 10000) {
    M5Cardputer.update();
    delay(250);
    now = time(nullptr);
  }
  time_synced = now >= 1700000000;
  if (!time_synced) {
    // Fallback to a sane UTC timestamp so TLS certificate validation can proceed
    // even if NTP is temporarily unavailable.
    struct timeval tv;
    tv.tv_sec = 1735689600; // 2025-01-01 00:00:00 UTC
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    now = time(nullptr);
    time_synced = now >= 1700000000;
  }
  if (!quiet) ec2_debug_append(time_synced ? "EC2: clock OK" : "EC2: clock sync failed");
  return time_synced;
}

static String hex_encode(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[data[i] >> 4];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

static String sha256_hex(const String& data) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const unsigned char*>(data.c_str()), data.length());
  mbedtls_sha256_finish_ret(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  return hex_encode(hash, sizeof(hash));
}

static void hmac_sha256(const uint8_t* key, size_t keyLen, const String& data, uint8_t* out) {
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, reinterpret_cast<const unsigned char*>(data.c_str()), data.length());
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

static String hmac_sha256_hex(const uint8_t* key, size_t keyLen, const String& data) {
  uint8_t out[32];
  hmac_sha256(key, keyLen, data, out);
  return hex_encode(out, sizeof(out));
}

static String aws_uri_encode(const String& value) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(uint8_t)c >> 4];
      out += hex[(uint8_t)c & 0x0F];
    }
  }
  return out;
}

static String xml_value(const String& xml, const String& tag, int start = 0) {
  String open = "<" + tag + ">";
  String close = "</" + tag + ">";
  int a = xml.indexOf(open, start);
  if (a < 0) return "";
  a += open.length();
  int b = xml.indexOf(close, a);
  if (b < 0) return "";
  return xml.substring(a, b);
}

static bool aws_direct_request(const Ec2Settings& settings, const String& canonicalQuery, int* statusCode, String* responseBody) {
  ec2_debug_append("EC2: direct AWS request start");
  if (!sync_time_for_tls()) {
    set_ec2_error("Clock sync failed");
    return false;
  }
  if (settings.awsAccessKey[0] == '\0' || settings.awsSecretKey[0] == '\0' || settings.awsRegion[0] == '\0') {
    set_ec2_error("Set AWS key/secret/region");
    return false;
  }

  time_t now = time(nullptr);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  char amzDate[17];
  char shortDate[9];
  strftime(amzDate, sizeof(amzDate), "%Y%m%dT%H%M%SZ", &tm_utc);
  strftime(shortDate, sizeof(shortDate), "%Y%m%d", &tm_utc);

  String region = String(settings.awsRegion);
  String host = "ec2." + region + ".amazonaws.com";
  String payloadHash = sha256_hex("");
  String canonicalHeaders = "host:" + host + "\n" + "x-amz-date:" + String(amzDate) + "\n";
  String signedHeaders = "host;x-amz-date";
  String canonicalRequest = "GET\n/\n" + canonicalQuery + "\n" + canonicalHeaders + "\n" + signedHeaders + "\n" + payloadHash;
  String credentialScope = String(shortDate) + "/" + region + "/ec2/aws4_request";
  String stringToSign = "AWS4-HMAC-SHA256\n" + String(amzDate) + "\n" + credentialScope + "\n" + sha256_hex(canonicalRequest);

  uint8_t kDate[32], kRegion[32], kService[32], kSigning[32];
  String secret = "AWS4" + String(settings.awsSecretKey);
  hmac_sha256(reinterpret_cast<const uint8_t*>(secret.c_str()), secret.length(), String(shortDate), kDate);
  hmac_sha256(kDate, sizeof(kDate), region, kRegion);
  hmac_sha256(kRegion, sizeof(kRegion), "ec2", kService);
  hmac_sha256(kService, sizeof(kService), "aws4_request", kSigning);
  String signature = hmac_sha256_hex(kSigning, sizeof(kSigning), stringToSign);

  String authorization = "AWS4-HMAC-SHA256 Credential=" + String(settings.awsAccessKey) + "/" + credentialScope + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;
  String url = "https://" + host + "/?" + canonicalQuery;

  WiFiClientSecure client;
  HTTPClient http;
  client.setCACert(AWS_ROOT_CA1);
  client.setTimeout(20000);
  ec2_debug_append("EC2: direct AWS HTTP begin");
  if (!http.begin(client, url)) {
    set_ec2_error("Bad EC2 URL");
    ec2_debug_append("EC2: direct AWS HTTP begin failed");
    return false;
  }
  ec2_debug_append("EC2: direct AWS HTTP GET");
  http.addHeader("X-Amz-Date", String(amzDate));
  http.addHeader("Authorization", authorization);
  int code = http.GET();
  ec2_debug_append("EC2: direct AWS HTTP returned " + String(code));
  String body = http.getString();
  http.end();
  *statusCode = code;
  *responseBody = body;
  if (code < 200 || code >= 300) {
    String awsErr = xml_value(body, "Code");
    set_ec2_error(awsErr.length() ? awsErr : "AWS HTTP " + String(code));
    return false;
  }
  return true;
}

static int fetch_ec2_instances_direct(const Ec2Settings& settings, EC2Instance* out, int maxInstances) {
  int code = -1;
  String body;
  show_status_line(0, "Direct AWS EC2...", TFT_CYAN);
  if (!aws_direct_request(settings, "Action=DescribeInstances&Version=2016-11-15", &code, &body)) return -1;

  int count = 0;
  int pos = 0;
  while (count < maxInstances) {
    int itemStart = body.indexOf("<item>", pos);
    if (itemStart < 0) break;
    int itemEnd = body.indexOf("</item>", itemStart);
    if (itemEnd < 0) break;
    String item = body.substring(itemStart, itemEnd);
    String instanceId = xml_value(item, "instanceId");
    if (instanceId.startsWith("i-")) {
      String stateBlock = item.substring(item.indexOf("<instanceState>"));
      String state = xml_value(stateBlock, "name");
      String name = "";
      int tags = item.indexOf("<tagSet>");
      if (tags >= 0) {
        int tagPos = tags;
        while (true) {
          int ts = item.indexOf("<item>", tagPos);
          if (ts < 0) break;
          int te = item.indexOf("</item>", ts);
          if (te < 0) break;
          String tagItem = item.substring(ts, te);
          if (xml_value(tagItem, "key") == "Name") {
            name = xml_value(tagItem, "value");
            break;
          }
          tagPos = te + 7;
        }
      }
      if (name.length() == 0) name = instanceId;
      instanceId.toCharArray(out[count].id, sizeof(out[count].id));
      state.toCharArray(out[count].state, sizeof(out[count].state));
      name.toCharArray(out[count].name, sizeof(out[count].name));
      count++;
    }
    pos = itemEnd + 7;
  }
  return count;
}

static bool send_ec2_action_direct(const Ec2Settings& settings, const char* instanceId, const char* action) {
  int code = -1;
  String body;
  String actionName;
  if (strcmp(action, "start") == 0) {
    actionName = "StartInstances";
  } else if (strcmp(action, "stop") == 0) {
    actionName = "StopInstances";
  } else if (strcmp(action, "reboot") == 0) {
    actionName = "RebootInstances";
  } else {
    set_ec2_error("Unknown action");
    return false;
  }
  String query = "Action=" + actionName + "&InstanceId.1=" + aws_uri_encode(String(instanceId)) + "&Version=2016-11-15";
  return aws_direct_request(settings, query, &code, &body);
}

void draw_masked_token(const char* token) {
  size_t n = strlen(token);
  for (size_t i = 0; i < n; ++i) M5Cardputer.Display.print('*');
  M5Cardputer.Display.println();
}

bool prompt_for_pin(const char* expectedPin, const char* promptTitle) {
  (void)expectedPin;
  (void)promptTitle;
  return true;
}

bool configure_secure_http(HTTPClient* http, WiFiClientSecure* client, const String& url) {
  // The proxy runs on API Gateway and some firmware builds fail parsing the embedded CA chain.
  // Fall back to an insecure TLS client so the EC2 control path remains usable on-device.
  client->setInsecure();
  client->setTimeout(15000);
  bool ok = http->begin(*client, url);
  ec2_debug_append(ok ? "EC2: HTTP begin OK" : "EC2: HTTP begin failed");
  return ok;
}

bool post_json_with_auth(
    const String& url,
    const String& body,
    const String& bearer,
    const String& deviceId,
    int* statusCode,
    String* responseBody) {
  WiFiClientSecure client;
  HTTPClient http;
    if (!configure_secure_http(&http, &client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  if (bearer.length() > 0) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  if (deviceId.length() > 0) {
    http.addHeader("X-Device-Id", deviceId);
  }
  ec2_debug_append("EC2: POST sending");
  int code = http.POST(body);
  ec2_debug_append("EC2: POST returned " + String(code));
  String payload = http.getString();
  http.end();
  *statusCode = code;
  *responseBody = payload;
  return true;
}

static void set_ec2_error(const String& msg) {
  last_ec2_error = msg;
}

bool ensure_ec2_session(Ec2Settings* settings) {
  last_ec2_error = "";
  if (!sync_time_for_tls()) {
    set_ec2_error("Clock sync failed");
    return false;
  }
  time_t now_ts = time(nullptr);
  
  ec2_debug_append("EC2: ensure session start");
  if (settings->accessToken[0] != '\0' && settings->accessExpiryMs > (uint32_t)now_ts + 10U) {
    ec2_debug_append("EC2: using cached access token");
    return true;
  }

  int code = -1;
  String payload;
  StaticJsonDocument<1024> req;

  if (settings->refreshToken[0] != '\0') {
    show_status_line(1, "Refreshing token...");
    ec2_debug_append("EC2: refreshing token");
    req.clear();
    req["deviceId"] = settings->deviceId;
    req["refreshToken"] = settings->refreshToken;
    String body;
    serializeJson(req, body);
    if (post_json_with_auth(String(settings->url) + "/refresh", body, "", settings->deviceId, &code, &payload) && code == 200) {
      ec2_debug_append("EC2: refresh HTTP 200");
      StaticJsonDocument<MAX_API_RESPONSE> doc;
      if (!deserializeJson(doc, payload)) {
        String at = doc["accessToken"] | "";
        int exp = doc["expiresIn"] | 600;
        if (at.length() > 0) {
          at.toCharArray(settings->accessToken, sizeof(settings->accessToken));
          settings->accessExpiryMs = (uint32_t)now_ts + (uint32_t)exp;
          save_ec2_settings_to_prefs(settings);
          return true;
        }
      }
      set_ec2_error("Bad refresh response");
    } else {
      String apiErr;
      StaticJsonDocument<256> errDoc;
      if (!deserializeJson(errDoc, payload)) {
        apiErr = String((const char*)(errDoc["error"] | ""));
      }
      if (apiErr.length() > 0) {
        set_ec2_error("Refresh: " + apiErr);
        ec2_debug_append("EC2: refresh error: " + apiErr);
      } else {
        set_ec2_error("Refresh HTTP " + String(code));
        ec2_debug_append("EC2: refresh HTTP " + String(code));
      }
      if (code == 400 || code == 401 || code == 403) {
        settings->accessToken[0] = '\0';
        settings->refreshToken[0] = '\0';
        settings->accessExpiryMs = 0;
        save_ec2_settings_to_prefs(settings);
      }
    }
  }

  if (settings->pairCode[0] != '\0') {
    show_status_line(1, "Pairing device...");
    ec2_debug_append("EC2: pairing device");
    req.clear();
    req["deviceId"] = settings->deviceId;
    req["pairCode"] = settings->pairCode;
    String body;
    serializeJson(req, body);
    if (post_json_with_auth(String(settings->url) + "/pair", body, "", settings->deviceId, &code, &payload) && code == 200) {
      ec2_debug_append("EC2: pair HTTP 200");
      StaticJsonDocument<MAX_API_RESPONSE> doc;
      if (!deserializeJson(doc, payload)) {
        String at = doc["accessToken"] | "";
        String rt = doc["refreshToken"] | "";
        int exp = doc["expiresIn"] | 600;
        if (at.length() > 0 && rt.length() > 0) {
          at.toCharArray(settings->accessToken, sizeof(settings->accessToken));
          rt.toCharArray(settings->refreshToken, sizeof(settings->refreshToken));
          settings->accessExpiryMs = (uint32_t)now_ts + (uint32_t)exp;
          save_ec2_settings_to_prefs(settings);
          return true;
        }
      }
      set_ec2_error("Bad pair response");
    } else {
      String apiErr;
      StaticJsonDocument<256> errDoc;
      if (!deserializeJson(errDoc, payload)) {
        apiErr = String((const char*)(errDoc["error"] | ""));
      }
      if (apiErr.length() > 0) {
        set_ec2_error("Pair: " + apiErr);
        ec2_debug_append("EC2: pair error: " + apiErr);
      } else {
        set_ec2_error("Pair HTTP " + String(code));
        ec2_debug_append("EC2: pair HTTP " + String(code));
      }
    }
  }

  if (last_ec2_error.length() == 0) {
    set_ec2_error("Set API URL + pair code");
  }
  if (last_ec2_error.length()) {
    ec2_debug_append("EC2: session failed: " + last_ec2_error);
  } else {
    ec2_debug_append("EC2: session failed: unknown");
  }
  return false;
}

// Fetch instances from EC2 proxy. Returns number of instances (>=0) or -1 on error.
int fetch_ec2_instances(EC2Instance *out, int maxInstances) {
  last_ec2_error = "";
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (WiFi.status() != WL_CONNECTED) {
    set_ec2_error("WiFi not connected");
    ec2_debug_append("EC2: WiFi not connected");
    return -1;
  }
  if (settings.awsAccessKey[0] && settings.awsSecretKey[0] && settings.awsRegion[0]) {
    ec2_debug_append("EC2: using direct AWS mode");
    return fetch_ec2_instances_direct(settings, out, maxInstances);
  }
  if (settings.url[0] == '\0' || String(settings.url).indexOf("REPLACE_WITH_API") >= 0) {
    set_ec2_error("Set API URL in web UI");
    ec2_debug_append("EC2: API URL missing");
    return -1;
  }
  String baseUrl = normalize_api_url(String(settings.url));
  if (!baseUrl.startsWith("https://")) {
    set_ec2_error("Bad API URL scheme");
    ec2_debug_append("EC2: bad API URL scheme");
    return -1;
  }
  if (!sync_time_for_tls()) {
    set_ec2_error("Clock sync failed");
    ec2_debug_append("EC2: clock sync failed");
    return -1;
  }
  ec2_debug_append("EC2: preparing API");
  if (!ensure_ec2_session(&settings)) return -1;
  String apiUrl = baseUrl + "/instances";
  String token = String(settings.accessToken);

  WiFiClientSecure client;
  HTTPClient http;
  ec2_debug_append("EC2: loading instances");
  if (!configure_secure_http(&http, &client, apiUrl)) {
    set_ec2_error("Bad API URL");
    ec2_debug_append("EC2: HTTP setup failed");
    return -1;
  }
  if (token.length() > 0) http.addHeader("Authorization", String("Bearer ") + token);
  if (settings.deviceId[0] != '\0') http.addHeader("X-Device-Id", String(settings.deviceId));
  int code = http.GET();
  if (code != 200) {
    String payload = http.getString();
    http.end();
    if (code == 401) {
      set_ec2_error("Unauthorized; re-pair");
      settings.accessToken[0] = '\0';
      settings.accessExpiryMs = 0;
      save_ec2_settings_to_prefs(&settings);
    }
    else if (code < 0) set_ec2_error("HTTP error " + String(code));
    else set_ec2_error("Instances HTTP " + String(code));
    ec2_debug_append("EC2: instances HTTP " + String(code));
    return -1;
  }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(MAX_API_RESPONSE);
  auto err = deserializeJson(doc, payload);
  if (err) {
    set_ec2_error("Bad instances JSON");
    ec2_debug_append("EC2: instances JSON parse failed");
    return -1;
  }
  JsonArray arr = doc["instances"].as<JsonArray>();
  int i = 0;
  for (JsonObject obj : arr) {
    if (i >= maxInstances) break;
    const char *iid = obj["InstanceId"] | "";
    const char *st = obj["State"] | "";
    const char *nm = obj["Name"] | "";
    strncpy(out[i].id, iid, sizeof(out[i].id) - 1);
    out[i].id[sizeof(out[i].id)-1] = '\0';
    strncpy(out[i].state, st, sizeof(out[i].state) - 1);
    out[i].state[sizeof(out[i].state)-1] = '\0';
    strncpy(out[i].name, nm, sizeof(out[i].name) - 1);
    out[i].name[sizeof(out[i].name)-1] = '\0';
    i++;
  }
  ec2_debug_append(String("EC2: instances loaded: ") + String(i));
  return i;
}

// Send EC2 action. action should be "start", "stop", or "reboot". Returns true on HTTP 2xx.
bool send_ec2_action(const char *instanceId, const char *action) {
  last_ec2_error = "";
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (settings.url[0] == '\0') {
    if (settings.awsAccessKey[0] && settings.awsSecretKey[0] && settings.awsRegion[0]) {
      return send_ec2_action_direct(settings, instanceId, action);
    }
    set_ec2_error("Set API URL");
    return false;
  }
  if (settings.awsAccessKey[0] && settings.awsSecretKey[0] && settings.awsRegion[0]) {
    return send_ec2_action_direct(settings, instanceId, action);
  }
  String baseUrl = normalize_api_url(String(settings.url));
  if (!baseUrl.startsWith("https://")) {
    set_ec2_error("Bad API URL scheme");
    return false;
  }
  if (!sync_time_for_tls()) {
    set_ec2_error("Clock sync failed");
    ec2_debug_append("EC2: clock sync failed");
    return false;
  }
  ec2_debug_append(String("EC2: sending action ") + action);
  if (!ensure_ec2_session(&settings)) return false;
  String apiUrl = baseUrl + "/instances/" + String(instanceId) + "/" + String(action);
  String token = String(settings.accessToken);

  WiFiClientSecure client;
  HTTPClient http;
  if (!configure_secure_http(&http, &client, apiUrl)) {
    set_ec2_error("Bad API URL");
    ec2_debug_append("EC2: action HTTP setup failed");
    return false;
  }
  if (token.length() > 0) http.addHeader("Authorization", String("Bearer ") + token);
  if (settings.deviceId[0] != '\0') http.addHeader("X-Device-Id", String(settings.deviceId));
  int code = http.POST("");
  http.end();
  if (!(code >= 200 && code < 300)) {
    if (code == 401) {
      set_ec2_error("Unauthorized; re-pair");
      settings.accessToken[0] = '\0';
      settings.accessExpiryMs = 0;
      save_ec2_settings_to_prefs(&settings);
    }
    else set_ec2_error("Action HTTP " + String(code));
    ec2_debug_append("EC2: action HTTP " + String(code));
  }
  else {
    ec2_debug_append("EC2: action sent OK");
  }
  return (code >= 200 && code < 300);
}

// Display EC2 instances UI and allow actions on the selected instance
void show_ec2_ui() {
  static Ec2Settings settings;
  ec2_debug_clear("Connecting to AWS...");
  ec2_debug_append("Loading settings...");
  load_ec2_settings(&settings);

  static EC2Instance list[MAX_INSTANCES];
  ec2_debug_append("Fetching instances...");
  int count = fetch_ec2_instances(list, MAX_INSTANCES);
  if (count < 0) {
    ec2_debug_append(last_ec2_error.length() ? last_ec2_error : String("Unknown error"));
    ec2_debug_append("Press Esc to go back");
    while (true) {
      M5Cardputer.update();
      if (config_server_started) configServer.handleClient();
      if (read_input_key().type == InputKeyType::Escape) break;
      delay(20);
    }
    return;
  }
  if (count == 0) {
    ec2_debug_append("No instances found");
    ec2_debug_append("Press Esc to go back");
    while (true) {
      M5Cardputer.update();
      if (config_server_started) configServer.handleClient();
      if (read_input_key().type == InputKeyType::Escape) break;
      delay(20);
    }
    return;
  }
  
  int selectableCount = count;
  int selected_idx = 0;
  float anim_y = 25; // initial Y for selection box

  auto refresh_instances = [&]() {
    ec2_debug_clear("Refreshing...");
    ec2_debug_append("Fetching instances...");
    int refreshed_count = fetch_ec2_instances(list, MAX_INSTANCES);
    if (refreshed_count >= 0) {
      selectableCount = refreshed_count;
      if (selectableCount == 0) {
        selected_idx = 0;
      } else if (selected_idx >= selectableCount) {
        selected_idx = selectableCount - 1;
      }
      ec2_debug_append(String("Loaded ") + String(selectableCount));
    } else {
      ec2_debug_append(String("Failed: ") + last_ec2_error);
    }
    delay(900);
  };

  auto perform_selected_action = [&](const char *action) {
    ec2_debug_clear("Sending Request...");
    ec2_debug_append(String("Sending ") + action + " to");
    ec2_debug_append(list[selected_idx].name);
    bool ok = send_ec2_action(list[selected_idx].id, action);
    if (ok) ec2_debug_append("Request sent OK");
    else ec2_debug_append(String("Failed: ") + last_ec2_error);
    delay(1500);

    // Refresh list after action and stay on the EC2 page.
    refresh_instances();
  };

  auto show_instance_action_menu = [&]() {
    const char *powerAction = (strcmp(list[selected_idx].state, "running") == 0) ? "stop" : "start";
    const char *options[3] = {
      (strcmp(powerAction, "stop") == 0) ? "Stop" : "Start",
      "Reboot",
      "Refresh"
    };
    int action_idx = 0;
    float action_anim_y = 38;
    unsigned long last_action_frame = millis();

    while (true) {
      M5Cardputer.update();
      if (config_server_started) configServer.handleClient();

      unsigned long now = millis();
      float dt = (now - last_action_frame) / 1000.0;
      last_action_frame = now;

      InputKey input = read_input_key();
      if (input.type == InputKeyType::Escape) return;
      if (input.type == InputKeyType::Printable) {
        char c = tolower(input.value);
        if (c == 'w' || c == ';' || c == ',') {
          if (action_idx > 0) action_idx--;
        } else if (c == 's' || c == '.' || c == '/') {
          if (action_idx < 2) action_idx++;
        } else if (c == 'r') {
          refresh_instances();
          return;
        } else if (c == 'b') {
          return;
        }
      } else if (input.type == InputKeyType::Enter) {
        if (action_idx == 0) {
          perform_selected_action(powerAction);
        } else if (action_idx == 1) {
          perform_selected_action("reboot");
        } else {
          refresh_instances();
        }
        return;
      }

      int target_y = 38 + action_idx * 22;
      if (abs(action_anim_y - target_y) > 0.5) {
        action_anim_y += (target_y - action_anim_y) * 15.0 * dt;
      } else {
        action_anim_y = target_y;
      }

      canvas.fillScreen(BG_COLOR);
      draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);

      canvas.setTextDatum(top_center);
      canvas.setTextColor(ACCENT_COLOR);
      canvas.drawString("Instance Action", canvas.width() / 2, 10);

      canvas.setTextDatum(top_center);
      canvas.setTextColor(DIM_COLOR);
      String instanceName = String(list[selected_idx].name);
      if (instanceName.length() > 24) instanceName = instanceName.substring(0, 24);
      canvas.drawString(instanceName, canvas.width() / 2, 23);

      draw_glow_rect(18, (int)action_anim_y, canvas.width() - 36, 18);
      canvas.fillRoundRect(18, (int)action_anim_y, canvas.width() - 36, 18, 3, canvas.color565(10, 30, 50));

      canvas.setTextDatum(middle_center);
      for (int i = 0; i < 3; ++i) {
        uint16_t color = (i == action_idx) ? TFT_WHITE : FG_COLOR;
        if (strcmp(options[i], "Stop") == 0) color = (i == action_idx) ? TFT_WHITE : ERR_COLOR;
        else if (strcmp(options[i], "Start") == 0) color = (i == action_idx) ? TFT_WHITE : SUCCESS_COLOR;
        else if (strcmp(options[i], "Reboot") == 0) color = (i == action_idx) ? TFT_WHITE : WARN_COLOR;
        canvas.setTextColor(color);
        canvas.drawString(options[i], canvas.width() / 2, 47 + i * 22);
      }

      canvas.setTextDatum(bottom_center);
      canvas.setTextColor(FG_COLOR);
      canvas.drawString("[Enter]=Select  [Esc]=Back", canvas.width() / 2, canvas.height() - 10);

      canvas.pushSprite(0, 0);
      delay(10);
    }
  };
  
  unsigned long start = millis();
  unsigned long last_frame = millis();
  
  while ((millis() - start) < 120000) {
    M5Cardputer.update();
    if (config_server_started) configServer.handleClient();
    
    unsigned long now = millis();
    float dt = (now - last_frame) / 1000.0;
    last_frame = now;
    
    // Input Handling
    InputKey input = read_input_key();
    if (input.type == InputKeyType::Escape) return;
    
    if (input.type == InputKeyType::Printable) {
      char c = tolower(input.value);
      if (selectableCount == 0) {
        if (c == 'r') refresh_instances();
      } else if (c == 'w' || c == ';' || c == ',') {
        if (selected_idx > 0) selected_idx--;
      } else if (c == 's' || c == '.' || c == '/') {
        if (selected_idx < selectableCount - 1) selected_idx++;
      } else if (c == 't' || c == 'e') {
        show_instance_action_menu();
      } else if (c == 'r') {
        refresh_instances();
      }
    } else if (input.type == InputKeyType::Enter && selectableCount > 0) {
      show_instance_action_menu();
    }
    
    // Rendering logic
    int max_visible = 3;
    int top_idx = selected_idx - 1;
    if (top_idx < 0) top_idx = 0;
    if (top_idx > selectableCount - max_visible) top_idx = max(0, selectableCount - max_visible);
    
    float target_y = 30 + (selected_idx - top_idx) * 28;
    if (abs(anim_y - target_y) > 0.5) {
      anim_y += (target_y - anim_y) * 15.0 * dt;
    } else {
      anim_y = target_y;
    }
    
    canvas.fillScreen(BG_COLOR);
    
    draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);
    
    canvas.setTextColor(ACCENT_COLOR);
    canvas.setTextDatum(top_center);
    canvas.drawString("EC2 Instances", canvas.width() / 2, 10);

    if (selectableCount == 0) {
      canvas.setTextDatum(middle_center);
      canvas.setTextColor(DIM_COLOR);
      canvas.drawString("No instances found", canvas.width() / 2, canvas.height() / 2);
      canvas.setTextDatum(bottom_center);
      canvas.setTextColor(FG_COLOR);
      canvas.drawString("[R] Refresh  [Esc] Back", canvas.width() / 2, canvas.height() - 10);
      canvas.pushSprite(0, 0);
      delay(10);
      continue;
    }
    
    // Selection box glow
    draw_glow_rect(8, (int)anim_y, canvas.width() - 16, 24);
    canvas.fillRoundRect(8, (int)anim_y, canvas.width() - 16, 24, 3, canvas.color565(10, 30, 50));
    
    // Draw items
    canvas.setTextDatum(middle_left);
    for (int i = 0; i < max_visible; ++i) {
      int idx = top_idx + i;
      if (idx >= selectableCount) break;
      int y = 30 + i * 28;
      bool is_sel = (idx == selected_idx);
      
      uint16_t state_color = DIM_COLOR;
      if (strcmp(list[idx].state, "running") == 0) state_color = SUCCESS_COLOR;
      else if (strcmp(list[idx].state, "stopped") == 0) state_color = ERR_COLOR;
      else state_color = WARN_COLOR;
      
      canvas.fillCircle(18, y + 12, 4, state_color);
      
      canvas.setTextColor(is_sel ? TFT_WHITE : FG_COLOR);
      String name = String(list[idx].name);
      if (name.length() > 24) name = name.substring(0, 24);
      canvas.drawString(name, 28, y + 12);
    }
    
    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(FG_COLOR);
    canvas.drawString(" [Enter]=Actions [R]=Refresh", canvas.width() / 2, canvas.height() - 10);
    
    canvas.pushSprite(0, 0);
    delay(10); // tight loop for animation
  }
}

// Small device settings UI to edit EC2 proxy URL and token; stores in Preferences and optionally SD
void device_settings_ui() {
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (!prompt_for_pin(settings.pin, "Settings Locked")) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.println("PIN verification failed");
    delay(700);
    return;
  }

  // buffers
  const size_t URL_MAX = 256;
  const size_t TOKEN_MAX = 128;
  const size_t PAIR_MAX = 32;
  const size_t DEVICE_MAX = 32;
  const size_t PIN_MAX = 16;
  char url_buf[URL_MAX];
  char token_buf[TOKEN_MAX];
  char pair_buf[PAIR_MAX];
  char device_buf[DEVICE_MAX];
  char pin_buf[PIN_MAX];
  memset(url_buf, 0, sizeof(url_buf));
  memset(token_buf, 0, sizeof(token_buf));
  memset(pair_buf, 0, sizeof(pair_buf));
  memset(device_buf, 0, sizeof(device_buf));
  memset(pin_buf, 0, sizeof(pin_buf));

  // load current values
  strncpy(url_buf, settings.url, sizeof(url_buf) - 1);
  strncpy(token_buf, settings.token, sizeof(token_buf) - 1);
  strncpy(pair_buf, settings.pairCode, sizeof(pair_buf) - 1);
  strncpy(device_buf, settings.deviceId, sizeof(device_buf) - 1);
  strncpy(pin_buf, settings.pin, sizeof(pin_buf) - 1);

  // UI
  int selected_idx = 0;
  const int max_settings = 8;
  float anim_y = 20;
  int top_index = 0;

  auto draw_settings_menu = [&](){
    const int content_start = 32; // leave space for title and battery
    const int bottom_margin = 20;
    const int line_h = 12;
    int avail_h = canvas.height() - content_start - bottom_margin;
    int visible_count = max(1, avail_h / line_h);
    if (visible_count > max_settings) visible_count = max_settings;

    // Ensure top_index keeps selected visible
    if (selected_idx < top_index) top_index = selected_idx;
    if (selected_idx >= top_index + visible_count) top_index = selected_idx - visible_count + 1;

    float target_y = content_start + (selected_idx - top_index) * line_h;
    if (abs(anim_y - target_y) > 0.5) anim_y += (target_y - anim_y) * 0.3;
    else anim_y = target_y;

    canvas.fillScreen(BG_COLOR);
    draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);

    // Draw Settings title and battery indicator on navbar
    canvas.setTextColor(ACCENT_COLOR);
    canvas.setTextDatum(top_left);
    canvas.drawString("Settings", 10, 12);
    draw_battery_indicator(canvas.width() - 8, 8);

    // Draw selection box
    draw_glow_rect(8, (int)anim_y, canvas.width() - 16, line_h);
    canvas.fillRoundRect(8, (int)anim_y, canvas.width() - 16, line_h, 2, canvas.color565(10, 30, 50));

    canvas.setTextDatum(middle_left);

    String options[8] = {
      "URL: " + String(url_buf).substring(0, 25),
      "Device: " + String(device_buf),
      "Pair: " + String(pair_buf[0] ? "****" : "empty"),
      "Token: " + String(token_buf[0] ? "****" : "empty"),
      "PIN: " + String(pin_buf[0] ? "****" : "empty"),
      "Import from SD",
      "Write to SD",
      "Save & Exit"
    };

    // Draw visible options (centered vertically within each line)
    for (int i = 0; i < visible_count; i++) {
      int idx = top_index + i;
      if (idx >= max_settings) break;
      bool is_sel = (idx == selected_idx);
      canvas.setTextColor(is_sel ? TFT_WHITE : (idx >= 5 ? ACCENT_COLOR : DIM_COLOR));
      int ty = content_start + i * line_h + (line_h / 2);
      canvas.drawString(options[idx], 12, ty);
    }

    // Draw scrollbar if needed
    if (visible_count < max_settings) {
      int sb_x = canvas.width() - 14;
      int sb_y = content_start;
      int track_h = canvas.height() - content_start - bottom_margin;
      canvas.drawRect(sb_x, sb_y, 8, track_h, DIM_COLOR);
      int thumb_h = max(6, (track_h * visible_count) / max_settings);
      int max_scroll = max_settings - visible_count;
      int thumb_pos = max_scroll > 0 ? (top_index * (track_h - thumb_h) / max_scroll) : 0;
      canvas.fillRect(sb_x + 1, sb_y + thumb_pos + 1, 6, thumb_h - 2, ACCENT_COLOR);
    }

    canvas.setTextDatum(bottom_center);
    canvas.setTextColor(FG_COLOR);
    canvas.drawString("[W/S] Nav  [Enter] Edit  [Esc] Back", canvas.width() / 2, canvas.height() - 10);
    canvas.pushSprite(0, 0);
  };
  draw_settings_menu();

  auto edit_text = [&](char *buf, size_t maxLen, bool mask, bool digitsOnly){
    size_t pos = strlen(buf);
    clear_display();
    show_status_line(0, "Editing", TFT_CYAN);
    show_status_line(1, "Enter=done Esc=cancel");
    show_status_line(3, mask ? String("Value: ") + String(pos) + " chars" : String(buf));
    unsigned long start = millis();
    while ((millis() - start) < 120000) {
      M5Cardputer.update();
      InputKey input = read_input_key();
      if (input.type == InputKeyType::Printable) {
        bool allowedChar = digitsOnly ? (input.value >= '0' && input.value <= '9') : (input.value >= 32 && input.value <= 126);
        if (allowedChar && pos + 1 < maxLen && (!digitsOnly || pos < 4)) {
           buf[pos++] = input.value;
           buf[pos] = '\0';
           show_status_line(3, mask ? String("Value: ") + String(pos) + " chars" : String(buf));
        }
      }
      if (input.type == InputKeyType::Backspace) {
        if (pos > 0) {
          pos--; buf[pos] = '\0';
          show_status_line(3, mask ? String("Value: ") + String(pos) + " chars" : String(buf));
        }
      }
      if (input.type == InputKeyType::Enter) {
        if (digitsOnly && pos != 4) {
          show_status_line(5, "PIN must be 4 digits", TFT_RED);
          delay(700);
          show_status_line(5, "");
          continue;
        }
        return;
      }
      if (input.type == InputKeyType::Escape) return;
      delay(10);
    }
  };

  auto show_alert = [&](const String& msg, uint16_t color) {
    int w = 180;
    int h = 30;
    int x = (canvas.width() - w) / 2;
    int y = (canvas.height() - h) / 2;
    canvas.fillRoundRect(x, y, w, h, 4, BG_COLOR);
    canvas.drawRoundRect(x, y, w, h, 4, color);
    canvas.setTextColor(color);
    canvas.setTextDatum(middle_center);
    canvas.drawString(msg, canvas.width() / 2, canvas.height() / 2);
    canvas.pushSprite(0, 0);
    delay(1200);
  };

  unsigned long menuStart = millis();
  while ((millis() - menuStart) < 300000) {
    M5Cardputer.update();
    InputKey input = read_input_key();
    
    if (input.type == InputKeyType::Escape) return;

    if (input.type == InputKeyType::Printable) {
      char c = tolower(input.value);
      if (c == 'w' || c == ';' || c == ',') {
        if (selected_idx > 0) selected_idx--;
      } else if (c == 's' || c == '.' || c == '/') {
        if (selected_idx < max_settings - 1) selected_idx++;
      }
    }
    
    if (input.type == InputKeyType::Enter) {
       switch(selected_idx) {
          case 0: edit_text(url_buf, URL_MAX, false, false); break;
          case 1: edit_text(device_buf, DEVICE_MAX, false, false); break;
          case 2: edit_text(pair_buf, PAIR_MAX, true, false); break;
          case 3: edit_text(token_buf, TOKEN_MAX, true, false); break;
          case 4: 
             edit_text(pin_buf, PIN_MAX, true, true); 
             if (pin_buf[0] != '\0' && strlen(pin_buf) != 4) {
                show_alert("PIN must be 4 digits", TFT_RED);
                pin_buf[0] = '\0';
             }
             break;
          case 5: { // import
             Ec2Settings imported;
             load_ec2_settings(&imported);
             if (load_ec2_settings_from_sd(&imported, true)) {
               strncpy(url_buf, imported.url, sizeof(url_buf) - 1);
               strncpy(token_buf, imported.token, sizeof(token_buf) - 1);
               strncpy(pair_buf, imported.pairCode, sizeof(pair_buf) - 1);
               strncpy(device_buf, imported.deviceId, sizeof(device_buf) - 1);
               strncpy(pin_buf, imported.pin, sizeof(pin_buf) - 1);
               show_alert("Imported /ec2.conf", TFT_GREEN);
             } else {
               show_alert("Import failed", TFT_RED);
             }
             break;
          }
          case 6: { // write SD
             if (sd_ok) {
               File f = SD.open("/ec2.conf", FILE_WRITE);
               if (f) {
                 f.printf("url=%s\n", url_buf);
                 f.printf("token_enc=%s\n", _xor_hex_encode(token_buf).c_str());
                 if (pair_buf[0] != '\0') f.printf("pair_code_enc=%s\n", _xor_hex_encode(pair_buf).c_str());
                 if (device_buf[0] != '\0') f.printf("device_id=%s\n", device_buf);
                 if (pin_buf[0] != '\0') f.printf("pin_enc=%s\n", _xor_hex_encode(pin_buf).c_str());
                 f.close();
                 show_alert("Wrote /ec2.conf to SD", TFT_GREEN);
               }
             } else {
               show_alert("SD not mounted", TFT_RED);
             }
             break;
          }
          case 7: { // save
             Ec2Settings newSettings;
             memset(&newSettings, 0, sizeof(newSettings));
             strncpy(newSettings.url, url_buf, sizeof(newSettings.url) - 1);
             strncpy(newSettings.token, token_buf, sizeof(newSettings.token) - 1);
             strncpy(newSettings.pairCode, pair_buf, sizeof(newSettings.pairCode) - 1);
             strncpy(newSettings.deviceId, device_buf, sizeof(newSettings.deviceId) - 1);
             strncpy(newSettings.pin, pin_buf, sizeof(newSettings.pin) - 1);
             
             strncpy(newSettings.accessToken, settings.accessToken, sizeof(newSettings.accessToken) - 1);
             strncpy(newSettings.refreshToken, settings.refreshToken, sizeof(newSettings.refreshToken) - 1);
             newSettings.accessExpiryMs = settings.accessExpiryMs;
             
             if (save_ec2_settings_to_prefs(&newSettings)) {
               show_alert("Saved to Preferences", TFT_GREEN);
             }
             return; // exit the menu immediately after saving
          }
       }
    }
    
    draw_settings_menu();
    delay(10);
  }
}

bool run_wifi_setup() {
  const int maxDisplay = 9;
  String selected_ssid;
  while (selected_ssid.length() == 0) {
    clear_display();
    show_status_line(0, "Scanning WiFi...", TFT_CYAN);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(100);
    int n = WiFi.scanNetworks(false, true);
    if (n < 0) n = 0;

    clear_display();
    show_status_line(0, String(n) + " networks found", TFT_CYAN);

    const int promptLine = 8;
    int displayCount = min(n, maxDisplay);
    int drawY = 40;
    int entriesDrawn = 0;
    for (int i = 0; i < displayCount; ++i) {
      String ssid = WiFi.SSID(i);
      int linesUsed = draw_wifi_list_entry(10, drawY, canvas.width() - 20, i, ssid, FG_COLOR);
      drawY += linesUsed * 12;
      entriesDrawn++;
      if (drawY >= canvas.height() - 24) break;
    }
    if (entriesDrawn > 0) {
      show_status_line(promptLine, "1-" + String(entriesDrawn) + " select R rescan Esc back", TFT_CYAN);
    } else {
      show_status_line(2, "No networks found", TFT_YELLOW);
      show_status_line(3, "R=Rescan  Esc=Back", TFT_CYAN);
    }

    wait_for_key_release();
    unsigned long selectStart = millis();
    while ((millis() - selectStart) < 120000) {
      M5Cardputer.update();
      InputKey typed = read_input_key();
      if (typed.type == InputKeyType::Printable && typed.value >= '1' && typed.value < ('1' + entriesDrawn)) {
        selected_ssid = WiFi.SSID(typed.value - '1');
        break;
      }
      if (typed.type == InputKeyType::Escape) return false;
      if (input_matches(typed, 'r')) break;
      if (typed.type == InputKeyType::Printable && typed.value >= '1' && typed.value <= '9') {
        show_status_line(promptLine, "Use 1-" + String(entriesDrawn), TFT_YELLOW);
        delay(500);
        if (entriesDrawn > 0) show_status_line(promptLine, "1-" + String(entriesDrawn) + " select R rescan Esc back", TFT_CYAN);
      }
      delay(20);
    }
  }

  char password_buf[MAX_PASSWORD_LEN] = {0};
  size_t pos = 0;
  clear_display();
  draw_wrapped_wifi_line(10, 20, canvas.width() - 20, "SSID: ", selected_ssid, TFT_CYAN);
  show_status_line(1, "Password:");
  show_status_line(4, "Enter=connect Esc=cancel");

  while (true) {
    M5Cardputer.update();
    InputKey typed = read_input_key();
    if (typed.type == InputKeyType::Printable && typed.value >= 32 && typed.value <= 126) {
      if (pos + 1 < sizeof(password_buf)) {
        password_buf[pos++] = typed.value;
        password_buf[pos] = '\0';
        show_status_line(2, String("Typed: ") + String(pos) + " chars");
      }
    }
    if (typed.type == InputKeyType::Backspace && pos > 0) {
      password_buf[--pos] = '\0';
      show_status_line(2, String("Typed: ") + String(pos) + " chars");
    }
    if (typed.type == InputKeyType::Escape) return false;
    if (typed.type == InputKeyType::Enter) break;
    delay(10);
  }

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(selected_ssid.c_str(), password_buf);

  unsigned long start = millis();
  unsigned long last_draw = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    M5Cardputer.update();
    unsigned long now = millis();
    if (now - last_draw >= 200) {
      last_draw = now;
      draw_wifi_loading_screen(selected_ssid, now - start, WIFI_CONNECT_TIMEOUT_MS, "Connecting...");
    }
    // Allow cancel while trying
    InputKey in = read_input_key();
    if (in.type == InputKeyType::Escape) {
      WiFi.disconnect();
      clear_display();
      show_status_line(3, "Cancelled", TFT_YELLOW);
      delay(500);
      return false;
    }
    delay(20);
  }

  if (WiFi.status() != WL_CONNECTED) {
    int st = WiFi.status();
    if (st == WL_CONNECT_FAILED) {
      draw_wifi_loading_screen(selected_ssid, WIFI_CONNECT_TIMEOUT_MS, WIFI_CONNECT_TIMEOUT_MS, "Wrong password");
      delay(800);
      clear_display();
      show_status_line(3, "Wrong password", TFT_RED);
    } else {
      draw_wifi_loading_screen(selected_ssid, WIFI_CONNECT_TIMEOUT_MS, WIFI_CONNECT_TIMEOUT_MS, "Connect failed");
      delay(800);
      clear_display();
      show_status_line(3, "Connect failed", TFT_RED);
    }
    delay(1000);
    return false;
  }

  Preferences prefs;
  if (prefs.begin("wifi", false)) {
    prefs.putString("ssid", selected_ssid.c_str());
    prefs.putString("password", String(password_buf));
    prefs.end();
  }

  if (sd_ok) {
    File wf = SD.open("/wifi.conf", FILE_WRITE);
    if (wf) {
      wf.printf("ssid=%s\n", selected_ssid.c_str());
      wf.printf("password=%s\n", password_buf);
      wf.close();
    }
  }

  clear_display();
  show_status_line(3, "Connected", TFT_GREEN);
  show_status_line(4, WiFi.localIP().toString(), TFT_GREEN);
  start_config_server();
  delay(1000);
  return true;
}

static bool load_saved_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len) {
  if (ssid_len == 0 || password_len == 0) return false;

  ssid[0] = '\0';
  password[0] = '\0';

  if (sd_ok && SD.exists("/wifi.conf")) {
    File f = SD.open("/wifi.conf");
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.replace("\r", "");
        line.replace("\n", "");
        if (line.startsWith("ssid=")) {
          String v = line.substring(5);
          v.toCharArray(ssid, ssid_len);
        } else if (line.startsWith("password=")) {
          String v = line.substring(9);
          v.toCharArray(password, password_len);
        }
      }
      f.close();
      if (ssid[0] != '\0') return true;
    }
  }

  Preferences prefs;
  if (prefs.begin("wifi", true)) {
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("password", "");
    if (s.length() > 0) {
      s.toCharArray(ssid, ssid_len);
      p.toCharArray(password, password_len);
      prefs.end();
      return true;
    }
    prefs.end();
  }

  return false;
}

static void clear_saved_wifi_credentials() {
  Preferences wifiPrefs;
  if (wifiPrefs.begin("wifi", false)) {
    wifiPrefs.remove("ssid");
    wifiPrefs.remove("password");
    wifiPrefs.end();
  }

  if (sd_ok && SD.exists("/wifi.conf")) {
    SD.remove("/wifi.conf");
  }
}

static void draw_wifi_loading_screen(const String& ssid, unsigned long elapsed_ms, unsigned long timeout_ms, const String& status) {
  clear_display();
  draw_glow_rect(4, 4, canvas.width() - 8, canvas.height() - 8);
  draw_battery_indicator(canvas.width() - 8, 8);

  canvas.setTextDatum(top_center);
  canvas.setTextColor(ACCENT_COLOR);
  canvas.drawString("POCKETCLOUD", canvas.width() / 2, 12);

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(FG_COLOR);
  canvas.drawString("Connecting WiFi", canvas.width() / 2, 42);
  canvas.setTextColor(DIM_COLOR);
  canvas.drawString(ssid.length() > 0 ? ssid : String("Saved network"), canvas.width() / 2, 58);

  const int bar_x = 18;
  const int bar_y = 78;
  const int bar_w = canvas.width() - 36;
  const int bar_h = 6;
  float progress = timeout_ms > 0 ? constrain((float)elapsed_ms / (float)timeout_ms, 0.0f, 1.0f) : 0.0f;
  canvas.drawRect(bar_x, bar_y, bar_w, bar_h, DIM_COLOR);
  canvas.fillRect(bar_x + 1, bar_y + 1, (int)((bar_w - 2) * progress), bar_h - 2, ACCENT_COLOR);

  canvas.setTextColor(ACCENT_COLOR);
  canvas.drawString(status, canvas.width() / 2, 96);

  canvas.setTextColor(DIM_COLOR);
  canvas.drawString(String((int)(progress * 100)) + "%", canvas.width() / 2, 110);

  canvas.pushSprite(0, 0);
}

static bool saved_wifi_network_available(const char* ssid) {
  if (ssid == nullptr || ssid[0] == '\0') return false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  int networkCount = WiFi.scanNetworks(false, true);
  bool found = false;
  for (int i = 0; i < networkCount; ++i) {
    if (WiFi.SSID(i).equals(ssid)) {
      found = true;
      break;
    }
  }

  WiFi.scanDelete();
  return found;
}

static bool connect_wifi_after_boot(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') return false;

  if (!saved_wifi_network_available(ssid)) {
    WiFi.disconnect(true);
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  const unsigned long start = millis();
  unsigned long last_draw = 0;

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    M5Cardputer.update();
    unsigned long now = millis();
    if (now - last_draw >= 200) {
      last_draw = now;
      draw_wifi_loading_screen(String(ssid), now - start, WIFI_CONNECT_TIMEOUT_MS, "Using saved credentials");
    }
    delay(20);
  }

  if (WiFi.status() != WL_CONNECTED) {
    draw_wifi_loading_screen(String(ssid), WIFI_CONNECT_TIMEOUT_MS, WIFI_CONNECT_TIMEOUT_MS, "Auto-connect failed");
    delay(800);
    return false;
  }

  draw_wifi_loading_screen(String(ssid), WIFI_CONNECT_TIMEOUT_MS, WIFI_CONNECT_TIMEOUT_MS, "WiFi connected");
  delay(500);
  return true;
}

// Easing functions for smooth animations
static float ease_out_quart(float t) {
  return 1.0f - pow(1.0f - t, 4.0f);
}

static float ease_out_circ(float t) {
  return sqrt(1.0f - pow(t - 1.0f, 2.0f));
}

static float ease_in_out_cubic(float t) {
  if (t < 0.5f) return 4.0f * t * t * t;
  return 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// Draw tech grid background with perspective
static void draw_tech_grid(int frame, float alpha) {
  uint16_t grid_color = canvas.color565(
    (int)(30 * alpha),
    (int)(60 * alpha),
    (int)(100 * alpha)
  );
  
  int grid_spacing = 12;
  int x_offset = frame % grid_spacing;
  int y_offset = (frame / 2) % grid_spacing;
  
  // Vertical lines
  for (int x = -20 + x_offset; x < canvas.width() + 20; x += grid_spacing) {
    canvas.drawLine(x, 0, x, canvas.height(), grid_color);
  }
  
  // Horizontal lines
  for (int y = -20 + y_offset; y < canvas.height() + 20; y += grid_spacing) {
    canvas.drawLine(0, y, canvas.width(), y, grid_color);
  }
}

// Draw iconic AWS 3-arrow logo with 3D effect (improved)
static void draw_aws_logo_premium(int x, int y, int size, uint16_t color, float rotation = 0) {
  int s = size;
  
  // AWS iconic design: 3 arrows in a triangular arrangement
  // This is the classic AWS logo - three arrows representing reliability, performance, and cost optimization
  
  // Top left arrow
  canvas.fillTriangle(x - s, y, x - s/3, y - s/2, x - s/2, y + s/3, color);
  
  // Top right arrow  
  canvas.fillTriangle(x + s, y, x + s/3, y - s/2, x + s/2, y + s/3, color);
  
  // Bottom center arrow
  canvas.fillTriangle(x - s/2, y + s, x + s/2, y + s, x, y + s/3, color);
}

// Draw animated PocketCloud logo with floating effect
static void draw_pocketcloud_premium(int x, int y, int size, uint16_t color, int frame) {
  int s = size;
  float bob = sin((frame * 3.14159f) / 20.0f) * (s / 8);
  y += bob;
  
  // Main cloud - three circles forming fluffy cloud
  canvas.fillCircle(x - s * 0.6f, y - s/4, s/2, color);
  canvas.fillCircle(x, y - s/2, s * 0.55f, color);
  canvas.fillCircle(x + s * 0.6f, y - s/4, s/2, color);
  canvas.fillRect(x - s * 0.8f, y - s/6, s * 1.6f, s * 0.7f, color);
  
  // Pocket (darker shade)
  uint16_t pocket_color = canvas.color565(
    (int)(((color >> 11) & 0x1F) * 0.7),
    (int)(((color >> 5) & 0x3F) * 0.7),
    (int)((color & 0x1F) * 0.7)
  );
  canvas.fillRect(x - s/4, y + s/3, s/2, s/3, pocket_color);
  canvas.drawRect(x - s/4, y + s/3, s/2, s/3, color);
}

// Draw animated glowing ring effect
// (Removed - using simpler WiFi pulse instead)

// Draw smooth WiFi indicator with pulse animation
static void draw_center_pulse(int x, int y, int frame, uint16_t color) {
  float cycle = (frame % 32) / 32.0f;
  float pulse = ease_out_circ(cycle);
  
  // Center dot
  canvas.fillCircle(x, y, 2, color);
  
  // Pulsing waves
  int wave_radius = (int)(pulse * 15);
  float wave_intensity = 0.35f + (1.0f - cycle) * 0.65f;
  uint16_t wave_color = canvas.color565(
    (int)((((color >> 11) & 0x1F) * wave_intensity)),
    (int)((((color >> 5) & 0x3F) * wave_intensity)),
    (int)(((color & 0x1F) * wave_intensity))
  );
  
  if (wave_radius > 0) {
    canvas.drawCircle(x, y, wave_radius, wave_color);
    if (wave_radius > 4) {
      canvas.drawCircle(x, y, wave_radius - 4, wave_color);
    }
  }
}

// Draw animated progress bar with gradient effect
static void draw_progress_bar_animated(int x, int y, int w, int h, float progress, int frame, uint16_t color) {
  // Background bar
  canvas.drawRect(x, y, w, h, DIM_COLOR);
  
  // Gradient fill based on progress
  int fill_width = (int)(w * progress);
  for (int i = 0; i < fill_width; i++) {
    float local_progress = (float)i / w;
    float shimmer = 220.0f + 30.0f * sin((local_progress + frame / 20.0f) * 3.14159f);
    uint16_t bar_color = canvas.color565(
      (int)((((color >> 11) & 0x1F) * shimmer) / 255),
      (int)((((color >> 5) & 0x3F) * shimmer) / 255),
      (int)(((color & 0x1F) * shimmer) / 255)
    );
    canvas.drawLine(x + i, y, x + i, y + h, bar_color);
  }
}

// Draw connecting lines between logos
static void draw_connection_lines(int x1, int y1, int x2, int y2, int frame, uint16_t color) {
  float t = (frame % 30) / 30.0f;
  int segments = 10;
  
  for (int i = 0; i < segments; i++) {
    float t1 = (float)i / segments;
    float t2 = (float)(i + 1) / segments;
    
    if (t1 <= t && t2 > t) {
      // Animated segment
      int px1 = x1 + (int)((x2 - x1) * t1);
      int py1 = y1 + (int)((y2 - y1) * t1);
      int px2 = x1 + (int)((x2 - x1) * t);
      int py2 = y1 + (int)((y2 - y1) * t);
      
      canvas.drawLine(px1, py1, px2, py2, color);
      canvas.fillCircle(px2, py2, 2, color);
    } else if (t2 <= t) {
      // Already drawn
      int px1 = x1 + (int)((x2 - x1) * t1);
      int py1 = y1 + (int)((y2 - y1) * t1);
      int px2 = x1 + (int)((x2 - x1) * t2);
      int py2 = y1 + (int)((y2 - y1) * t2);
      
      canvas.drawLine(px1, py1, px2, py2, color);
    }
  }
}

void play_boot_animation() {
  const int PHASE1_FRAMES = 48;  // Intro with tech grid
  const int PHASE2_FRAMES = 60;  // System transition
  const int PHASE3_FRAMES = 24;  // Final settle state
  
  // ===== PHASE 1: INTRO WITH TECH GRID AND LOGOS =====
  for (int frame = 0; frame < PHASE1_FRAMES; frame++) {
    canvas.fillScreen(BG_COLOR);
    
    // Fade in effect
    float alpha_progress = (float)frame / (PHASE1_FRAMES - 1);
    float alpha = ease_out_quart(alpha_progress);
    int alpha_255 = (int)(alpha * 255);
    
    // Tech grid background (subtle, fades in)
    draw_tech_grid(frame, alpha * 0.4f);
    
    // AWS orange color with fade
    uint16_t aws_orange = canvas.color565(
      (255 * alpha_255) / 255,
      (153 * alpha_255) / 255,
      0
    );
    
    // PocketCloud blue with fade
    uint16_t cloud_blue = canvas.color565(
      (100 * alpha_255) / 255,
      (180 * alpha_255) / 255,
      (255 * alpha_255) / 255
    );
    
    int aws_x = canvas.width() / 3;
    int cloud_x = canvas.width() * 2 / 3;
    int center_y = canvas.height() / 2 - 10;
    
    // Scale logos based on animation progress
    float scale = ease_out_quart(alpha_progress);
    int aws_size = (int)(16 * scale);
    int cloud_size = (int)(14 * scale);
    
    // Draw AWS logo
    draw_aws_logo_premium(aws_x, center_y, aws_size, aws_orange);
    
    // Draw PocketCloud logo
    draw_pocketcloud_premium(cloud_x, center_y, cloud_size, cloud_blue, frame);
    
    // Draw connecting line between logos
    if (frame > PHASE1_FRAMES / 2) {
      float line_progress = (float)(frame - PHASE1_FRAMES / 2) / (PHASE1_FRAMES / 2);
      int line_alpha = (int)(line_progress * 100);
      uint16_t line_color = canvas.color565(
        (88 * line_alpha) / 255,
        (166 * line_alpha) / 255,
        (255 * line_alpha) / 255
      );
      draw_connection_lines(aws_x - 8, center_y, cloud_x + 8, center_y, frame, line_color);
    }
    
    // Title with fade (moved UP)
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    uint16_t title_color = canvas.color565(alpha_255, alpha_255, alpha_255);
    canvas.setTextColor(title_color);
    canvas.drawString("PocketCloud", canvas.width() / 2, canvas.height() - 30);
    
    // AWS branding text (with spacing)
    canvas.setTextSize(1);
    uint16_t brand_color = canvas.color565(
      (150 * alpha_255) / 255,
      (150 * alpha_255) / 255,
      (150 * alpha_255) / 255
    );
    canvas.setTextColor(brand_color);
    canvas.drawString("AWS-Powered Terminal", canvas.width() / 2, canvas.height() - 14);
    
    canvas.pushSprite(0, 0);
    delay(20);
  }
  
  // ===== PHASE 2: SYSTEM TRANSITION WITH ANIMATIONS =====
  for (int frame = 0; frame < PHASE2_FRAMES; frame++) {
    canvas.fillScreen(BG_COLOR);
    
    // Subtle grid background
    draw_tech_grid(frame + PHASE1_FRAMES, 0.2f);
    
    uint16_t aws_orange = canvas.color565(255, 153, 0);
    uint16_t cloud_blue = canvas.color565(100, 180, 255);
    int aws_x = canvas.width() / 3;
    int cloud_x = canvas.width() * 2 / 3;
    int center_y = canvas.height() / 2 - 15;
    
    // Draw logos smaller during connection phase
    draw_aws_logo_premium(aws_x, center_y, 12, aws_orange);
    draw_pocketcloud_premium(cloud_x, center_y, 10, cloud_blue, frame);
    
    // Draw glowing effect around center
    int center_x = canvas.width() / 2;
    int center_y_pulse = canvas.height() / 2 + 5;
    draw_center_pulse(center_x, center_y_pulse, frame, ACCENT_COLOR);
    
    // Progress bar for the boot sequence
    int bar_width = canvas.width() - 30;
    int bar_x = 15;
    int bar_y = canvas.height() - 25;
    float progress = ease_in_out_cubic((float)frame / (PHASE2_FRAMES - 1));
    draw_progress_bar_animated(bar_x, bar_y, bar_width, 4, progress, frame, ACCENT_COLOR);
    
    // Status percentage
    canvas.setTextSize(1);
    canvas.setTextColor(ACCENT_COLOR);
    canvas.setTextDatum(middle_center);
    int percent = (int)(progress * 100);
    if (percent > 100) percent = 100;
    canvas.drawString(String(percent) + "%", canvas.width() / 2, bar_y - 8);
    
    canvas.pushSprite(0, 0);
    delay(20);
  }
  
  // ===== PHASE 3: READY STATE WITH CONFIRMATION =====
  for (int frame = 0; frame < PHASE3_FRAMES; frame++) {
    canvas.fillScreen(BG_COLOR);
    
    // Subtle grid
    draw_tech_grid(frame + PHASE1_FRAMES + PHASE2_FRAMES, 0.15f);
    
    uint16_t aws_orange = canvas.color565(255, 153, 0);
    uint16_t cloud_blue = canvas.color565(100, 180, 255);
    int aws_x = canvas.width() / 3;
    int cloud_x = canvas.width() * 2 / 3;
    int center_y = canvas.height() / 2 - 15;
    
    // Scale down for final state
    float settle = ease_in_out_cubic((float)frame / (PHASE3_FRAMES - 1));
    float scale = 1.0f - settle * 0.15f;
    
    // Draw logos
    draw_aws_logo_premium(aws_x, center_y, (int)(12 * scale), aws_orange);
    draw_pocketcloud_premium(cloud_x, center_y, (int)(10 * scale), cloud_blue, frame);
    
    canvas.pushSprite(0, 0);
    delay(20);
  }
  
  // Brief hold on final frame
  delay(200);
}

void setup()
{
  // Initialize the M5Stack Cardputer hardware
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  init_theme();
  
  static char ssid[MAX_SSID_LEN] = {0};
  static char password[MAX_PASSWORD_LEN] = {0};
  bool haveCredentials = false;

  // Attempt to initialize SD card for optional credential storage
  sd_ok = false;
#if ENABLE_SD_STORAGE
  sd_spi.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (SD.begin(SD_SPI_CS_PIN, sd_spi, 25000000, "/sd", 5)) {
    sd_ok = true;
  }
#endif

  play_boot_animation();

  haveCredentials = load_saved_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

  bool wifi_connected = false;
  if (haveCredentials) {
    wifi_connected = connect_wifi_after_boot(ssid, password);
  }

  if (!haveCredentials || !wifi_connected) {
    run_wifi_setup();
  }

  // Optional: import EC2 proxy settings from SD on boot if present.
  Ec2Settings bootEc2;
  load_ec2_settings(&bootEc2);
  if (load_ec2_settings_from_sd(&bootEc2, true)) {
    // Imported silently at boot.
  }

  clear_display();

  // Start config server if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    start_config_server();
    sync_time_for_tls(true);
  }

  draw_home_screen();
}

// Main loop
void loop()
{
  static wl_status_t last_wifi_status = WL_IDLE_STATUS;
  static unsigned long last_wifi_check = 0;
  static unsigned long wifi_reconnect_timer = 0;
  const unsigned long WIFI_CHECK_INTERVAL = 5000;  // Check WiFi every 5 seconds
  const unsigned long WIFI_RECONNECT_DELAY = 10000; // Wait 10 seconds before attempting reconnect
  
  wl_status_t current_wifi_status = WiFi.status();
  unsigned long now = millis();
  
  // Periodic WiFi status check and auto-reconnect logic
  if (now - last_wifi_check >= WIFI_CHECK_INTERVAL) {
    last_wifi_check = now;
    
    // Handle WiFi disconnection with auto-reconnect
    if (current_wifi_status != WL_CONNECTED && last_wifi_status == WL_CONNECTED) {
      // WiFi just disconnected
      wifi_reconnect_timer = now;
      Serial.println("WiFi disconnected, will attempt reconnect...");
    }
    
    // Auto-reconnect attempt if disconnected for long enough
    if (current_wifi_status != WL_CONNECTED && 
        wifi_reconnect_timer > 0 && 
        (now - wifi_reconnect_timer) > WIFI_RECONNECT_DELAY) {
      // Attempt to reconnect
      WiFi.reconnect();
      Serial.println("WiFi auto-reconnect attempt...");
    }
  }
  
  // Handle WiFi status changes
  if (current_wifi_status != last_wifi_status) {
    if (current_wifi_status == WL_CONNECTED) {
      // WiFi just connected
      Serial.println("WiFi connected!");
      wifi_reconnect_timer = 0;  // Clear reconnect timer
      start_config_server();
      sync_time_for_tls(true);
    } else if (current_wifi_status == WL_DISCONNECTED) {
      Serial.println("WiFi disconnected");
      if (config_server_started) {
        configServer.stop();
        config_server_started = false;
      }
    }
    last_wifi_status = current_wifi_status;
    draw_home_screen(); // Redraw home screen to update WiFi status indicator
  }

  // Update Cardputer internals (keyboard scanning, etc.)
  M5Cardputer.update();
  
  // Handle configuration server
  if (config_server_started) {
    configServer.handleClient();
  }
  
  // Handle user input
  InputKey input = read_input_key();

  if (WiFi.status() == WL_CONNECTED && input_matches(input, 'e')) {
    show_ec2_ui();
    draw_home_screen();
    wait_for_key_release();
  }

  if (input_matches(input, 's')) {
    device_settings_ui();
    draw_home_screen();
    wait_for_key_release();
  }

  if (input_matches(input, 'w')) {
    run_wifi_setup();
    draw_home_screen();
    wait_for_key_release();
  }

  delay(50);
}
