#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "hardware_config.h"

// Global SD flag (set in setup)
static bool sd_ok = false;

struct EC2Instance {
  char id[40];
  char name[64];
  char state[20];
};

struct Ec2Settings {
  char url[256];
  char token[128]; // legacy static bearer token fallback
  char pairCode[32];
  char deviceId[32];
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
    String u = prefs.getString("url", "");
    String tokenEnc = prefs.getString("token_enc", "");
    String pairEnc = prefs.getString("pair_code_enc", "");
    String devId = prefs.getString("device_id", "");
    String accessEnc = prefs.getString("access_token_enc", "");
    String refreshEnc = prefs.getString("refresh_token_enc", "");
    String pinEnc = prefs.getString("pin_enc", "");
    String t = tokenEnc.length() > 0 ? _xor_hex_decode(tokenEnc) : prefs.getString("token", "");
    String pc = pairEnc.length() > 0 ? _xor_hex_decode(pairEnc) : prefs.getString("pair_code", "");
    String at = accessEnc.length() > 0 ? _xor_hex_decode(accessEnc) : prefs.getString("access_token", "");
    String rt = refreshEnc.length() > 0 ? _xor_hex_decode(refreshEnc) : prefs.getString("refresh_token", "");
    String p = pinEnc.length() > 0 ? _xor_hex_decode(pinEnc) : prefs.getString("pin", "");
    if (u.length() > 0) u.toCharArray(out->url, sizeof(out->url));
    if (t.length() > 0) t.toCharArray(out->token, sizeof(out->token));
    if (pc.length() > 0) pc.toCharArray(out->pairCode, sizeof(out->pairCode));
    if (devId.length() > 0) devId.toCharArray(out->deviceId, sizeof(out->deviceId));
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
  File f = SD_MMC.open("/ec2.conf", FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
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

void draw_masked_token(const char* token) {
  size_t n = strlen(token);
  for (size_t i = 0; i < n; ++i) M5Cardputer.Display.print('*');
  M5Cardputer.Display.println();
}

bool prompt_for_pin(const char* expectedPin, const char* promptTitle) {
  if (expectedPin == nullptr || expectedPin[0] == '\0') return true;

  char entered[5] = {0};
  size_t pos = 0;
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.println(promptTitle);
  M5Cardputer.Display.println("Enter 4-digit PIN then Enter:");

  unsigned long started = millis();
  while ((millis() - started) < 30000) {
    M5Cardputer.update();
    for (char ch = '0'; ch <= '9'; ++ch) {
      if (M5Cardputer.Keyboard.isKeyPressed(ch) && pos < 4) {
        entered[pos++] = ch;
        entered[pos] = '\0';
        M5Cardputer.Display.print('*');
        delay(120);
      }
    }
    if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_BACKSPACE) && pos > 0) {
      pos--;
      entered[pos] = '\0';
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10, 10);
      M5Cardputer.Display.println(promptTitle);
      M5Cardputer.Display.println("Enter 4-digit PIN then Enter:");
      for (size_t i = 0; i < pos; ++i) M5Cardputer.Display.print('*');
      delay(120);
    }
    if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_ENTER) || M5Cardputer.Keyboard.isKeyPressed('\n') || M5Cardputer.Keyboard.isKeyPressed('\r')) {
      return pos == 4 && strcmp(entered, expectedPin) == 0;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) return false;
    delay(10);
  }

  return false;
}

bool configure_secure_http(HTTPClient* http, WiFiClientSecure* client, const String& url) {
  client->setCACert(AWS_ROOT_CA1);
  client->setTimeout(15000);
  return http->begin(*client, url);
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
  int code = http.POST(body);
  String payload = http.getString();
  http.end();
  *statusCode = code;
  *responseBody = payload;
  return true;
}

bool ensure_ec2_session(Ec2Settings* settings) {
  const uint32_t now = millis();
  if (settings->accessToken[0] != '\0' && settings->accessExpiryMs > now + 5000U) {
    return true;
  }

  int code = -1;
  String payload;
  StaticJsonDocument<1024> req;

  if (settings->refreshToken[0] != '\0') {
    req.clear();
    req["deviceId"] = settings->deviceId;
    req["refreshToken"] = settings->refreshToken;
    String body;
    serializeJson(req, body);
    if (post_json_with_auth(String(settings->url) + "/refresh", body, "", settings->deviceId, &code, &payload) && code == 200) {
      StaticJsonDocument<MAX_API_RESPONSE> doc;
      if (!deserializeJson(doc, payload)) {
        String at = doc["accessToken"] | "";
        int exp = doc["expiresIn"] | 600;
        if (at.length() > 0) {
          at.toCharArray(settings->accessToken, sizeof(settings->accessToken));
          settings->accessExpiryMs = now + (uint32_t)exp * 1000U;
          save_ec2_settings_to_prefs(settings);
          return true;
        }
      }
    }
  }

  if (settings->pairCode[0] != '\0') {
    req.clear();
    req["deviceId"] = settings->deviceId;
    req["pairCode"] = settings->pairCode;
    String body;
    serializeJson(req, body);
    if (post_json_with_auth(String(settings->url) + "/pair", body, "", settings->deviceId, &code, &payload) && code == 200) {
      StaticJsonDocument<MAX_API_RESPONSE> doc;
      if (!deserializeJson(doc, payload)) {
        String at = doc["accessToken"] | "";
        String rt = doc["refreshToken"] | "";
        int exp = doc["expiresIn"] | 600;
        if (at.length() > 0 && rt.length() > 0) {
          at.toCharArray(settings->accessToken, sizeof(settings->accessToken));
          rt.toCharArray(settings->refreshToken, sizeof(settings->refreshToken));
          settings->pairCode[0] = '\0';
          settings->accessExpiryMs = now + (uint32_t)exp * 1000U;
          save_ec2_settings_to_prefs(settings);
          return true;
        }
      }
    }
  }

  // legacy fallback: static admin token
  if (settings->token[0] != '\0') {
    strncpy(settings->accessToken, settings->token, sizeof(settings->accessToken) - 1);
    settings->accessExpiryMs = now + 600000U;
    return true;
  }
  return false;
}

// Fetch instances from EC2 proxy. Returns number of instances (>=0) or -1 on error.
int fetch_ec2_instances(EC2Instance *out, int maxInstances) {
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (settings.url[0] == '\0') return -1;
  if (!ensure_ec2_session(&settings)) return -1;
  String apiUrl = String(settings.url) + "/instances";
  String token = String(settings.accessToken);

  WiFiClientSecure client;
  HTTPClient http;
  if (!configure_secure_http(&http, &client, apiUrl)) return -1;
  if (token.length() > 0) http.addHeader("Authorization", String("Bearer ") + token);
  if (settings.deviceId[0] != '\0') http.addHeader("X-Device-Id", String(settings.deviceId));
  int code = http.GET();
  if (code != 200) {
    http.end();
    return -1;
  }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<MAX_API_RESPONSE> doc;
  auto err = deserializeJson(doc, payload);
  if (err) return -1;
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
  return i;
}

// Send start/stop action. action should be "start" or "stop". Returns true on HTTP 200.
bool send_ec2_action(const char *instanceId, const char *action) {
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (settings.url[0] == '\0') return false;
  if (!ensure_ec2_session(&settings)) return false;
  String apiUrl = String(settings.url) + "/instances/" + String(instanceId) + "/" + String(action);
  String token = String(settings.accessToken);

  WiFiClientSecure client;
  HTTPClient http;
  if (!configure_secure_http(&http, &client, apiUrl)) return false;
  if (token.length() > 0) http.addHeader("Authorization", String("Bearer ") + token);
  if (settings.deviceId[0] != '\0') http.addHeader("X-Device-Id", String(settings.deviceId));
  int code = http.POST("");
  http.end();
  return (code >= 200 && code < 300);
}

// Display EC2 instances UI and allow start/stop via numeric selection
void show_ec2_ui() {
  Ec2Settings settings;
  load_ec2_settings(&settings);
  if (!prompt_for_pin(settings.pin, "EC2 Control Locked")) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.println("PIN verification failed");
    delay(700);
    return;
  }

  EC2Instance list[MAX_INSTANCES];
  int count = fetch_ec2_instances(list, MAX_INSTANCES);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (count < 0) {
    M5Cardputer.Display.println("Failed to fetch instances");
    return;
  }
  if (count == 0) {
    M5Cardputer.Display.println("No instances found");
    return;
  }
  const int selectableCount = (count > 9) ? 9 : count;
  if (count > 9) {
    M5Cardputer.Display.println("Showing first 9 instances");
  }
  for (int i = 0; i < selectableCount; ++i) {
    String line = String(i + 1) + ": " + String(list[i].name) + " (" + String(list[i].state) + ")";
    M5Cardputer.Display.println(line);
  }
  M5Cardputer.Display.println("Select instance 1-" + String(selectableCount));

  unsigned long start = millis();
  while ((millis() - start) < 120000) {
    M5Cardputer.update();
    for (int i = 0; i < selectableCount; ++i) {
      char key = '1' + i;
      if (M5Cardputer.Keyboard.isKeyPressed(key)) {
        // selected
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setCursor(10, 10);
        M5Cardputer.Display.println("Instance: " + String(list[i].name));
        M5Cardputer.Display.println("State: " + String(list[i].state));
        if (strcmp(list[i].state, "running") == 0) {
          M5Cardputer.Display.println("Press 't' to stop");
        } else {
          M5Cardputer.Display.println("Press 't' to start");
        }
        M5Cardputer.Display.println("Press 'b' to go back");
        unsigned long innerStart = millis();
        while ((millis() - innerStart) < 60000) {
          M5Cardputer.update();
          if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) {
            const char *action = (strcmp(list[i].state, "running") == 0) ? "stop" : "start";
            M5Cardputer.Display.println(String("Sending ") + action + "...");
            bool ok = send_ec2_action(list[i].id, action);
            if (ok) M5Cardputer.Display.println("Request sent"); else M5Cardputer.Display.println("Request failed");
            delay(800);
            return; // return to main screen after action
          }
          if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) {
            return;
          }
        }
        return;
      }
    }
    delay(50);
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
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setCursor(10, 10);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.println("Device Settings");
  M5Cardputer.Display.println("Edit URL/token/pair/device");
  M5Cardputer.Display.println("u=url, k=legacy token, c=pair code");
  M5Cardputer.Display.println("d=device id");
  M5Cardputer.Display.println("Press 'p' to edit PIN");
  M5Cardputer.Display.println("Press 'i' to import /ec2.conf from SD");
  M5Cardputer.Display.println("Press 'w' to write SD, 's' save prefs, 'b' back");
  M5Cardputer.Display.println("");
  M5Cardputer.Display.println("URL: ");
  M5Cardputer.Display.println(String(url_buf));
  M5Cardputer.Display.println("Device: " + String(device_buf));
  M5Cardputer.Display.println("Pair code:");
  draw_masked_token(pair_buf);
  M5Cardputer.Display.println("Token: ");
  draw_masked_token(token_buf);
  M5Cardputer.Display.println("PIN set: " + String(pin_buf[0] == '\0' ? "no" : "yes"));

  auto edit_text = [&](char *buf, size_t maxLen, bool mask, bool digitsOnly){
    size_t pos = strlen(buf);
    unsigned long start = millis();
    while ((millis() - start) < 120000) {
      M5Cardputer.update();
      char begin = digitsOnly ? '0' : 32;
      char end = digitsOnly ? '9' : 126;
      for (char ch = begin; ch <= end; ++ch) {
        if (M5Cardputer.Keyboard.isKeyPressed(ch)) {
          if (pos + 1 < maxLen && (!digitsOnly || pos < 4)) {
            buf[pos++] = ch;
            buf[pos] = '\0';
            // redraw small area
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setCursor(10, 10);
            M5Cardputer.Display.println("Editing (Enter to finish)");
            if (mask) {
              draw_masked_token(buf);
            } else {
              M5Cardputer.Display.println(String(buf));
            }
            delay(150);
          }
        }
      }
      if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_BACKSPACE)) {
        if (pos > 0) {
          pos--; buf[pos] = '\0';
          M5Cardputer.Display.fillScreen(TFT_BLACK);
          M5Cardputer.Display.setCursor(10, 10);
          M5Cardputer.Display.println("Editing (Enter to finish)");
          if (mask) {
            draw_masked_token(buf);
          } else {
            M5Cardputer.Display.println(String(buf));
          }
          delay(150);
        }
      }
      if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_ENTER) || M5Cardputer.Keyboard.isKeyPressed('\n') || M5Cardputer.Keyboard.isKeyPressed('\r')) {
        if (digitsOnly && pos != 4) {
          M5Cardputer.Display.fillScreen(TFT_BLACK);
          M5Cardputer.Display.setCursor(10, 10);
          M5Cardputer.Display.println("PIN must be 4 digits");
          delay(700);
          continue;
        }
        return;
      }
      if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) return;
      delay(10);
    }
  };

  unsigned long menuStart = millis();
  while ((millis() - menuStart) < 300000) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isKeyPressed('u') || M5Cardputer.Keyboard.isKeyPressed('U')) {
      edit_text(url_buf, URL_MAX, false, false);
      // redisplay
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10,10);
      M5Cardputer.Display.println("URL: ");
      M5Cardputer.Display.println(String(url_buf));
    }
    if (M5Cardputer.Keyboard.isKeyPressed('k') || M5Cardputer.Keyboard.isKeyPressed('K')) {
      edit_text(token_buf, TOKEN_MAX, true, false);
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10,10);
      M5Cardputer.Display.println("Token: ");
      draw_masked_token(token_buf);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C')) {
      edit_text(pair_buf, PAIR_MAX, true, false);
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10,10);
      M5Cardputer.Display.println("Pair code:");
      draw_masked_token(pair_buf);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('d') || M5Cardputer.Keyboard.isKeyPressed('D')) {
      edit_text(device_buf, DEVICE_MAX, false, false);
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10,10);
      M5Cardputer.Display.println("Device: " + String(device_buf));
    }
    if (M5Cardputer.Keyboard.isKeyPressed('p') || M5Cardputer.Keyboard.isKeyPressed('P')) {
      edit_text(pin_buf, PIN_MAX, true, true);
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setCursor(10,10);
      M5Cardputer.Display.println("PIN set: " + String(pin_buf[0] == '\0' ? "no" : "yes"));
      if (pin_buf[0] != '\0' && strlen(pin_buf) != 4) {
        M5Cardputer.Display.println("PIN must be 4 digits");
        pin_buf[0] = '\0';
        delay(700);
      }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('i') || M5Cardputer.Keyboard.isKeyPressed('I')) {
      Ec2Settings imported;
      load_ec2_settings(&imported);
      if (load_ec2_settings_from_sd(&imported, true)) {
        strncpy(url_buf, imported.url, sizeof(url_buf) - 1);
        strncpy(token_buf, imported.token, sizeof(token_buf) - 1);
        strncpy(pin_buf, imported.pin, sizeof(pin_buf) - 1);
        M5Cardputer.Display.println("Imported /ec2.conf");
      } else {
        M5Cardputer.Display.println("Import failed");
      }
      delay(400);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
      Ec2Settings newSettings;
      memset(&newSettings, 0, sizeof(newSettings));
      strncpy(newSettings.url, url_buf, sizeof(newSettings.url) - 1);
      strncpy(newSettings.token, token_buf, sizeof(newSettings.token) - 1);
      strncpy(newSettings.pairCode, pair_buf, sizeof(newSettings.pairCode) - 1);
      strncpy(newSettings.deviceId, device_buf, sizeof(newSettings.deviceId) - 1);
      strncpy(newSettings.pin, pin_buf, sizeof(newSettings.pin) - 1);
      if (newSettings.pin[0] != '\0' && strlen(newSettings.pin) != 4) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setCursor(10, 10);
        M5Cardputer.Display.println("PIN must be 4 digits");
        delay(700);
        continue;
      }
      newSettings.accessToken[0] = '\0';
      newSettings.refreshToken[0] = '\0';
      newSettings.accessExpiryMs = 0;
      if (save_ec2_settings_to_prefs(&newSettings)) {
        M5Cardputer.Display.println("Saved to Preferences");
      }
      delay(500);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
      if (sd_ok) {
        File f = SD_MMC.open("/ec2.conf", FILE_WRITE);
        if (f) {
          f.printf("url=%s\n", url_buf);
          f.printf("token_enc=%s\n", _xor_hex_encode(token_buf).c_str());
          if (pair_buf[0] != '\0') {
            f.printf("pair_code_enc=%s\n", _xor_hex_encode(pair_buf).c_str());
          }
          if (device_buf[0] != '\0') {
            f.printf("device_id=%s\n", device_buf);
          }
          if (pin_buf[0] != '\0') {
            f.printf("pin_enc=%s\n", _xor_hex_encode(pin_buf).c_str());
          }
          f.close();
          M5Cardputer.Display.println("Wrote /ec2.conf to SD");
        }
      } else {
        M5Cardputer.Display.println("SD not mounted");
      }
      delay(500);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) {
      return;
    }
    delay(50);
  }
}

void setup()
{
  // Initialize the M5Stack Cardputer hardware
  M5Cardputer.begin();

  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  // Attempt to initialize SD card for optional credential storage
  sd_ok = false;
  if (SD_MMC.begin()) {
    sd_ok = true;
    M5Cardputer.Display.setCursor(10, 170);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.println("SD: mounted");
  } else {
    M5Cardputer.Display.setCursor(10, 170);
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.println("SD: not present");
  }

  // Optional: import EC2 proxy settings from SD on boot if present.
  Ec2Settings bootEc2;
  load_ec2_settings(&bootEc2);
  if (load_ec2_settings_from_sd(&bootEc2, true)) {
    M5Cardputer.Display.setCursor(10, 185);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.println("EC2 config imported");
  }

  // Try to load WiFi credentials from SD card first
  static char ssid[MAX_SSID_LEN] = {0};
  static char password[MAX_PASSWORD_LEN] = {0};
  bool haveCredentials = false;

  if (sd_ok) {
    File f = SD_MMC.open("/wifi.conf");
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("ssid=")) {
          String v = line.substring(5);
          v.toCharArray(ssid, MAX_SSID_LEN);
        } else if (line.startsWith("password=")) {
          String v = line.substring(9);
          v.toCharArray(password, MAX_PASSWORD_LEN);
        }
      }
      f.close();
      if (ssid[0] != '\0') haveCredentials = true;
    }
  }

  // If no SD credentials, check Preferences (non-volatile storage)
  if (!haveCredentials) {
    Preferences prefs;
    if (prefs.begin("wifi", true)) {
      String s = prefs.getString("ssid", "");
      String p = prefs.getString("password", "");
      if (s.length() > 0) {
        s.toCharArray(ssid, MAX_SSID_LEN);
        p.toCharArray(password, MAX_PASSWORD_LEN);
        haveCredentials = true;
      }
      prefs.end();
    }
  }

  // If we have credentials, attempt connection
  if (haveCredentials) {
    M5Cardputer.Display.setCursor(10, 200);
    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.print("Connecting to ");
    M5Cardputer.Display.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
      M5Cardputer.update();
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      M5Cardputer.Display.setCursor(10, 220);
      M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
      M5Cardputer.Display.print("WiFi OK: ");
      M5Cardputer.Display.println(WiFi.localIP().toString());
    } else {
      M5Cardputer.Display.setCursor(10, 220);
      M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
      M5Cardputer.Display.println("WiFi connect failed");
    }
  }

  // Finally, scan nearby SSIDs and display them on screen
  M5Cardputer.Display.setCursor(10, 260);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.println("Scanning SSIDs...");
  int n = WiFi.scanNetworks();
  M5Cardputer.Display.println(String(n) + " networks found");
  const int maxDisplay = 9;
  int displayCount = (n > maxDisplay) ? maxDisplay : n;
  for (int i = 0; i < displayCount; ++i) {
    String line = String(i + 1) + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + ")";
    M5Cardputer.Display.println(line);
  }

  // Interactive selection of SSID using keyboard numeric keys 1..displayCount
  if (displayCount > 0) {
    M5Cardputer.Display.println("Select network: press 1-" + String(displayCount) + ", R=rescan");
    String selected_ssid;
    char password_buf[MAX_PASSWORD_LEN];
    memset(password_buf, 0, sizeof(password_buf));

    auto readPassword = [&](char *outBuf, size_t maxLen) {
      size_t pos = 0;
      M5Cardputer.Display.setCursor(10, 320);
      M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5Cardputer.Display.print("Password: ");
      while (true) {
        M5Cardputer.update();
        // check printable ASCII
        for (char ch = 32; ch <= 126; ++ch) {
          if (M5Cardputer.Keyboard.isKeyPressed(ch)) {
            if (pos + 1 < maxLen) {
              outBuf[pos++] = ch;
              M5Cardputer.Display.print('*');
              delay(150);
            }
          }
        }
        // Backspace handling
        if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_BACKSPACE)) {
          if (pos > 0) {
            pos--;
            outBuf[pos] = '\0';
            M5Cardputer.Display.setCursor(10, 320);
            M5Cardputer.Display.print("Password: ");
            for (size_t k = 0; k < pos; ++k) M5Cardputer.Display.print('*');
            delay(150);
          }
        }
        // Enter (several checks)
        if (M5Cardputer.Keyboard.isKeyPressed((char)KEY_ENTER) || M5Cardputer.Keyboard.isKeyPressed('\n') || M5Cardputer.Keyboard.isKeyPressed('\r')) {
          outBuf[pos] = '\0';
          return;
        }
        delay(10);
      }
    };

    bool picked = false;
    unsigned long selectStart = millis();
    while (!picked && (millis() - selectStart) < 120000) { // 2 minute selection timeout
      M5Cardputer.update();
      // check numeric keys 1..displayCount
      for (int i = 0; i < displayCount; ++i) {
        char key = '1' + i;
        if (M5Cardputer.Keyboard.isKeyPressed(key)) {
          selected_ssid = WiFi.SSID(i);
          picked = true;
          break;
        }
      }
      // allow rescanning with 'r' or 'R'
      if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
        M5Cardputer.Display.setCursor(10, 300);
        M5Cardputer.Display.println("Rescanning...");
        n = WiFi.scanNetworks();
        displayCount = (n > maxDisplay) ? maxDisplay : n;
        M5Cardputer.Display.setCursor(10, 300);
        for (int i = 0; i < displayCount; ++i) {
          M5Cardputer.Display.println(String(i + 1) + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + ")");
        }
      }
      delay(50);
    }

    if (picked) {
      // Prompt for password entry
      M5Cardputer.Display.setCursor(10, 300);
      M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5Cardputer.Display.println("Selected: " + selected_ssid);
      readPassword(password_buf, sizeof(password_buf));

      // Save credentials to Preferences (NVS)
      Preferences prefs;
      if (prefs.begin("wifi", false)) {
        prefs.putString("ssid", selected_ssid.c_str());
        prefs.putString("password", String(password_buf));
        prefs.end();
        M5Cardputer.Display.println("Saved to Preferences");
      }

      // Also try to save to SD if mounted
      if (sd_ok) {
        File wf = SD_MMC.open("/wifi.conf", FILE_WRITE);
        if (wf) {
          wf.printf("ssid=%s\n", selected_ssid.c_str());
          wf.printf("password=%s\n", password_buf);
          wf.close();
          M5Cardputer.Display.println("Saved to SD:/wifi.conf");
        }
      }

      // Attempt connection immediately
      WiFi.mode(WIFI_STA);
      WiFi.begin(selected_ssid.c_str(), password_buf);
      unsigned long start2 = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start2) < WIFI_CONNECT_TIMEOUT_MS) {
        M5Cardputer.update();
        delay(200);
      }
      if (WiFi.status() == WL_CONNECTED) {
        M5Cardputer.Display.println("Connected: " + WiFi.localIP().toString());
      } else {
        M5Cardputer.Display.println("Connect failed");
      }
    }
  }

}

// Main loop
void loop()
{
  // Update Cardputer internals (keyboard scanning, etc.)
  M5Cardputer.update();

  // Simple keyboard feedback (non-blocking)
  if (M5Cardputer.Keyboard.isPressed()) {
    M5Cardputer.Display.setCursor(10, 340);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.println("Key pressed");
  }

  // Open EC2 UI when connected and user presses 'e'
  if (WiFi.status() == WL_CONNECTED && (M5Cardputer.Keyboard.isKeyPressed('e') || M5Cardputer.Keyboard.isKeyPressed('E'))) {
    show_ec2_ui();
  }

  // Open settings when user presses 's'
  if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
    device_settings_ui();
  }

  delay(50);
}
