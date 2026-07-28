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
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=custom"

# libraries/ 在项目根目录(ARDUINO_DIRECTORIES_USER 指向项目根)
export ARDUINO_DIRECTORIES_USER="$ROOT"

if [ ! -f "$SKETCH/secrets.h" ]; then
  echo "!! 缺少 secrets.h,先从模板复制(编译可过,烧录前请填真实凭据)"
  cp "$SKETCH/secrets.h.example" "$SKETCH/secrets.h"
fi

cmd="${1:-build}"

find_port() {
  if [ -n "${PORT:-}" ]; then echo "$PORT"; return; fi
  ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null | head -1
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
  *)
    echo "用法: build.sh [build|flash|monitor]"
    exit 1
    ;;
esac
