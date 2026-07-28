// WiFi + SNTP + 飞书多维表格取数
#pragma once

#include <stdbool.h>
#include "menu_store.h"

typedef struct {
  bool wifiOk;
  bool timeOk;   // SNTP 校时成功(仅 needTime 时尝试)
  bool fetchOk;  // 菜单拉取并解析成功
} SyncResult;

// 全流程:连 WiFi →(可选)SNTP 校时并回写硬件 RTC → 获取 token → 拉取今明两天菜单。
// fetchOk 为 true 时 menu 中为最新数据(lastSync 已更新),调用方负责持久化与断开 WiFi。
SyncResult Lark_SyncAll(bool needTime, MenuData *menu);

void Net_Disconnect();
