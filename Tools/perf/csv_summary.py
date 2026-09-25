"""Summarise an Unreal CSV profiler capture (B-27 benchmarks).

    python csv_summary.py <capture.csv> [--skip N] [--stats "FrameTime|GPU/"] [--combat]

Default: frame time, fps, p99, hitches and the thread / GPU times after skipping the first
N frames (loading). --stats prints the mean and p95 of every column whose name contains one
of the |-separated words. --combat splits a combat_bench.cmd capture into the time before the
teleport and 10 s windows after it, with Valhalla/CombatEventsReceived per second.

The CSV profiler writes its header row at the end of the file ([HasHeaderRowAtEnd]); rows
from before a stat first appeared are shorter, so they are padded.
"""

import argparse
import csv
import statistics as st

csv.field_size_limit(10 ** 9)


def load(path):
    rows = list(csv.reader(open(path, encoding="utf-8", errors="ignore")))
    if rows and rows[-1] and rows[-1][0].startswith("[HasHeaderRowAtEnd]"):
        header, body = rows[-2], rows[1:-2]
    else:
        header, body = rows[0], rows[1:]
    return header, [r + [""] * (len(header) - len(r)) for r in body]


def column(header, data, name):
    if name not in header:
        return [0.0] * len(data)
    j = header.index(name)
    out = []
    for r in data:
        try:
            out.append(float(r[j]))
        except ValueError:
            out.append(0.0)
    return out


def summary(label, idx, cols):
    ft = [cols["FrameTime"][i] for i in idx]
    if not ft:
        return
    s = sorted(ft)
    ev = sum(cols["Valhalla/CombatEventsReceived"][i] for i in idx)
    secs = sum(ft) / 1000.0
    print(f"{label:24s} fps {1000 / st.mean(ft):6.1f}  frame {st.mean(ft):6.2f} ms  p99 {s[int(len(s) * .99)]:6.2f}  "
          f"max {s[-1]:7.1f}  >33ms {sum(1 for x in ft if x > 33.3):3d} | "
          f"GT {st.mean(cols['GameThreadTime'][i] for i in idx):5.2f}  RT {st.mean(cols['RenderThreadTime'][i] for i in idx):5.2f}  "
          f"GPU {st.mean(cols['GPUTime'][i] for i in idx):5.2f} | events/s {ev / max(secs, 1e-3):5.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--skip", type=int, default=600)
    ap.add_argument("--stats", default="")
    ap.add_argument("--combat", action="store_true")
    a = ap.parse_args()
    header, data = load(a.csv)
    names = ["FrameTime", "GameThreadTime", "RenderThreadTime", "GPUTime", "Valhalla/CombatEventsReceived", "View/PosX"]
    cols = {n: column(header, data, n) for n in names}
    print(f"{a.csv}: {len(data)} frames, {len(header)} columns")
    if a.combat:
        t, times = 0.0, []
        for f in cols["FrameTime"]:
            t += f / 1000.0
            times.append(t)
        tp = next((i for i, x in enumerate(cols["View/PosX"]) if x > 70000), None)
        if tp is None:
            print("no teleport into Eldmoor found (View/PosX never passed 70000)")
            return
        t0 = times[tp]
        summary("before (idle)", [i for i in range(len(data)) if t0 - 25 <= times[i] < t0 - 2], cols)
        k = 0
        while t0 + k < times[-1]:
            summary(f"fight {k}-{k + 10} s", [i for i in range(len(data)) if t0 + k <= times[i] < t0 + k + 10], cols)
            k += 10
        return
    idx = list(range(a.skip, len(data)))
    summary(f"frames {a.skip}+", idx, cols)
    if a.stats:
        words = [w.lower() for w in a.stats.split("|")]
        for n in header:
            if any(w in n.lower() for w in words):
                v = sorted(column(header, data, n)[a.skip:])
                if v and abs(st.mean(v)) >= 0.02:
                    print(f"  {n:60s} mean {st.mean(v):9.2f}  p95 {v[int(len(v) * .95)]:9.2f}")


if __name__ == "__main__":
    main()
