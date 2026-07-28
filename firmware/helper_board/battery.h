#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  float voltage;   // 电池电压 V
  uint8_t percent; // 0-100
  bool plugged;    // 外部供电(电压启发式,见 config.h)
} BatteryState;

BatteryState Battery_Read();
