// m5paper trmnl firmware
// uses M5Unified (M5EPD is deprecated), supports PNG and BMP only

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5GFX.h>
#include <ArduinoJson.h>

#define TRMNL_API_KEY "YOUR_TRMNL_API_KEY"
#define M5PAPER_WAKE_BUTTON 39
#define M5EPD_MAIN_PWR_PIN 2

const char* WIFI_SSID = "network-name";
const char* WIFI_PASS = "network-password";

M5Canvas canvas(&M5.Display);
HTTPClient http;
RTC_DATA_ATTR int lastRefreshRate = 900;  // persists across deep sleep

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  cfg.output_power = true;
  cfg.internal_rtc = true;
  M5.begin(cfg);

  M5.Display.setRotation(1);  // landscape mode (960x540)
  M5.Display.clear();
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  setCpuFrequencyMhz(80);
  btStop();

  canvas.createSprite(960, 540);
  canvas.setTextSize(2);

  pinMode(M5PAPER_WAKE_BUTTON, INPUT_PULLUP);

  connectWiFi();

  float batteryVoltage = getBatteryVoltage();
  Serial.printf("Battery voltage: %.2f V\n", batteryVoltage);

  fetchAndDisplay(batteryVoltage);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  canvas.clear();
  canvas.drawString("Connecting to WiFi...", 20, 20);
  canvas.pushSprite(0, 0);
  M5.Display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 60000) {
      Serial.println("\nWiFi failed; restarting...");
      canvas.clear();
      canvas.drawString("WiFi failed! Restarting...", 20, 20);
      canvas.pushSprite(0, 0);
      M5.Display.display();
      delay(2000);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

float getBatteryVoltage() {
  int32_t level = M5.Power.getBatteryLevel();
  float voltage = M5.Power.getBatteryVoltage() / 1000.0;  // convert mV to V

  Serial.printf("Battery level: %d%%, voltage: %.2f V\n", level, voltage);
  return voltage;
}

void fetchAndDisplay(float batteryVoltage) {
  Serial.println("Requesting display JSON from TRMNL...");

  http.begin("https://trmnl.app/api/display");
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  http.addHeader("Access-Token", TRMNL_API_KEY);
  http.addHeader("ID", WiFi.macAddress());
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Battery-Voltage", String(batteryVoltage, 2));
  http.addHeader("User-Agent", "M5Paper-TRMNL/1.0");
  http.addHeader("FW-Version", "1.0.0");
  http.addHeader("RSSI", String(WiFi.RSSI()));
  http.addHeader("Model", "m5paper");
  http.addHeader("Width", "960");
  http.addHeader("Height", "540");
  http.addHeader("Refresh-Rate", String(lastRefreshRate));

  int code = http.GET();
  if (code < 200 || code >= 300) {
    Serial.printf("TRMNL GET failed, code %d\n", code);
    http.end();
    goToDeepSleep(300);  // retry in 5 min
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  auto err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("JSON parse error; sleeping 5 min");
    goToDeepSleep(300);
  }

  int status = doc["status"] | -1;
  if (status != 0) {
    Serial.printf("TRMNL API status error: %d\n", status);
    goToDeepSleep(300);
  }

  const char* imageUrl = doc["image_url"];
  if (!imageUrl || strlen(imageUrl) == 0) {
    Serial.println("No image_url in response; sleeping 5 min");
    goToDeepSleep(300);
  }

  int refreshSec = doc["refresh_rate"] | 900;
  lastRefreshRate = refreshSec;  // stored in RTC memory for next wake

  Serial.printf("Image URL: %s\nRefresh in: %d s\n", imageUrl, refreshSec);

  displayImage(imageUrl);
  goToDeepSleep(refreshSec);
}

void displayImage(const char* imageUrl) {
  Serial.println("Loading image...");
  canvas.clear();

  canvas.drawString("Loading image...", 20, 20);
  canvas.pushSprite(0, 0);
  M5.Display.display();

  bool success = false;

  // try PNG first (most common from TRMNL)
  Serial.println("Trying to load as PNG...");
  success = canvas.drawPngUrl(imageUrl);

  if (!success) {
    Serial.println("PNG load failed, trying BMP...");
    success = canvas.drawBmpUrl(imageUrl);
  }

  if (!success) {
    Serial.println("Direct URL methods failed, trying manual download...");
    success = downloadAndDisplayImage(imageUrl);
  }

  if (!success) {
    Serial.println("Image display failed!");
    canvas.clear();
    canvas.drawString("Failed to load image", 20, 20);
    canvas.drawString("Check console for details", 20, 50);
    canvas.pushSprite(0, 0);
    M5.Display.display();
  } else {
    canvas.pushSprite(0, 0);
    M5.Display.display();
    Serial.println("Image displayed successfully.");
  }
}

bool downloadAndDisplayImage(const char* url) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(30000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed, code: %d\n", code);
    http.end();
    return false;
  }

  String contentType = http.header("Content-Type");
  Serial.printf("Content-Type: %s\n", contentType.c_str());

  int len = http.getSize();
  if (len <= 0 || len > 200000) {  // max 200KB
    Serial.printf("Invalid content length: %d\n", len);
    http.end();
    return false;
  }

  uint8_t* buffer = (uint8_t*)malloc(len);
  if (!buffer) {
    Serial.println("Failed to allocate memory for image");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t bytesRead = stream->readBytes(buffer, len);
  http.end();

  if (bytesRead != len) {
    Serial.printf("Read size mismatch: expected %d, got %d\n", len, bytesRead);
    free(buffer);
    return false;
  }

  bool success = false;

  if (contentType.indexOf("png") != -1) {
    Serial.println("Content-Type indicates PNG format");
    success = canvas.drawPng(buffer, bytesRead, 0, 0);
  }
  else if (contentType.indexOf("bmp") != -1 || contentType.indexOf("bitmap") != -1) {
    Serial.println("Content-Type indicates BMP format");
    success = canvas.drawBmp(buffer, bytesRead, 0, 0);
  }

  // fallback to magic byte detection
  if (!success) {
    Serial.println("Falling back to magic byte detection...");

    // PNG: 0x89 0x50 0x4E 0x47
    if (bytesRead >= 4 && buffer[0] == 0x89 && buffer[1] == 0x50 &&
        buffer[2] == 0x4E && buffer[3] == 0x47) {
      Serial.println("Detected PNG format by magic bytes");
      success = canvas.drawPng(buffer, bytesRead, 0, 0);
    }
    // BMP: 'BM'
    else if (bytesRead >= 2 && buffer[0] == 'B' && buffer[1] == 'M') {
      Serial.println("Detected BMP format by magic bytes");
      success = canvas.drawBmp(buffer, bytesRead, 0, 0);
    }
    else {
      Serial.println("Unknown image format");
      Serial.print("Magic bytes: ");
      for (int i = 0; i < min((size_t)8, bytesRead); i++) {
        Serial.printf("%02X ", buffer[i]);
      }
      Serial.println();
    }
  }

  free(buffer);
  return success;
}

void goToDeepSleep(int seconds) {
  Serial.printf("Sleeping for %d seconds (or until button press)...\n", seconds);

  canvas.deleteSprite();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  M5.Display.sleep();
  M5.Display.waitDisplay();

  // CRITICAL: hold power pin during deep sleep for M5Paper
  gpio_hold_en(GPIO_NUM_2);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)M5PAPER_WAKE_BUTTON, 0);  // wake on LOW

  M5.Power.deepSleep(0);  // 0 = use ESP32's configured wake sources

  esp_deep_sleep_start();  // fallback
}

void loop() {
  // never reached; ESP32 restarts on wake from deep sleep
}
