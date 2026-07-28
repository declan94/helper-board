#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "lark_client.h"
#include "sensors.h"
#include "config.h"
#include "secrets.h"

static const char *TAG_URL_TOKEN = "https://open.feishu.cn/open-apis/auth/v3/tenant_access_token/internal";

// DigiCert Global Root G2 — open.feishu.cn 证书链根,有效期至 2038-01
static const char DIGICERT_G2_ROOT[] = R"(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)";

// token 缓存在 RTC 内存,深睡眠不丢,有效期内(<2h)免重复获取
RTC_DATA_ATTR static char sTokenCache[160] = {0};
RTC_DATA_ATTR static time_t sTokenExpiry = 0;

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_CONNECT_TIMEOUT_MS) return false;
    delay(100);
  }
  return true;
}

void Net_Disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static bool syncTimeToRtc() {
  configTzTime(TZ_STRING, NTP_SERVER1, NTP_SERVER2);
  uint32_t t0 = millis();
  while (millis() - t0 < SNTP_TIMEOUT_MS) {
    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    if (tmNow.tm_year + 1900 >= 2024) {
      Rtc_SetTime(&tmNow);  // 回写硬件 RTC,掉电走时靠它
      return true;
    }
    delay(200);
  }
  return false;
}

static bool fetchToken(String &token) {
  time_t now = time(NULL);
  if (sTokenCache[0] && now < sTokenExpiry - 300) {
    token = sTokenCache;
    return true;
  }

  WiFiClientSecure client;
  client.setCACert(DIGICERT_G2_ROOT);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, TAG_URL_TOKEN)) return false;
  http.addHeader("Content-Type", "application/json; charset=utf-8");

  JsonDocument req;
  req["app_id"] = LARK_APP_ID;
  req["app_secret"] = LARK_APP_SECRET;
  String body;
  serializeJson(req, body);

  int code = http.POST(body);
  if (code != 200) {
    log_e("token http %d", code);
    http.end();
    return false;
  }
  JsonDocument resp;
  DeserializationError err = deserializeJson(resp, http.getStream());
  http.end();
  if (err || resp["code"].as<int>() != 0) {
    log_e("token parse/code err: %s / %d", err.c_str(), resp["code"].as<int>());
    return false;
  }
  token = resp["tenant_access_token"].as<const char *>();
  strlcpy(sTokenCache, token.c_str(), sizeof(sTokenCache));
  sTokenExpiry = now + resp["expire"].as<long>();
  return true;
}

// 本地日历日 0 点的 epoch 毫秒(dayOffset: 0=今天, 1=明天)
static int64_t localMidnightMs(int dayOffset) {
  time_t now = time(NULL) + (time_t)dayOffset * 86400;
  struct tm d;
  localtime_r(&now, &d);
  d.tm_hour = 0;
  d.tm_min = 0;
  d.tm_sec = 0;
  return (int64_t)mktime(&d) * 1000LL;
}

// 多维表格文本字段值是 [{text,type}] 分段数组,拼接为整段
static void joinTextField(JsonVariantConst field, char *out, size_t outLen) {
  out[0] = '\0';
  if (field.is<JsonArrayConst>()) {
    for (JsonVariantConst seg : field.as<JsonArrayConst>()) {
      const char *t = seg["text"].as<const char *>();
      if (t) strlcat(out, t, outLen);
    }
  } else if (field.is<const char *>()) {
    strlcpy(out, field.as<const char *>(), outLen);
  }
}

static bool fetchMenu(const String &token, MenuData *menu) {
  int64_t msToday = localMidnightMs(0);
  int64_t msTomorrow = localMidnightMs(1);

  JsonDocument req;
  JsonArray fieldNames = req["field_names"].to<JsonArray>();
  fieldNames.add(LARK_FIELD_DATE);
  fieldNames.add(LARK_FIELD_BREAKFAST);
  fieldNames.add(LARK_FIELD_LUNCH);
  fieldNames.add(LARK_FIELD_DINNER);
  fieldNames.add(LARK_FIELD_NOTE);
  JsonObject filter = req["filter"].to<JsonObject>();
  filter["conjunction"] = "or";
  JsonArray conds = filter["conditions"].to<JsonArray>();
  for (int64_t ms : { msToday, msTomorrow }) {
    JsonObject c = conds.add<JsonObject>();
    c["field_name"] = LARK_FIELD_DATE;
    c["operator"] = "is";
    JsonArray v = c["value"].to<JsonArray>();
    v.add("ExactDate");
    v.add(String((long long)ms));
  }
  req["automatic_fields"] = false;
  String body;
  serializeJson(req, body);

  String url = String("https://open.feishu.cn/open-apis/bitable/v1/apps/") + LARK_BASE_APP_TOKEN + "/tables/" + LARK_TABLE_ID + "/records/search?page_size=10";

  WiFiClientSecure client;
  client.setCACert(DIGICERT_G2_ROOT);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Authorization", String("Bearer ") + token);

  int code = http.POST(body);
  if (code != 200) {
    log_e("search http %d: %s", code, http.getString().c_str());
    http.end();
    return false;
  }
  JsonDocument resp;
  DeserializationError err = deserializeJson(resp, http.getStream());
  http.end();
  if (err || resp["code"].as<int>() != 0) {
    log_e("search parse/code err: %s / %d", err.c_str(), resp["code"].as<int>());
    return false;
  }

  menu->today.valid = false;
  menu->tomorrow.valid = false;
  for (JsonVariantConst item : resp["data"]["items"].as<JsonArrayConst>()) {
    JsonVariantConst fields = item["fields"];
    int64_t ms = fields[LARK_FIELD_DATE].as<long long>();
    DayMenu *row = NULL;
    if (ms == msToday) row = &menu->today;
    else if (ms == msTomorrow) row = &menu->tomorrow;
    if (!row) continue;

    time_t sec = (time_t)(ms / 1000);
    struct tm d;
    localtime_r(&sec, &d);
    strftime(row->date, sizeof(row->date), "%Y-%m-%d", &d);
    joinTextField(fields[LARK_FIELD_BREAKFAST], row->breakfast, sizeof(row->breakfast));
    joinTextField(fields[LARK_FIELD_LUNCH], row->lunch, sizeof(row->lunch));
    joinTextField(fields[LARK_FIELD_DINNER], row->dinner, sizeof(row->dinner));
    joinTextField(fields[LARK_FIELD_NOTE], row->note, sizeof(row->note));
    row->valid = true;
  }
  menu->lastSync = time(NULL);
  return true;
}

SyncResult Lark_SyncAll(bool needTime, MenuData *menu) {
  SyncResult r = { false, false, false };
  r.wifiOk = connectWifi();
  if (!r.wifiOk) return r;

  if (needTime) {
    r.timeOk = syncTimeToRtc();
    if (!r.timeOk) return r;  // 时间都没有,日期过滤无意义
  }

  String token;
  if (fetchToken(token)) {
    r.fetchOk = fetchMenu(token, menu);
  }
  return r;
}
