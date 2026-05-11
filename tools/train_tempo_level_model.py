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
import random
from collections import defaultdict
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
class Row:
    path: str
    x: list[float]
    y: int


@dataclass
class Dataset:
    rows: list[Row]

    @property
    def x(self) -> list[list[float]]:
        return [r.x for r in self.rows]

    @property
    def y(self) -> list[int]:
        return [r.y for r in self.rows]


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


def load_dataset(
    path: Path,
    require_has_gold: bool,
    require_bpm_lock: bool,
    dedupe_by_path: bool,
) -> Dataset:
    rows: list[Row] = []

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

            has_gold = int(float(row.get("has_gold", 1) or 0))
            bpm_lock = int(float(row.get("bpm_lock", 0) or 0))
            if require_has_gold and has_gold == 0:
                continue
            if require_bpm_lock and bpm_lock == 0:
                continue

            path_value = row.get("path", "")
            target_mult = nearest_multiplier(bpm_raw, bpm_gold)
            label_idx = CLASSES.index(target_mult)

            rows.append(
                Row(
                    path=path_value,
                    x=[bpm_raw, swing, level_conf, ambiguous],
                    y=label_idx,
                )
            )

    if dedupe_by_path:
        deduped: dict[str, Row] = {}
        for r in rows:
            key = r.path or f"__row_{len(deduped)}"
            deduped[key] = r
        rows = list(deduped.values())

    if not rows:
        raise RuntimeError("No valid rows in dataset after filtering")

    return Dataset(rows=rows)


def split_dataset(dataset: Dataset, val_fraction: float, seed: int) -> tuple[Dataset, Dataset]:
    if val_fraction <= 0.0:
        return dataset, Dataset(rows=[])

    by_class: dict[int, list[Row]] = defaultdict(list)
    for r in dataset.rows:
        by_class[r.y].append(r)

    rng = random.Random(seed)
    train_rows: list[Row] = []
    val_rows: list[Row] = []
    for cls_rows in by_class.values():
        rng.shuffle(cls_rows)
        n = len(cls_rows)
        n_val = max(1, int(round(n * val_fraction))) if n > 1 else 0
        if n_val >= n:
            n_val = n - 1
        val_rows.extend(cls_rows[:n_val])
        train_rows.extend(cls_rows[n_val:])

    if not train_rows:
        train_rows = val_rows
        val_rows = []

    return Dataset(rows=train_rows), Dataset(rows=val_rows)


def softmax(logits: list[float]) -> list[float]:
    m = max(logits)
    exps = [math.exp(v - m) for v in logits]
    s = sum(exps)
    if s <= 0.0:
        return [1.0 / len(logits)] * len(logits)
    return [v / s for v in exps]


def compute_class_weights(y: list[int], mode: str) -> list[float]:
    counts = [0] * len(CLASSES)
    for yi in y:
        counts[yi] += 1

    if mode == "none":
        return [1.0] * len(CLASSES)

    # balanced: inverse-frequency normalized to mean weight = 1.0
    total = max(1, sum(counts))
    weights = []
    for c in counts:
        if c == 0:
            weights.append(0.0)
        else:
            weights.append(total / (len(CLASSES) * c))
    mean_w = sum(weights) / len(weights)
    if mean_w > 0:
        weights = [w / mean_w for w in weights]
    return weights


def train(dataset: Dataset, epochs: int, lr: float, l2: float, class_weight_mode: str):
    x_mean, x_std = mean_std(dataset.x)
    x_norm = normalize(dataset.x, x_mean, x_std)

    n = len(x_norm)
    d = len(x_norm[0])
    k = len(CLASSES)

    w = [[0.0 for _ in range(d)] for _ in range(k)]
    b = [0.0 for _ in range(k)]

    class_weights = compute_class_weights(dataset.y, class_weight_mode)

    for _ in range(epochs):
        grad_w = [[0.0 for _ in range(d)] for _ in range(k)]
        grad_b = [0.0 for _ in range(k)]

        for row, target in zip(x_norm, dataset.y):
            logits = [sum(wc[i] * row[i] for i in range(d)) + b[c] for c, wc in enumerate(w)]
            probs = softmax(logits)
            sample_weight = class_weights[target]
            for c in range(k):
                diff = (probs[c] - (1.0 if c == target else 0.0)) * sample_weight
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


def predict_labels(dataset: Dataset, w, b, mean, std) -> list[int]:
    if not dataset.rows:
        return []
    x_norm = normalize(dataset.x, mean, std)
    preds: list[int] = []
    for row in x_norm:
        logits = [sum(wc[i] * row[i] for i in range(len(row))) + b[c] for c, wc in enumerate(w)]
        pred = max(range(len(logits)), key=lambda i: logits[i])
        preds.append(pred)
    return preds


def accuracy(y_true: list[int], y_pred: list[int]) -> float:
    if not y_true:
        return 0.0
    correct = sum(1 for t, p in zip(y_true, y_pred) if t == p)
    return correct / len(y_true)


def confusion_matrix(y_true: list[int], y_pred: list[int]) -> list[list[int]]:
    k = len(CLASSES)
    m = [[0 for _ in range(k)] for _ in range(k)]
    for t, p in zip(y_true, y_pred):
        m[t][p] += 1
    return m


def class_balance(dataset: Dataset) -> list[int]:
    counts = [0] * len(CLASSES)
    for y in dataset.y:
        counts[y] += 1
    return counts


def export_model(
    path: Path,
    w,
    b,
    mean,
    std,
    train_acc: float,
    val_acc: float,
    train_rows: int,
    val_rows: int,
) -> None:
    payload = {
        "model_type": "softmax_linear",
        "feature_names": FEATURE_NAMES,
        "classes": CLASSES,
        "weights": w,
        "bias": b,
        "feature_mean": mean,
        "feature_std": std,
        "training_rows": train_rows,
        "validation_rows": val_rows,
        "training_accuracy": train_acc,
        "validation_accuracy": val_acc,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def print_confusion(title: str, matrix: list[list[int]]) -> None:
    print(title)
    print("  true\\pred  0.5   1.0   2.0")
    for i, row in enumerate(matrix):
        print(f"      {CLASSES[i]:>3}   {row[0]:>3}   {row[1]:>3}   {row[2]:>3}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--epochs", type=int, default=1000)
    parser.add_argument("--lr", type=float, default=0.06)
    parser.add_argument("--l2", type=float, default=1e-3)
    parser.add_argument("--class-weight", choices=["balanced", "none"], default="none")
    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--require-has-gold", action="store_true", default=True)
    parser.add_argument("--no-require-has-gold", action="store_true")
    parser.add_argument("--require-bpm-lock", action="store_true")
    parser.add_argument("--no-dedupe", action="store_true")
    args = parser.parse_args()

    require_has_gold = args.require_has_gold and not args.no_require_has_gold
    dedupe_by_path = not args.no_dedupe

    full_ds = load_dataset(
        Path(args.input),
        require_has_gold=require_has_gold,
        require_bpm_lock=args.require_bpm_lock,
        dedupe_by_path=dedupe_by_path,
    )
    train_ds, val_ds = split_dataset(full_ds, args.val_fraction, args.seed)

    w, b, mean, std = train(
        train_ds,
        epochs=args.epochs,
        lr=args.lr,
        l2=args.l2,
        class_weight_mode=args.class_weight,
    )

    train_pred = predict_labels(train_ds, w, b, mean, std)
    train_acc = accuracy(train_ds.y, train_pred)
    train_cm = confusion_matrix(train_ds.y, train_pred)

    val_pred = predict_labels(val_ds, w, b, mean, std)
    val_acc = accuracy(val_ds.y, val_pred) if val_ds.rows else 0.0
    val_cm = confusion_matrix(val_ds.y, val_pred) if val_ds.rows else [[0] * 3 for _ in range(3)]

    export_model(
        Path(args.output),
        w,
        b,
        mean,
        std,
        train_acc=train_acc,
        val_acc=val_acc,
        train_rows=len(train_ds.rows),
        val_rows=len(val_ds.rows),
    )

    print(f"Rows total: {len(full_ds.rows)}")
    print(f"Rows train: {len(train_ds.rows)}")
    print(f"Rows val:   {len(val_ds.rows)}")
    print(f"Class balance (total): {class_balance(full_ds)} for classes {CLASSES}")
    print(f"Training accuracy:   {train_acc:.3f}")
    print(f"Validation accuracy: {val_acc:.3f}" if val_ds.rows else "Validation accuracy: n/a (no val rows)")
    print_confusion("Training confusion matrix:", train_cm)
    if val_ds.rows:
        print_confusion("Validation confusion matrix:", val_cm)
    print(f"Model written to: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
