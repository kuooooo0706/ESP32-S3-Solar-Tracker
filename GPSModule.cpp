
#include "GPSModule.h"

// 提供 settimeofday() 函式
// 用來設定 ESP32 的系統時鐘
#include <sys/time.h>

// =====================================================
// begin()
// 功能：初始化 GPS
// 執行時機：ESP32 開機後執行一次
// =====================================================
void GPSModule::begin() {

  // 將系統時區設定為 UTC
  // 避免 mktime() 受到本地時區影響
  setenv("TZ", "UTC0", 1);
  tzset();

  // 設定 UART 接收緩衝區大小
  // 2048 bytes，用來緩衝 GPS 傳來的 NMEA 字元
  // 必須在 UART begin() 前設定
  gpsSerial.setRxBufferSize(2048);

  // 啟動 UART1
  // 9600：NEO-6M 常見預設鮑率
  // SERIAL_8N1：8 個資料位元、無同位檢查、1 停止位元
  // 17：ESP32-S3 接收腳位 RX
  // 18：ESP32-S3 傳送腳位 TX
  gpsSerial.begin(9600, SERIAL_8N1, 17, 18);

  // 開機立即開始取得 GPS 資料
  startAcquisition();

  // 顯示初始化完成訊息
  Serial.println("GPS initialized");
}

// =====================================================
// startAcquisition()
// 功能：開始一次 GPS 資料取得流程
// =====================================================
void GPSModule::startAcquisition() {

  // FSM 切換至正在取得定位的狀態
  state = ACQUIRING;

  // 記錄本次嘗試開始的時間
  // 之後用來計算是否超過 120 秒
  stateStart = millis();

  Serial.println("GPS acquisition started");
}

// =====================================================
// update()
// 功能：執行 GPS 資料接收與 FSM 更新
//
// 特點：
// 1. 不使用 delay()
// 2. 持續接收 NMEA 訊息
// 3. 開機立即取得 GPS
// 4. 成功後每 30 分鐘更新一次
// 5. 失敗時自動重試
//
// 必須在 loop() 中持續呼叫
// =====================================================
void GPSModule::update() {

  // ---------------------------------------------
  // 第一階段：讀取 UART 接收緩衝區
  // ---------------------------------------------

  // gpsSerial.available()
  // 回傳目前 UART 緩衝區內可讀取的位元組數量
  while (gpsSerial.available() > 0) {

    // 從 UART 讀取一個字元
    char c = gpsSerial.read();

    // 將字元交給 TinyGPSPlus
    // TinyGPSPlus 會持續組合並解析 NMEA 訊息
    gps.encode(c);
  }

  // 取得 ESP32 開機後經過的時間
  // millis() 的單位為毫秒
  unsigned long now = millis();

  // ---------------------------------------------
  // 第二階段：FSM 狀態判斷
  // ---------------------------------------------
  switch (state) {

    // ===========================================
    // 狀態一：ACQUIRING
    // 正在嘗試取得有效 GPS 資料
    // ===========================================
    case ACQUIRING:

      // 檢查是否收到符合條件的 GPS 資料
      if (saveIfValid()) {

        // 記錄本次成功更新的時間
        lastSuccess = now;

        // 切換至 IDLE 狀態
        // 下一次更新將在 30 分鐘後啟動
        state = IDLE;

        // 設定更新旗標
        // 讓主程式知道有新的 GPS 快照
        newData = true;

        // 使用 GPS UTC 校準 ESP32 系統時鐘
        syncClock();

        Serial.println("GPS data updated");
      }

      // 若定位尚未成功，檢查是否逾時
      else if (now - stateStart >= FIX_TIMEOUT) {

        // 超過 120 秒仍無符合條件的定位資料
        // 轉入重試等待狀態
        state = RETRY_WAIT;

        // 記錄進入重試等待的時間
        stateStart = now;

        Serial.println("GPS timeout, retry later");
      }

      break;

    // ===========================================
    // 狀態二：IDLE
    // 已有有效資料，等待下一次更新
    // ===========================================
    case IDLE:

      // 檢查距離上一次成功更新
      // 是否已經經過 30 分鐘
      if (now - lastSuccess >= UPDATE_INTERVAL) {

        // 到達更新時間
        // 重新進入 ACQUIRING
        startAcquisition();
      }

      break;

    // ===========================================
    // 狀態三：RETRY_WAIT
    // 定位失敗後，等待重試
    // ===========================================
    case RETRY_WAIT:

      // 檢查是否已經等待 10 秒
      if (now - stateStart >= RETRY_INTERVAL) {

        // 重新開始取得 GPS
        startAcquisition();
      }

      break;
  }
}

// =====================================================
// saveIfValid()
// 功能：確認 GPS 資料有效並儲存
//
// 回傳值：
// true  = 資料符合條件，儲存成功
// false = 資料無效、過舊或不符合門檻
// =====================================================
bool GPSModule::saveIfValid() {

  // 確認 TinyGPSPlus 剛收到新的位置資料
  // 避免重複儲存同一筆定位
  if (!gps.location.isUpdated()) {
    return false;
  }

  // ---------------------------------------------
  // 確認各項 GPS 欄位都已取得有效資料
  // ---------------------------------------------
  if (!gps.location.isValid() ||
      !gps.altitude.isValid() ||
      !gps.date.isValid() ||
      !gps.time.isValid() ||
      !gps.hdop.isValid() ||
      !gps.satellites.isValid()) {

    return false;
  }

  // ---------------------------------------------
  // 確認資料不是過舊的 NMEA 欄位
  //
  // age() 表示距離該欄位最近一次更新的毫秒數
  // 此處要求資料年齡不超過 3000 ms
  // ---------------------------------------------
  if (gps.location.age() > 3000 ||
      gps.altitude.age() > 3000 ||
      gps.date.age() > 3000 ||
      gps.time.age() > 3000 ||
      gps.hdop.age() > 3000 ||
      gps.satellites.age() > 3000) {

    return false;
  }

  // ---------------------------------------------
  // 檢查定位品質與日期時間範圍
  // ---------------------------------------------
  if (
      // 排除明顯異常的年份
      gps.date.year() < 2024 ||

      // 檢查月份
      gps.date.month() < 1 ||
      gps.date.month() > 12 ||

      // 檢查日期基本範圍
      gps.date.day() < 1 ||
      gps.date.day() > 31 ||

      // 檢查 UTC 時間範圍
      gps.time.hour() > 23 ||
      gps.time.minute() > 59 ||
      gps.time.second() > 59 ||

      // 初期設定至少使用 4 顆衛星
      gps.satellites.value() < 4 ||

      // HDOP 必須為正值
      gps.hdop.hdop() <= 0.0 ||

      // 初期設定 HDOP 不超過 5
      // 此值可以依實際環境調整
      gps.hdop.hdop() > 5.0
  ) {
    return false;
  }

  // ---------------------------------------------
  // 建立暫存 GPSData
  // 只有全部檢查成功後才覆蓋正式資料
  // ---------------------------------------------
  GPSData temp;

  // 儲存經緯度，單位為十進位角度
  temp.latitude = gps.location.lat();
  temp.longitude = gps.location.lng();

  // 儲存海拔高度，單位為公尺
  temp.altitude = gps.altitude.meters();

  // 儲存水平精度因子
  temp.hdop = gps.hdop.hdop();

  // 儲存參與定位的衛星數量
  temp.satellites = gps.satellites.value();

  // 儲存 UTC 日期
  temp.year = gps.date.year();
  temp.month = gps.date.month();
  temp.day = gps.date.day();

  // 儲存 UTC 時間
  temp.hour = gps.time.hour();
  temp.minute = gps.time.minute();
  temp.second = gps.time.second();

  // 標記資料有效
  temp.valid = true;

  // 將全部通過檢查的資料正式儲存
  data = temp;

  // 回傳成功
  return true;
}

// =====================================================
// syncClock()
// 功能：將 GPS UTC 時間同步至 ESP32 系統時鐘
//
// GPS 快照每 30 分鐘才更新一次
// 但 ESP32 系統時鐘會持續向前計時
// =====================================================
void GPSModule::syncClock() {

  // 建立時間結構並初始化為 0
  struct tm utc = {};

  // struct tm 的年份從 1900 開始計算
  // 例如 2026 必須填入 126
  utc.tm_year = data.year - 1900;

  // struct tm 的月份範圍為 0~11
  utc.tm_mon = data.month - 1;

  // 日期：1~31
  utc.tm_mday = data.day;

  // UTC 時、分、秒
  utc.tm_hour = data.hour;
  utc.tm_min = data.minute;
  utc.tm_sec = data.second;

  // 不使用日光節約時間
  utc.tm_isdst = 0;

  // 將 UTC 日期時間轉換為 Unix timestamp
  // 因為 begin() 已設定時區為 UTC
  // mktime() 會依 UTC 時區進行轉換
  time_t timestamp = mktime(&utc);

  // 建立 timeval 結構
  struct timeval tv = {};

  // 設定 Unix 秒數
  tv.tv_sec = timestamp;

  // 微秒部分暫時設定為 0
  // 此版本只有秒級 GPS 時間同步
  tv.tv_usec = 0;

  // 設定 ESP32 系統時鐘
  settimeofday(&tv, nullptr);
}

// =====================================================
// getData()
// 功能：提供其他模組讀取 GPS 快照
// =====================================================
const GPSData& GPSModule::getData() const {

  // 回傳 GPSData 的唯讀參考
  // 不需要重新複製整個資料結構
  return data;
}

// =====================================================
// hasNewData()
// 功能：檢查是否有新 GPS 快照
//
// 注意：
// 此函式讀取後會自動清除旗標
// =====================================================
bool GPSModule::hasNewData() {

  // 保存目前的旗標
  bool result = newData;

  // 清除旗標
  // 避免同一筆資料被當作新資料重複處理
  newData = false;

  // 回傳原本的狀態
  return result;
}

// =====================================================
// getUTC()
// 功能：讀取 ESP32 系統目前的 UTC 日期時間
//
// 回傳：
// true  = 已完成 GPS 時間同步
// false = 尚未取得有效 GPS 資料
// =====================================================
bool GPSModule::getUTC(struct tm &utc) const {

  // 沒有成功取得 GPS，就不提供系統時間
  if (!data.valid) {
    return false;
  }

  // 取得 ESP32 系統時鐘目前時間
  // 單位：Unix 秒數
  time_t now = time(nullptr);

  // 將 Unix 秒數轉換為 UTC 年月日時分秒
  // gmtime_r() 使用呼叫端提供的記憶體
  return gmtime_r(&now, &utc) != nullptr;
}
