#!/usr/bin/env python3
"""生成呼叫铃声 PCM 头文件: 叮咚 + "Please Check The Message!" 语音。

产物 firmware/helper_board/src/audio/ringtone_pcm.h 是 16kHz / 单声道 / 16bit
有符号 PCM,直接编进固件 .rodata(flash 映射,播放时不占 RAM)。

语音走 macOS 内置 say + afconvert,不依赖任何外部素材。想换词/换音色改下面的
VOICE_* 常量重跑即可;想换成中文把 VOICE_NAME 改成中文音色(如 Tingting)。
"""

import array
import math
import os
import subprocess
import sys
import tempfile
import wave

SAMPLE_RATE = 16000

# ---- 语音 ----
VOICE_TEXT = "Please check the message!"
VOICE_NAME = "Samantha"
VOICE_RATE = 165  # 词/分钟,慢一点更清楚

# ---- 叮咚 ----
# 下行两音,经典门铃音程(大三度)。E5 → C5。
CHIME_NOTES = [(659.25, 0.42), (523.25, 0.62)]
CHIME_DECAY_TAU = 0.26  # 指数衰减时间常数,越小越"脆"
CHIME_ATTACK = 0.006    # 起音斜坡,避免爆音咔哒

# ---- 编排 ----
GAP_AFTER_CHIME = 0.22  # 叮咚与语音之间
GAP_AFTER_VOICE = 0.85  # 循环间隔:烧在样本尾部,循环播放就是重放整段

PEAK = 0.72  # 归一化目标峰值,留余量给功放,避免削顶失真


def synth_chime():
    """合成叮咚。基频 + 二次谐波,指数衰减,音符间重叠成自然余韵。"""
    total = sum(d for _, d in CHIME_NOTES)
    buf = [0.0] * int(total * SAMPLE_RATE)
    pos = 0
    for freq, dur in CHIME_NOTES:
        n = int(dur * SAMPLE_RATE)
        for i in range(n):
            t = i / SAMPLE_RATE
            env = math.exp(-t / CHIME_DECAY_TAU)
            if t < CHIME_ATTACK:
                env *= t / CHIME_ATTACK
            s = math.sin(2 * math.pi * freq * t)
            s += 0.25 * math.sin(2 * math.pi * freq * 2 * t)  # 二次谐波,金属质感
            if pos + i < len(buf):
                buf[pos + i] += env * s
        pos += n
    return buf


def synth_voice():
    """用 macOS say 合成语音,再转成 16kHz 单声道 16bit。"""
    with tempfile.TemporaryDirectory() as tmp:
        aiff = os.path.join(tmp, "v.aiff")
        wav = os.path.join(tmp, "v.wav")
        subprocess.run(
            ["say", "-v", VOICE_NAME, "-r", str(VOICE_RATE), "-o", aiff, VOICE_TEXT],
            check=True,
        )
        subprocess.run(
            ["afconvert", "-f", "WAVE", "-d", f"LEI16@{SAMPLE_RATE}", "-c", "1", aiff, wav],
            check=True,
        )
        with wave.open(wav, "rb") as w:
            assert w.getframerate() == SAMPLE_RATE, w.getframerate()
            assert w.getnchannels() == 1, w.getnchannels()
            assert w.getsampwidth() == 2, w.getsampwidth()
            raw = w.readframes(w.getnframes())
    samples = array.array("h")
    samples.frombytes(raw)
    return [s / 32768.0 for s in samples]


def normalize(buf, peak):
    hi = max((abs(s) for s in buf), default=0.0)
    if hi == 0:
        return buf
    k = peak / hi
    return [s * k for s in buf]


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "firmware/helper_board/src/audio/ringtone_pcm.h")

    chime = normalize(synth_chime(), PEAK)
    voice = normalize(synth_voice(), PEAK)

    clip = chime
    clip += [0.0] * int(GAP_AFTER_CHIME * SAMPLE_RATE)
    clip += voice
    clip += [0.0] * int(GAP_AFTER_VOICE * SAMPLE_RATE)

    pcm = array.array("h", (max(-32768, min(32767, int(round(s * 32767)))) for s in clip))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("// 由 tools/gen_ringtone.py 生成,请勿手改。\n")
        f.write(f'// 16kHz / 单声道 / 16bit PCM — 叮咚 + "{VOICE_TEXT}"\n')
        f.write(f"// 时长 {len(pcm) / SAMPLE_RATE:.2f}s,占 flash {len(pcm) * 2 / 1024:.0f}KB\n")
        f.write("#pragma once\n\n#include <stdint.h>\n\n")
        f.write(f"#define RINGTONE_SAMPLE_RATE {SAMPLE_RATE}\n")
        f.write(f"#define RINGTONE_SAMPLE_COUNT {len(pcm)}\n\n")
        f.write("const int16_t kRingtonePcm[RINGTONE_SAMPLE_COUNT] = {\n")
        for i in range(0, len(pcm), 16):
            f.write("  " + ",".join(str(v) for v in pcm[i : i + 16]) + ",\n")
        f.write("};\n")

    print(f"✅ {out}")
    print(f"   {len(pcm)} 样本 / {len(pcm) / SAMPLE_RATE:.2f}s / {len(pcm) * 2 / 1024:.0f}KB flash")

    # 试听文件:烧录前先在电脑上听一遍,不满意改上面的常数重跑
    preview = os.path.join(root, "build", "ringtone_preview.wav")
    os.makedirs(os.path.dirname(preview), exist_ok=True)
    with wave.open(preview, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.tobytes())
    print(f"🔊 试听: afplay {os.path.relpath(preview, os.getcwd())}")


if __name__ == "__main__":
    if sys.platform != "darwin":
        sys.exit("需要 macOS(依赖内置 say / afconvert)")
    main()
