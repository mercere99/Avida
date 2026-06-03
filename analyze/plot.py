#!/usr/bin/env python3
"""
plot_multi_csv_columns.py

Plot one or more numeric columns from one or more CSV files.

Required style:

  python plot_multi_csv_columns.py \
    ../build/data/Run-base-100/Pareto.csv \
    ../build/data/Run-structure-100/Pareto.csv \
    ../build/data/Run-multipath-100/Pareto.csv \
    --x 0 --y 1

The first row of each CSV file is assumed to contain column headers. Headers are
used for axis labels and legend labels. Columns can be specified either by
zero-based index or by exact header name.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt


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
        available = ", ".join(repr(header) for header in headers)
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
    """
    Read numeric data from the selected x column and y columns.

    Any row with missing or non-numeric selected values is skipped. This keeps
    all y-series from a given file aligned to the same x-values.
    """
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


def make_label(
    csv_file: Path,
    y_header: str,
    num_files: int,
    num_y_cols: int,
    label_mode: str,
) -> str:
    """Create a legend label for one plotted series."""
    # For paths like Run-base-100/Pareto.csv, the parent directory is usually
    # more informative than the filename, since every file may be Pareto.csv.
    file_label = csv_file.parent.name or csv_file.stem

    if label_mode == "file":
        return file_label
    if label_mode == "column":
        return y_header
    if label_mode == "both":
        return f"{file_label}: {y_header}"

    # auto
    if num_files == 1:
        return y_header
    if num_y_cols == 1:
        return file_label
    return f"{file_label}: {y_header}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot selected CSV columns from one or more files."
    )

    parser.add_argument(
        "csv_files",
        nargs="+",
        type=Path,
        help="Input CSV file or files.",
    )
    parser.add_argument(
        "--x",
        required=True,
        help="X-axis column, specified by zero-based index or exact header name.",
    )
    parser.add_argument(
        "--y",
        required=True,
        nargs="+",
        help="Y-axis column(s), specified by zero-based index or exact header name.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output image file, such as plot.png, plot.pdf, or plot.svg. If omitted, display interactively.",
    )
    parser.add_argument("--title", help="Optional plot title.")
    parser.add_argument("--delimiter", default=",", help="CSV delimiter. Default: comma.")
    parser.add_argument("--scatter", action="store_true", help="Use scatter plots instead of line plots.")
    parser.add_argument("--no-markers", action="store_true", help="For line plots, do not draw point markers.")
    parser.add_argument("--logx", action="store_true", help="Use a logarithmic x-axis.")
    parser.add_argument("--logy", action="store_true", help="Use a logarithmic y-axis.")
    parser.add_argument(
        "--label-mode",
        choices=["auto", "file", "column", "both"],
        default="auto",
        help="Legend labels: auto, file, column, or both. Default: auto.",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Display the plot even when --output is used.",
    )

    args = parser.parse_args()

    plotted_lines = 0
    x_axis_label: str | None = None
    y_axis_label: str | None = None

    try:
        for csv_file in args.csv_files:
            headers = read_headers(csv_file, args.delimiter)

            x_col = parse_column(args.x, headers)
            y_cols = [parse_column(y_selector, headers) for y_selector in args.y]

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

            for y_col in y_cols:
                label = make_label(
                    csv_file=csv_file,
                    y_header=headers[y_col],
                    num_files=len(args.csv_files),
                    num_y_cols=len(y_cols),
                    label_mode=args.label_mode,
                )

                if args.scatter:
                    plt.scatter(x_values, y_values[y_col], label=label)
                else:
                    marker = None if args.no_markers else "o"
                    plt.plot(x_values, y_values[y_col], marker=marker, label=label)

                plotted_lines += 1

    except FileNotFoundError as error:
        print(f"Error: file not found: {error.filename}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if plotted_lines == 0:
        print("Error: no valid numeric data found.", file=sys.stderr)
        return 1

    plt.xlabel(x_axis_label if x_axis_label is not None else args.x)
    plt.ylabel(y_axis_label if y_axis_label is not None else "Value")

    if plotted_lines > 1:
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
