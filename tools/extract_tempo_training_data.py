#!/usr/bin/env python3
"""Extract Laroche tempo-level training data from Mixxx tracks.

Runs the existing Mixxx smoke test analyzer over a root directory, parses
CSV rows emitted by the test, joins with hand-corrected BPM values from
mixxxdb.sqlite, and writes a training CSV.
"""

from __future__ import annotations

import argparse
import csv
import os
import sqlite3
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse


def normalize_track_location(value: str) -> str:
    if not value:
        return ""
    parsed = urlparse(value)
    if parsed.scheme == "file":
        path = unquote(parsed.path)
        return os.path.normpath(path)
    return os.path.normpath(value)


def load_gold_bpm(db_path: Path, require_locked: bool) -> dict[str, dict[str, float]]:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    try:
        rows = conn.execute(
            """
            SELECT tl.location AS location,
                   l.bpm AS bpm,
                   l.bpm_lock AS bpm_lock,
                   l.id AS track_id
            FROM library l
            JOIN track_locations tl ON l.location = tl.id
            WHERE l.bpm IS NOT NULL AND l.bpm > 0
            """
        ).fetchall()
    finally:
        conn.close()

    gold: dict[str, dict[str, float]] = {}
    for row in rows:
        if require_locked and int(row["bpm_lock"] or 0) == 0:
            continue
        path = normalize_track_location(str(row["location"]))
        gold[path] = {
            "bpm_gold": float(row["bpm"]),
            "bpm_lock": float(row["bpm_lock"] or 0),
            "track_id": float(row["track_id"]),
        }
    return gold


def run_analysis(mixxx_test: Path, root: Path, max_files: int) -> list[dict[str, str]]:
    env = os.environ.copy()
    env["MIXXX_LAROCHE_ROOT"] = str(root)
    env["MIXXX_LAROCHE_MAX_FILES"] = str(max_files)
    env["MIXXX_LAROCHE_EMIT_CSV"] = "1"

    cmd = [
        str(mixxx_test),
        "--gtest_filter=AnalyzerLarocheBalboaSmokeTest.AnalyzeBalboaMp3Collection",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, check=True)

    header: list[str] | None = None
    rows: list[dict[str, str]] = []
    for line in proc.stdout.splitlines():
        if not line.startswith("CSV|"):
            continue
        payload = line[4:]
        values = payload.split("\t")
        if header is None:
            header = values
            continue
        if len(values) != len(header):
            continue
        rows.append(dict(zip(header, values)))

    if not rows:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError("No CSV rows were emitted by analyzer test")

    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mixxx-test", default="./build/mixxx-test")
    parser.add_argument("--root", required=True)
    parser.add_argument("--db", default=str(Path.home() / ".mixxx" / "mixxxdb.sqlite"))
    parser.add_argument("--max-files", type=int, default=500)
    parser.add_argument("--require-bpm-lock", action="store_true")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    mixxx_test = Path(args.mixxx_test)
    root = Path(args.root)
    db = Path(args.db)
    output = Path(args.output)

    gold = load_gold_bpm(db, require_locked=args.require_bpm_lock)
    analysis_rows = run_analysis(mixxx_test, root, args.max_files)

    output.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "path",
        "artist",
        "title",
        "bpm_raw",
        "bpm_selected",
        "level_multiplier",
        "level_confidence",
        "swing_pct",
        "ambiguous",
        "time_ms",
        "bpm_gold",
        "bpm_lock",
        "track_id",
    ]

    matched = 0
    with output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in analysis_rows:
            path = normalize_track_location(row.get("path", ""))
            meta = gold.get(path)
            if not meta:
                continue
            matched += 1
            writer.writerow(
                {
                    "path": path,
                    "artist": row.get("artist", ""),
                    "title": row.get("title", ""),
                    "bpm_raw": row.get("bpm_raw", ""),
                    "bpm_selected": row.get("bpm_selected", ""),
                    "level_multiplier": row.get("level_multiplier", ""),
                    "level_confidence": row.get("level_confidence", ""),
                    "swing_pct": row.get("swing_pct", ""),
                    "ambiguous": 1.0 if row.get("ambiguous", "no") == "yes" else 0.0,
                    "time_ms": row.get("time_ms", ""),
                    "bpm_gold": meta["bpm_gold"],
                    "bpm_lock": meta["bpm_lock"],
                    "track_id": meta["track_id"],
                }
            )

    print(f"Wrote {matched} matched rows to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
