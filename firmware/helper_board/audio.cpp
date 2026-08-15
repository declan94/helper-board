#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include "audio.h"
#include "config.h"
#include "src/audio/ringtone_pcm.h"

// ES8311 寄存器序列蒸馏自 espressif esp_codec_dev 的 es8311.c(Apache-2.0),
// 把 master/slave、MCLK 来源、采样率、位宽等通用分支按本板固定取值折叠成常量:
//   从模式(ESP32 做 I2S 主) / 用外部 MCLK / 不反相 / 16kHz / 16bit / 仅 DAC。
// MCLK = 16000 × 256 = 4.096MHz,对应参考实现系数表 {4096000, 16000} 那一行,
// 该行的分频系数恰好全为 1,所以下面几个时钟寄存器写的都是 0。
static const uint8_t ES8311_ADDR = 0x18;

static const uint8_t REG_RESET = 0x00, REG_CLK1 = 0x01, REG_CLK2 = 0x02, REG_CLK3 = 0x03;
static const uint8_t REG_CLK4 = 0x04, REG_CLK5 = 0x05, REG_CLK6 = 0x06, REG_CLK7 = 0x07;
static const uint8_t REG_CLK8 = 0x08, REG_SDPIN = 0x09, REG_SDPOUT = 0x0A;
static const uint8_t REG_SYS0B = 0x0B, REG_SYS0C = 0x0C, REG_SYS0D = 0x0D, REG_SYS0E = 0x0E;
static const uint8_t REG_SYS10 = 0x10, REG_SYS11 = 0x11, REG_SYS12 = 0x12, REG_SYS13 = 0x13;
static const uint8_t REG_SYS14 = 0x14, REG_ADC15 = 0x15, REG_ADC16 = 0x16, REG_ADC17 = 0x17;
static const uint8_t REG_ADC1B = 0x1B, REG_ADC1C = 0x1C;
static const uint8_t REG_DACVOL = 0x32, REG_DAC37 = 0x37, REG_GPIO44 = 0x44, REG_GP45 = 0x45;
static const uint8_t REG_CHIPID1 = 0xFD, REG_CHIPID2 = 0xFE;

static i2s_chan_handle_t sTx = NULL;
static bool sCodecUp = false;

// 每次喂给 I2S 的帧数。512 帧 @16kHz = 32ms,即按键中止的最小粒度。
// 放静态区:本工程 loopTask 栈紧张(见 helper_board.ino 的注释)。
static const size_t kChunkFrames = 512;
static int16_t sStereoBuf[kChunkFrames * 2];

static bool regWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool regRead(uint8_t reg, uint8_t *val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ES8311_ADDR, (uint8_t)1) != 1) return false;
  *val = Wire.read();
  return true;
}

// 读改写:保留原值里本次不该动的位
static bool regUpdate(uint8_t reg, uint8_t andMask, uint8_t orMask) {
  uint8_t v;
  if (!regRead(reg, &v)) return false;
  return regWrite(reg, (uint8_t)((v & andMask) | orMask));
}

// 深睡期间这几个脚是被 gpio_hold_en 锁住的,醒来后必须先解锁才能重新配置。
// 与 LCD 引脚不同,音频脚解锁后短暂悬空无害(马上就会被驱动),不需要延后。
static void audioUnhold() {
  const gpio_num_t pins[] = { (gpio_num_t)PIN_AUDIO_PA, (gpio_num_t)PIN_I2S_MCLK,
                              (gpio_num_t)PIN_I2S_BCLK, (gpio_num_t)PIN_I2S_WS,
                              (gpio_num_t)PIN_I2S_DOUT };
  for (gpio_num_t p : pins) gpio_hold_dis(p);
}

static void paEnable(bool on) {
  // 这里刻意不用 gpio_reset_pin:它会瞬间挂上内部上拉,在功放使能脚上就是一声"啪"
  gpio_set_direction((gpio_num_t)PIN_AUDIO_PA, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)PIN_AUDIO_PA, on ? 1 : 0);
}

// 对应参考实现的 es8311_open + es8311_set_fs
static bool codecOpen() {
  uint8_t id1 = 0, id2 = 0;
  // 首次 I2C 写偶发失败(参考实现有同样的注释),先写两遍抗噪寄存器
  regWrite(REG_GPIO44, 0x08);
  regWrite(REG_GPIO44, 0x08);
  if (!regRead(REG_CHIPID1, &id1) || !regRead(REG_CHIPID2, &id2)) {
    log_e("ES8311 无应答(I2C 0x%02x)", ES8311_ADDR);
    return false;
  }
  if (id1 != 0x83 || id2 != 0x11) log_w("ES8311 chip id 异常: %02x %02x", id1, id2);

  bool ok = true;
  ok &= regWrite(REG_CLK1, 0x30);
  ok &= regWrite(REG_CLK2, 0x00);
  ok &= regWrite(REG_CLK3, 0x10);
  ok &= regWrite(REG_ADC16, 0x24);
  ok &= regWrite(REG_CLK4, 0x10);
  ok &= regWrite(REG_CLK5, 0x00);
  ok &= regWrite(REG_SYS0B, 0x00);
  ok &= regWrite(REG_SYS0C, 0x00);
  ok &= regWrite(REG_SYS10, 0x1F);
  ok &= regWrite(REG_SYS11, 0x7F);
  ok &= regWrite(REG_RESET, 0x80);  // 从模式:bit6 清零
  ok &= regWrite(REG_CLK1, 0x3F);   // 用外部 MCLK、不反相
  ok &= regUpdate(REG_CLK6, ~0x20, 0x00);  // SCLK 不反相
  ok &= regWrite(REG_SYS13, 0x10);
  ok &= regWrite(REG_ADC1B, 0x0A);
  ok &= regWrite(REG_ADC1C, 0x6A);
  ok &= regWrite(REG_GPIO44, 0x08);  // 纯放音,不需要 DAC→ADC 内部参考回环

  // ---- 位宽 16bit + 标准 I2S 格式(参考实现 set_bits_per_sample / config_fmt)----
  ok &= regUpdate(REG_SDPIN, 0xFF, 0x0C);
  ok &= regUpdate(REG_SDPOUT, 0xFF, 0x0C);
  ok &= regUpdate(REG_SDPIN, 0xFC, 0x00);
  ok &= regUpdate(REG_SDPOUT, 0xFC, 0x00);

  // ---- 时钟系数(config_sample,MCLK 4.096M / 16kHz,各分频均为 1)----
  ok &= regUpdate(REG_CLK2, 0x07, 0x00);  // pre_div=1, pre_multi=1
  ok &= regWrite(REG_CLK5, 0x00);         // adc_div=1, dac_div=1
  ok &= regUpdate(REG_CLK3, 0x80, 0x10);  // fs_mode=0, adc_osr=0x10
  ok &= regUpdate(REG_CLK4, 0x80, 0x20);  // dac_osr=0x20
  ok &= regUpdate(REG_CLK7, 0xC0, 0x00);  // lrck_h=0
  ok &= regWrite(REG_CLK8, 0xFF);         // lrck_l=0xff
  ok &= regUpdate(REG_CLK6, 0xE0, 0x03);  // bclk_div=4 → 写 3

  ok &= regWrite(REG_DACVOL, AUDIO_VOLUME_REG);
  return ok;
}

// 对应参考实现的 es8311_start(codec_mode = DAC)
static bool codecStart() {
  bool ok = true;
  ok &= regWrite(REG_RESET, 0x80);
  ok &= regWrite(REG_CLK1, 0x3F);
  ok &= regUpdate(REG_SDPIN, 0xBF, 0x00);   // DAC 通道取消静音
  ok &= regUpdate(REG_SDPOUT, 0xBF, 0x40);  // ADC 通道保持静音(不用麦克风)
  ok &= regWrite(REG_ADC17, 0xBF);
  ok &= regWrite(REG_SYS0E, 0x02);
  ok &= regWrite(REG_SYS12, 0x00);
  ok &= regWrite(REG_SYS14, 0x1A);
  ok &= regUpdate(REG_SYS14, ~0x40, 0x00);  // 非数字麦
  ok &= regWrite(REG_SYS0D, 0x01);
  ok &= regWrite(REG_ADC15, 0x40);
  ok &= regWrite(REG_DAC37, 0x08);
  ok &= regWrite(REG_GP45, 0x00);
  return ok;
}

// 对应参考实现的 es8311_suspend
static void codecSuspend() {
  regWrite(REG_DACVOL, 0x00);
  regWrite(REG_ADC17, 0x00);
  regWrite(REG_SYS0E, 0xFF);
  regWrite(REG_SYS12, 0x02);
  regWrite(REG_SYS14, 0x00);
  regWrite(REG_SYS0D, 0xFA);
  regWrite(REG_ADC15, 0x00);
  regWrite(REG_CLK2, 0x10);
  regWrite(REG_RESET, 0x00);
  regWrite(REG_RESET, 0x1F);
  regWrite(REG_CLK1, 0x30);
  regWrite(REG_CLK1, 0x00);
  regWrite(REG_GP45, 0x00);
  regWrite(REG_SYS0D, 0xFC);
  regWrite(REG_CLK2, 0x00);
}

static bool i2sStart() {
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = 4;
  chanCfg.dma_frame_num = 256;  // 4×256 帧 ≈ 64ms 缓冲
  if (i2s_new_channel(&chanCfg, &sTx, NULL) != ESP_OK) return false;

  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RINGTONE_SAMPLE_RATE);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // 必须 256,与 codec 系数表对应
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = (gpio_num_t)PIN_I2S_MCLK;
  std.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
  std.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
  std.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  std.gpio_cfg.invert_flags.mclk_inv = false;
  std.gpio_cfg.invert_flags.bclk_inv = false;
  std.gpio_cfg.invert_flags.ws_inv = false;

  if (i2s_channel_init_std_mode(sTx, &std) != ESP_OK) return false;
  return i2s_channel_enable(sTx) == ESP_OK;
}

bool Audio_Init() {
  audioUnhold();
  paEnable(false);  // 先确保功放是关的,避免初始化过程中的杂音上喇叭
  if (!i2sStart()) {
    log_e("I2S 初始化失败");
    Audio_Deinit();
    return false;
  }
  if (!codecOpen() || !codecStart()) {
    log_e("ES8311 初始化失败");
    Audio_Deinit();
    return false;
  }
  sCodecUp = true;

  // 先喂一段静音把 DMA 灌满、时钟稳定,再开功放,消掉上电"啪"声
  memset(sStereoBuf, 0, sizeof(sStereoBuf));
  size_t written = 0;
  for (int i = 0; i < 2; i++)
    i2s_channel_write(sTx, sStereoBuf, sizeof(sStereoBuf), &written, pdMS_TO_TICKS(200));
  paEnable(true);
  delay(10);
  return true;
}

int Audio_PlayRingtone(int maxLoops, bool (*shouldStop)()) {
  if (!sTx) return 0;
  int loops = 0;
  for (; loops < maxLoops; loops++) {
    size_t pos = 0;
    while (pos < RINGTONE_SAMPLE_COUNT) {
      if (shouldStop && shouldStop()) return loops;
      size_t n = RINGTONE_SAMPLE_COUNT - pos;
      if (n > kChunkFrames) n = kChunkFrames;
      // flash 里存的是单声道,复制成左右声道再送 I2S(省一半 flash)
      for (size_t k = 0; k < n; k++) {
        int16_t s = kRingtonePcm[pos + k];
        sStereoBuf[2 * k] = s;
        sStereoBuf[2 * k + 1] = s;
      }
      size_t written = 0;
      if (i2s_channel_write(sTx, sStereoBuf, n * 2 * sizeof(int16_t), &written,
                            pdMS_TO_TICKS(500)) != ESP_OK)
        return loops;
      pos += n;
    }
  }
  return loops;
}

void Audio_Deinit() {
  paEnable(false);  // 先切功放,DMA 里残留的最多 64ms 音频不会漏出去
  delay(5);
  if (sCodecUp) {
    codecSuspend();
    sCodecUp = false;
  }
  if (sTx) {
    i2s_channel_disable(sTx);
    i2s_del_channel(sTx);
    sTx = NULL;
  }
}

void Audio_IdleSafe() {
  audioUnhold();
  paEnable(false);  // 功放先关,且不经 gpio_reset_pin(见 paEnable 注释)

  // I2S 四个脚可能还连在外设矩阵上,要 reset 才能断开改成普通输出。
  // 它们不接功放使能,过一下内部上拉无害。
  const gpio_num_t i2sPins[] = { (gpio_num_t)PIN_I2S_MCLK, (gpio_num_t)PIN_I2S_BCLK,
                                 (gpio_num_t)PIN_I2S_WS, (gpio_num_t)PIN_I2S_DOUT };
  for (gpio_num_t p : i2sPins) {
    gpio_reset_pin(p);
    gpio_set_direction(p, GPIO_MODE_OUTPUT);
    gpio_set_level(p, 0);
  }
}
