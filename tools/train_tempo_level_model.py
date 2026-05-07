#!/usr/bin/env python3
"""Train and export a lightweight tempo-level correction model.

Model: multinomial logistic regression (softmax), implemented in pure Python.
Input: CSV from tools/extract_tempo_training_data.py
Output: JSON for runtime inference in AnalyzerLarocheSwingBeats.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path

CLASSES = [0.5, 1.0, 2.0]
FEATURE_NAMES = [
    "bpm_raw",
    "swing_pct",
    "level_confidence",
    "ambiguous",
]


@dataclass
class Dataset:
    x: list[list[float]]
    y: list[int]


def nearest_multiplier(bpm_raw: float, bpm_gold: float) -> float:
    candidates = [m * bpm_raw for m in CLASSES]
    idx = min(range(len(candidates)), key=lambda i: abs(candidates[i] - bpm_gold))
    return CLASSES[idx]


def mean_std(x: list[list[float]]) -> tuple[list[float], list[float]]:
    n = len(x)
    d = len(x[0])
    mean = [0.0] * d
    for row in x:
        for i, v in enumerate(row):
            mean[i] += v
    mean = [v / n for v in mean]

    var = [0.0] * d
    for row in x:
        for i, v in enumerate(row):
            dv = v - mean[i]
            var[i] += dv * dv
    std = [math.sqrt(v / n) for v in var]
    std = [s if s > 1e-9 else 1.0 for s in std]
    return mean, std


def normalize(x: list[list[float]], mean: list[float], std: list[float]) -> list[list[float]]:
    out: list[list[float]] = []
    for row in x:
        out.append([(row[i] - mean[i]) / std[i] for i in range(len(row))])
    return out


def load_dataset(path: Path) -> Dataset:
    feats: list[list[float]] = []
    labels: list[int] = []

    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                bpm_raw = float(row["bpm_raw"])
                bpm_gold = float(row["bpm_gold"])
                swing = float(row.get("swing_pct", 0.0) or 0.0)
                level_conf = float(row.get("level_confidence", 0.0) or 0.0)
                ambiguous = float(row.get("ambiguous", 0.0) or 0.0)
            except (ValueError, KeyError):
                continue
            if bpm_raw <= 0.0 or bpm_gold <= 0.0:
                continue

            feats.append([bpm_raw, swing, level_conf, ambiguous])
            target_mult = nearest_multiplier(bpm_raw, bpm_gold)
            labels.append(CLASSES.index(target_mult))

    if not feats:
        raise RuntimeError("No valid rows in dataset")
    return Dataset(x=feats, y=labels)


def softmax(logits: list[float]) -> list[float]:
    m = max(logits)
    exps = [math.exp(v - m) for v in logits]
    s = sum(exps)
    if s <= 0.0:
        return [1.0 / len(logits)] * len(logits)
    return [v / s for v in exps]


def train(dataset: Dataset, epochs: int, lr: float, l2: float):
    x_mean, x_std = mean_std(dataset.x)
    x_norm = normalize(dataset.x, x_mean, x_std)

    n = len(x_norm)
    d = len(x_norm[0])
    k = len(CLASSES)

    w = [[0.0 for _ in range(d)] for _ in range(k)]
    b = [0.0 for _ in range(k)]

    for _ in range(epochs):
        grad_w = [[0.0 for _ in range(d)] for _ in range(k)]
        grad_b = [0.0 for _ in range(k)]

        for row, target in zip(x_norm, dataset.y):
            logits = [sum(wc[i] * row[i] for i in range(d)) + b[c] for c, wc in enumerate(w)]
            probs = softmax(logits)
            for c in range(k):
                diff = probs[c] - (1.0 if c == target else 0.0)
                grad_b[c] += diff
                for i in range(d):
                    grad_w[c][i] += diff * row[i]

        inv_n = 1.0 / n
        for c in range(k):
            grad_b[c] *= inv_n
            for i in range(d):
                grad_w[c][i] = grad_w[c][i] * inv_n + l2 * w[c][i]
                w[c][i] -= lr * grad_w[c][i]
            b[c] -= lr * grad_b[c]

    return w, b, x_mean, x_std


def evaluate(dataset: Dataset, w, b, mean, std) -> float:
    x_norm = normalize(dataset.x, mean, std)
    correct = 0
    for row, target in zip(x_norm, dataset.y):
        logits = [sum(wc[i] * row[i] for i in range(len(row))) + b[c] for c, wc in enumerate(w)]
        pred = max(range(len(logits)), key=lambda i: logits[i])
        if pred == target:
            correct += 1
    return correct / len(dataset.y)


def export_model(path: Path, w, b, mean, std, accuracy: float, rows: int) -> None:
    payload = {
        "model_type": "softmax_linear",
        "feature_names": FEATURE_NAMES,
        "classes": CLASSES,
        "weights": w,
        "bias": b,
        "feature_mean": mean,
        "feature_std": std,
        "training_rows": rows,
        "training_accuracy": accuracy,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--epochs", type=int, default=1000)
    parser.add_argument("--lr", type=float, default=0.06)
    parser.add_argument("--l2", type=float, default=1e-3)
    args = parser.parse_args()

    ds = load_dataset(Path(args.input))
    w, b, mean, std = train(ds, epochs=args.epochs, lr=args.lr, l2=args.l2)
    acc = evaluate(ds, w, b, mean, std)
    export_model(Path(args.output), w, b, mean, std, acc, rows=len(ds.y))

    print(f"Trained rows: {len(ds.y)}")
    print(f"Training accuracy: {acc:.3f}")
    print(f"Model written to: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
