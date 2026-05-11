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
import tempfile
import time
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


def load_processed_paths(output: Path) -> set[str]:
    if not output.exists() or output.stat().st_size == 0:
        return set()

    processed: set[str] = set()
    with output.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            path = normalize_track_location(row.get("path", ""))
            if path:
                processed.add(path)
    return processed


def run_analysis(
    mixxx_test: Path,
    root: Path,
    max_files: int,
    verbose: bool,
    gold_paths: set[str] | None = None,
    skip_paths: set[str] | None = None,
    on_row=None,
) -> dict[str, int]:
    env = os.environ.copy()
    env["MIXXX_LAROCHE_ROOT"] = str(root)
    env["MIXXX_LAROCHE_MAX_FILES"] = str(max_files)
    env["MIXXX_LAROCHE_EMIT_CSV"] = "1"

    skip_paths = skip_paths or set()

    processed_file_list: Path | None = None
    if skip_paths:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", delete=False) as tmp:
            processed_file_list = Path(tmp.name)
            for p in sorted(skip_paths):
                tmp.write(f"{p}\n")
        env["MIXXX_LAROCHE_PROCESSED_PATHS_FILE"] = str(processed_file_list)

    cmd = [
        str(mixxx_test),
        "--gtest_filter=AnalyzerLarocheBalboaSmokeTest.AnalyzeBalboaMp3Collection",
        "--gtest_brief=1",
    ]

    start = time.monotonic()
    interactive = sys.stdout.isatty() and not verbose
    print(
        f"Running analyzer on {root} (max_files={max_files}, "
        f"already_processed={len(skip_paths)})..."
    )

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
        bufsize=1,
    )

    header: list[str] | None = None
    rows_emitted = 0
    matched_seen = 0
    unmatched_seen = 0

    assert proc.stdout is not None
    for raw_line in proc.stdout:
        line = raw_line.rstrip("\n")
        if line.startswith("CSV|"):
            values = line[4:].split("\t")
            if header is None:
                header = values
                continue
            if len(values) != len(header):
                continue
            row = dict(zip(header, values))
            rows_emitted += 1

            path = normalize_track_location(row.get("path", ""))
            if gold_paths is None or path in gold_paths:
                matched_seen += 1
            else:
                unmatched_seen += 1

            if on_row is not None:
                on_row(row)

            elapsed = max(1, int(time.monotonic() - start))
            analyzed = rows_emitted
            eta = ""
            if analyzed < max_files:
                rate = analyzed / elapsed
                if rate > 0:
                    eta = f", eta~{int((max_files - analyzed) / rate)}s"
            progress_line = (
                f"  progress: {elapsed}s elapsed, analyzed={analyzed}/{max_files}, "
                f"matched={matched_seen}, unmatched={unmatched_seen}{eta}"
            )
            if not verbose and interactive:
                print(progress_line, end="\r", flush=True)
            else:
                print(progress_line, flush=True)
            continue

        if verbose:
            print(line)

    return_code = proc.wait()
    if processed_file_list and processed_file_list.exists():
        processed_file_list.unlink()
    if return_code != 0:
        raise subprocess.CalledProcessError(return_code, cmd)

    if not verbose and interactive:
        print(" " * 120, end="\r")
    print(
        f"Analyzer finished in {int(time.monotonic() - start)}s "
        f"(rows_emitted={rows_emitted}, matched={matched_seen}, unmatched={unmatched_seen})"
    )

    if rows_emitted == 0:
        raise RuntimeError("No CSV rows were emitted by analyzer test")

    return {
        "rows_emitted": rows_emitted,
        "matched_seen": matched_seen,
        "unmatched_seen": unmatched_seen,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mixxx-test", default="./build/mixxx-test")
    parser.add_argument("--root", required=True)
    parser.add_argument("--db", default=str(Path.home() / ".mixxx" / "mixxxdb.sqlite"))
    parser.add_argument("--max-files", type=int, default=2000)
    parser.add_argument("--require-bpm-lock", action="store_true")
    parser.add_argument("--output", required=True)
    parser.add_argument("--verbose", action="store_true", help="Stream analyzer output while running")
    args = parser.parse_args()

    mixxx_test = Path(args.mixxx_test)
    root = Path(args.root)
    db = Path(args.db)
    output = Path(args.output)

    gold = load_gold_bpm(db, require_locked=args.require_bpm_lock)

    output.parent.mkdir(parents=True, exist_ok=True)
    processed_paths = load_processed_paths(output)

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
        "has_gold",
    ]

    file_exists = output.exists() and output.stat().st_size > 0
    mode = "a" if file_exists else "w"

    written = 0
    matched = 0
    unmatched = 0
    skipped_existing = 0

    with output.open(mode, newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        if not file_exists:
            writer.writeheader()
            f.flush()

        def on_row(row: dict[str, str]) -> None:
            nonlocal written, matched, unmatched, skipped_existing
            path = normalize_track_location(row.get("path", ""))
            if path in processed_paths:
                skipped_existing += 1
                return

            meta = gold.get(path)
            out_row = {
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
                "bpm_gold": "",
                "bpm_lock": "",
                "track_id": "",
                "has_gold": 0,
            }

            if meta:
                out_row["bpm_gold"] = meta["bpm_gold"]
                out_row["bpm_lock"] = meta["bpm_lock"]
                out_row["track_id"] = meta["track_id"]
                out_row["has_gold"] = 1
                matched += 1
            else:
                unmatched += 1

            writer.writerow(out_row)
            f.flush()
            processed_paths.add(path)
            written += 1

        run_stats = run_analysis(
            mixxx_test,
            root,
            args.max_files,
            args.verbose,
            gold_paths=set(gold.keys()),
            skip_paths=processed_paths,
            on_row=on_row,
        )

    print(
        f"Incremental write complete: wrote={written}, matched={matched}, "
        f"unmatched={unmatched}, skipped_existing={skipped_existing}, "
        f"rows_emitted={run_stats['rows_emitted']}, output={output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
