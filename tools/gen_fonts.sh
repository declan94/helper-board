#!/usr/bin/env bash
# 生成 LVGL 中文字库:Noto Sans SC(思源黑体,OFL 协议)→ GB2312 一二级全字集 + ASCII
# 依赖:node(npx lv_font_conv)、python3(fonttools 处理可变字体)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT/tools/fonts-src"
OUT_DIR="$ROOT/firmware/helper_board/src/fonts"
mkdir -p "$SRC_DIR" "$OUT_DIR"

VF="$SRC_DIR/NotoSansSC-VF.ttf"
STATIC="$SRC_DIR/NotoSansSC-Regular-static.ttf"
CHARSET="$SRC_DIR/gb2312.txt"

if [ ! -f "$VF" ]; then
  echo "== 下载 Noto Sans SC 可变字体 =="
  curl -fL "https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf" -o "$VF"
fi

if [ ! -f "$STATIC" ]; then
  echo "== 实例化为静态 Regular(wght=400) =="
  VENV="$SRC_DIR/venv"
  [ -d "$VENV" ] || python3 -m venv "$VENV"
  "$VENV/bin/pip" -q install "fonttools>=4.0"
  "$VENV/bin/fonttools" varLib.instancer "$VF" wght=400 -o "$STATIC" >/dev/null
fi

echo "== 生成 GB2312 字符集 =="
python3 - "$CHARSET" <<'EOF'
import sys
chars = []
# GB2312 全部分区(1-9 区符号 + 16-87 区一二级汉字)
for hi in range(0xA1, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.append(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError:
            pass
extra = "°±×÷"  # 兜底符号(部分已含于 GB2312)
seen = set()
out = []
for c in ''.join(chars) + extra:
    if c not in seen:
        seen.add(c)
        out.append(c)
open(sys.argv[1], 'w', encoding='utf-8').write(''.join(out))
print(f"charset: {len(out)} chars")
EOF

gen() {
  local size=$1 name=$2
  echo "== 生成 $name (${size}px) =="
  npx --yes lv_font_conv \
    --font "$STATIC" \
    --size "$size" \
    --bpp 1 \
    --format lvgl \
    --lv-include lvgl.h \
    --range 0x20-0x7E \
    --symbols "$(cat "$CHARSET")" \
    --no-compress \
    -o "$OUT_DIR/$name.c"
  # lv_font_conv 生成的字体名与文件名一致
  ls -lh "$OUT_DIR/$name.c" | awk '{print "  ->", $9, $5}'
}

gen 16 font_cjk_16
gen 22 font_cjk_22
gen 30 font_cjk_30
echo "完成。"
