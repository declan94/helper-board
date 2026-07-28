// 家庭菜单显示面板 — Waveshare ESP32-S3-RLCD-4.2
// 深睡眠事件驱动:醒来 → 干一件事(重绘 / 切页 / 同步)→ 继续睡
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "display_bsp.h"
#include "sensors.h"
#include "battery.h"
#include "power.h"
#include "menu_store.h"
#include "lark_client.h"
#include "ui.h"

// KEY 切页后的临时页面;-1 表示跟随时段默认页。定时唤醒时清除。
RTC_DATA_ATTR static int8_t sPageOverride = -1;

static DisplayPort *display = NULL;

// 判断本次定时唤醒是否需要联网同步
static bool shouldSync(time_t now, time_t lastSync) {
  if (now - lastSync > SYNC_MAX_AGE_SEC) return true;
  static const int forceHours[] = SYNC_FORCE_HOURS;
  struct tm lt;
  localtime_r(&now, &lt);
  for (int h : forceHours) {
    if (lt.tm_hour == h) {
      // 本整点后还没同步过 → 强制同步(覆盖"出门前改当日菜单"场景)
      struct tm hourStart = lt;
      hourStart.tm_min = 0;
      hourStart.tm_sec = 0;
      if (lastSync < mktime(&hourStart)) return true;
    }
  }
  return false;
}

static uint32_t secondsToNextWake(time_t now, bool timeValid) {
  if (!timeValid) return 300;  // 时间未知:5 分钟后重试同步
  uint32_t rem = WAKE_INTERVAL_SEC - (uint32_t)(now % WAKE_INTERVAL_SEC);
  if (rem < 60) rem += WAKE_INTERVAL_SEC;  // 避免贴着边界醒两次
  return rem;
}

void setup() {
  Serial.begin(115200);
  WakeCause wake = Power_GetWakeCause();  // 尽早调用:长按判定从唤醒瞬间计时
  log_i("wake cause: %d", wake);

  setenv("TZ", TZ_STRING, 1);
  tzset();
  Sensors_Init();

  // 硬件 RTC → 系统时间
  struct tm rtcTm;
  bool timeValid = Rtc_GetTime(&rtcTm);
  if (timeValid) {
    struct timeval tv = { mktime(&rtcTm), 0 };
    settimeofday(&tv, NULL);
  }

  // ---- 决策:同步?切页? ----
  bool needSync = false;
  switch (wake) {
    case WAKE_COLD:
      needSync = true;
      sPageOverride = -1;
      break;
    case WAKE_KEY_LONG:
      needSync = true;
      break;
    case WAKE_KEY_SHORT:
      break;  // 下面结合默认页计算
    case WAKE_TIMER:
      sPageOverride = -1;  // 回到时段默认页
      break;
  }

  // ---- 加载缓存菜单(需要日期字符串) ----
  UiModel m = {};
  time_t now = time(NULL);
  char todayStr[11] = "", tomorrowStr[11] = "";
  if (timeValid) {
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", &lt);
    time_t tmr = now + 86400;
    localtime_r(&tmr, &lt);
    strftime(tomorrowStr, sizeof(tomorrowStr), "%Y-%m-%d", &lt);
    MenuStore_Load(&m.menu, todayStr, tomorrowStr);
    if (wake == WAKE_TIMER && shouldSync(now, m.menu.lastSync)) needSync = true;
  } else {
    needSync = true;  // 时间无效必须联网校时
  }

  // ---- 联网同步 ----
  if (needSync) {
    m.syncAttempted = true;
    SyncResult r = Lark_SyncAll(!timeValid, &m.menu);
    if (r.timeOk) timeValid = true;
    // NTP 可能大幅修正时间(如出厂残留),日期字符串必须重算
    now = time(NULL);
    if (timeValid) {
      struct tm lt;
      localtime_r(&now, &lt);
      strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", &lt);
      time_t tmr = now + 86400;
      localtime_r(&tmr, &lt);
      strftime(tomorrowStr, sizeof(tomorrowStr), "%Y-%m-%d", &lt);
    }
    m.syncOk = r.fetchOk;
    if (r.fetchOk) {
      MenuStore_Save(&m.menu);
    } else if (timeValid) {
      // 拉取失败:按(可能已被校正的)日期退回缓存
      MenuStore_Load(&m.menu, todayStr, tomorrowStr);
    }
    Net_Disconnect();
    log_i("sync: wifi=%d time=%d fetch=%d", r.wifiOk, r.timeOk, r.fetchOk);
  }

  // ---- 页面决策 ----
  localtime_r(&now, &m.now);
  m.timeValid = timeValid;
  MenuPage defaultPage = timeValid
                           ? MenuPage_DefaultFor(m.now.tm_hour * 60 + m.now.tm_min)
                           : PAGE_BREAKFAST;
  if (wake == WAKE_KEY_SHORT) {
    sPageOverride = (sPageOverride < 0) ? ((int8_t)defaultPage + 1) % PAGE_COUNT
                                        : (sPageOverride + 1) % PAGE_COUNT;
  }
  m.page = (sPageOverride >= 0) ? (MenuPage)sPageOverride : defaultPage;

  // ---- 采集传感器/电量 ----
  m.sensorOk = Shtc3_Read(&m.tempC, &m.humi);
  m.batt = Battery_Read();
  log_i("t=%.1f h=%.1f batt=%.2fV %d%% page=%d", m.tempC, m.humi, m.batt.voltage,
        m.batt.percent, (int)m.page);

  // ---- 渲染并睡眠 ----
  display = new DisplayPort(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC, PIN_LCD_CS,
                            PIN_LCD_RST, LCD_WIDTH, LCD_HEIGHT);
  display->RLCD_Init(wake == WAKE_COLD);
  Ui_RenderAll(display, &m);
  display->RLCD_EnterLowPower();

  now = time(NULL);
  Power_DeepSleep(secondsToNextWake(now, timeValid));
}

void loop() {
  // 永远到不了:setup 末尾深睡,醒来重新跑 setup
}
