#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "power.h"
#include "config.h"

static const int lcdPins[] = { PIN_LCD_RST, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_SCK, PIN_LCD_MOSI };

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
      // 实测:KEY(GPIO18)按下时 bit0 置位,BOOT(GPIO0)按下时不置位,
      // 与配置(EXT1 mask=bit0=GPIO0)相反——机制成谜,按实测映射。
      uint64_t ext1 = esp_sleep_get_ext1_wakeup_status();
      log_i("ext1 status=%llx", ext1);
      if (ext1 & 1ULL) return WAKE_KEY_PAGE;  // KEY(GPIO18)
      return WAKE_KEY_SYNC;                   // BOOT(GPIO0)
    }
    default:
      return WAKE_COLD;
  }
}

void Power_DeepSleep(uint32_t seconds) {
  // 锁定 LCD 引脚电平,保证深睡期间面板供电/控制脚不漂移,画面由 ST7305 LPM 自持
  // 两个按键均低有效、外部 10K 上拉:KEY(GPIO18)走 EXT0,BOOT(GPIO0)走 EXT1。
  // 深睡唤醒走 ROM 快速路径不采样 strap,BOOT 作唤醒键安全(实测多次)。
  const gpio_num_t keys[] = { (gpio_num_t)PIN_KEY, GPIO_NUM_0 };
  for (gpio_num_t k : keys) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_init(k));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_direction(k, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pullup_en(k));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pulldown_dis(k));
  }

  // 等按键松开再入睡(RTC 域读),避免手还按着立即重复唤醒
  uint32_t t0 = millis();
  while ((rtc_gpio_get_level((gpio_num_t)PIN_KEY) == 0 || rtc_gpio_get_level(GPIO_NUM_0) == 0)
         && millis() - t0 < 3000)
    delay(10);

  for (int pin : lcdPins) gpio_hold_en((gpio_num_t)pin);
  gpio_deep_sleep_hold_en();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_err_t err0 = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);
  esp_err_t err1 = esp_sleep_enable_ext1_wakeup(1ULL << 0, ESP_EXT1_WAKEUP_ANY_LOW);
  log_i("ext0(key18) err=%d | ext1(boot0) err=%d", err0, err1);

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}
