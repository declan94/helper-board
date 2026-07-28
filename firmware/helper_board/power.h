// 深睡眠管理:唤醒源判定、按键长短按判定、LCD 引脚电平保持、入睡
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  WAKE_COLD = 0,   // 上电/复位,面板需要完整初始化
  WAKE_TIMER,      // 定时唤醒
  WAKE_KEY_SHORT,  // KEY 单击:切换页面
  WAKE_KEY_LONG,   // KEY 长按:强制同步
} WakeCause;

// 必须在 setup 里尽早调用(长按判定需要从唤醒瞬间开始计时)
WakeCause Power_GetWakeCause();

// 刷新完成后调用:面板进低功耗保持、锁定 LCD 引脚、配置唤醒源并深睡。不返回。
void Power_DeepSleep(uint32_t seconds);
