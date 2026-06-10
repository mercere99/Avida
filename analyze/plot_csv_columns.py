#!/usr/bin/env python3
"""
plot_csv_columns.py

Plot two columns from a CSV file against each other.

The first row of the CSV is assumed to contain column headers. These headers
are used as axis labels when columns are selected by name, and also as labels
when columns are selected by index.

Examples:
  python plot_csv_columns.py data.csv Time Fitness
  python plot_csv_columns.py data.csv 0 3
  python plot_csv_columns.py data.csv "Generation" "Mean Fitness" -o fitness.png
  python plot_csv_columns.py data.csv x_column y_column --title "My Plot" --show
"""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt


def parse_column_selector(selector: str, headers: list[str]) -> int:
    """Return the zero-based column index selected by name or integer index."""
    if selector in headers:
        return headers.index(selector)

    try:
        index = int(selector)
    except ValueError:
        available = ", ".join(repr(h) for h in headers)
        raise ValueError(
            f"Column {selector!r} was not found. Available columns are: {available}"
        )

    if index < 0 or index >= len(headers):
        raise ValueError(
            f"Column index {index} is out of range; valid indices are 0 to {len(headers) - 1}."
        )

    return index


def read_numeric_columns(csv_path: Path, x_col: int, y_col: int) -> tuple[list[float], list[float]]:
    """Read two numeric columns from a CSV file, skipping rows with missing values."""
    x_values: list[float] = []
    y_values: list[float] = []

    with csv_path.open(newline="") as fp:
        reader = csv.reader(fp)
        headers = next(reader, None)

        if headers is None:
            raise ValueError(f"{csv_path} is empty.")

        for row_num, row in enumerate(reader, start=2):
            if len(row) <= max(x_col, y_col):
                print(f"Skipping row {row_num}: not enough columns.", file=sys.stderr)
                continue

            x_raw = row[x_col].strip()
            y_raw = row[y_col].strip()

            if x_raw == "" or y_raw == "":
                print(f"Skipping row {row_num}: missing value.", file=sys.stderr)
                continue

            try:
                x_values.append(float(x_raw))
                y_values.append(float(y_raw))
            except ValueError:
                print(
                    f"Skipping row {row_num}: could not convert "
                    f"{x_raw!r}, {y_raw!r} to numbers.",
                    file=sys.stderr,
                )

    return x_values, y_values


def get_headers(csv_path: Path) -> list[str]:
    with csv_path.open(newline="") as fp:
        reader = csv.reader(fp)
        headers = next(reader, None)

    if headers is None:
        raise ValueError(f"{csv_path} is empty.")

    return [header.strip() for header in headers]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot two columns from a CSV file against each other."
    )
    parser.add_argument("csv_file", type=Path, help="Path to the input CSV file.")
    parser.add_argument("x_column", help="Column name or zero-based column index for the x-axis.")
    parser.add_argument("y_column", help="Column name or zero-based column index for the y-axis.")
    parser.add_argument(
        "-o", "--output",
        type=Path,
        default=None,
        help="Output image filename, such as plot.png or plot.pdf. If omitted, the plot is displayed.",
    )
    parser.add_argument("--title", default=None, help="Optional plot title.")
    parser.add_argument("--show", action="store_true", help="Display the plot even if --output is used.")
    parser.add_argument("--scatter", action="store_true", help="Use a scatter plot instead of a line plot.")
    parser.add_argument("--delimiter", default=",", help="CSV delimiter. Default: comma.")

    args = parser.parse_args()

    try:
        headers = get_headers(args.csv_file)
        x_col = parse_column_selector(args.x_column, headers)
        y_col = parse_column_selector(args.y_column, headers)
        x_values, y_values = read_numeric_columns(args.csv_file, x_col, y_col)
    except FileNotFoundError:
        print(f"Error: file not found: {args.csv_file}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    if not x_values:
        print("Error: no valid numeric data found for the selected columns.", file=sys.stderr)
        return 1

    x_label = headers[x_col]
    y_label = headers[y_col]

    if args.scatter:
        plt.scatter(x_values, y_values)
    else:
        plt.plot(x_values, y_values, marker="o")

    plt.xlabel(x_label)
    plt.ylabel(y_label)

    if args.title:
        plt.title(args.title)

    plt.tight_layout()

    if args.output:
        plt.savefig(args.output)
        print(f"Saved plot to {args.output}")

    if args.show or not args.output:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
