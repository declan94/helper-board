// WiFi 连接与快连缓存(Lark 同步与呼叫轮询共用)
#pragma once

#include <stdbool.h>

// 提前异步发起连接(不阻塞)。若 RTC 内存里有上次连上的 BSSID + 信道,直接定向
// 连接,省掉全信道扫描(实测是单次唤醒里最大的一块时间)。
void Net_BeginConnect();

// 阻塞等待连上。走快连且超时未成时,会当场作废缓存并回落全信道扫描重连一次,
// 避免一次快连失败白白浪费整个唤醒周期。连上后自动刷新缓存。
bool Net_WaitConnected();

// 断开并关射频。
void Net_Disconnect();
