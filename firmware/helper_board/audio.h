// 呼叫铃声播放:ES8311 codec(仅 DAC)+ I2S。
//
// 板载音频链路: ESP32-S3 --I2S--> ES8311(0x18) --> 功放(GPIO46 使能) --> 喇叭
// 只用放音,ES7210(麦克风)全程不上电。
#pragma once

#include <stdbool.h>

// 上电初始化 codec 与 I2S,并打开功放。失败(通常是 ES8311 没应答)返回 false。
// 调用前 Wire 必须已 begin(Sensors_Init 里做了)。
bool Audio_Init();

// 播放铃声,最多 maxLoops 遍。每 ~32ms 调一次 shouldStop,返回 true 立即中止。
// shouldStop 可为 NULL。返回实际播放完的遍数。
int Audio_PlayRingtone(int maxLoops, bool (*shouldStop)());

// 关功放、codec 进 suspend、释放 I2S。Audio_Init 失败后调用也安全。
void Audio_Deinit();

// 深睡前必须调用(无论有没有播过音):把功放使能与 I2S 引脚全部拉低成输出。
// GPIO45 是 VDD_SPI strap 脚、GPIO46 是 ROM log strap 脚,低电平正好是二者的
// 安全取值;拉低之后由 power.cpp 的 gpio_hold_en 锁住跨深睡。
void Audio_IdleSafe();
