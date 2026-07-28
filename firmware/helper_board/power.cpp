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

  // 深睡前按键引脚被切到 RTC 功能域(配置唤醒所需),醒来必须切回数字域,
  // 否则 digitalRead 恒读高——长按被误判为短按并在按住期间反复唤醒切页
  rtc_gpio_deinit((gpio_num_t)PIN_KEY);
  rtc_gpio_deinit(GPIO_NUM_0);

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1: {
      // 区分长短按:等待松开,超过阈值即长按
      pinMode(PIN_KEY, INPUT_PULLUP);
      uint32_t start = millis();
      while (digitalRead(PIN_KEY) == LOW) {
        if (millis() - start >= KEY_LONGPRESS_MS) return WAKE_KEY_LONG;
        delay(10);
      }
      return WAKE_KEY_SHORT;
    }
    default:
      return WAKE_COLD;
  }
}

void Power_DeepSleep(uint32_t seconds) {
  // 等按键松开,避免手还按着就再次触发唤醒
  pinMode(PIN_KEY, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(PIN_KEY) == LOW && millis() - t0 < 3000) delay(10);

  // 锁定 LCD 引脚电平,保证深睡期间面板供电/控制脚不漂移,画面由 ST7305 LPM 自持
  for (int pin : lcdPins) gpio_hold_en((gpio_num_t)pin);
  gpio_deep_sleep_hold_en();

  // KEY 低电平唤醒:EXT0 单引脚方式,完整走 RTC GPIO 初始化
  // (板上 KEY=GPIO18 有外部 10K 上拉,内部上拉只是兜底)
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_init((gpio_num_t)PIN_KEY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_direction((gpio_num_t)PIN_KEY, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pullup_en((gpio_num_t)PIN_KEY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY));
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_err_t err = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);

  // 诊断:BOOT 键(GPIO0,外部上拉)作为第二唤醒源(EXT1 ANY_LOW)。
  // BOOT 一定连通(下载模式可进即证明),用于验证唤醒机制本身。
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_init(GPIO_NUM_0));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pullup_en(GPIO_NUM_0));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pulldown_dis(GPIO_NUM_0));
  esp_err_t err1 = esp_sleep_enable_ext1_wakeup(1ULL << 0, ESP_EXT1_WAKEUP_ANY_LOW);
  log_i("ext0(key18) err=%d lvl=%d | ext1(boot0) err=%d lvl=%d", err,
        rtc_gpio_get_level((gpio_num_t)PIN_KEY), err1, rtc_gpio_get_level(GPIO_NUM_0));

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}
