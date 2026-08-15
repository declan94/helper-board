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
static bool sConnectFailed = false;  // 本次唤醒已判定连不上,后续调用方直接放弃

static void startConnect(bool fast) {
  sUsedFastPath = fast;
  if (fast) WiFi.begin(WIFI_SSID, WIFI_PASSWORD, sChannel, sBssid);
  else WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTxPower(WIFI_TX_POWER);  // 取值与调整理由见 config.h
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
  if (WiFi.status() == WL_CONNECTED) return true;
  // 一次唤醒里有多个调用方(查呼叫、发回执、Lark 同步)。第一个已经等满超时判定
  // 连不上之后,后面的再调用不会重新发起连接(sBeginCalled 已置位),只会对着一个
  // 死连接再空等一轮 —— 射频白开成倍时间。直接短路。
  if (sConnectFailed) return false;

  Net_BeginConnect();  // 若未提前发起则此刻发起

  // 第一次机会:若走了快连就用短超时,失败即作废缓存,不值得为一个可能已经
  // 不在的 AP 等满全程(换 AP、换路由器、AP 换信道都会走到这里)。
  if (waitUntil(sUsedFastPath ? WIFI_FAST_CONNECT_TIMEOUT_MS : WIFI_CONNECT_TIMEOUT_MS)) {
    rememberAp();
    return true;
  }
  if (sUsedFastPath) {
    log_w("快连失败(ch=%d),作废缓存回落全信道扫描", sChannel);
    sChannel = 0;
  }

  // 全信道扫描重试。握手超时(HANDSHAKE_TIMEOUT)多是偶发,每次都彻底断开重连,
  // 而不是接着等一个已经卡住的连接 —— 干等只会白白耗着射频。
  for (int attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; attempt++) {
    log_w("WiFi 重连尝试 %d/%d", attempt, WIFI_CONNECT_ATTEMPTS);
    WiFi.disconnect(true);
    delay(100);
    startConnect(false);
    if (waitUntil(WIFI_CONNECT_TIMEOUT_MS)) {
      rememberAp();
      return true;
    }
  }
  sConnectFailed = true;
  return false;
}

void Net_Disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  sBeginCalled = false;
  sConnectFailed = false;
}
