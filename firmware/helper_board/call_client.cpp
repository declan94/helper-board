#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "call_client.h"
#include "config.h"
#include "secrets.h"

// ntfy.sh 用 Let's Encrypt 证书,根为 ISRG Root X1(有效至 2035-06)。
// 自建 ntfy 且用了别家证书的话,把下面这张换成对应的根证书即可。
static const char NTFY_CA_ROOT[] = R"(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)";

// 最后处理过的消息 id。RTC 内存,深睡保留、掉电清零。
// 掉电后没有 id 可用,退化成"只看最近一个轮询周期",避免开机把陈年旧消息当新呼叫。
RTC_DATA_ATTR static char sLastId[24] = { 0 };

static void applyAuth(HTTPClient &http) {
#ifdef NTFY_TOKEN
  if (strlen(NTFY_TOKEN) > 0) http.addHeader("Authorization", String("Bearer ") + NTFY_TOKEN);
#endif
}

bool Call_Poll(CallMessage *out) {
  out->valid = false;
  out->text[0] = '\0';  // 空正文的消息也算有效呼叫,别让上层读到未初始化的内容
  out->sentAt = 0;

  // since 用上次的消息 id:两次轮询之间积压的都能取到,不会漏也不会重。
  String since = sLastId[0] ? String(sLastId) : (String(CALL_POLL_INTERVAL_SEC) + "s");
  String url = String(NTFY_BASE_URL) + "/" + NTFY_TOPIC_CALL + "/json?poll=1&since=" + since;

  WiFiClientSecure client;
  client.setCACert(NTFY_CA_ROOT);
  HTTPClient http;
  http.setTimeout(CALL_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  applyAuth(http);

  int code = http.GET();
  if (code != 200) {
    log_e("ntfy poll http %d", code);
    http.end();
    return false;
  }
  // 返回体是 NDJSON:一行一条消息。呼叫消息很短,整体读进来再逐行解析。
  String body = http.getString();
  http.end();

  int found = 0;
  int lineStart = 0;
  while (lineStart < (int)body.length()) {
    int nl = body.indexOf('\n', lineStart);
    if (nl < 0) nl = body.length();
    String line = body.substring(lineStart, nl);
    lineStart = nl + 1;
    line.trim();
    if (line.isEmpty()) continue;

    JsonDocument doc;
    if (deserializeJson(doc, line)) continue;
    const char *event = doc["event"].as<const char *>();
    if (!event || strcmp(event, "message") != 0) continue;  // 跳过 open/keepalive 等控制事件

    const char *id = doc["id"].as<const char *>();
    const char *msg = doc["message"].as<const char *>();
    if (id) strlcpy(sLastId, id, sizeof(sLastId));
    // 积压多条时后面的覆盖前面的:最后一条代表最新意图,只按它响一次铃
    if (msg) strlcpy(out->text, msg, sizeof(out->text));
    out->sentAt = (time_t)doc["time"].as<long long>();
    out->valid = true;
    found++;
  }
  if (found > 1) log_i("ntfy: %d 条积压,按最后一条处理", found);
  return true;  // 轮询本身成功(有没有新呼叫看 out->valid)
}

void Call_SendAck(const char *detail) {
  String url = String(NTFY_BASE_URL) + "/" + NTFY_TOPIC_ACK;

  // 重试一次:回执紧跟在响铃之后,而响铃期间功放拉电流叠加射频,连接可能已经掉了。
  // 一次重试足以覆盖"刚掉线、驱动正在自动重连"的窗口;再失败就放弃,不拖住入睡。
  for (int attempt = 1; attempt <= 2; attempt++) {
    log_i("ntfy ack 尝试 %d: wifi=%d heap=%u", attempt, (int)WiFi.status(),
          (unsigned)ESP.getFreeHeap());

    WiFiClientSecure client;
    client.setCACert(NTFY_CA_ROOT);
    HTTPClient http;
    http.setTimeout(CALL_HTTP_TIMEOUT_MS);
    if (http.begin(client, url)) {
      applyAuth(http);
      http.addHeader("Title", "Helper board");
      http.addHeader("Tags", "white_check_mark");
      int code = http.POST((uint8_t *)detail, strlen(detail));
      http.end();
      log_i("ntfy ack http %d: %s", code, detail);
      if (code == 200) return;
    } else {
      log_e("ntfy ack http.begin 失败");
    }
    if (attempt == 1) delay(1000);
  }
  log_e("ntfy ack 两次均失败,放弃");
}
