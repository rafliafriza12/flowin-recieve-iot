/*
 * Flowin IoT — ESP32 Water Flow Meter (batched HTTPS)
 *
 * Sesuai backend: POST <BASE_URL>/iot/<USER_ID>/<METER_ID>
 * Body batch:
 *   { "batch": [ { "usedWater": <float liter>, "ts": <epoch_ms> }, ... ] }
 *
 * Catatan:
 *  - Buffer sample tiap detik di RAM, flush ke server per BATCH_FLUSH_SEC detik
 *    atau saat buffer hampir penuh.
 *  - ts diisi epoch ms via NTP (wajib supaya cron 7-hari di backend benar).
 *  - Kalau koneksi gagal, batch tetap di buffer dan dicoba lagi pada flush
 *    berikutnya. Saat boot, total volume kumulatif dipulihkan dari NVS.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

// ====== KONFIGURASI ======
#define WIFI_SSID         "4G-UFI-644"
#define WIFI_PASS         "1234567890"

// Ganti dengan domain Vercel Anda
#define BACKEND_BASE_URL  "https://your-app.vercel.app"
#define USER_ID           "67be92313e2d361fd62358e6"
#define METER_ID          "67be92cf3e2d361fd62358eb"

// Pin & sensor
#define FLOW_SENSOR_PIN   34
#define PPL               210     // Pulse per Liter (kalibrasi)

// OLED
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_RESET        -1
#define OLED_ADDR         0x3C

// Sampling & batch
#define SAMPLE_INTERVAL_MS  1000UL    // hitung 1 sample per detik
#define BATCH_FLUSH_SEC     30        // flush tiap 30 detik
#define BUFFER_CAPACITY     120       // hard cap (~2 menit data)
#define HTTP_TIMEOUT_MS     8000

// NTP
#define NTP_SERVER1       "pool.ntp.org"
#define NTP_SERVER2       "time.google.com"
#define GMT_OFFSET_SEC    (7 * 3600)  // WIB
#define DST_OFFSET_SEC    0

// ====== STATE ======
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;

volatile uint32_t pulseCount = 0;
float flowRate = 0.0f;          // L/min (display only)
float totalVolume = 0.0f;       // kumulatif liter (persisted di NVS)

unsigned long lastSampleMs = 0;
unsigned long lastFlushMs = 0;

struct Sample {
  float    usedWater;   // delta liter pada detik ini
  uint64_t tsMs;        // epoch ms
};

Sample buffer[BUFFER_CAPACITY];
int bufLen = 0;

// ====== ISR ======
void IRAM_ATTR pulseISR() {
  pulseCount++;
}

// ====== UTIL ======
uint64_t epochMillis() {
  // Pakai gettimeofday untuk presisi ms.
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

bool timeIsSynced() {
  // Setelah NTP sync, time(nullptr) > 1700000000 (Nov 2023+).
  return time(nullptr) > 1700000000;
}

void drawDisplay(int wifiOk, int bufNow) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Flowin Water Monitor");
  display.println("--------------------");
  display.print("Flow : ");
  display.print(flowRate, 2);
  display.println(" L/min");
  display.print("Total: ");
  display.print(totalVolume, 2);
  display.println(" L");
  display.print("Buf  : ");
  display.print(bufNow);
  display.print("/");
  display.println(BUFFER_CAPACITY);
  display.print("WiFi : ");
  display.println(wifiOk ? WiFi.localIP().toString() : "DISCONNECTED");
  display.display();
}

// ====== JARINGAN ======
void connectWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi FAILED, will retry in loop");
  }
}

void syncTime() {
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
  Serial.print("Sync NTP");
  unsigned long t0 = millis();
  while (!timeIsSynced() && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Epoch now: ");
  Serial.println((unsigned long)time(nullptr));
}

// ====== BUFFER & FLUSH ======
void pushSample(float usedWater) {
  if (bufLen >= BUFFER_CAPACITY) {
    // Buffer penuh: drop sample tertua (geser kiri 1).
    for (int i = 1; i < BUFFER_CAPACITY; i++) buffer[i - 1] = buffer[i];
    bufLen = BUFFER_CAPACITY - 1;
    Serial.println("WARN: buffer full, drop oldest");
  }
  buffer[bufLen].usedWater = usedWater;
  buffer[bufLen].tsMs      = epochMillis();
  bufLen++;
}

String buildBatchPayload() {
  // {"batch":[{"usedWater":x,"ts":y}, ...]}
  String out;
  out.reserve(32 + bufLen * 56);
  out += "{\"batch\":[";
  for (int i = 0; i < bufLen; i++) {
    if (i > 0) out += ",";
    out += "{\"usedWater\":";
    out += String(buffer[i].usedWater, 4);
    out += ",\"ts\":";
    // ESP32 String tidak punya overload uint64_t — pecah jadi high/low atau pakai char buf.
    char tsbuf[24];
    snprintf(tsbuf, sizeof(tsbuf), "%llu", (unsigned long long)buffer[i].tsMs);
    out += tsbuf;
    out += "}";
  }
  out += "]}";
  return out;
}

bool flushBatch() {
  if (bufLen == 0) return true;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("flush: WiFi down, skip");
    return false;
  }
  if (!timeIsSynced()) {
    Serial.println("flush: NTP belum sync, skip (ts tidak valid)");
    return false;
  }

  String url = String(BACKEND_BASE_URL) + "/iot/" + USER_ID + "/" + METER_ID;
  String body = buildBatchPayload();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  Serial.printf("POST %s (%d entries, %d bytes)\n", url.c_str(), bufLen, body.length());
  int code = http.POST(body);
  bool ok = (code >= 200 && code < 300);

  if (ok) {
    Serial.printf("Flush OK, code=%d\n", code);
    bufLen = 0;
  } else {
    Serial.printf("Flush FAIL, code=%d, payload=%s\n", code, http.getString().c_str());
  }
  http.end();
  return ok;
}

// ====== SAMPLING ======
void sampleTick() {
  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  noInterrupts();
  uint32_t pulses = pulseCount;
  pulseCount = 0;
  interrupts();

  float deltaLiter = (float)pulses / (float)PPL;     // liter pada interval 1 detik
  flowRate         = deltaLiter * 60.0f;             // L/min (untuk display)
  totalVolume     += deltaLiter;

  // Persist total kumulatif setiap kali ada aliran (hemat write NVS).
  if (deltaLiter > 0.0f) {
    preferences.putFloat("volume", totalVolume);
    pushSample(deltaLiter);
  }

  drawDisplay(WiFi.status() == WL_CONNECTED, bufLen);
}

void flushTick() {
  unsigned long now = millis();
  bool dueByTime = (now - lastFlushMs) >= ((unsigned long)BATCH_FLUSH_SEC * 1000UL);
  bool dueBySize = bufLen >= (BUFFER_CAPACITY - 5);

  if ((dueByTime || dueBySize) && bufLen > 0) {
    lastFlushMs = now;
    flushBatch();
  } else if (dueByTime) {
    // tidak ada data, geser jendela waktu agar tidak akumulasi drift.
    lastFlushMs = now;
  }
}

// ====== SETUP / LOOP ======
void setup() {
  Serial.begin(115200);
  delay(200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init FAIL");
    for (;;) delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();

  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseISR, FALLING);

  preferences.begin("waterData", false);
  totalVolume = preferences.getFloat("volume", 0.0f);
  Serial.printf("Restored totalVolume = %.2f L\n", totalVolume);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
  }

  lastSampleMs = millis();
  lastFlushMs  = millis();
}

void loop() {
  // Reconnect WiFi kalau drop.
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      Serial.println("WiFi reconnect...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  } else if (!timeIsSynced()) {
    static unsigned long lastNtp = 0;
    if (millis() - lastNtp > 15000) {
      lastNtp = millis();
      syncTime();
    }
  }

  sampleTick();
  flushTick();
}
