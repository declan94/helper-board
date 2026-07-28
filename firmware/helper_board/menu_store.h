// 菜单数据模型 + NVS 持久缓存(断网/重启后仍可显示最近一次同步结果)
#pragma once

#include <time.h>
#include <stdbool.h>

typedef enum {
  PAGE_BREAKFAST = 0,
  PAGE_LUNCH = 1,
  PAGE_DINNER = 2,
  PAGE_TOMORROW = 3,
  PAGE_COUNT = 4,
} MenuPage;

typedef struct {
  char date[11];        // "2026-07-28"
  char breakfast[256];  // 多行文本,一行一道菜
  char lunch[256];
  char dinner[256];
  char note[128];
  bool valid;
} DayMenu;

typedef struct {
  DayMenu today;
  DayMenu tomorrow;
  time_t lastSync;  // 0 = 从未同步
} MenuData;

// 从 NVS 加载缓存,并按传入的今/明日期字符串重新对号入座
// (跨天后旧缓存里的"明天"会自动变成"今天")
void MenuStore_Load(MenuData *out, const char *todayStr, const char *tomorrowStr);
void MenuStore_Save(const MenuData *data);

// 根据当前时刻(分钟数)得出默认页
MenuPage MenuPage_DefaultFor(int minuteOfDay);
