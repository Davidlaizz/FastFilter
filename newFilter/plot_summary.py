#!/usr/bin/env python3

import argparse
import math
import statistics
from pathlib import Path

import matplotlib.pyplot as plt


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


def load_hitrate(path: Path):
    blocks = parse_hitrate_blocks(path)
    if not blocks:
        return None
    neg = median_series(blocks, 0)
    pos = median_series(blocks, 1)
    return neg, pos


def draw_dataset(dataset: str, base_dir: Path, out_path: Path):
    methods = ["phash", "dhash", "ahash"]
    colors = {"phash": "#1f77b4", "dhash": "#ff7f0e", "ahash": "#2ca02c"}

    fig, ax = plt.subplots(figsize=(8.8, 4.6))
    any_plotted = False

    for method in methods:
        hitrate_path = base_dir / f"results_{dataset}_{method}" / "SimHash-4x16-OR-hitrate"
        if not hitrate_path.exists():
            continue
        series = load_hitrate(hitrate_path)
        if series is None:
            continue
        neg, pos = series
        rounds = len(pos)
        load = [(i + 1) / rounds for i in range(rounds)]

        # negative series may have NaN in last round (no negative queries)
        neg_trim = [v for v in neg if not math.isnan(v)]
        load_neg = load[: len(neg_trim)]

        ax.plot(load, pos, marker="o", linewidth=1.6, markersize=3,
                color=colors[method], label=f"{method} positive")
        ax.plot(load_neg, neg_trim, marker="s", linewidth=1.2, markersize=3,
                color=colors[method], linestyle="--", label=f"{method} negative")
        any_plotted = True

    if not any_plotted:
        print(f"No data found for dataset: {dataset}")
        return

    ax.set_title(f"Query Hit Rates vs Load ({dataset})")
    ax.set_xlabel("Load")
    ax.set_ylabel("Hit rate")
    ax.set_ylim(0.0, 1.05)
    ax.grid(alpha=0.35)
    ax.legend(loc="best", ncol=2, fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--outdir", required=True, help="Output directory for summary PDFs")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    base_dir = outdir

    draw_dataset("columbia", base_dir, outdir / "summary_columbia.pdf")
    draw_dataset("lg", base_dir, outdir / "summary_lg.pdf")


if __name__ == "__main__":
    main()
