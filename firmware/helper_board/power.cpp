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
      // KEY 唤醒。用 RTC 域读电平区分长短按(数字域 digitalRead 在深睡
      // 唤醒后受 hold/功能域影响不可靠,RTC 域读取已在日志中验证准确)
      rtc_gpio_init((gpio_num_t)PIN_KEY);
      rtc_gpio_set_direction((gpio_num_t)PIN_KEY, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en((gpio_num_t)PIN_KEY);
      rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY);
      uint32_t start = millis();
      while (rtc_gpio_get_level((gpio_num_t)PIN_KEY) == 0) {
        if (millis() - start >= KEY_LONGPRESS_MS) return WAKE_KEY_SYNC;
        delay(10);
      }
      return WAKE_KEY_PAGE;
    }
    default:
      return WAKE_COLD;
  }
}

void Power_DeepSleep(uint32_t seconds) {
  // 锁定 LCD 引脚电平,保证深睡期间面板供电/控制脚不漂移,画面由 ST7305 LPM 自持
  // KEY 低电平唤醒:EXT0 单引脚方式,完整走 RTC GPIO 初始化
  // (板上 KEY=GPIO18 有外部 10K 上拉,内部上拉只是兜底)
  // GPIO0/BOOT 是 strap 引脚,严禁配置为唤醒源(唤醒复位采样时按住会进下载模式)
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_init((gpio_num_t)PIN_KEY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_direction((gpio_num_t)PIN_KEY, RTC_GPIO_MODE_INPUT_ONLY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pullup_en((gpio_num_t)PIN_KEY));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY));

  // 等按键松开再入睡(RTC 域读),避免手还按着立即重复唤醒
  uint32_t t0 = millis();
  while (rtc_gpio_get_level((gpio_num_t)PIN_KEY) == 0 && millis() - t0 < 3000) delay(10);

  for (int pin : lcdPins) gpio_hold_en((gpio_num_t)pin);
  gpio_deep_sleep_hold_en();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_err_t err = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);
  log_i("ext0(key18) err=%d lvl=%d", err, rtc_gpio_get_level((gpio_num_t)PIN_KEY));

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}
