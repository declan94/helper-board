#include <Arduino.h>
#include <WiFi.h>
#include "net.h"
#include "config.h"
#include "secrets.h"

// 快连缓存放 RTC 内存:深睡保留、掉电清零。掉电本来就要冷启动、本来就得重学,
// 所以不必落 NVS,省掉 flash 磨损和"缓存过期"这类判断。
RTC_DATA_ATTR static uint8_t sBssid[6] = { 0 };
RTC_DATA_ATTR static uint8_t sChannel = 0;  // 0 = 无缓存

static bool sBeginCalled = false;
static bool sUsedFastPath = false;

static void startConnect(bool fast) {
  sUsedFastPath = fast;
  if (fast) WiFi.begin(WIFI_SSID, WIFI_PASSWORD, sChannel, sBssid);
  else WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // 降低发射功率,削减射频电流尖峰(与屏幕升压叠加疑似触发掉电复位);
  // 家用距离 11dBm 足够,连接不稳可逐级调回 WIFI_POWER_19_5dBm
  WiFi.setTxPower(WIFI_POWER_11dBm);
}

void Net_BeginConnect() {
  if (sBeginCalled) return;
  sBeginCalled = true;
  WiFi.mode(WIFI_STA);
  startConnect(sChannel > 0);
}

static bool waitUntil(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > timeoutMs) return false;
    delay(20);
  }
  return true;
}

// 连上之后记下 AP 身份,供下次唤醒直连
static void rememberAp() {
  const uint8_t *b = WiFi.BSSID();
  if (b) memcpy(sBssid, b, 6);
  sChannel = (uint8_t)WiFi.channel();
}

bool Net_WaitConnected() {
  Net_BeginConnect();  // 若未提前发起则此刻发起

  if (waitUntil(sUsedFastPath ? WIFI_FAST_CONNECT_TIMEOUT_MS : WIFI_CONNECT_TIMEOUT_MS)) {
    rememberAp();
    return true;
  }
  if (!sUsedFastPath) return false;

  // 快连没连上:换 AP、换路由器、或那台 AP 恰好不在了。作废缓存当场重来一次,
  // 代价是这次唤醒多花两三秒,好过整个轮询周期收不到呼叫。
  log_w("快连失败(ch=%d),回落全信道扫描", sChannel);
  sChannel = 0;
  WiFi.disconnect(true);
  startConnect(false);
  if (!waitUntil(WIFI_CONNECT_TIMEOUT_MS)) return false;
  rememberAp();
  return true;
}

void Net_Disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  sBeginCalled = false;
}
