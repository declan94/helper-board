// 深睡眠管理:唤醒源判定、按键长短按判定、LCD 引脚电平保持、入睡
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  WAKE_COLD = 0,  // 上电/复位,面板需要完整初始化
  WAKE_TIMER,     // 定时唤醒
  WAKE_KEY_PAGE,  // KEY 键(GPIO18):切换页面
  WAKE_KEY_SYNC,  // BOOT 键(GPIO0):强制同步
} WakeCause;

// 按键分辨不依赖唤醒原因码(实测本板 EXT0/EXT1 上报与配置相反),
// 而是读 EXT1 状态寄存器:bit0 置位 = BOOT(GPIO0)触发,否则为 KEY。
// 深睡唤醒走 ROM 快速路径不采样 strap,BOOT 用作唤醒键安全(实测)。
WakeCause Power_GetWakeCause();

// 运行期读 KEY(GPIO18)是否按下。深睡唤醒后数字域 digitalRead 不可靠(hold/功能域),
// 一律走 RTC 域;首次调用会补做 rtc_gpio_init(冷启动时还没配过)。
bool Power_KeyPressed();

// 刷新完成后调用:面板进低功耗保持、锁定 LCD 引脚、配置唤醒源并深睡。不返回。
void Power_DeepSleep(uint32_t seconds);
