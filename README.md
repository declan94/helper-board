# helper-board 家庭菜单显示面板

基于 Waveshare **ESP32-S3-RLCD-4.2**(4.2 寸反射式 LCD,类墨水屏观感、超低功耗)的菜单面板:
家人在 **Lark 多维表格(Base)** 里填每天的早/中/晚菜单,面板定时拉取并显示,供家中帮工查看。
(对接 Lark 国际版 `open.larksuite.com`;中国版飞书只需改 `lark_client.cpp` 中的域名,两版根证书均已内置)

```
┌────────────────────────────────────────────┐
│ 7月28日 周二        26.5°C  58%      82% ▮ │   日期 · 温湿度 · 电量
├────────────────────────────────────────────┤
│    [早餐]   ■午餐■   [晚餐]   [明日]        │   KEY 键切换
│                                            │
│    西红柿炒蛋                               │
│    清蒸鲈鱼                                 │   当前餐次菜单
│    蒜蓉油麦菜                               │
├────────────────────────────────────────────┤
│ 备注:少辣            更新于 07-28 11:02    │
└────────────────────────────────────────────┘
```

## 工作方式

- **18650 电池供电、深睡眠架构**:绝大部分时间深睡(屏幕靠 ST7305 低功耗模式自持画面),每 30 分钟醒来联网同步一次(SNTP 校时 + 拉取今明两天菜单,缓存进 NVS,断网时显示缓存),菜单更新最迟半小时自动上屏
- **餐次自动切换**:05:00 早餐 → 10:00 午餐 → 15:30 晚餐 → 20:30 明日菜单预览
- **KEY 键**(GPIO18):按一下循环切页(早/中/晚/明日);**BOOT 键**(GPIO0):按一下立即联网同步(改完表格想马上生效时用)
- 时间由板载 PCF85063 RTC 维持,联网时 SNTP 校准;**不显示时钟**(深睡下无法走字),只显示日期

## 快速开始

```bash
# 0. 依赖:arduino-cli(brew install arduino-cli)、node、python3
arduino-cli core install esp32:esp32 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# 1. 生成中文字库(首次;GB2312 全字集 × 3 字号,产物已可复用)
./tools/gen_fonts.sh

# 2. 配置 Lark 应用与表格 → 见 docs/lark-setup.md
cp firmware/helper_board/secrets.h.example firmware/helper_board/secrets.h
#    编辑 secrets.h 填 WiFi 与 Lark 凭据

# 3. 编译 / 烧录(USB-C 连接开发板)
./tools/build.sh          # 仅编译
./tools/build.sh flash    # 编译并烧录
./tools/build.sh monitor  # 串口日志(115200)
```

## 项目结构

```
firmware/helper_board/    Arduino 主工程
  helper_board.ino          唤醒分发主流程(醒来→干一件事→重绘→深睡)
  config.h                  引脚 / 餐次时段 / 同步策略
  secrets.h(.example)       WiFi + Lark 凭据(不入库)
  display_bsp.*             ST7305 驱动(改自官方例程,增加 LPM 保持模式)
  ui.*                      LVGL 9 单次渲染布局
  sensors.*                 SHTC3 温湿度 + PCF85063 RTC(Wire)
  battery.*                 电量 ADC(GPIO4,×3 分压)
  power.*                   深睡眠 / 唤醒源 / 按键长短按 / GPIO 保持
  lark_client.*             WiFi + SNTP + Lark token + Bitable 查询
  menu_store.*              菜单数据模型 + NVS 缓存
  src/fonts/                生成的 GB2312 字库(思源黑体,OFL)
  partitions.csv            16MB 自定义分区(8MB app)
libraries/                LVGL 9.3.0(Waveshare 移植配置)+ ArduinoJson(vendored)
tools/gen_fonts.sh        中文字库生成(Noto Sans SC → lv_font_conv)
tools/build.sh            arduino-cli 编译/烧录封装
docs/lark-setup.md        Lark 应用 + 多维表格配置指南
```

## 真机验证清单(烧录后按顺序检查)

1. **上电冷启动**:屏幕白闪后出现完整界面;串口应有 `wake cause: 0`、`sync: wifi=1 time=1 fetch=1`
2. **深睡画面保持**:界面渲染后设备深睡,画面应保持不消失(若消失说明深睡时屏幕供电被切,需改用 light sleep,改 `power.cpp`)
3. **按 KEY**:1~2 秒内切到下一页;**按 BOOT**:联网同步(页脚"更新于"时间变化)
4. **改表格 → 按 BOOT**:屏幕内容更新
5. **餐次窗口**:各时段默认页正确;20:30 后显示"明日"
6. **断网降级**:关路由后按 BOOT,页脚显示"同步失败·缓存 xx-xx xx:xx"
7. **休眠电流**(可选,USB 电流计):深睡时整机应在 1mA 以下量级;若显著偏高,排查 `power.cpp` 的 GPIO 保持配置
8. **电池图标**:插拔 USB-C 后(下一次唤醒)充电符号切换

## 已知取舍与风险

- **深睡时屏幕保持**依据 ST7305 LPM 特性与社区实测,但未在本板实测确认 —— 是首要验证项(见清单第 2 条)
- **充电状态**用电压 >4.15V 启发式判断(原理图如有 VBUS/CHG 检测脚可改 `battery.cpp` 精确化)
- `app_secret` 明文存于设备 flash:家用场景可接受,应用权限已收敛为 bitable 只读
- HTTPS 根证书已内置于 `lark_client.cpp`(Lark 国际版 DigiCert Global Root G3 + 飞书 G2,均 2038 到期)
