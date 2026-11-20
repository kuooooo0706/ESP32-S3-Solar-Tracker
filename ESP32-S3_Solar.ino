
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <SolarPosition.h>
#include <TimeLib.h>

// =======================================
// 1. 使用者設定區域
// =======================================

// Wi-Fi
const char* WIFI_SSID     = "vivo Y04";
const char* WIFI_PASSWORD = "        ";

// GPS 座標（請自行修改）
// 北緯為正，南緯為負
// 東經為正，西經為負
const float LATITUDE = 22.6000;
const float LONGITUDE = 120.3000;

// 台灣時間 UTC+8
const long GMT_OFFSET_SEC = 8 * 3600;

// 每 10 秒計算一次太陽位置
const unsigned long SOLAR_INTERVAL = 10000;

// 每 30 秒檢查一次 Wi-Fi
const unsigned long WIFI_INTERVAL = 30000;

// =======================================
// 2. 全域變數
// =======================================

SolarPosition solar(LATITUDE, LONGITUDE);

unsigned long lastSolarUpdate = 0;
unsigned long lastWiFiCheck = 0;

bool timeSynced = false;

// =======================================
// 3. Wi-Fi 連線
// =======================================

void connectWiFi() {

  Serial.println();
  Serial.println("[WiFi] Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  // 最多等待 15 秒
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  }
  else {
    Serial.println();
    Serial.println("[WiFi] Connection failed.");
  }
}

// =======================================
// 4. NTP 時間同步
// =======================================

void syncNTP() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] WiFi unavailable.");
    return;
  }

  Serial.println("[NTP] Synchronizing...");

  // 系統內部使用 UTC
  // 台灣時間只在顯示時轉換
  configTime(
    0,
    0,
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com"
  );

  struct tm timeInfo;

  // 最多等待約 10 秒
  if (getLocalTime(&timeInfo, 10000)) {
    timeSynced = true;
    Serial.println("[NTP] Sync successful!");
  }
  else {
    Serial.println("[NTP] Sync failed.");
  }
}

// =======================================
// 5. 顯示 UTC 與台灣時間
// =======================================

void printTime(time_t utcTime) {

  struct tm utcInfo;
  gmtime_r(&utcTime, &utcInfo);

  Serial.printf(
    "UTC Time    : %04d-%02d-%02d "
    "%02d:%02d:%02d\n",
    utcInfo.tm_year + 1900,
    utcInfo.tm_mon + 1,
    utcInfo.tm_mday,
    utcInfo.tm_hour,
    utcInfo.tm_min,
    utcInfo.tm_sec
  );

  // UTC + 8 小時
  time_t taiwanTime =
      utcTime + GMT_OFFSET_SEC;

  struct tm taiwanInfo;
  gmtime_r(&taiwanTime, &taiwanInfo);

  Serial.printf(
    "Taiwan Time : %04d-%02d-%02d "
    "%02d:%02d:%02d\n",
    taiwanInfo.tm_year + 1900,
    taiwanInfo.tm_mon + 1,
    taiwanInfo.tm_mday,
    taiwanInfo.tm_hour,
    taiwanInfo.tm_min,
    taiwanInfo.tm_sec
  );
}

// =======================================
// 6. 計算太陽位置
// =======================================

void updateSolarPosition() {

  if (!timeSynced) {
    Serial.println(
      "[Solar] Waiting for NTP time..."
    );
    return;
  }

  // 取得 ESP32 系統 UTC 時間
  time_t utcNow = time(nullptr);

  // 呼叫 SolarPosition Library
  SolarPosition_t result =
      solar.getSolarPosition(utcNow);

  Serial.println();
  Serial.println("==========================");
  Serial.println("     SOLAR POSITION");
  Serial.println("==========================");

  printTime(utcNow);

  Serial.printf(
    "Latitude    : %.6f\n", LATITUDE
  );

  Serial.printf(
    "Longitude   : %.6f\n", LONGITUDE
  );

  Serial.printf(
    "Azimuth     : %.4f deg\n",
    result.azimuth
  );

  Serial.printf(
    "Elevation   : %.4f deg\n",
    result.elevation
  );

  if (result.elevation > 0) {
    Serial.println("Sun Status  : Above horizon");
  }
  else {
    Serial.println("Sun Status  : Below horizon");
  }

  Serial.println("==========================");
}

// =======================================
// 7. Arduino Setup
// =======================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP32-S3 Solar Tracker");
  Serial.println("Initializing...");

  connectWiFi();

  syncNTP();

  updateSolarPosition();

  lastSolarUpdate = millis();
  lastWiFiCheck = millis();
}

// =======================================
// 8. Arduino Loop
// =======================================

void loop() {

  unsigned long currentMillis = millis();

  Serial.println("hello");

  // 每 30 秒檢查 Wi-Fi
  if (currentMillis - lastWiFiCheck >=
      WIFI_INTERVAL) {

    lastWiFiCheck = currentMillis;

    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    // 如果尚未取得有效時間，重新嘗試
    if (!timeSynced) {
      syncNTP();
    }
  }

  // 每 10 秒重新計算太陽位置
  if (currentMillis - lastSolarUpdate >=
      SOLAR_INTERVAL) {

    lastSolarUpdate = currentMillis;

    updateSolarPosition();
  }

  delay(10);
}
