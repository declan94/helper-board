// 深睡眠管理:唤醒源判定、按键长短按判定、LCD 引脚电平保持、入睡
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  WAKE_COLD = 0,  // 上电/复位,面板需要完整初始化
  WAKE_TIMER,     // 定时唤醒
  WAKE_KEY_PAGE,  // KEY 短按:切换页面
  WAKE_KEY_SYNC,  // KEY 长按:强制同步
} WakeCause;

// 注意:BOOT(GPIO0)是启动模式 strap 引脚,深睡唤醒复位时若仍按住会把
// 芯片带进下载模式假死,绝不可用作运行期按键。唯一用户按键为 KEY(GPIO18),
// 长短按用 RTC 域电平读取区分(数字域读取在深睡唤醒后不可靠)。
WakeCause Power_GetWakeCause();

// 刷新完成后调用:面板进低功耗保持、锁定 LCD 引脚、配置唤醒源并深睡。不返回。
void Power_DeepSleep(uint32_t seconds);
