#!/usr/bin/env python3
"""
display_valley.py

Visualize the ApplyValley gene-to-trait transformation.
Plots trait value (y) vs gene value (x) for the full range 0–100.
A dashed reference line shows where trait == gene (the valley peaks).
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def apply_valley(values: np.ndarray) -> np.ndarray:
    """Vectorized translation of OrgTypeDOSSIER::ApplyValley."""
    values = np.asarray(values, dtype=float)
    result = values.copy()
    mask = (values >= 8.0) & (values <= 99.0)
    v = values[mask]
    valley_id = np.floor((np.sqrt(8.0 * (v - 8.0) + 1.0) - 1.0) / 2.0)
    valley_start = 8.0 + valley_id * (valley_id + 1) / 2.0
    result[mask] = valley_start - (v - valley_start)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot the ApplyValley gene-to-trait transformation."
    )
    parser.add_argument(
        "-o", "--output",
        type=Path,
        help="Output image file (e.g. valley.png). If omitted, display interactively.",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Display interactively even when --output is used.",
    )
    args = parser.parse_args()

    gene = np.linspace(0, 100, 10001)
    trait = apply_valley(gene)

    plt.plot(gene, trait, color="steelblue", linewidth=2.5, label="Trait (after valleys)")
    plt.plot([0, 100], [0, 100], color="gray", linewidth=0.8,
             linestyle="--", label="Trait = Gene (no valley)")

    plt.xlabel("Gene Value")
    plt.ylabel("Trait Value")
    plt.title("ApplyValley: Gene → Trait")
    plt.legend()
    plt.tight_layout()

    if args.output:
        plt.savefig(args.output)
        print(f"Saved to {args.output}")

    if args.show or not args.output:
        plt.show()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
