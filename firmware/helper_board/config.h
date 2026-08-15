#pragma once

// ===== 硬件引脚 (Waveshare ESP32-S3-RLCD-4.2) =====
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41

#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14

// 两个按键。2026-08-15 用响铃时的屏幕诊断实测确认对应关系(此前一直记反,
// 并因此在 power.cpp 留下过一条"机制成谜"的注释):
//   按 KEY 键  → 拉低 GPIO0  → 走 EXT1 唤醒 → 切页
//   按 BOOT 键 → 拉低 GPIO18 → 走 EXT0 唤醒 → 强制同步
// BOOT 同时是进下载模式用的那个键。两者均低有效、外部 10K 上拉。
#define PIN_BTN_KEY 0    // 切页
#define PIN_BTN_BOOT 18  // 强制同步
#define PIN_BAT_ADC 4    // ADC1_CH3,电池电压经 1/3 分压

// 板载音频(取自官方 07_Audio_Test 的板级配置 S3_RLCD_4_2)。
// ES8311 codec @0x18 与 SHTC3/PCF85063 共用 I2C;ES7210 麦克风 @0x40 本项目不用。
// 注意 PIN_I2S_WS(45)是 VDD_SPI strap、PIN_AUDIO_PA(46)是 ROM log strap,
// 深睡前必须锁在低电平,见 audio.cpp / power.cpp。
#define PIN_I2S_MCLK 16
#define PIN_I2S_BCLK 9
#define PIN_I2S_WS 45
#define PIN_I2S_DOUT 8
#define PIN_AUDIO_PA 46

#define LCD_WIDTH 400    // 横屏
#define LCD_HEIGHT 300

// ===== 时间与餐次 =====
#define TZ_STRING "CST-8"          // UTC+8(如在其他时区,按 POSIX TZ 格式修改)
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.cloudflare.com"

// 默认餐次时段(含起点不含终点): [5:00,9:00)早 [9:00,13:00)午 [13:00,19:30)晚 [19:30,~)明日预览
#define MEAL_BREAKFAST_START_MIN (5 * 60)
#define MEAL_LUNCH_START_MIN (9 * 60)
#define MEAL_DINNER_START_MIN (13 * 60)
#define TOMORROW_PREVIEW_START_MIN (19 * 60 + 30)

// ===== 唤醒与同步策略 =====
// 唤醒周期由呼叫轮询决定(见下方 CALL_POLL_INTERVAL_SEC),菜单同步搭它的顺风车:
// 每 SYNC_EVERY_N_POLLS 次轮询做一次完整 Lark 同步。

// 发射功率:压低是为了削减射频电流尖峰(与屏幕升压叠加疑似触发过掉电复位)。
// 若日志频繁出现 AUTH_EXPIRE / HANDSHAKE_TIMEOUT,说明 11dBm 对你家距离偏弱,
// 逐级往上调 WIFI_POWER_13dBm / 15dBm / 19_5dBm。
// 2026-08-15:11dBm 下连续多次冷启动握手超时(AUTH_EXPIRE / HANDSHAKE_TIMEOUT),
// 板子离路由器较远,上调到 15dBm。若仍不稳可再上 19_5dBm,但要留意掉电复位
// (当初压到 11dBm 就是为了避开射频尖峰与屏幕升压叠加)。
#define WIFI_TX_POWER WIFI_POWER_15dBm

// 握手超时(HANDSHAKE_TIMEOUT)多为偶发,同样的时间预算拆成两次尝试比死等一次更容易成功。
// 每次尝试之间会彻底断开重来,而不是对着一个卡住的连接继续等。
#define WIFI_CONNECT_ATTEMPTS 2
#define WIFI_CONNECT_TIMEOUT_MS 7000  // 单次尝试的超时(总预算 ≈ 2×7s,与原来的 12s 相当)
#define WIFI_FAST_CONNECT_TIMEOUT_MS 4000  // 定向连接(已知 BSSID+信道)正常 1 秒内完成,超时即回落
#define SNTP_TIMEOUT_MS 8000

// ===== 呼叫(ntfy)=====
#define CALL_POLL_INTERVAL_SEC 600      // 呼叫轮询周期。改小 = 更及时更费电(5 分钟约多耗 0.24mA)
#define SYNC_EVERY_N_POLLS 3            // 每 N 次轮询顺带做一次完整 Lark 同步(3×10min = 30min)
#define CALL_RING_MAX_LOOPS 4           // 铃声最多循环遍数(单遍约 3.4s),按 KEY 可提前停
#define CALL_HTTP_TIMEOUT_MS 6000
#define CALL_CHANNEL_STALE_SEC (2 * 3600)  // 呼叫通道多久没通就在页脚报警

// ES8311 DAC 音量寄存器(0x32):0xBF = 0dB 数字直通,再往上是数字增益会削顶。
// 铃声 PCM 峰值留了约 3dB 余量,想更响优先改 tools/gen_ringtone.py 里的 PEAK。
#define AUDIO_VOLUME_REG 0xBF

// 自检开关:置 1 时冷启动会直接响一遍铃声,用来验证音频链路。验完改回 0。
#define AUDIO_SELFTEST 0

// ===== 传感器 =====
#define TEMP_OFFSET_C 0.0f  // SHTC3 自发热补偿(常亮工况官方用 4.0,深睡工况几乎无自热)

// ===== 电池 =====
#define BAT_VOLTAGE_FULL 4.12f
#define BAT_VOLTAGE_EMPTY 3.00f
#define BAT_PLUGGED_THRESHOLD 4.15f   // 高于此电压视为外部供电(无 VBUS 检测脚时的启发式)
#define BAT_LOW_WARN_PCT 15
