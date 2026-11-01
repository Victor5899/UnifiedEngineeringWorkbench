#!/usr/bin/env python3
"""Redraw the Task F loss figure from the C++ training CSV.

This script does not train the network and does not modify the CSV.
The C++ program is the source of the loss history.

From the repository root, after the CSV exists:

    python3 scripts/plot_backpropagation_loss.py

Optional arguments are the CSV path and the PNG path. The defaults are
results/backpropagation_training.csv and results/backpropagation_loss.png.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def read_history(path: Path) -> tuple[list[int], list[float]]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"step", "loss", "w", "b", "v", "c"}
        if reader.fieldnames is None or not required.issubset(set(reader.fieldnames)):
            raise SystemExit(f"{path} is missing columns step,loss,w,b,v,c.")
        steps: list[int] = []
        losses: list[float] = []
        for row in reader:
            steps.append(int(row["step"]))
            losses.append(float(row["loss"]))
    if steps != list(range(101)):
        raise SystemExit(f"{path} must contain steps 0 through 100. Found {len(steps)} data rows.")
    return steps, losses


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot Task F training loss from the C++ CSV.")
    parser.add_argument("csv_path", nargs="?", default="results/backpropagation_training.csv")
    parser.add_argument("png_path", nargs="?", default="results/backpropagation_loss.png")
    args = parser.parse_args()

    source = Path(args.csv_path)
    destination = Path(args.png_path)
    if not source.is_file():
        raise SystemExit(
            f"CSV not found: {source}\n"
            "Generate it with: ./build/workbench backprop --output results/backpropagation_training.csv"
        )

    steps, losses = read_history(source)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    destination.parent.mkdir(parents=True, exist_ok=True)
    figure, axis = plt.subplots(figsize=(8.2, 4.6), dpi=140)
    axis.plot(steps, losses, color="#0f6e6e", linewidth=1.8)
    axis.set_title("Training loss versus step")
    axis.set_xlabel("Training step")
    axis.set_ylabel("Loss")
    axis.set_xlim(0, 100)
    axis.set_xticks([0, 20, 40, 60, 80, 100])
    peak = max(losses)
    if min(losses) >= 0.0:
        axis.set_ylim(0.0, peak * 1.08 if peak > 0.0 else 1.0)
    axis.grid(True, linestyle="-", linewidth=0.6, alpha=0.7)
    axis.set_axisbelow(True)
    figure.tight_layout()
    figure.savefig(destination)
    plt.close(figure)
    print(f"Plotted {len(steps)} points from {source}")
    print(f"Initial loss: {losses[0]}")
    print(f"Final loss: {losses[-1]}")
    print(f"Plot written to: {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
