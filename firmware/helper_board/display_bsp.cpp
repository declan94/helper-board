#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "display_bsp.h"

DisplayPort::DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height,
                         spi_host_device_t spihost)
  : mosi_(mosi), scl_(scl), dc_(dc), cs_(cs), rst_(rst), width_(width), height_(height) {
  // 深睡唤醒路径:引脚仍处于 gpio_hold 锁定。先按空闲电平配置为输出,
  // 再逐个解除保持——保证任何时刻 RST/CS 都不悬空(悬空会硬复位面板)。
  const struct { int pin; uint32_t level; } idlePins[] = {
    { rst_, 1 }, { cs_, 1 }, { dc_, 0 }, { scl_, 0 }, { mosi_, 0 }
  };
  for (auto &p : idlePins) {
    gpio_config_t conf = {};
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.mode = GPIO_MODE_OUTPUT;
    conf.pin_bit_mask = (0x1ULL << p.pin);
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&conf));
    gpio_set_level((gpio_num_t)p.pin, p.level);
    gpio_hold_dis((gpio_num_t)p.pin);  // 引脚已是正确电平,此刻解锁无毛刺
  }

  spi_bus_config_t buscfg = {};
  int transfer = width_ * height_;
  buscfg.miso_io_num = -1;
  buscfg.mosi_io_num = mosi_;
  buscfg.sclk_io_num = scl_;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = transfer;
  ESP_ERROR_CHECK(spi_bus_initialize(spihost, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = dc_;
  io_config.cs_gpio_num = cs_;
  io_config.pclk_hz = 10 * 1000 * 1000;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spihost, &io_config, &io_handle));

  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = (0x1ULL << rst_);
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
  gpio_set_level((gpio_num_t)rst_, 1);

  DisplayLen = transfer >> 3;  // 1bpp
  DispBuffer = (uint8_t *)heap_caps_malloc(DisplayLen, MALLOC_CAP_DMA);
  assert(DispBuffer);
  RLCD_ColorClear(ColorWhite);
}

void DisplayPort::RLCD_Init(bool coldBoot) {
  if (coldBoot) {
    RLCD_Reset();
    RLCD_PanelConfig();
    RLCD_ColorClear(ColorWhite);
  } else {
    // 面板保持着上次画面,切回 HPM 以便写入新帧
    RLCD_SendCommand(0x38);  // High Power Mode
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 官方初始化序列(ST7305)
void DisplayPort::RLCD_PanelConfig(void) {
  RLCD_SendCommand(0xD6);  // NVM Load Control
  RLCD_SendData(0x17);
  RLCD_SendData(0x02);

  RLCD_SendCommand(0xD1);  // Booster Enable
  RLCD_SendData(0x01);

  RLCD_SendCommand(0xC0);  // Gate Voltage Control
  RLCD_SendData(0x11);
  RLCD_SendData(0x04);

  RLCD_SendCommand(0xC1);  // VSHP Setting
  RLCD_SendData(0x69);
  RLCD_SendData(0x69);
  RLCD_SendData(0x69);
  RLCD_SendData(0x69);

  RLCD_SendCommand(0xC2);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);

  RLCD_SendCommand(0xC4);
  RLCD_SendData(0x4B);
  RLCD_SendData(0x4B);
  RLCD_SendData(0x4B);
  RLCD_SendData(0x4B);

  RLCD_SendCommand(0xC5);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);
  RLCD_SendData(0x19);

  RLCD_SendCommand(0xD8);
  RLCD_SendData(0x80);
  RLCD_SendData(0xE9);

  RLCD_SendCommand(0xB2);  // Frame Rate Control
  RLCD_SendData(0x02);

  RLCD_SendCommand(0xB3);  // Update Period Gate EQ Control (HPM)
  RLCD_SendData(0xE5);
  RLCD_SendData(0xF6);
  RLCD_SendData(0x05);
  RLCD_SendData(0x46);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x76);
  RLCD_SendData(0x45);

  RLCD_SendCommand(0xB4);  // Update Period Gate EQ Control (LPM)
  RLCD_SendData(0x05);
  RLCD_SendData(0x46);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x77);
  RLCD_SendData(0x76);
  RLCD_SendData(0x45);

  RLCD_SendCommand(0x62);
  RLCD_SendData(0x32);
  RLCD_SendData(0x03);
  RLCD_SendData(0x1F);

  RLCD_SendCommand(0xB7);
  RLCD_SendData(0x13);

  RLCD_SendCommand(0xB0);
  RLCD_SendData(0x64);

  RLCD_SendCommand(0x11);  // Sleep Out
  vTaskDelay(pdMS_TO_TICKS(200));

  RLCD_SendCommand(0xC9);
  RLCD_SendData(0x00);

  RLCD_SendCommand(0x36);  // Memory Access Control
  RLCD_SendData(0x48);

  RLCD_SendCommand(0x3A);  // Pixel Format
  RLCD_SendData(0x11);

  RLCD_SendCommand(0xB9);
  RLCD_SendData(0x20);

  RLCD_SendCommand(0xB8);
  RLCD_SendData(0x29);

  RLCD_SendCommand(0x21);  // Inversion On

  RLCD_SendCommand(0x2A);  // Column Address
  RLCD_SendData(0x12);
  RLCD_SendData(0x2A);

  RLCD_SendCommand(0x2B);  // Row Address
  RLCD_SendData(0x00);
  RLCD_SendData(0xC7);

  RLCD_SendCommand(0x35);  // TE On
  RLCD_SendData(0x00);

  RLCD_SendCommand(0xD0);
  RLCD_SendData(0xFF);

  RLCD_SendCommand(0x38);  // High Power Mode
  RLCD_SendCommand(0x29);  // Display On
}

void DisplayPort::RLCD_ColorClear(uint8_t color) {
  memset(DispBuffer, color, DisplayLen);
}

void DisplayPort::RLCD_Display() {
  RLCD_SendCommand(0x2A);
  RLCD_SendData(0x12);
  RLCD_SendData(0x2A);

  RLCD_SendCommand(0x2B);
  RLCD_SendData(0x00);
  RLCD_SendData(0xC7);

  RLCD_SendCommand(0x2C);  // Memory Write
  RLCD_SendBuffer(DispBuffer, DisplayLen);
}

void DisplayPort::RLCD_EnterLowPower() {
  RLCD_SendCommand(0x39);  // Low Power Mode:面板自持画面
}

// 横屏像素映射(来自官方 RLCD_SetLandscapePixel 移位算法)
void DisplayPort::RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color) {
  if (x >= width_ || y >= height_) return;
  uint16_t inv_y = (height_ - 1 - y);
  const uint16_t H4 = height_ >> 2;
  uint16_t byte_x = x >> 1;
  uint16_t block_y = inv_y >> 2;
  uint32_t index = byte_x * H4 + block_y;
  uint8_t local_x = x & 0x01;
  uint8_t local_y = inv_y & 0x03;
  uint8_t mask = 1 << (7 - ((local_y << 1) | local_x));
  if (color)
    DispBuffer[index] |= mask;
  else
    DispBuffer[index] &= ~mask;
}

void DisplayPort::RLCD_Reset(void) {
  gpio_set_level((gpio_num_t)rst_, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level((gpio_num_t)rst_, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level((gpio_num_t)rst_, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
}

void DisplayPort::RLCD_SendCommand(uint8_t Reg) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, Reg, NULL, 0));
}

void DisplayPort::RLCD_SendData(uint8_t Data) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, -1, &Data, 1));
}

void DisplayPort::RLCD_SendBuffer(uint8_t *Data, int len) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, Data, len));
}
