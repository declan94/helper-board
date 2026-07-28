// ST7305 反射式 LCD 驱动,改写自 Waveshare 官方 09_LVGL_V9_Test/display_bsp
// 变更:去掉 PSRAM 查表(单次渲染用移位算法即可)、增加深睡眠所需的
// 低功耗保持模式(LPM)切换与免复位热启动。
#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

enum ColorSelection {
  ColorBlack = 0,
  ColorWhite = 0xff
};

class DisplayPort {
private:
  esp_lcd_panel_io_handle_t io_handle = NULL;
  int mosi_, scl_, dc_, cs_, rst_;
  int width_, height_;
  uint8_t *DispBuffer = NULL;
  int DisplayLen = 0;

  void RLCD_SendCommand(uint8_t Reg);
  void RLCD_SendData(uint8_t Data);
  void RLCD_SendBuffer(uint8_t *Data, int len);
  void RLCD_Reset(void);
  void RLCD_PanelConfig(void);

public:
  DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height,
              spi_host_device_t spihost = SPI3_HOST);

  // coldBoot=true:硬复位并下发完整初始化序列(会闪白一次)
  // coldBoot=false:深睡眠热启动,面板寄存器仍在,只切回高功耗模式准备刷新
  void RLCD_Init(bool coldBoot);
  void RLCD_ColorClear(uint8_t color);
  void RLCD_Display();
  void RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color);  // 横屏坐标
  void RLCD_EnterLowPower();   // LPM:面板自刷新保持画面,µA 级功耗
};
