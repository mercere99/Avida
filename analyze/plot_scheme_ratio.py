#!/usr/bin/env python3
"""
plot_scheme_ratio.py

Like plot_schemes.py, but plots the ratio of two columns (numerator / denominator)
averaged across seed runs for each selection scheme.

Expected directory layout:
  <landscape_dir>/
    cohort/      seed_001/  <datafile>
                 seed_002/  <datafile>  ...
    downsample/  seed_001/  ...
    epsilon/     seed_001/  ...
    informed/    seed_001/  ...
    lexicase/    seed_001/  ...
    tournament/  seed_001/  ...

The ratio is computed per seed run, then averaged across seeds.
Division by zero produces NaN and is excluded from the mean.

Example:
  python plot_scheme_ratio.py data/multipath/ Pareto.csv --x 0 --y 1 2
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

SCHEMES = ["tournament", "lexicase", "downsample", "informed", "cohort", "epsilon"]


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


def read_ratio_data(
    csv_file: Path,
    x_col: int,
    num_col: int,
    den_col: int,
    delimiter: str,
) -> tuple[list[float], list[float]]:
    """Read x values and the per-row ratio (numerator / denominator)."""
    x_values: list[float] = []
    ratio_values: list[float] = []
    selected_cols = [x_col, num_col, den_col]
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
            raw = [row[c].strip() for c in selected_cols]
            if any(v == "" for v in raw):
                continue
            try:
                x_val, num_val, den_val = [float(v) for v in raw]
            except ValueError:
                continue
            x_values.append(x_val)
            ratio_values.append(num_val / den_val if den_val != 0.0 else float("nan"))

    return x_values, ratio_values


def average_series(
    series: list[tuple[np.ndarray, np.ndarray]],
) -> tuple[np.ndarray, np.ndarray] | None:
    """Average a list of (x, y) series, persisting as long as any run continues.
    Series that have ended contribute NaN beyond their final x-value and are
    excluded from the mean at those points."""
    if not series:
        return None
    x_grid = max(series, key=lambda pair: pair[0][-1])[0]
    y_matrix = np.full((len(series), len(x_grid)), np.nan)
    for i, (x_arr, y_arr) in enumerate(series):
        valid = (x_grid >= x_arr[0]) & (x_grid <= x_arr[-1])
        y_matrix[i, valid] = np.interp(x_grid[valid], x_arr, y_arr)
    return x_grid, np.nanmean(y_matrix, axis=0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Plot the averaged ratio of two columns per scheme across seed runs."
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
        nargs=2,
        metavar=("NUMERATOR", "DENOMINATOR"),
        help="Two Y-axis columns (by index or name): numerator then denominator.",
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

            seed_series: list[tuple[np.ndarray, np.ndarray]] = []
            x_col: int | None = None
            num_col: int | None = None
            den_col: int | None = None

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
                    num_col = parse_column(args.y[0], headers)
                    den_col = parse_column(args.y[1], headers)
                    if x_axis_label is None:
                        x_axis_label = headers[x_col]
                    if y_axis_label is None:
                        y_axis_label = f"{headers[num_col]} / {headers[den_col]}"

                x_values, ratio_values = read_ratio_data(
                    data_file, x_col, num_col, den_col, args.delimiter
                )
                if not x_values:
                    continue

                seed_series.append((np.array(x_values), np.array(ratio_values)))

            if not seed_series:
                continue

            result = average_series(seed_series)
            if result is None:
                print(
                    f"Warning: could not compute average for scheme '{scheme}', skipping.",
                    file=sys.stderr,
                )
                continue

            x_avg, y_avg = result
            plt.plot(x_avg, y_avg, label=scheme)
            plotted_lines += 1

    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if plotted_lines == 0:
        print("Error: no data to plot.", file=sys.stderr)
        return 1

    plt.xlabel(x_axis_label if x_axis_label is not None else args.x)
    plt.ylabel(f"Average {y_axis_label}" if y_axis_label is not None else "Average Ratio")
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
