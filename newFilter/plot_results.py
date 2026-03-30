#!/usr/bin/env python3

import argparse
import statistics
from pathlib import Path

import matplotlib.pyplot as plt


def parse_perf_blocks(path: Path):
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == "BENCH_START":
            i += 1
            rows = []
            while i < len(lines) and lines[i].strip() != "BENCH_END":
                line = lines[i].strip()
                if line:
                    parts = [p.strip() for p in line.split(",")]
                    if len(parts) >= 3:
                        rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
                i += 1
            if rows:
                blocks.append(rows)
        i += 1
    return blocks


def parse_hitrate_blocks(path: Path):
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == "HITRATE_START":
            i += 1
            rows = []
            while i < len(lines) and lines[i].strip() != "HITRATE_END":
                line = lines[i].strip()
                if line:
                    parts = [p.strip() for p in line.split(",")]
                    if len(parts) >= 2:
                        rows.append((float(parts[0]), float(parts[1])))
                i += 1
            if rows:
                blocks.append(rows)
        i += 1
    return blocks


def median_series(blocks, column):
    rounds = len(blocks[0])
    out = []
    for r in range(rounds):
        out.append(statistics.median(block[r][column] for block in blocks))
    return out


def extract_n_from_perf(path: Path, default_n: int = 65000):
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("FILTER_MAX_CAPACITY"):
            return int(line.split("\t")[1].strip())
    return default_n


def draw_figure1(perf_blocks, n, out_path: Path):
    bench_precision = len(perf_blocks[0])
    step = n // bench_precision
    load = [(i + 1) / bench_precision for i in range(bench_precision)]

    add_ns = median_series(perf_blocks, 0)
    uni_ns = median_series(perf_blocks, 1)
    yes_ns = median_series(perf_blocks, 2)

    add_ops = [step / (x / 1e9) for x in add_ns]
    uni_ops = [step / (x / 1e9) for x in uni_ns]
    yes_ops = [step / (x / 1e9) for x in yes_ns]

    fig, axes = plt.subplots(1, 3, figsize=(14, 4), sharex=True)
    titles = ["(a) Insertions", "(b) Uniform lookups", "(c) Yes lookups"]
    series = [add_ops, uni_ops, yes_ops]

    for ax, title, y in zip(axes, titles, series):
        ax.plot(load, y, marker="o", linewidth=1.8, markersize=3, label="newFilter")
        ax.set_title(title)
        ax.set_xlabel("Load")
        ax.set_ylabel("ops/sec")
        ax.grid(alpha=0.35)
        ax.legend(loc="best")

    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def draw_figure2(hitrate_blocks, out_path: Path):
    bench_precision = len(hitrate_blocks[0])
    load = [(i + 1) / bench_precision for i in range(bench_precision)]
    neg_hit = median_series(hitrate_blocks, 0)
    pos_hit = median_series(hitrate_blocks, 1)

    fig, ax = plt.subplots(figsize=(8.5, 4.2))
    ax.plot(load, neg_hit, marker="o", linewidth=1.8, markersize=3, label="Negative query hit rate")
    ax.plot(load, pos_hit, marker="s", linewidth=1.8, markersize=3, label="Positive query hit rate")
    ax.set_title("Query Hit Rates vs Load (newFilter)")
    ax.set_xlabel("Load")
    ax.set_ylabel("Hit rate")
    ax.set_ylim(0.0, 1.05)
    ax.grid(alpha=0.35)
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--perf", default="../scripts/Inputs/SimHash-4x16-OR")
    parser.add_argument("--hitrate", default="../scripts/Inputs/SimHash-4x16-OR-hitrate")
    parser.add_argument("--outdir", default="../scripts")
    args = parser.parse_args()

    perf_path = Path(args.perf)
    hitrate_path = Path(args.hitrate)
    out_dir = Path(args.outdir)
    out_dir.mkdir(parents=True, exist_ok=True)

    perf_blocks = parse_perf_blocks(perf_path)
    if not perf_blocks:
        raise RuntimeError(f"No BENCH_START blocks found in {perf_path}")

    hitrate_blocks = parse_hitrate_blocks(hitrate_path)
    if not hitrate_blocks:
        raise RuntimeError(f"No HITRATE_START blocks found in {hitrate_path}")

    n = extract_n_from_perf(perf_path)
    fig1_path = out_dir / "newfilter-throughput.pdf"
    fig2_path = out_dir / "newfilter-hitrate.pdf"
    draw_figure1(perf_blocks, n, fig1_path)
    draw_figure2(hitrate_blocks, fig2_path)
    print(f"Saved: {fig1_path}")
    print(f"Saved: {fig2_path}")


if __name__ == "__main__":
    main()
