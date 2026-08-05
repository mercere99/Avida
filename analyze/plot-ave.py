#!/usr/bin/env python3
"""
plot-ave.py

Like plot.py, but plots every series in translucent gray (no markers) and
overlays the pointwise average of all series in solid black.

When series have different x-ranges, the average is computed over the
intersection of all ranges using the first file's x-values as the grid.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

CI_Z_SCORE = 1.96


def read_headers(csv_file: Path, delimiter: str) -> list[str]:
    """Read and return the header row from a CSV file."""
    with csv_file.open(newline="") as fp:
        reader = csv.reader(fp, delimiter=delimiter)
        headers = next(reader, None)

    if headers is None:
        raise ValueError(f"{csv_file} is empty.")

    return [header.strip() for header in headers]


def parse_column(selector: str, headers: list[str]) -> int:
    """Convert a column name or zero-based column index into an integer index."""
    if selector in headers:
        return headers.index(selector)

    try:
        index = int(selector)
    except ValueError:
        available = ", ".join(repr(h) for h in headers)
        raise ValueError(
            f"Column {selector!r} was not found. Available columns: {available}"
        )

    if index < 0 or index >= len(headers):
        raise ValueError(
            f"Column index {index} is out of range. "
            f"Valid indices are 0 through {len(headers) - 1}."
        )

    return index


def read_selected_data(
    csv_file: Path,
    x_col: int,
    y_cols: list[int],
    delimiter: str,
) -> tuple[list[float], dict[int, list[float]]]:
    """Read numeric data from the selected x column and y columns."""
    x_values: list[float] = []
    y_values: dict[int, list[float]] = {col: [] for col in y_cols}

    selected_cols = [x_col, *y_cols]
    max_col = max(selected_cols)

    with csv_file.open(newline="") as fp:
        reader = csv.reader(fp, delimiter=delimiter)
        next(reader, None)  # Skip headers.

        for row_num, row in enumerate(reader, start=2):
            if len(row) <= max_col:
                print(
                    f"Skipping {csv_file}:{row_num}: expected at least "
                    f"{max_col + 1} columns, found {len(row)}.",
                    file=sys.stderr,
                )
                continue

            selected_raw = [row[col].strip() for col in selected_cols]

            if any(value == "" for value in selected_raw):
                print(
                    f"Skipping {csv_file}:{row_num}: missing selected value.",
                    file=sys.stderr,
                )
                continue

            try:
                selected_numeric = [float(value) for value in selected_raw]
            except ValueError:
                print(
                    f"Skipping {csv_file}:{row_num}: selected values are not all numeric.",
                    file=sys.stderr,
                )
                continue

            x_values.append(selected_numeric[0])
            for y_col, y_value in zip(y_cols, selected_numeric[1:]):
                y_values[y_col].append(y_value)

    return x_values, y_values


def summarize_series(
    series: list[tuple[np.ndarray, np.ndarray]],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Return the pointwise mean and 95% confidence bounds for the mean."""
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
            "Plot CSV series in gray and overlay their pointwise average in black."
        )
    )

    parser.add_argument("csv_files", nargs="+", type=Path, help="Input CSV file(s).")
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
        "-o",
        "--output",
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
        help="Shade the pointwise 95%% confidence interval around the average.",
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

    # Collect all series as (x_array, y_array) pairs.
    all_series: list[tuple[np.ndarray, np.ndarray]] = []
    x_axis_label: str | None = None
    y_axis_label: str | None = None

    try:
        for csv_file in args.csv_files:
            headers = read_headers(csv_file, args.delimiter)
            x_col = parse_column(args.x, headers)
            y_cols = [parse_column(y_sel, headers) for y_sel in args.y]

            if x_axis_label is None:
                x_axis_label = headers[x_col]
            elif headers[x_col] != x_axis_label:
                print(
                    f"Warning: x-axis header in {csv_file} is {headers[x_col]!r}, "
                    f"but earlier files used {x_axis_label!r}.",
                    file=sys.stderr,
                )

            if y_axis_label is None and len(y_cols) == 1:
                y_axis_label = headers[y_cols[0]]

            x_values, y_values = read_selected_data(
                csv_file=csv_file,
                x_col=x_col,
                y_cols=y_cols,
                delimiter=args.delimiter,
            )

            if not x_values:
                print(
                    f"Warning: no valid numeric rows found in {csv_file}.",
                    file=sys.stderr,
                )
                continue

            x_arr = np.array(x_values)
            for y_col in y_cols:
                all_series.append((x_arr, np.array(y_values[y_col])))

    except FileNotFoundError as error:
        print(f"Error: file not found: {error.filename}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if not all_series:
        print("Error: no valid numeric data found.", file=sys.stderr)
        return 1

    # Plot individual series in translucent gray.
    for x_arr, y_arr in all_series:
        plt.plot(x_arr, y_arr, color="gray", linewidth=0.8, alpha=0.4)

    # Compute the pointwise average using the longest run as the x-grid so the
    # average persists as long as any run is still going. Series that have ended
    # contribute NaN beyond their final x-value and are excluded from the mean.
    x_grid, y_avg, ci_lower, ci_upper = summarize_series(all_series)

    plt.plot(x_grid, y_avg, color="black", linewidth=2, label="Average")
    if args.confidence_interval:
        plt.fill_between(
            x_grid,
            ci_lower,
            ci_upper,
            color="black",
            alpha=0.2,
            linewidth=0,
        )

    plt.xlabel(x_axis_label if x_axis_label is not None else args.x)
    plt.ylabel(y_axis_label if y_axis_label is not None else "Value")
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
