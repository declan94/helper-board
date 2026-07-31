#pragma once

// ===== 硬件引脚 (Waveshare ESP32-S3-RLCD-4.2) =====
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41

#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14

#define PIN_KEY 18       // KEY 键:切页(EXT0);BOOT 键 GPIO0:强制同步(EXT1,按状态寄存器分辨)
#define PIN_BAT_ADC 4    // ADC1_CH3,电池电压经 1/3 分压

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
#define WAKE_INTERVAL_SEC (30 * 60)   // 定时唤醒周期,每次唤醒都联网同步菜单

#define WIFI_CONNECT_TIMEOUT_MS 12000
#define SNTP_TIMEOUT_MS 8000

// ===== 传感器 =====
#define TEMP_OFFSET_C 0.0f  // SHTC3 自发热补偿(常亮工况官方用 4.0,深睡工况几乎无自热)

// ===== 电池 =====
#define BAT_VOLTAGE_FULL 4.12f
#define BAT_VOLTAGE_EMPTY 3.00f
#define BAT_PLUGGED_THRESHOLD 4.15f   // 高于此电压视为外部供电(无 VBUS 检测脚时的启发式)
#define BAT_LOW_WARN_PCT 15
