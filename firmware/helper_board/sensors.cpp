#include <Arduino.h>
#include <Wire.h>
#include "sensors.h"
#include "config.h"

static const uint8_t SHTC3_ADDR = 0x70;
static const uint8_t PCF85063_ADDR = 0x51;

static const uint16_t SHTC3_CMD_WAKEUP = 0x3517;
static const uint16_t SHTC3_CMD_SLEEP = 0xB098;
static const uint16_t SHTC3_CMD_MEASURE = 0x7866;  // T 在前,polling,普通精度

void Sensors_Init() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
}

static bool shtc3_cmd(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xff);
  return Wire.endTransmission() == 0;
}

// Sensirion CRC8, poly 0x31, init 0xFF
static uint8_t shtc3_crc(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

bool Shtc3_Read(float *tempC, float *humi) {
  if (!shtc3_cmd(SHTC3_CMD_WAKEUP)) return false;
  delay(1);
  if (!shtc3_cmd(SHTC3_CMD_MEASURE)) return false;
  delay(20);
  uint8_t buf[6];
  if (Wire.requestFrom(SHTC3_ADDR, (uint8_t)6) != 6) return false;
  for (int i = 0; i < 6; i++) buf[i] = Wire.read();
  shtc3_cmd(SHTC3_CMD_SLEEP);
  if (shtc3_crc(buf, 2) != buf[2] || shtc3_crc(buf + 3, 2) != buf[5]) return false;
  uint16_t rawT = (buf[0] << 8) | buf[1];
  uint16_t rawH = (buf[3] << 8) | buf[4];
  *tempC = 175.0f * rawT / 65536.0f - 45.0f - TEMP_OFFSET_C;
  *humi = 100.0f * rawH / 65536.0f;
  return true;
}

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool Rtc_GetTime(struct tm *out) {
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(0x04);  // Seconds 寄存器起
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(PCF85063_ADDR, (uint8_t)7) != 7) return false;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = Wire.read();
  if (r[0] & 0x80) return false;  // OS 标志:掉电过,时间不可信
  memset(out, 0, sizeof(*out));
  out->tm_sec = bcd2dec(r[0] & 0x7f);
  out->tm_min = bcd2dec(r[1] & 0x7f);
  out->tm_hour = bcd2dec(r[2] & 0x3f);
  out->tm_mday = bcd2dec(r[3] & 0x3f);
  out->tm_wday = r[4] & 0x07;
  out->tm_mon = bcd2dec(r[5] & 0x1f) - 1;
  out->tm_year = bcd2dec(r[6]) + 100;  // PCF 存 00-99,按 20xx
  return (out->tm_year + 1900) >= 2024;
}

bool Rtc_SetTime(const struct tm *t) {
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(0x04);
  Wire.write(dec2bcd(t->tm_sec));  // 写秒时 OS 标志自动清零
  Wire.write(dec2bcd(t->tm_min));
  Wire.write(dec2bcd(t->tm_hour));
  Wire.write(dec2bcd(t->tm_mday));
  Wire.write(t->tm_wday & 0x07);
  Wire.write(dec2bcd(t->tm_mon + 1));
  Wire.write(dec2bcd((t->tm_year + 1900) % 100));
  return Wire.endTransmission() == 0;
}
