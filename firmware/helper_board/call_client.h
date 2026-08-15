// 呼叫通道:轮询 ntfy 取呼叫消息、回执确认
//
// 触发端在手机上放一个快捷方式 POST 一下即可:
//   curl -d "回个电话" https://ntfy.sh/<你的呼叫 topic>
// 公有服务器上 topic 名就是唯一的凭证,务必用足够随机的长串。
#pragma once

#include <stdbool.h>
#include <time.h>

typedef struct {
  bool valid;
  char text[128];  // 消息正文,直接上屏
  time_t sentAt;   // 发送时刻(不是收到时刻)——轮询有延迟,屏幕上要显示这个
} CallMessage;

// 轮询一次。需要 WiFi 已连上(调用方先 Net_WaitConnected)。
// 返回值是"这次轮询有没有成功",用于判断呼叫通道是否健康;
// "有没有新呼叫"看 out->valid —— 两者必须分开,否则"没人叫我"会被误当成"通道挂了"。
// 只取上次已处理之后的消息;一个周期内积压多条时取最后一条(最新的意图)。
bool Call_Poll(CallMessage *out);

// 回执:让呼叫方知道这条呼叫的下落(响铃结束 / 有人按键确认)。
// detail 会原样出现在你手机的通知里。失败不重试:回执丢了不影响主流程。
void Call_SendAck(const char *detail);
