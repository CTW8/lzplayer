#!/usr/bin/env python3
"""跨轮次对照（perf-metrics 7e 的执行工具）。

报告生成器只能回答"这一次跑得怎么样"，**回答不了"比上次好了还是坏了"**，
而后者才是回归测试的核心。7e 写死了"N≥3 取中位数"的规则，但此前没有任何
工具执行它——本项目的教训是**规则靠人执行防不住**。

用法:
  ./scripts/compare-reports.py 基线目录 新目录 [新目录2 新目录3 ...]

  基线与新版都可以给多个 run（用逗号分隔），会按 7e 的规则取中位数：
  ./scripts/compare-reports.py "a1,a2,a3" "b1,b2,b3"
"""
import io
import json
import os
import sys

# 显著回退阈值。按列分别定，因为各列的正常波动幅度差一个数量级：
# CPU 单次运行内方差就有 30 个百分点，而 fps 稳定在 ±1 以内。
# 阈值宽于正常波动才不会天天报假警——本项目更怕"狼来了"而不是漏报，
# 因为一旦开始忽略告警，这个工具就等于不存在。
THRESHOLDS = {
    "fps":         {"pct": 5.0,  "worse": "down", "unit": ""},
    "cpu":         {"pct": 25.0, "worse": "up",   "unit": "%"},
    "dropLate":    {"abs": 1.0,  "worse": "up",   "unit": "帧/秒"},
    "dropOvf":     {"abs": 0.5,  "worse": "up",   "unit": "帧/秒"},
    "syncWorstMs": {"abs": 15.0, "worse": "down", "unit": "ms"},
    "vpark":       {"pct": 50.0, "worse": "up",   "unit": "次/秒"},
    "apark":       {"pct": 50.0, "worse": "up",   "unit": "次/秒"},
    "rssMb":       {"pct": 15.0, "worse": "up",   "unit": "MB"},
    "fd":          {"pct": 15.0, "worse": "up",   "unit": "个"},
}

# 必须一致才允许比较的指纹项。**跨素材、跨解码路径的数字不可比** ——
# 允许这种比较会产出一份看起来像回归报告、实际毫无意义的东西
MUST_MATCH = ["asset", "software", "device"]


def load(path):
    p = path if path.endswith(".json") else os.path.join(path, "report.json")
    if not os.path.exists(p):
        return None
    try:
        return json.load(io.open(p))
    except Exception:
        return None


def median(vals):
    v = sorted(vals)
    n = len(v)
    return v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2.0


def collect(dirs):
    """多个 run → 每列的中位数。7e: 跨轮次对照取 N>=3 中位数。
    不足 3 个仍然算，但会在报告里标出来——样本不足是事实，不是拒绝理由，
    但读的人必须知道。"""
    reps = [r for r in (load(d) for d in dirs) if r]
    if not reps:
        return None, [], 0
    out = {}
    for col in THRESHOLDS:
        vals = []
        for r in reps:
            st = (r.get("stats") or {}).get("steady") or {}
            v = (st.get(col) or {}).get("p50")
            if v is None:
                v = (st.get(col) or {}).get("mean")
            if v is not None:
                vals.append(v)
        if vals:
            out[col] = (median(vals), len(vals))
    return out, reps, len(reps)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    base_dirs = sys.argv[1].split(",")
    new_dirs = sys.argv[2].split(",")

    base, base_reps, nb = collect(base_dirs)
    new, new_reps, nn = collect(new_dirs)
    if not base or not new:
        print("FAIL 读不到 report.json —— 先跑 gen-report.py")
        return 1

    # 指纹校验：不一致就拒绝比较，而不是比出一堆没意义的数字
    be, ne = base_reps[0].get("env", {}), new_reps[0].get("env", {})
    mismatch = [k for k in MUST_MATCH if be.get(k) != ne.get(k)]
    if mismatch:
        print("拒绝比较 —— 指纹不一致，跨素材/跨解码路径的数字不可比：")
        for k in mismatch:
            print("  %-10s 基线=%s  新版=%s" % (k, be.get(k), ne.get(k)))
        return 2

    print("=" * 68)
    print("跨轮次对照   素材=%s 软解=%s" % (be.get("asset"), be.get("software")))
    print("基线 %d 个 run / 新版 %d 个 run   （7e 要求 N>=3 取中位数）"
          % (nb, nn))
    if nb < 3 or nn < 3:
        print("!! 样本不足 3 个，中位数抗噪能力有限，结论仅供参考")
    print("=" * 68)
    print("%-13s %10s %10s %10s   %s" % ("列", "基线", "新版", "变化", "判定"))

    regress, improve = [], []
    for col, th in THRESHOLDS.items():
        if col not in base or col not in new:
            continue
        b, nb_n = base[col]
        n, nn_n = new[col]
        delta = n - b
        pct = (delta / b * 100.0) if b else 0.0

        # 判断方向：worse=up 表示数值变大是回退
        worse_dir = th["worse"]
        got_worse = (delta > 0) if worse_dir == "up" else (delta < 0)

        if "pct" in th:
            over = abs(pct) >= th["pct"]
            change = "%+.1f%%" % pct
        else:
            over = abs(delta) >= th["abs"]
            change = "%+.1f" % delta

        if over and got_worse:
            mark = "回退"
            regress.append((col, b, n, change))
        elif over:
            mark = "改善"
            improve.append((col, b, n, change))
        else:
            mark = "持平"
        print("%-13s %10.1f %10.1f %10s   %s" % (col, b, n, change, mark))

    print("-" * 68)
    if regress:
        print("发现 %d 处回退：" % len(regress))
        for col, b, n, c in regress:
            print("  %-12s %.1f → %.1f (%s)" % (col, b, n, c))
    else:
        print("无显著回退")
    if improve:
        print("%d 处改善：%s" % (len(improve), ", ".join(c[0] for c in improve)))

    # 判据层面的对照：PASS 变 FAIL 比任何数字变化都严重
    def verdicts(reps):
        d = {}
        for c in (reps[0].get("criteria") or []):
            d[c["criterion"]] = c["verdict"]
        return d

    bv, nv = verdicts(base_reps), verdicts(new_reps)
    flips = [(k, bv[k], nv[k]) for k in bv if k in nv and bv[k] != nv[k]]
    if flips:
        print("-" * 68)
        print("判据变化（比数字变化更值得看）：")
        for k, o, n_ in flips:
            sev = "!!" if (o == "PASS" and n_ == "FAIL") else "  "
            print("  %s %-20s %s → %s" % (sev, k, o, n_))

    return 1 if regress or any(o == "PASS" and n == "FAIL" for _, o, n in flips) else 0


if __name__ == "__main__":
    sys.exit(main())
