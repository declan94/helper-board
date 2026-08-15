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

- **18650 电池供电、深睡眠架构**:绝大部分时间深睡(屏幕靠 ST7305 低功耗模式自持画面),每 10 分钟醒来查一次呼叫,每 3 次(即 30 分钟)顺带做一次完整同步(SNTP 校时 + 拉取今明两天菜单,缓存进 NVS,断网时显示缓存)
- **餐次自动切换**:05:00 早餐 → 09:00 午餐 → 13:00 晚餐 → 19:30 明日菜单预览
- **KEY 键**:切页(早/中/晚/明日循环);**BOOT 键**:立即联网同步,期间右下角显示 Updating...(改完表格想马上生效时用)。响铃时两个键都能停铃
- ⚠️ 按键与 GPIO 的对应和代码里的常量名相反:**KEY 接 GPIO0、BOOT 接 GPIO18**(2026-08-15 实测),`PIN_KEY 18` 这个名字实际指向 BOOT 键。唤醒映射据此是自洽的,别照名字去"修正"
- 时间由板载 PCF85063 RTC 维持,联网时 SNTP 校准;**不显示时钟**(深睡下无法走字),只显示日期

## 呼叫功能

人在外面时,手机上一键让板子响铃并显示留言,叫帮工去看手机。

```
                手机快捷方式 POST
  你 ─────────────────────────────► ntfy.sh/<call topic>
                                          │
                            板子每 10 分钟轮询一次
                                          ▼
                          叮咚 + "Please check the message!"
                          循环最多 8 遍,留言全屏显示
                                          │
                       按 KEY 停铃 ────────┴──► ntfy.sh/<ack topic> ──► 你手机收到回执
```

- **触发**:`curl -d "回个电话" https://ntfy.sh/<你的 call topic>`。手机上做成桌面快捷方式即可一键呼叫。留言支持中文,直接全屏显示
- **响铃**:板载 ES8311 + 功放 + 喇叭,铃声编在固件里(`tools/gen_ringtone.py` 生成)。循环播放直到按 KEY 或到 `CALL_RING_MAX_LOOPS` 次上限
- **留言占屏**:铃响完没人按键,留言会一直留在屏幕上直到有人按 KEY 确认 —— 人当时不在跟前也不会把消息错过
- **回执**:停铃后板子回一条到 ack topic,你手机上能看到"人已收到"以及具体时间
- **通道健康**:超过 2 小时没成功轮询过,页脚显示 `CALL OFFLINE!`。这类静默失效在外面是感知不到的,只能靠屋里的人看见
- **时效与功耗**:10 分钟轮询约多耗 0.24mA(相对原来 ~0.58mA 的均值,续航损失约 28%)。想更及时就调小 `config.h` 里的 `CALL_POLL_INTERVAL_SEC`,5 分钟大约再多 0.24mA
- 轮询用缓存的 BSSID + 信道定向连接(存 RTC 内存),跳过全信道扫描,单次唤醒从 3.5s 压到约 1.3s;快连失败会当场作废缓存并回落全扫描

## 快速开始

```bash
# 0. 依赖:arduino-cli(brew install arduino-cli)、node、python3
arduino-cli core install esp32:esp32 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# 1. 生成中文字库(首次;GB2312 全字集 × 5 字号,产物已可复用)
./tools/gen_fonts.sh

# 2. 生成呼叫铃声(首次;macOS 内置 say 合成,产物已可复用)
python3 tools/gen_ringtone.py
afplay build/ringtone_preview.wav   # 烧录前先试听,想换词/换音色改脚本顶部常量

# 3. 配置 Lark 应用与表格 → 见 docs/lark-setup.md
cp firmware/helper_board/secrets.h.example firmware/helper_board/secrets.h
#    编辑 secrets.h 填 WiFi、Lark 凭据、以及呼叫用的 ntfy topic
#    topic 名就是密码,用随机长串生成:
#    python3 -c "import secrets; print('call-'+secrets.token_urlsafe(24))"

# 4. 编译 / 烧录(USB-C 连接开发板)
./tools/build.sh          # 仅编译
./tools/build.sh flash    # 编译并烧录
./tools/build.sh monitor  # 串口日志(115200)
```

## 项目结构

```
firmware/helper_board/    Arduino 主工程
  helper_board.ino          唤醒分发主流程(醒来→干一件事→重绘→深睡)
  config.h                  引脚 / 餐次时段 / 轮询与同步策略 / 音频参数
  secrets.h(.example)       WiFi + Lark 凭据 + ntfy topic(不入库)
  display_bsp.*             ST7305 驱动(改自官方例程,增加 LPM 保持模式)
  ui.*                      LVGL 9 单次渲染布局(菜单页 + 呼叫全屏页)
  sensors.*                 SHTC3 温湿度 + PCF85063 RTC(Wire)
  battery.*                 电量 ADC(GPIO4,×3 分压)
  power.*                   深睡眠 / 唤醒源 / 按键读取 / GPIO 保持
  net.*                     WiFi 连接 + BSSID/信道快连缓存(两个客户端共用)
  lark_client.*             SNTP + Lark token + Bitable 查询
  call_client.*             ntfy 呼叫轮询 + 停铃回执
  audio.*                   ES8311 codec(仅 DAC)+ I2S 铃声播放
  menu_store.*              菜单数据模型 + NVS 缓存
  src/fonts/                生成的 GB2312 字库(思源黑体,OFL)
  src/audio/                生成的铃声 PCM
  partitions.csv            16MB 自定义分区(8MB app)
libraries/                LVGL 9.3.0(Waveshare 移植配置)+ ArduinoJson(vendored)
tools/gen_fonts.sh        中文字库生成(Noto Sans SC → lv_font_conv)
tools/gen_ringtone.py     铃声生成(叮咚合成 + macOS say 语音 → PCM 头文件)
tools/build.sh            arduino-cli 编译/烧录封装
docs/lark-setup.md        Lark 应用 + 多维表格配置指南
```

## 真机验证清单(烧录后按顺序检查)

1. **上电冷启动**:屏幕白闪后出现完整界面;串口应有 `wake cause: 0`、`sync: wifi=1 time=1 fetch=1`
2. **深睡画面保持**:界面渲染后设备深睡,画面应保持不消失(若消失说明深睡时屏幕供电被切,需改用 light sleep,改 `power.cpp`)
3. **按 KEY**:1~2 秒内切到下一页;**按 BOOT**:右下角先出现 Updating...,随后 Updated 时间刷新
4. **改表格 → 按 BOOT**:屏幕内容更新
5. **餐次窗口**:各时段默认页正确;19:30 后显示"明日"
6. **断网降级**:关路由后按 BOOT,页脚显示"同步失败·缓存 xx-xx xx:xx"
7. **休眠电流**(可选,USB 电流计):深睡时整机应在 1mA 以下量级;若显著偏高,排查 `power.cpp` 的 GPIO 保持配置。**加了音频之后这一项要重测**:ES8311 / 功放若没被 suspend 干净会常耗几 mA,整个续航模型就废了
8. **音频自检**:`config.h` 里 `AUDIO_SELFTEST` 改 1 烧录 → 冷启动应听到叮咚 + 语音两遍,按 KEY 能中途停;验完改回 0
9. **呼叫全链路**:`curl -d "测试" https://ntfy.sh/<你的 call topic>` → 最多 10 分钟内板子响铃并显示"测试" → 按 KEY 停铃 → 手机 ntfy 上收到回执
10. **留言占屏**:发一条呼叫后不按键,等铃响完;之后每次定时唤醒屏幕都应保持留言,直到按 KEY 才回到菜单
11. **快连生效**:串口连续两次唤醒的日志里不应出现"快连失败,回落全信道扫描";偶尔出现一次是正常的(AP 换信道),持续出现说明缓存有问题
8. **电池图标**:插拔 USB-C 后(下一次唤醒)充电符号切换

## 已知取舍与风险

- **深睡时屏幕保持**依据 ST7305 LPM 特性与社区实测,但未在本板实测确认 —— 是首要验证项(见清单第 2 条)
- **充电状态**用电压 >4.15V 启发式判断(原理图如有 VBUS/CHG 检测脚可改 `battery.cpp` 精确化)
- `app_secret` 明文存于设备 flash:家用场景可接受,应用权限已收敛为 bitable 只读
- HTTPS 根证书已内置于 `lark_client.cpp`(Lark 国际版 DigiCert Global Root G3 + 飞书 G2,均 2038 到期)
