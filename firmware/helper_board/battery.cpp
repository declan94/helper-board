#include <Arduino.h>
#include "battery.h"
#include "config.h"

BatteryState Battery_Read() {
  // 多次采样取平均,analogReadMilliVolts 带出厂校准
  uint32_t mv = 0;
  const int N = 8;
  for (int i = 0; i < N; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  float v = (mv / (float)N) * 3.0f / 1000.0f;  // 板载 1/3 分压

  BatteryState st;
  st.voltage = v;
  if (v <= BAT_VOLTAGE_EMPTY) st.percent = 0;
  else if (v >= BAT_VOLTAGE_FULL) st.percent = 100;
  else st.percent = (uint8_t)((v - BAT_VOLTAGE_EMPTY) / (BAT_VOLTAGE_FULL - BAT_VOLTAGE_EMPTY) * 100.0f);
  st.plugged = v > BAT_PLUGGED_THRESHOLD;
  return st;
}
