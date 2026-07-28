#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "power.h"
#include "config.h"

static const int lcdPins[] = { PIN_LCD_RST, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_SCK, PIN_LCD_MOSI };

WakeCause Power_GetWakeCause() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // 醒来后先解除引脚保持,否则 SPI 无法驱动屏幕
  for (int pin : lcdPins) gpio_hold_dis((gpio_num_t)pin);

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WAKE_TIMER;
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

  // KEY 低电平唤醒(EXT1),启用 RTC 域内部上拉兜底
  rtc_gpio_pullup_en((gpio_num_t)PIN_KEY);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_KEY, ESP_EXT1_WAKEUP_ANY_LOW);

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}
