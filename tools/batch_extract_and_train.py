#!/usr/bin/env python3
"""Batch extraction + iterative training loop for Laroche tempo-level model.

Workflow:
1) Run extract_tempo_training_data.py repeatedly (incremental output CSV).
2) After `batch_matched` newly matched rows have been added, run training.
3) Track validation accuracy and early-stop when no meaningful improvement.
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class CsvStats:
    rows_total: int
    rows_matched: int


def read_csv_stats(path: Path) -> CsvStats:
    if not path.exists() or path.stat().st_size == 0:
        return CsvStats(rows_total=0, rows_matched=0)

    total = 0
    matched = 0
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            total += 1
            has_gold_raw = row.get("has_gold", "0")
            try:
                has_gold = int(float(has_gold_raw or 0))
            except ValueError:
                has_gold = 0
            if has_gold > 0:
                matched += 1
    return CsvStats(rows_total=total, rows_matched=matched)


def run_command(cmd: list[str]) -> None:
    print("$", " ".join(cmd))
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--extract-script", default="tools/extract_tempo_training_data.py")
    parser.add_argument("--train-script", default="tools/train_tempo_level_model.py")
    parser.add_argument("--root", required=True)
    parser.add_argument("--db", default=str(Path.home() / ".mixxx" / "mixxxdb.sqlite"))
    parser.add_argument("--output-csv", required=True)
    parser.add_argument("--model-out", required=True)

    parser.add_argument("--extract-max-files", type=int, default=200)
    parser.add_argument("--batch-matched", type=int, default=200)
    parser.add_argument("--min-train-matched", type=int, default=200)
    parser.add_argument("--require-bpm-lock", action="store_true")
    parser.add_argument("--extract-verbose", action="store_true")

    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--epochs", type=int, default=1200)
    parser.add_argument("--lr", type=float, default=0.06)
    parser.add_argument("--l2", type=float, default=1e-3)
    parser.add_argument("--class-weight", choices=["none", "balanced"], default="none")
    parser.add_argument("--train-require-bpm-lock", action="store_true")

    parser.add_argument("--patience", type=int, default=3)
    parser.add_argument("--min-improvement", type=float, default=0.002)
    parser.add_argument("--max-rounds", type=int, default=200)
    parser.add_argument("--state-json", default="")
    parser.add_argument(
        "--resume-from-state",
        action="store_true",
        help="Resume counters/metrics from --state-json",
    )
    args = parser.parse_args()

    extract_script = Path(args.extract_script)
    train_script = Path(args.train_script)
    output_csv = Path(args.output_csv)
    model_out = Path(args.model_out)
    best_model_out = model_out.with_suffix(model_out.suffix + ".best")

    accumulated_new_matched = 0
    best_val = float("-inf")
    no_improve_rounds = 0
    train_rounds = 0
    start_round = 1

    if args.state_json:
        state_path = Path(args.state_json)
    else:
        state_path = None

    if args.resume_from_state:
        if state_path is None:
            raise RuntimeError("--resume-from-state requires --state-json")
        if not state_path.exists():
            raise RuntimeError(f"State file not found: {state_path}")

        state = json.loads(state_path.read_text(encoding="utf-8"))
        start_round = int(state.get("round", 0)) + 1
        accumulated_new_matched = int(state.get("batch_accum_matched", 0))
        no_improve_rounds = int(state.get("no_improve_rounds", 0))
        train_rounds = int(state.get("train_rounds", 0))
        best_val_raw = state.get("best_validation_accuracy")
        if best_val_raw is not None:
            best_val = float(best_val_raw)

        print(
            f"Resuming from state: round={start_round}, "
            f"batch_accum_matched={accumulated_new_matched}, "
            f"best_val={best_val if best_val != float('-inf') else 'n/a'}, "
            f"no_improve={no_improve_rounds}, train_rounds={train_rounds}"
        )

    for loop_idx in range(args.max_rounds):
        round_idx = start_round + loop_idx
        before = read_csv_stats(output_csv)
        extract_cmd = [
            args.python,
            str(extract_script),
            "--root",
            args.root,
            "--db",
            args.db,
            "--max-files",
            str(args.extract_max_files),
            "--output",
            str(output_csv),
        ]
        if args.require_bpm_lock:
            extract_cmd.append("--require-bpm-lock")
        if args.extract_verbose:
            extract_cmd.append("--verbose")

        run_command(extract_cmd)

        after = read_csv_stats(output_csv)
        delta_rows = after.rows_total - before.rows_total
        delta_matched = after.rows_matched - before.rows_matched
        accumulated_new_matched += max(0, delta_matched)

        print(
            f"[round {round_idx}] csv_total={after.rows_total} (+{delta_rows}), "
            f"csv_matched={after.rows_matched} (+{delta_matched}), "
            f"batch_accum_matched={accumulated_new_matched}/{args.batch_matched}"
        )

        if state_path:
            state_payload = {
                "round": round_idx,
                "csv_rows_total": after.rows_total,
                "csv_rows_matched": after.rows_matched,
                "batch_accum_matched": accumulated_new_matched,
                "best_validation_accuracy": None if best_val == float("-inf") else best_val,
                "no_improve_rounds": no_improve_rounds,
                "train_rounds": train_rounds,
                "best_model_path": str(best_model_out) if best_model_out.exists() else "",
            }
            state_path.parent.mkdir(parents=True, exist_ok=True)
            state_path.write_text(json.dumps(state_payload, indent=2), encoding="utf-8")

        if delta_rows <= 0:
            print("No new rows were added by extractor. Stopping.")
            break

        if after.rows_matched < args.min_train_matched:
            continue

        if accumulated_new_matched < args.batch_matched:
            continue

        train_rounds += 1
        train_cmd = [
            args.python,
            str(train_script),
            "--input",
            str(output_csv),
            "--output",
            str(model_out),
            "--val-fraction",
            str(args.val_fraction),
            "--epochs",
            str(args.epochs),
            "--lr",
            str(args.lr),
            "--l2",
            str(args.l2),
            "--class-weight",
            args.class_weight,
        ]
        if args.train_require_bpm_lock:
            train_cmd.append("--require-bpm-lock")

        run_command(train_cmd)

        model_data = json.loads(model_out.read_text(encoding="utf-8"))
        val_acc = float(model_data.get("validation_accuracy", 0.0))
        train_acc = float(model_data.get("training_accuracy", 0.0))
        print(
            f"[train {train_rounds}] train_acc={train_acc:.4f}, "
            f"val_acc={val_acc:.4f}, best_val={best_val if best_val != float('-inf') else 'n/a'}"
        )

        if val_acc > best_val + args.min_improvement:
            best_val = val_acc
            no_improve_rounds = 0
            best_model_out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(model_out, best_model_out)
            print(f"  improvement accepted; best model updated: {best_model_out}")
        else:
            no_improve_rounds += 1
            print(f"  no meaningful improvement (patience {no_improve_rounds}/{args.patience})")

        accumulated_new_matched = 0

        if state_path:
            state_payload = {
                "round": round_idx,
                "csv_rows_total": after.rows_total,
                "csv_rows_matched": after.rows_matched,
                "batch_accum_matched": accumulated_new_matched,
                "best_validation_accuracy": None if best_val == float("-inf") else best_val,
                "no_improve_rounds": no_improve_rounds,
                "train_rounds": train_rounds,
                "best_model_path": str(best_model_out) if best_model_out.exists() else "",
            }
            state_path.parent.mkdir(parents=True, exist_ok=True)
            state_path.write_text(json.dumps(state_payload, indent=2), encoding="utf-8")

        if no_improve_rounds >= args.patience:
            print("Early stopping: no further validation improvement.")
            break

    print("Done.")
    if best_model_out.exists():
        print(f"Best model: {best_model_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
