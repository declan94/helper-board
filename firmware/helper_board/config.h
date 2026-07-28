#pragma once

// ===== 硬件引脚 (Waveshare ESP32-S3-RLCD-4.2) =====
#define PIN_LCD_MOSI 12
#define PIN_LCD_SCK 11
#define PIN_LCD_DC 5
#define PIN_LCD_CS 40
#define PIN_LCD_RST 41

#define PIN_I2C_SDA 13
#define PIN_I2C_SCL 14

#define PIN_KEY 18       // 自定义按键,低有效,RTC GPIO,可唤醒深睡眠
#define PIN_BAT_ADC 4    // ADC1_CH3,电池电压经 1/3 分压

#define LCD_WIDTH 400    // 横屏
#define LCD_HEIGHT 300

// ===== 时间与餐次 =====
#define TZ_STRING "CST-8"          // Asia/Shanghai
#define NTP_SERVER1 "ntp.aliyun.com"
#define NTP_SERVER2 "cn.pool.ntp.org"

// 默认餐次时段(小时,含起点不含终点): [5,10)早 [10,15.5)午 [15.5,20.5)晚 [20.5,~)明日预览
#define MEAL_BREAKFAST_START_MIN (5 * 60)
#define MEAL_LUNCH_START_MIN (10 * 60)
#define MEAL_DINNER_START_MIN (15 * 60 + 30)
#define TOMORROW_PREVIEW_START_MIN (20 * 60 + 30)

// ===== 唤醒与同步策略 =====
#define WAKE_INTERVAL_SEC (30 * 60)   // 定时唤醒周期
#define SYNC_MAX_AGE_SEC (2 * 3600)   // 缓存超过该时长则联网同步
// 这些整点所在的首个定时唤醒强制同步(覆盖三餐前更新当日菜单的场景)
#define SYNC_FORCE_HOURS {6, 10, 15, 20}

#define WIFI_CONNECT_TIMEOUT_MS 12000
#define SNTP_TIMEOUT_MS 8000
#define KEY_LONGPRESS_MS 1000         // 唤醒后按住超过该时长视为长按(强制同步)

// ===== 传感器 =====
#define TEMP_OFFSET_C 0.0f  // SHTC3 自发热补偿(常亮工况官方用 4.0,深睡工况几乎无自热)

// ===== 电池 =====
#define BAT_VOLTAGE_FULL 4.12f
#define BAT_VOLTAGE_EMPTY 3.00f
#define BAT_PLUGGED_THRESHOLD 4.15f   // 高于此电压视为外部供电(无 VBUS 检测脚时的启发式)
#define BAT_LOW_WARN_PCT 15
