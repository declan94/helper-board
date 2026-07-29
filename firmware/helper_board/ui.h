#pragma once

#include <time.h>
#include "display_bsp.h"
#include "battery.h"
#include "menu_store.h"

typedef struct {
  struct tm now;
  bool timeValid;
  BatteryState batt;
  MenuData menu;
  MenuPage page;
  bool syncing;        // 正在同步:页脚显示 Updating...
  bool syncAttempted;  // 本次唤醒尝试过联网同步
  bool syncOk;
} UiModel;

// 单次全量渲染:构建 LVGL 界面并刷到屏幕(内部完成 lv_init 与 display 注册)
void Ui_RenderAll(DisplayPort *port, const UiModel *m);

// 启动画面:冷启动联网期间(可达 20+ 秒)先给出可见反馈
void Ui_Splash(DisplayPort *port, const char *text);
