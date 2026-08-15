#pragma once

#include <time.h>
#include "display_bsp.h"
#include "battery.h"
#include "menu_store.h"

typedef struct {
  struct tm now;
  time_t nowEpoch;
  bool timeValid;
  BatteryState batt;
  MenuData menu;
  MenuPage page;
  bool syncing;             // 正在同步:页脚显示 Updating...
  bool syncAttempted;       // 本次唤醒尝试过联网同步
  bool syncOk;
  time_t callChannelOkAt;   // 呼叫通道最近一次成功轮询的时刻,0 = 从未成功
} UiModel;

// 单次全量渲染:构建 LVGL 界面并刷到屏幕(内部完成 lv_init 与 display 注册)
void Ui_RenderAll(DisplayPort *port, const UiModel *m);

// 启动画面:冷启动联网期间(可达 20+ 秒)先给出可见反馈
void Ui_Splash(DisplayPort *port, const char *text);

// 呼叫全屏页:响铃期间显示留言与"发出时刻"。
// 显示的是消息发出的时间而不是收到的时间——轮询周期最长 10 分钟,
// 看的人得知道这条是刚发的还是十分钟前的。
void Ui_CallScreen(DisplayPort *port, const char *text, time_t sentAt, bool timeValid);
