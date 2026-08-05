#!/usr/bin/env python3
"""
plot_schemes.py

Compare a statistic across selection schemes for a given fitness landscape.

Expected directory layout:
  <landscape_dir>/
    cohort/      seed_001/  <datafile>
                 seed_002/  <datafile>  ...
    downsample/  seed_001/  ...
    epsilon/     seed_001/  ...
    informed/    seed_001/  ...
    lexicase/    seed_001/  ...

Each scheme's line is the pointwise average across all seed runs found.
Missing scheme directories or seed files produce a warning and are skipped.

Example:
  python plot_schemes.py data/multipath/ Lexicase.csv --x 0 --y 1
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

SCHEMES = ["tournament", "lexicase", "downsample", "informed", "cohort", "epsilon"]
CI_Z_SCORE = 1.96


def read_headers(csv_file: Path, delimiter: str) -> list[str]:
    with csv_file.open(newline="") as fp:
        reader = csv.reader(fp, delimiter=delimiter)
        headers = next(reader, None)
    if headers is None:
        raise ValueError(f"{csv_file} is empty.")
    return [h.strip() for h in headers]


def parse_column(selector: str, headers: list[str]) -> int:
    if selector in headers:
        return headers.index(selector)
    try:
        index = int(selector)
    except ValueError:
        available = ", ".join(repr(h) for h in headers)
        raise ValueError(
            f"Column {selector!r} not found. Available columns: {available}"
        )
    if index < 0 or index >= len(headers):
        raise ValueError(
            f"Column index {index} out of range "
            f"(0 through {len(headers) - 1})."
        )
    return index


def read_selected_data(
    csv_file: Path,
    x_col: int,
    y_cols: list[int],
    delimiter: str,
) -> tuple[list[float], dict[int, list[float]]]:
    x_values: list[float] = []
    y_values: dict[int, list[float]] = {col: [] for col in y_cols}
    selected_cols = [x_col, *y_cols]
    max_col = max(selected_cols)

    with csv_file.open(newline="") as fp:
        reader = csv.reader(fp, delimiter=delimiter)
        next(reader, None)
        for row_num, row in enumerate(reader, start=2):
            if len(row) <= max_col:
                print(
                    f"Skipping {csv_file}:{row_num}: expected at least "
                    f"{max_col + 1} columns, found {len(row)}.",
                    file=sys.stderr,
                )
                continue
            selected_raw = [row[col].strip() for col in selected_cols]
            if any(v == "" for v in selected_raw):
                continue
            try:
                nums = [float(v) for v in selected_raw]
            except ValueError:
                continue
            x_values.append(nums[0])
            for y_col, y_val in zip(y_cols, nums[1:]):
                y_values[y_col].append(y_val)

    return x_values, y_values


def average_series(
    series: list[tuple[np.ndarray, np.ndarray]],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray] | None:
    """Summarize (x, y) series, persisting as long as any run continues.
    Series that have ended contribute NaN beyond their final x-value and are
    excluded from the mean and confidence interval at those points."""
    if not series:
        return None
    x_grid = max(series, key=lambda pair: pair[0][-1])[0]
    y_matrix = np.full((len(series), len(x_grid)), np.nan)
    for i, (x_arr, y_arr) in enumerate(series):
        valid = (x_grid >= x_arr[0]) & (x_grid <= x_arr[-1])
        y_matrix[i, valid] = np.interp(x_grid[valid], x_arr, y_arr)

    observed = ~np.isnan(y_matrix)
    sample_counts = np.sum(observed, axis=0)
    totals = np.sum(np.where(observed, y_matrix, 0.0), axis=0)
    y_avg = np.divide(
        totals,
        sample_counts,
        out=np.full(len(x_grid), np.nan),
        where=sample_counts > 0,
    )

    deviations = np.zeros_like(y_matrix)
    np.subtract(y_matrix, y_avg, out=deviations, where=observed)
    sum_squared_deviations = np.sum(deviations**2, axis=0)
    sample_variance = np.divide(
        sum_squared_deviations,
        sample_counts - 1,
        out=np.full(len(x_grid), np.nan),
        where=sample_counts > 1,
    )
    variance_of_mean = np.divide(
        sample_variance,
        sample_counts,
        out=np.full(len(x_grid), np.nan),
        where=sample_counts > 1,
    )
    margin = CI_Z_SCORE * np.sqrt(variance_of_mean)

    return x_grid, y_avg, y_avg - margin, y_avg + margin


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Plot averaged per-scheme lines from seed runs inside a landscape directory."
        )
    )
    parser.add_argument(
        "landscape_dir",
        type=Path,
        help="Root landscape directory containing one subdirectory per scheme.",
    )
    parser.add_argument(
        "datafile",
        help="Name of the data file to read within each seed directory.",
    )
    parser.add_argument(
        "--x",
        required=True,
        help="X-axis column, by zero-based index or exact header name.",
    )
    parser.add_argument(
        "--y",
        required=True,
        nargs="+",
        help="Y-axis column(s), by zero-based index or exact header name.",
    )
    parser.add_argument(
        "-o", "--output",
        type=Path,
        help="Output image file (e.g. plot.png). If omitted, display interactively.",
    )
    parser.add_argument("--title", help="Optional plot title.")
    parser.add_argument("--delimiter", default=",", help="CSV delimiter. Default: comma.")
    parser.add_argument("--logx", action="store_true", help="Logarithmic x-axis.")
    parser.add_argument("--logy", action="store_true", help="Logarithmic y-axis.")
    parser.add_argument(
        "--confidence-interval",
        "--ci",
        action="store_true",
        help="Shade pointwise 95%% confidence intervals around scheme averages.",
    )
    parser.add_argument(
        "--no-legend",
        action="store_true",
        help="Do not include a legend in the plot.",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Display interactively even when --output is used.",
    )

    args = parser.parse_args()

    if not args.landscape_dir.is_dir():
        print(
            f"Error: landscape directory not found: {args.landscape_dir}",
            file=sys.stderr,
        )
        return 1

    x_axis_label: str | None = None
    y_axis_label: str | None = None
    plotted_lines = 0

    try:
        for scheme in SCHEMES:
            scheme_dir = args.landscape_dir / scheme
            if not scheme_dir.is_dir():
                print(
                    f"Warning: scheme directory not found, skipping: {scheme_dir}",
                    file=sys.stderr,
                )
                continue

            seed_dirs = sorted(
                d for d in scheme_dir.iterdir()
                if d.is_dir() and d.name.startswith("seed_")
            )
            if not seed_dirs:
                print(
                    f"Warning: no seed_??? directories found in {scheme_dir}, skipping.",
                    file=sys.stderr,
                )
                continue

            # Collect one series per seed, per y column.
            # seed_series[y_col] = list of (x_arr, y_arr)
            seed_series: dict[int, list[tuple[np.ndarray, np.ndarray]]] = {}
            x_col: int | None = None
            y_cols: list[int] | None = None

            for seed_dir in seed_dirs:
                data_file = seed_dir / args.datafile
                if not data_file.exists():
                    print(
                        f"Warning: {data_file} not found, skipping.",
                        file=sys.stderr,
                    )
                    continue

                headers = read_headers(data_file, args.delimiter)

                if x_col is None:
                    x_col = parse_column(args.x, headers)
                    y_cols = [parse_column(y_sel, headers) for y_sel in args.y]
                    for col in y_cols:
                        seed_series[col] = []
                    if x_axis_label is None:
                        x_axis_label = headers[x_col]
                    if y_axis_label is None and len(y_cols) == 1:
                        y_axis_label = headers[y_cols[0]]

                x_values, y_values = read_selected_data(
                    data_file, x_col, y_cols, args.delimiter
                )
                if not x_values:
                    continue

                x_arr = np.array(x_values)
                for col in y_cols:
                    seed_series[col].append((x_arr, np.array(y_values[col])))

            if x_col is None or y_cols is None:
                continue  # No readable files found for this scheme.

            for y_col in y_cols:
                result = average_series(seed_series[y_col])
                if result is None:
                    print(
                        f"Warning: could not compute average for scheme '{scheme}', skipping.",
                        file=sys.stderr,
                    )
                    continue

                x_avg, y_avg, ci_lower, ci_upper = result
                label = scheme if len(y_cols) == 1 else f"{scheme}: {headers[y_col]}"
                line, = plt.plot(x_avg, y_avg, label=label)
                if args.confidence_interval:
                    plt.fill_between(
                        x_avg,
                        ci_lower,
                        ci_upper,
                        color=line.get_color(),
                        alpha=0.2,
                        linewidth=0,
                    )
                plotted_lines += 1

    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if plotted_lines == 0:
        print("Error: no data to plot.", file=sys.stderr)
        return 1

    plt.xlabel(x_axis_label if x_axis_label is not None else args.x)
    raw_ylabel = y_axis_label if y_axis_label is not None else "Value"
    plt.ylabel(f"Average {raw_ylabel}")
    if not args.no_legend:
        plt.legend()

    if args.title:
        plt.title(args.title)
    if args.logx:
        plt.xscale("log")
    if args.logy:
        plt.yscale("log")

    plt.tight_layout()

    if args.output:
        plt.savefig(args.output)
        print(f"Saved plot to {args.output}")

    if args.show or not args.output:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
