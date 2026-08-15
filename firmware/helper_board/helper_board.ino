// 家庭菜单显示面板 — Waveshare ESP32-S3-RLCD-4.2
// 深睡眠事件驱动:醒来 → 干一件事(查呼叫 / 重绘 / 切页 / 同步)→ 继续睡
//
// 唤醒节奏:每 CALL_POLL_INTERVAL_SEC(10 分钟)醒一次查呼叫,每 SYNC_EVERY_N_POLLS
// 次顺带做一次完整 Lark 同步(3×10min = 30 分钟,与原来的菜单同步周期一致)。
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "audio.h"
#include "call_client.h"
#include "display_bsp.h"
#include "sensors.h"
#include "battery.h"
#include "power.h"
#include "menu_store.h"
#include "lark_client.h"
#include "net.h"
#include "ui.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // TLS 握手 + UI 模型栈开销,默认 8KB 偏紧

// KEY 切页后的临时页面;-1 表示跟随时段默认页。定时唤醒时清除。
RTC_DATA_ATTR static int8_t sPageOverride = -1;
RTC_DATA_ATTR static uint32_t sWakeCount = 0;  // 唤醒计数(诊断用,断电清零)
RTC_DATA_ATTR static uint8_t sPollsSinceSync = 0;

// 呼叫留言"占屏"状态:铃响完没人按 KEY,说明人当时不在跟前,留言就一直留在屏幕上
// 直到有人按键确认 —— 否则下一次定时唤醒重绘菜单会把留言抹掉,声音和字都错过了。
RTC_DATA_ATTR static bool sCallLatched = false;
RTC_DATA_ATTR static char sCallText[128] = { 0 };
RTC_DATA_ATTR static time_t sCallSentAt = 0;
RTC_DATA_ATTR static time_t sCallChannelOkAt = 0;  // 最近一次轮询成功,页脚据此报警

static DisplayPort *display = NULL;

static uint32_t secondsToNextWake(time_t now, bool timeValid) {
  if (!timeValid) return 300;  // 时间未知:5 分钟后重试同步
  uint32_t rem = CALL_POLL_INTERVAL_SEC - (uint32_t)(now % CALL_POLL_INTERVAL_SEC);
  if (rem < 60) rem += CALL_POLL_INTERVAL_SEC;  // 避免贴着边界醒两次
  return rem;
}

// 响铃:留言上屏 → 循环播放直到按 KEY 或到次数上限 → 回执。
// 返回是否被按键确认(false = 响完了没人理,留言继续占屏)。
static bool ringForCall(const CallMessage *call, bool timeValid) {
  Ui_CallScreen(display, call->text, call->sentAt, timeValid);

  bool audioOk = Audio_Init();
  int loops = audioOk ? Audio_PlayRingtone(CALL_RING_MAX_LOOPS, Power_KeyPressed) : 0;
  Audio_Deinit();
  // 播满上限 = 没人按;提前返回 = 被 Power_KeyPressed 中止。
  // 音频起不来时一律按"没确认"处理,至少让留言留在屏幕上。
  bool acked = audioOk && loops < CALL_RING_MAX_LOOPS;
  log_i("call: audio=%d loops=%d acked=%d text=%s", audioOk, loops, acked, call->text);

  char detail[96];
  if (!audioOk) snprintf(detail, sizeof(detail), "Delivered to screen, but audio failed");
  else if (acked) snprintf(detail, sizeof(detail), "Heard and acknowledged (rang %dx)", loops);
  else snprintf(detail, sizeof(detail), "Rang %dx, nobody pressed the key yet", loops);
  Call_SendAck(detail);
  return acked;
}

void setup() {
  Serial.begin(115200);
  WakeCause wake = Power_GetWakeCause();  // 尽早调用:长按判定从唤醒瞬间计时
  sWakeCount++;
  log_i("wake cause: %d, count: %u", wake, sWakeCount);

  setenv("TZ", TZ_STRING, 1);
  tzset();
  Sensors_Init();

  // 屏幕尽早初始化:冷启动联网可达 20+ 秒,先给出可见反馈,
  // 否则按 PWR 开机后长时间黑屏会被误认为没开机
  display = new DisplayPort(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC, PIN_LCD_CS,
                            PIN_LCD_RST, LCD_WIDTH, LCD_HEIGHT);
  display->RLCD_Init(wake == WAKE_COLD);
  if (wake == WAKE_COLD) Ui_Splash(display, "Starting...");

#if AUDIO_SELFTEST
  // 音频链路自检:冷启动直接响两遍,按 KEY 提前停。验完把 config.h 里的开关改回 0。
  // 刻意排在 Net_BeginConnect 之前:射频不开,把"功放电流尖峰"这一个变量单独隔离出来。
  // 分阶段打日志:万一响铃时掉电复位,从日志断在哪一行就能判断是初始化还是播放的问题。
  if (wake == WAKE_COLD) {
    Ui_Splash(display, "Audio test...");
    log_i("selftest: init begin");
    Serial.flush();
    bool audioOk = Audio_Init();
    log_i("selftest: init=%d, play begin", audioOk);
    Serial.flush();
    int loops = audioOk ? Audio_PlayRingtone(2, Power_KeyPressed) : 0;
    log_i("selftest: play done, loops=%d", loops);
    Serial.flush();
    Audio_Deinit();
    log_i("selftest: PASS=%d (init=%d loops=%d)", audioOk && loops > 0, audioOk, loops);
    Serial.flush();
  }
#endif

  // WiFi 在屏幕初始化完成后再异步发起:屏幕升压/初始化与射频校准的
  // 电流尖峰错开,避免叠加触发掉电复位
  if (wake != WAKE_KEY_PAGE) Net_BeginConnect();

  // 硬件 RTC → 系统时间
  struct tm rtcTm;
  bool timeValid = Rtc_GetTime(&rtcTm);
  if (timeValid) {
    struct timeval tv = { mktime(&rtcTm), 0 };
    settimeofday(&tv, NULL);
  }

  // ---- 决策:查呼叫?同步?切页? ----
  bool needSync = false;
  bool needCallPoll = false;
  bool ackLatchedCall = false;
  switch (wake) {
    case WAKE_COLD:
      needSync = true;
      needCallPoll = true;
      sPageOverride = -1;
      sCallLatched = false;
      break;
    case WAKE_KEY_SYNC:  // BOOT 键:强制同步
      needSync = true;
      needCallPoll = true;  // 手动同步顺带查一次呼叫,反正射频已经开着
      break;
    case WAKE_KEY_PAGE:
      // 留言占屏时按 KEY = "我看到了",解除占屏并补发一条回执,而不是切页
      if (sCallLatched) {
        sCallLatched = false;
        ackLatchedCall = true;
      }
      break;
    case WAKE_TIMER:
      sPageOverride = -1;  // 回到时段默认页
      needCallPoll = true;
      if (++sPollsSinceSync >= SYNC_EVERY_N_POLLS) {
        needSync = true;  // 菜单更新最迟 CALL_POLL_INTERVAL_SEC × SYNC_EVERY_N_POLLS 生效
        sPollsSinceSync = 0;
      }
      break;
  }
  if (ackLatchedCall) Net_BeginConnect();  // 切页路径本来不联网,补发回执要用

  // ---- 加载缓存菜单(需要日期字符串) ----
  static UiModel m;  // ~2KB,放静态区,不占 loopTask 栈(实测栈溢出过)
  memset(&m, 0, sizeof(m));
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
  } else {
    needSync = true;  // 时间无效必须联网校时
  }

  // ---- 查呼叫 ----
  // 排在 Lark 同步之前:呼叫是要立刻响的,不能等几秒钟的取菜单流程
  if (needCallPoll && Net_WaitConnected()) {
    CallMessage call;
    if (Call_Poll(&call)) {
      sCallChannelOkAt = time(NULL);
      if (call.valid) {
        strlcpy(sCallText, call.text, sizeof(sCallText));
        sCallSentAt = call.sentAt;
        sCallLatched = !ringForCall(&call, timeValid);
      }
    }
  }

  // 留言占屏期间按 KEY 确认:补一条回执,让呼叫方知道人终于看到了
  if (ackLatchedCall && Net_WaitConnected()) Call_SendAck("Seen (acknowledged later at the board)");

  // ---- 联网同步 ----
  if (needSync) {
    m.syncAttempted = true;
    // 留言正占着屏幕时不插这一帧,否则刚响完的呼叫会被 Updating... 盖掉
    if (wake == WAKE_KEY_SYNC && !sCallLatched) {
      // 手动同步给即时反馈:先按当前缓存渲染一帧,页脚显示 Updating...
      static UiModel pre;  // 同上,避免栈上 2KB 拷贝
      pre = m;
      localtime_r(&now, &pre.now);
      pre.nowEpoch = now;
      pre.timeValid = timeValid;
      MenuPage prePage = timeValid ? MenuPage_DefaultFor(pre.now.tm_hour * 60 + pre.now.tm_min)
                                   : PAGE_BREAKFAST;
      pre.page = (sPageOverride >= 0) ? (MenuPage)sPageOverride : prePage;
      pre.batt = Battery_Read();
      pre.syncing = true;
      Ui_RenderAll(display, &pre);
    }
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
    log_i("sync: wifi=%d time=%d fetch=%d", r.wifiOk, r.timeOk, r.fetchOk);
  }
  Net_Disconnect();  // 查呼叫、回执、同步都可能开过射频,统一在这里关

  // ---- 页面决策 ----
  localtime_r(&now, &m.now);
  m.nowEpoch = now;
  m.timeValid = timeValid;
  m.callChannelOkAt = sCallChannelOkAt;
  MenuPage defaultPage = timeValid
                           ? MenuPage_DefaultFor(m.now.tm_hour * 60 + m.now.tm_min)
                           : PAGE_BREAKFAST;
  // 这次按键被当作"确认留言"消费掉了,就不再兼作切页
  if (wake == WAKE_KEY_PAGE && !ackLatchedCall) {
    sPageOverride = (sPageOverride < 0) ? ((int8_t)defaultPage + 1) % PAGE_COUNT
                                        : (sPageOverride + 1) % PAGE_COUNT;
  }
  m.page = (sPageOverride >= 0) ? (MenuPage)sPageOverride : defaultPage;

  // ---- 采集电量 ----
  m.batt = Battery_Read();
  log_i("batt=%.2fV %d%% page=%d latched=%d", m.batt.voltage, m.batt.percent, (int)m.page,
        sCallLatched);

  // ---- 渲染并睡眠 ----
  // 留言未被确认时,屏幕一直留给留言;后续每次定时唤醒也走这条分支保持画面
  if (sCallLatched) Ui_CallScreen(display, sCallText, sCallSentAt, timeValid);
  else Ui_RenderAll(display, &m);
  display->RLCD_EnterLowPower();

  now = time(NULL);
  Power_DeepSleep(secondsToNextWake(now, timeValid));
}

void loop() {
  // 永远到不了:setup 末尾深睡,醒来重新跑 setup
}
