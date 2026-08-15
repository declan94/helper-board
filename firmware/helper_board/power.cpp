#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "power.h"
#include "audio.h"
#include "config.h"

static const int lcdPins[] = { PIN_LCD_RST, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_SCK, PIN_LCD_MOSI };

// 音频引脚同样要跨深睡锁住,而且比 LCD 更要命:
//   GPIO45(I2S WS)是 VDD_SPI strap —— 唤醒复位若采样到高电平,flash 供电会
//     被切到 1.8V;GPIO46(功放使能)是 ROM log strap。两者的安全取值都是低,
//     和"功放关断"恰好一致,所以统一拉低再锁。
//   悬空的功放使能脚还会让功放随机导通,既漏电又出底噪。
static const int audioPins[] = { PIN_AUDIO_PA, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT };

WakeCause Power_GetWakeCause() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  // 注意:LCD 引脚的 gpio_hold 此处不解除!必须等 DisplayPort 把引脚重新
  // 配置到空闲电平后再解除,否则 RST 悬空漂低会硬复位面板导致黑屏。

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1: {
      // 用 EXT1 状态寄存器区分按键(唤醒原因码在本板上不可靠)。
      // 2026-08-15 查明按键与引脚的真实对应后,这里其实完全自洽:
      //   按 KEY  → 拉低 GPIO0  → EXT1(mask=bit0=GPIO0)触发 → bit0 置位
      //   按 BOOT → 拉低 GPIO18 → EXT0 触发                 → ext1 状态为 0
      // (旧注释说"与配置相反、机制成谜",是因为把 KEY 当成了 GPIO18。
      //  行为一直是对的,错的只是理由。引脚对应详见 config.h 的告示。)
      uint64_t ext1 = esp_sleep_get_ext1_wakeup_status();
      log_i("ext1 status=%llx", ext1);
      if (ext1 & 1ULL) return WAKE_KEY_PAGE;  // KEY 键(GPIO0):切页
      return WAKE_KEY_SYNC;                   // BOOT 键(GPIO18):强制同步
    }
    default:
      return WAKE_COLD;
  }
}

bool Power_KeyPressed() {
  static bool inited = false;
  static bool wasPressed = false;
  if (!inited) {
    // 必须先把两个脚从 RTC 域交还数字域再读:一个 pad 同时只能挂一个 MUX,深睡期间
    // 它们归 RTC 域(EXT0/EXT1 唤醒要用),留在那儿的话运行期 rtc_gpio_get_level
    // 恒返回"未按下"—— 这正是响铃时按键失灵的真因。
    // 入睡时 Power_DeepSleep 会重新 rtc_gpio_init,EXT0/EXT1 唤醒不受影响。
    // 两个键都读:停铃这种场合,按哪个都该管用。
    for (gpio_num_t p : { (gpio_num_t)PIN_BTN_KEY, (gpio_num_t)PIN_BTN_BOOT }) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_deinit(p));
      pinMode(p, INPUT_PULLUP);
    }
    inited = true;
    log_i("按键运行期读取就绪: KEY(G%d)=%d BOOT(G%d)=%d (静息电平应均为 1)", PIN_BTN_KEY,
          digitalRead(PIN_BTN_KEY), PIN_BTN_BOOT, digitalRead(PIN_BTN_BOOT));
  }
  bool key = digitalRead(PIN_BTN_KEY) == LOW;  // 低有效
  bool boot = digitalRead(PIN_BTN_BOOT) == LOW;
  bool pressed = key || boot;
  if (pressed && !wasPressed)
    log_i("按键按下: %s%s", key ? "KEY " : "", boot ? "BOOT" : "");
  wasPressed = pressed;
  return pressed;
}

void Power_DeepSleep(uint32_t seconds) {
  // 锁定 LCD 引脚电平,保证深睡期间面板供电/控制脚不漂移,画面由 ST7305 LPM 自持
  // 两个按键均低有效、外部 10K 上拉:GPIO18(BOOT 键)走 EXT0,GPIO0(KEY 键)走 EXT1。
  // 深睡唤醒走 ROM 快速路径不采样 strap,GPIO0 作唤醒键安全(实测多次)。
  const gpio_num_t keys[] = { (gpio_num_t)PIN_BTN_KEY, (gpio_num_t)PIN_BTN_BOOT };
  for (gpio_num_t k : keys) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_init(k));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_direction(k, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pullup_en(k));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pulldown_dis(k));
  }

  // 等按键松开再入睡(RTC 域读),避免手还按着立即重复唤醒
  uint32_t t0 = millis();
  while ((rtc_gpio_get_level((gpio_num_t)PIN_BTN_KEY) == 0 || rtc_gpio_get_level((gpio_num_t)PIN_BTN_BOOT) == 0)
         && millis() - t0 < 3000)
    delay(10);

  // 音频链路无条件复位到静默态(哪怕这次唤醒根本没播过音),再连同 LCD 一起锁
  Audio_IdleSafe();

  for (int pin : lcdPins) gpio_hold_en((gpio_num_t)pin);
  for (int pin : audioPins) ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_hold_en((gpio_num_t)pin));
  gpio_deep_sleep_hold_en();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_err_t err0 = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_BOOT, 0);
  esp_err_t err1 = esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN_KEY, ESP_EXT1_WAKEUP_ANY_LOW);
  log_i("ext0(BOOT,G18) err=%d | ext1(KEY,G0) err=%d", err0, err1);

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}
