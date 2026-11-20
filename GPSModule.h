// 使用 NEO-6M GPS 
#ifndef GPS_MODULE_H
#define GPS_MODULE_H

// Arduino 核心函式庫
// 提供 millis()、Serial 等基本功能
#include <Arduino.h>

// TinyGPSPlus 函式庫
// 負責解析 GPS 傳送的 NMEA 訊息
#include <TinyGPS++.h>

// C/C++ 時間相關函式庫
// 提供 struct tm、time_t 等資料型別
#include <time.h>

// =====================================================
// GPS 資料結構
// 用來集中儲存從 NEO-6M 取得的定位資訊
// =====================================================
struct GPSData {

  // -----------------------------
  // GPS 位置資訊
  // -----------------------------

  // 緯度，單位：度
  // 北緯為正值，南緯為負值
  double latitude = 0.0;

  // 經度，單位：度
  // 東經為正值，西經為負值
  double longitude = 0.0;

  // 海拔高度，單位：公尺
  // 通常是相對平均海平面的高度
  double altitude = 0.0;

  // 水平精度因子 HDOP
  // 無單位，數值通常越小代表衛星幾何條件越好
  // 注意：HDOP 不等於以公尺表示的定位誤差
  double hdop = 0.0;

  // 參與定位的衛星數量
  uint32_t satellites = 0;

  // -----------------------------
  // UTC 日期
  // -----------------------------

  int year = 0;   // 年，例如 2026
  int month = 0;  // 月，例如 11
  int day = 0;    // 日，例如 5

  // -----------------------------
  // UTC 時間（24 小時制）
  // -----------------------------

  int hour = 0;    // 時，0~23
  int minute = 0;  // 分，0~59
  int second = 0;  // 秒，0~59

  // GPS 資料是否曾成功取得
  // false：尚未取得有效定位
  // true ：已取得至少一筆有效定位資料
  bool valid = false;
};

// =====================================================
// GPSModule 類別
//
// 功能：
// 1. 初始化 NEO-6M GPS
// 2. 透過 UART 讀取 NMEA 資料
// 3. 解析並檢查定位資料
// 4. 開機取得第一次有效定位
// 5. 每 30 分鐘更新一次定位快照
// 6. 同步 ESP32 系統 UTC 時鐘
// 7. 使用 FSM 管理更新流程
// =====================================================
class GPSModule {

public:

  // 初始化 GPS UART 與狀態機
  // 在 setup() 中呼叫一次
  void begin();

  // GPS 模組的主要更新函式
  // 必須在 loop() 中持續呼叫
  // 內部使用 millis()，不使用 delay()
  void update();

  // 取得最近一次成功儲存的 GPS 資料
  // const 表示呼叫端不能直接修改內部資料
  const GPSData& getData() const;

  // 檢查是否有新的 GPS 快照
  // 回傳 true 後會清除更新旗標
  bool hasNewData();

  // 取得目前的 UTC 日期與時間
  // 使用 ESP32 系統時鐘
  // 成功回傳 true，尚未同步則回傳 false
  bool getUTC(struct tm &utc) const;

private:

  // =================================================
  // GPS 有限狀態機 FSM
  // =================================================
  enum State {

    // 正在等待有效 GPS 定位資料
    ACQUIRING,

    // 已成功取得 GPS，等待下一次更新
    IDLE,

    // GPS 定位逾時，等待重新嘗試
    RETRY_WAIT
  };

  // 使用 ESP32 的 UART1
  // UART0 通常保留給除錯或其他用途
  HardwareSerial gpsSerial{1};

  // TinyGPSPlus 解析器物件
  // 將 NMEA 字元解析為各種 GPS 參數
  TinyGPSPlus gps;

  // 儲存最近一次成功取得的 GPS 資料
  GPSData data;

  // FSM 目前狀態
  // 開機預設進入 ACQUIRING
  State state = ACQUIRING;

  // 進入目前狀態的時間，單位 ms
  unsigned long stateStart = 0;

  // 最近一次 GPS 更新成功的時間
  unsigned long lastSuccess = 0;

  // 是否有新的 GPS 快照
  bool newData = false;

  // =================================================
  // 時間參數設定
  // =================================================

  // GPS 成功更新後，每 30 分鐘更新一次
  // 30 分 × 60 秒 × 1000 ms
  static constexpr unsigned long UPDATE_INTERVAL =
      30UL * 60UL * 1000UL;

  // 每次嘗試定位最多等待 120 秒
  static constexpr unsigned long FIX_TIMEOUT =
      120UL * 1000UL;

  // 定位逾時後，等待 10 秒再重新嘗試
  static constexpr unsigned long RETRY_INTERVAL =
      10UL * 1000UL;

  // 開始一次 GPS 定位取得流程
  void startAcquisition();

  // 檢查 GPS 資料是否有效
  // 符合條件才儲存到 GPSData
  bool saveIfValid();

  // 用 GPS 取得的 UTC 時間
  // 校準 ESP32 的系統時鐘
  void syncClock();
};

#endif
