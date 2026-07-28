#!/usr/bin/env bash
# 编译/烧录固件
#   ./tools/build.sh            仅编译
#   ./tools/build.sh flash      编译并烧录(自动探测串口,或用 PORT=/dev/xxx 指定)
#   ./tools/build.sh monitor    打开串口监视器
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/firmware/helper_board"
BUILD_DIR="$ROOT/build"

# N16R8 模组:16MB QIO Flash + 8MB OPI PSRAM;USB-C 走原生 USB(CDC 串口)
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=custom,DebugLevel=info"

# libraries/ 在项目根目录(ARDUINO_DIRECTORIES_USER 指向项目根)
export ARDUINO_DIRECTORIES_USER="$ROOT"

if [ ! -f "$SKETCH/secrets.h" ]; then
  echo "!! 缺少 secrets.h,先从模板复制(编译可过,烧录前请填真实凭据)"
  cp "$SKETCH/secrets.h.example" "$SKETCH/secrets.h"
fi

cmd="${1:-build}"

find_port() {
  if [ -n "${PORT:-}" ]; then echo "$PORT"; return; fi
  # ls 无匹配时返回非零,加 || true 防止 set -e 静默退出
  { ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null || true; } | head -1
}

case "$cmd" in
  build)
    arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SKETCH"
    ;;
  flash)
    port=$(find_port)
    [ -n "$port" ] || { echo "未找到串口,插好 USB 后用 PORT=/dev/cu.xxx 指定"; exit 1; }
    arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SKETCH"
    arduino-cli upload --fqbn "$FQBN" --input-dir "$BUILD_DIR" -p "$port" "$SKETCH"
    ;;
  monitor)
    port=$(find_port)
    [ -n "$port" ] || { echo "未找到串口"; exit 1; }
    arduino-cli monitor -p "$port" -c baudrate=115200
    ;;
  watch)
    # 等待设备出现(按 KEY 唤醒 / 进下载模式均可)→ 烧录 → 抓启动日志并判读
    # 每个阶段有终端输出 + 语音播报
    speak() { say "$1" 2>/dev/null || true; }
    echo "⏳ 等待设备出现...(进下载模式:长按PWR关机 → 按住BOOT → 单击PWR → 松BOOT)"
    echo "   BOOT 是否生效不用看屏幕(黑屏是正常的),检测到端口我会立刻提示"
    while :; do
      port=$(find_port)
      [ -n "$port" ] && break
      sleep 1
    done
    echo "🔌 检测到设备 $port,开始烧录..."
    speak "检测到设备,开始烧录"
    if arduino-cli upload --fqbn "$FQBN" --input-dir "$BUILD_DIR" -p "$port" "$SKETCH" 2>&1 | tail -2; then
      echo "✅ 烧录成功,设备重启,抓取 35 秒启动日志..."
      speak "烧录成功"
    else
      echo "❌ 烧录失败(设备可能中途睡了,重进下载模式再试)"
      speak "烧录失败"
      exit 1
    fi
    sleep 1
    for i in $(seq 1 10); do
      port=$(find_port); [ -n "$port" ] && break; sleep 1
    done
    LOG=$(mktemp)
    if [ -n "$port" ]; then
      stty -f "$port" 115200 raw -echo 2>/dev/null || true
      cat "$port" > "$LOG" 2>/dev/null &
      CATPID=$!
      sleep 35
      kill $CATPID 2>/dev/null || true
      echo "―――― 启动日志 ――――"
      cat "$LOG"
      echo "―――――――――――――"
      S=$(grep -o "sync: wifi=[01] time=[01] fetch=[01]" "$LOG" | tail -1)
      if [ -n "$S" ]; then
        w=${S#*wifi=}; w=${w:0:1}; t=${S#*time=}; t=${t:0:1}; f=${S#*fetch=}; f=${f:0:1}
        [ "$w" = 1 ] && WV="✅WiFi" || WV="❌WiFi"
        [ "$t" = 1 ] && TV="✅校时" || TV="❌校时"
        [ "$f" = 1 ] && FV="✅菜单" || FV="❌菜单"
        echo "结论: $WV $TV $FV"
        [ "$w$t$f" = "111" ] && speak "全部成功" || speak "有步骤失败,看终端"
      else
        echo "结论: 本次启动未触发同步(看屏幕页脚 Updated 时间即可)"
        speak "烧录完成"
      fi
    else
      echo "设备已快速入睡,看屏幕确认即可(页脚 Updated = 成功)"
      speak "烧录完成,请看屏幕"
    fi
    ;;
  *)
    echo "用法: build.sh [build|flash|monitor|watch]"
    exit 1
    ;;
esac
