#!/usr/bin/env bash
# 网络播放测试素材普查（net-playback-harness 步骤1）。
#
# 扫描源目录、ffprobe 全指纹落 manifest、把挑选集复制到 serving 目录。
#
# **素材是用户私人文件，一律不入库**：serving 目录、manifest、素材副本都由
# .gitignore 排除，仓库只进本脚本。
#
# 用法: ./scripts/scan-assets.sh [源目录] [serving目录]
#       默认 ~/Downloads → assets/serving
set -uo pipefail

SRC="${1:-$HOME/Downloads}"
SERVING="${2:-assets/serving}"
MANIFEST="assets/manifest.json"
mkdir -p "$SERVING" "$(dirname "$MANIFEST")"

# VEBufferedDataSource::Config 的默认缓存大小。manifest 里据此算"缓存排空
# 时间"——1080p 约 5Mbps 时 32MB ≈ 50 秒存量，**不限速时"断流 5 秒"什么都
# 不会发生**，用例会 PASS 却什么都没验到。场景参数校验直接读这个字段。
CACHE_BYTES=$((32 * 1024 * 1024))

echo "扫描 $SRC ..."

python3 - "$SRC" "$SERVING" "$MANIFEST" "$CACHE_BYTES" <<'PY'
import json, os, subprocess, sys, shutil

src, serving, manifest_path, cache_bytes = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])

# 挑选集：每条都是合成矩阵没有的真实轴。key 是 serving 里的稳定文件名——
# 原文件名带空格和中文，直接做 URL 会踩转义，且 harness 命令行难引用
PICKS = {
    "lv_0_20250904221155.mp4":        ("portrait-vfr-a",   "竖屏1080x1920 + VFR"),
    "lv_0_20250904221453.mp4":        ("portrait-vfr-b",   "竖屏1080x1920 + VFR"),
    "VID_20250904_081827.mp4":        ("real-hevc-4k",     "真实HEVC 4K + VFR"),
    "VID_20250904_082411.mp4":        ("real-hevc-1080p",  "真实HEVC 1080p"),
    "test.mp4":                       ("long-2h-dualaudio","2.2h + 双音轨"),
    "test2.mp4":                      ("long-53min",       "53min 长稳主力"),
    "截图 2026-08-16 20.57.24.mp4":   ("noaudio-tiny",     "无音轨/1878x1180/1.47s"),
    "截图 2026-08-16 20.57.36.mp4":   ("noaudio-odd",      "无音轨/2596x1436/60fps"),
}

def probe(path):
    try:
        out = subprocess.run(
            ["ffprobe", "-v", "error", "-show_streams", "-show_format",
             "-of", "json", path],
            capture_output=True, text=True, timeout=120).stdout
        return json.loads(out) if out.strip() else None
    except Exception:
        return None

def ratio(s):
    """'30/1' -> 30.0；'0/0' 或缺失 -> None"""
    if not s or "/" not in s:
        return None
    a, b = s.split("/", 1)
    try:
        a, b = float(a), float(b)
        return a / b if b else None
    except ValueError:
        return None

items, images = [], 0
for name in sorted(os.listdir(src)):
    p = os.path.join(src, name)
    if not os.path.isfile(p):
        continue
    ext = os.path.splitext(name)[1].lower()
    if ext in (".png", ".jpg", ".jpeg"):
        images += 1
        continue
    if ext not in (".mp4", ".mkv", ".ts", ".mov", ".m4a"):
        continue

    d = probe(p)
    if not d:
        items.append({"name": name, "probe_ok": False,
                      "note": "ffprobe 失败——本身就是有效的错误路径素材"})
        continue

    streams = d.get("streams") or []
    v = next((s for s in streams if s.get("codec_type") == "video"), None)
    auds = [s for s in streams if s.get("codec_type") == "audio"]
    fmt = d.get("format") or {}
    bit_rate = int(fmt.get("bit_rate") or 0)

    r_fr = ratio(v.get("r_frame_rate")) if v else None
    avg_fr = ratio(v.get("avg_frame_rate")) if v else None
    # VFR 判定：r_frame_rate 是容器声明的"最大帧率"，avg 是实际平均。
    # 两者差得远即变帧率。手机录制常见 r=1000000/1 这种哨兵值。
    # **必须标出来**：否则"fps ≈ r_frame_rate"这类判据会在它们身上稳定误报
    # 实测(2026-08-21)四个真 VFR: portrait-vfr-a/b(r=1000000/1 哨兵, 实际 30.15)
    # 与 noaudio-tiny/odd(声明 60fps、实际 29.3 —— 屏幕录制按需出帧)。
    # 两个 real-hevc-* **不是** VFR: 90000/3001 只是时基写法, 算出来 29.99 恒定,
    # 看分母 3001 就当 VFR 是错的(立项时我就这么误判过)。
    is_vfr = bool(r_fr and avg_fr and (r_fr > 1000 or abs(r_fr - avg_fr) / max(avg_fr, 1) > 0.02))

    it = {
        "name": name,
        "probe_ok": True,
        "size_bytes": os.path.getsize(p),
        "duration_sec": float(fmt.get("duration") or 0),
        "bit_rate": bit_rate,
        "video": None if not v else {
            "codec": v.get("codec_name"), "width": v.get("width"),
            "height": v.get("height"),
            "r_frame_rate": v.get("r_frame_rate"),
            "avg_frame_rate": v.get("avg_frame_rate"),
            "avg_fps": round(avg_fr, 3) if avg_fr else None,
        },
        "audio_track_count": len(auds),
        "audio_codecs": [a.get("codec_name") for a in auds],
        "is_vfr": is_vfr,
        # 缓存排空时间：cacheBytes / 码率。stall 类场景若注入时长小于它,
        # 播放器靠缓存就能撑过去 —— 用例会 PASS 却什么都没验到,
        # 而"没发生"与"没在测"长得一模一样。步骤4 的参数校验读这个字段
        "cache_drain_sec": round(cache_bytes * 8.0 / bit_rate, 1) if bit_rate else None,
    }
    if name in PICKS:
        alias, why = PICKS[name]
        it["picked_as"] = alias
        it["picked_why"] = why
        dst = os.path.join(serving, alias + os.path.splitext(name)[1])
        if not os.path.exists(dst):
            shutil.copy2(p, dst)
        it["serving_path"] = dst
    items.append(it)

manifest = {
    "source_dir": src, "serving_dir": serving,
    "cache_bytes_assumed": cache_bytes,
    "image_count": images,
    "video_count": len(items),
    "picked_count": sum(1 for i in items if i.get("picked_as")),
    "items": items,
}
open(manifest_path, "w").write(json.dumps(manifest, ensure_ascii=False, indent=2))

print("视频 %d 个, 图片 %d 张, 挑选 %d 个" % (len(items), images, manifest["picked_count"]))
print("%-22s %-9s %-16s %-6s %-5s %s" % ("别名", "编码", "分辨率", "时长s", "VFR", "音轨"))
for i in items:
    if not i.get("picked_as"):
        continue
    v = i.get("video") or {}
    print("%-22s %-9s %-16s %-6.1f %-5s %d" % (
        i["picked_as"], v.get("codec") or "-",
        "%sx%s" % (v.get("width"), v.get("height")),
        i["duration_sec"], "是" if i["is_vfr"] else "否",
        i["audio_track_count"]))
PY

echo "manifest → $MANIFEST"
