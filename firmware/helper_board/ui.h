#pragma once

#include <time.h>
#include "display_bsp.h"
#include "battery.h"
#include "menu_store.h"

typedef struct {
  struct tm now;
  bool timeValid;
  char wakeTag[12];  // 唤醒诊断:C=冷启 T=定时 K=按键 L=长按 + 计数 + KEY电平,如 "T12 k1"
  BatteryState batt;
  MenuData menu;
  MenuPage page;
  bool syncAttempted;  // 本次唤醒尝试过联网同步
  bool syncOk;
} UiModel;

// 单次全量渲染:构建 LVGL 界面并刷到屏幕(内部完成 lv_init 与 display 注册)
void Ui_RenderAll(DisplayPort *port, const UiModel *m);
