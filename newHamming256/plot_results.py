#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def linear_fit_with_r2(x: pd.Series, y: pd.Series) -> tuple[float, float, float]:
    slope, intercept = np.polyfit(x.to_numpy(dtype=float), y.to_numpy(dtype=float), 1)
    y_pred = slope * x + intercept
    ss_res = ((y - y_pred) ** 2).sum()
    ss_tot = ((y - y.mean()) ** 2).sum()
    r2 = 1.0 - (ss_res / ss_tot) if ss_tot != 0 else 1.0
    return float(slope), float(intercept), float(r2)


def draw_plot(
    x: pd.Series,
    y: pd.Series,
    x_label: str,
    y_label: str,
    title: str,
    out_path: Path,
) -> tuple[float, float, float]:
    slope, intercept, r2 = linear_fit_with_r2(x, y)

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(x, y, marker="o", label="Measured")
    ax.plot(x, slope * x + intercept, linestyle="--", label=f"Linear fit (R^2={r2:.6f})")
    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    return slope, intercept, r2


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot linearity results for 256-bit Hamming benchmark.")
    parser.add_argument("--csv", default="../scripts/hamming256_linear.csv")
    parser.add_argument("--outdir", default="../scripts")
    args = parser.parse_args()

    csv_path = Path(args.csv)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    if not csv_path.exists():
        raise FileNotFoundError(f"CSV not found: {csv_path}")

    df = pd.read_csv(csv_path)
    if df.empty:
        raise ValueError(f"CSV has no rows: {csv_path}")

    time_vs_n = outdir / "hamming256-time-vs-n.pdf"
    slope_n, intercept_n, r2_n = draw_plot(
        df["n"],
        df["total_time_sec"],
        x_label="N (database size)",
        y_label="Total time (sec)",
        title="256-bit Hamming: N vs Total Time",
        out_path=time_vs_n,
    )

    time_vs_cmp = outdir / "hamming256-time-vs-comparisons.pdf"
    slope_cmp, intercept_cmp, r2_cmp = draw_plot(
        df["comparisons"],
        df["total_time_sec"],
        x_label="Comparisons (Q * N)",
        y_label="Total time (sec)",
        title="256-bit Hamming: Comparisons vs Total Time",
        out_path=time_vs_cmp,
    )

    summary_path = outdir / "hamming256-linearity-summary.txt"
    summary_path.write_text(
        "\n".join(
            [
                f"time_vs_n_slope={slope_n:.12f}",
                f"time_vs_n_intercept={intercept_n:.12f}",
                f"time_vs_n_r2={r2_n:.12f}",
                f"time_vs_comparisons_slope={slope_cmp:.12e}",
                f"time_vs_comparisons_intercept={intercept_cmp:.12f}",
                f"time_vs_comparisons_r2={r2_cmp:.12f}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    print(f"Saved: {time_vs_n}")
    print(f"Saved: {time_vs_cmp}")
    print(f"Saved: {summary_path}")


if __name__ == "__main__":
    main()
