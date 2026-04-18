#!/usr/bin/env python3

import argparse
import math
import statistics
from pathlib import Path

import matplotlib.pyplot as plt


def read_lines(path: Path):
    return path.read_text(encoding="utf-8", errors="ignore").splitlines()


def parse_blocks(path: Path, start_tag: str, end_tag: str):
    lines = read_lines(path)
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].strip() == start_tag:
            i += 1
            rows = []
            while i < len(lines) and lines[i].strip() != end_tag:
                line = lines[i].strip()
                if line:
                    rows.append([part.strip() for part in line.split(",")])
                i += 1
            if rows:
                blocks.append(rows)
        i += 1
    return blocks


def extract_n_from_perf(path: Path, default_n=1000000):
    for line in read_lines(path):
        if line.startswith("FILTER_MAX_CAPACITY"):
            return int(line.split("\t")[1].strip())
    return default_n


def median_series(blocks, column, default=0.0):
    rounds = len(blocks[0])
    result = []
    for r in range(rounds):
        values = []
        for block in blocks:
            try:
                values.append(float(block[r][column]))
            except Exception:
                values.append(default)
        result.append(statistics.median(values))
    return result


def safe_ratio(numerator, denominator):
    return (numerator / denominator) if denominator > 0 else 0.0


def draw_throughput(perf_blocks, n, out_path: Path):
    rounds = len(perf_blocks[0])
    step = max(1, n // rounds)
    load = [(i + 1) / rounds for i in range(rounds)]

    add_ns = median_series(perf_blocks, 0)
    neg_ns = median_series(perf_blocks, 1)
    pos_ns = median_series(perf_blocks, 2)

    def to_ops(elapsed_ns):
        if elapsed_ns <= 0:
            return 0.0
        return step / (elapsed_ns / 1e9)

    add_ops = [to_ops(x) for x in add_ns]
    neg_ops = [to_ops(x) for x in neg_ns]
    pos_ops = [to_ops(x) for x in pos_ns]

    fig, axes = plt.subplots(1, 3, figsize=(14, 4), sharex=True)
    axes[0].plot(load, add_ops, marker="o", linewidth=1.8, markersize=3)
    axes[0].set_title("(a) Insertions")
    axes[1].plot(load[:-1], neg_ops[:-1], marker="o", linewidth=1.8, markersize=3)
    axes[1].set_title("(b) Uniform lookups")
    axes[2].plot(load, pos_ops, marker="o", linewidth=1.8, markersize=3)
    axes[2].set_title("(c) Yes lookups")
    for ax in axes:
        ax.set_xlabel("Load")
        ax.set_ylabel("ops/sec")
        ax.grid(alpha=0.35)
    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def draw_hitrate(hitrate_blocks, out_path: Path):
    rounds = len(hitrate_blocks[0])
    load = [(i + 1) / rounds for i in range(rounds)]
    neg_hit = median_series(hitrate_blocks, 0, default=math.nan)
    pos_hit = median_series(hitrate_blocks, 1, default=math.nan)

    fig, ax = plt.subplots(figsize=(8.5, 4.2))
    ax.plot(load[:-1], neg_hit[:-1], marker="o", linewidth=1.8, markersize=3, label="Negative query hit rate")
    ax.plot(load, pos_hit, marker="s", linewidth=1.8, markersize=3, label="Positive query hit rate")
    ax.set_title("Query Hit Rates vs Load (newfilter256_compact)")
    ax.set_xlabel("Load")
    ax.set_ylabel("Hit rate")
    ax.set_ylim(0.0, 1.05)
    ax.grid(alpha=0.35)
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def draw_items(fill_blocks, out_path: Path):
    rounds = len(fill_blocks[0])
    load = [(i + 1) / rounds for i in range(rounds)]
    logical_items = median_series(fill_blocks, 1)
    l1_slots = median_series(fill_blocks, 2)
    l2_slots = median_series(fill_blocks, 3)
    occupancy = median_series(fill_blocks, 6)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.2), sharex=True)
    axes[0].plot(load, logical_items, marker="o", linewidth=1.8, markersize=3, label="Logical items")
    axes[0].plot(load, l1_slots, marker="s", linewidth=1.5, markersize=3, label="L1 slots")
    axes[0].plot(load, l2_slots, marker="^", linewidth=1.5, markersize=3, label="L2 slots")
    axes[0].set_title("Items and Slots vs Load")
    axes[0].set_ylabel("Count")
    axes[0].grid(alpha=0.35)
    axes[0].legend(loc="best")

    axes[1].plot(load, occupancy, marker="o", linewidth=1.8, markersize=3, label="Slot occupancy")
    axes[1].set_title("Slot Occupancy vs Load")
    axes[1].set_ylabel("Ratio")
    axes[1].set_ylim(0.0, 1.05)
    axes[1].grid(alpha=0.35)
    axes[1].legend(loc="best")

    for ax in axes:
        ax.set_xlabel("Load")
    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def draw_filter_branches(fill_blocks, out_path: Path):
    rounds = len(fill_blocks[0])
    load = [(i + 1) / rounds for i in range(rounds)]

    collect_calls = median_series(fill_blocks, 16, default=1.0)
    hit0 = median_series(fill_blocks, 19)
    hit1 = median_series(fill_blocks, 20)
    hit2 = median_series(fill_blocks, 21)
    hit3 = median_series(fill_blocks, 22)
    hit4 = median_series(fill_blocks, 23)
    l1_hit_ratio = median_series(fill_blocks, 24)
    l2_hit_ratio = median_series(fill_blocks, 25)

    hit0_ratio = [safe_ratio(v, c) for v, c in zip(hit0, collect_calls)]
    hit1_ratio = [safe_ratio(v, c) for v, c in zip(hit1, collect_calls)]
    hit2_ratio = [safe_ratio(v, c) for v, c in zip(hit2, collect_calls)]
    hit3_ratio = [safe_ratio(v, c) for v, c in zip(hit3, collect_calls)]
    hit4_ratio = [safe_ratio(v, c) for v, c in zip(hit4, collect_calls)]

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.2), sharex=True)
    axes[0].plot(load, hit0_ratio, marker="o", linewidth=1.4, markersize=3, label="0-seg")
    axes[0].plot(load, hit1_ratio, marker="o", linewidth=1.4, markersize=3, label="1-seg")
    axes[0].plot(load, hit2_ratio, marker="o", linewidth=1.4, markersize=3, label="2-seg")
    axes[0].plot(load, hit3_ratio, marker="o", linewidth=1.4, markersize=3, label="3-seg")
    axes[0].plot(load, hit4_ratio, marker="o", linewidth=1.4, markersize=3, label="4-seg")
    axes[0].set_title("Filter Max-Hit Branch Ratio")
    axes[0].set_xlabel("Load")
    axes[0].set_ylabel("Ratio per collect")
    axes[0].set_ylim(0.0, 1.05)
    axes[0].grid(alpha=0.35)
    axes[0].legend(loc="best")

    axes[1].plot(load, l1_hit_ratio, marker="s", linewidth=1.6, markersize=3, label="L1 way hit ratio")
    axes[1].plot(load, l2_hit_ratio, marker="^", linewidth=1.6, markersize=3, label="L2 way hit ratio")
    axes[1].set_title("L1/L2 Probe Hit Ratio")
    axes[1].set_xlabel("Load")
    axes[1].set_ylabel("Ratio")
    axes[1].set_ylim(0.0, 1.05)
    axes[1].grid(alpha=0.35)
    axes[1].legend(loc="best")

    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def draw_verify_branches(fill_blocks, out_path: Path):
    rounds = len(fill_blocks[0])
    load = [(i + 1) / rounds for i in range(rounds)]

    checked_b1 = median_series(fill_blocks, 31)
    checked_b2 = median_series(fill_blocks, 32)
    checked_b3 = median_series(fill_blocks, 33)
    checked_b4 = median_series(fill_blocks, 34)
    dup_b1 = median_series(fill_blocks, 35)
    dup_b2 = median_series(fill_blocks, 36)
    dup_b3 = median_series(fill_blocks, 37)
    dup_b4 = median_series(fill_blocks, 38)
    bits_b1 = median_series(fill_blocks, 39)
    bits_b2 = median_series(fill_blocks, 40)
    bits_b3 = median_series(fill_blocks, 41)
    bits_b4 = median_series(fill_blocks, 42)

    dup_rate_b1 = [safe_ratio(a, b) for a, b in zip(dup_b1, checked_b1)]
    dup_rate_b2 = [safe_ratio(a, b) for a, b in zip(dup_b2, checked_b2)]
    dup_rate_b3 = [safe_ratio(a, b) for a, b in zip(dup_b3, checked_b3)]
    dup_rate_b4 = [safe_ratio(a, b) for a, b in zip(dup_b4, checked_b4)]

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.2), sharex=True)
    axes[0].plot(load, dup_rate_b1, marker="o", linewidth=1.4, markersize=3, label="branch-1")
    axes[0].plot(load, dup_rate_b2, marker="o", linewidth=1.4, markersize=3, label="branch-2")
    axes[0].plot(load, dup_rate_b3, marker="o", linewidth=1.4, markersize=3, label="branch-3")
    axes[0].plot(load, dup_rate_b4, marker="o", linewidth=1.4, markersize=3, label="branch-4")
    axes[0].set_title("Verify Duplicate Rate by Branch")
    axes[0].set_xlabel("Load")
    axes[0].set_ylabel("Dup / Checked")
    axes[0].set_ylim(0.0, 1.05)
    axes[0].grid(alpha=0.35)
    axes[0].legend(loc="best")

    axes[1].plot(load, bits_b1, marker="s", linewidth=1.4, markersize=3, label="bits-branch1")
    axes[1].plot(load, bits_b2, marker="s", linewidth=1.4, markersize=3, label="bits-branch2")
    axes[1].plot(load, bits_b3, marker="s", linewidth=1.4, markersize=3, label="bits-branch3")
    axes[1].plot(load, bits_b4, marker="s", linewidth=1.4, markersize=3, label="bits-branch4")
    axes[1].set_title("Compared Bits by Branch")
    axes[1].set_xlabel("Load")
    axes[1].set_ylabel("Cumulative bits")
    axes[1].grid(alpha=0.35)
    axes[1].legend(loc="best")

    fig.tight_layout()
    fig.savefig(out_path, format="pdf", bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--perf", default="../scripts/Inputs/NewFilter256Compact")
    parser.add_argument("--hitrate", default="../scripts/Inputs/NewFilter256Compact-hitrate")
    parser.add_argument("--fill", default="../scripts/Inputs/NewFilter256Compact-fill")
    parser.add_argument("--outdir", default="../scripts")
    args = parser.parse_args()

    perf_path = Path(args.perf)
    hitrate_path = Path(args.hitrate)
    fill_path = Path(args.fill)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    perf_blocks = parse_blocks(perf_path, "BENCH_START", "BENCH_END")
    if not perf_blocks:
        raise RuntimeError(f"No BENCH blocks found in {perf_path}")

    hitrate_blocks = parse_blocks(hitrate_path, "HITRATE_START", "HITRATE_END")
    if not hitrate_blocks:
        raise RuntimeError(f"No HITRATE blocks found in {hitrate_path}")

    fill_blocks = parse_blocks(fill_path, "FILL_START", "FILL_END")
    if not fill_blocks:
        raise RuntimeError(f"No FILL blocks found in {fill_path}")

    n = extract_n_from_perf(perf_path)

    throughput = outdir / "newfilter256compact-throughput.pdf"
    hitrate = outdir / "newfilter256compact-hitrate.pdf"
    items = outdir / "newfilter256compact-items.pdf"
    filter_branches = outdir / "newfilter256compact-filter-branches.pdf"
    verify_branches = outdir / "newfilter256compact-verify-branches.pdf"

    draw_throughput(perf_blocks, n, throughput)
    draw_hitrate(hitrate_blocks, hitrate)
    draw_items(fill_blocks, items)
    draw_filter_branches(fill_blocks, filter_branches)
    draw_verify_branches(fill_blocks, verify_branches)

    print(f"Saved: {throughput}")
    print(f"Saved: {hitrate}")
    print(f"Saved: {items}")
    print(f"Saved: {filter_branches}")
    print(f"Saved: {verify_branches}")


if __name__ == "__main__":
    main()
