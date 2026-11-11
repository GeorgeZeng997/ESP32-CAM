#!/usr/bin/env python3
"""Assemble recorded ESP32-CAM SD session frames (manifest.csv or frame_*.jpg) into an MP4 video."""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence


@dataclass(frozen=True)
class FrameRecord:
    frame_index: int
    filename: str
    uptime_ms: int
    size_bytes: int
    width: int
    height: int


def load_manifest(session_dir: Path) -> List[FrameRecord]:
    """Parse `<session_dir>/manifest.csv` and keep only rows whose JPEG file exists on disk."""
    manifest_path = session_dir / "manifest.csv"
    if not manifest_path.is_file():
        return []

    records: List[FrameRecord] = []
    with manifest_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            try:
                idx = int(row["frame_index"])
                fname = row["filename"].strip()
                uptime_ms = int(row["uptime_ms"])
                size_bytes = int(row["bytes"])
                width = int(row.get("width") or 0)
                height = int(row.get("height") or 0)
            except (KeyError, TypeError, ValueError):
                continue

            candidate = (session_dir / fname).resolve()
            if candidate.parent != session_dir.resolve() or not candidate.is_file():
                continue

            records.append(
                FrameRecord(
                    frame_index=idx,
                    filename=fname,
                    uptime_ms=uptime_ms,
                    size_bytes=size_bytes,
                    width=width,
                    height=height,
                )
            )

    records.sort(key=lambda r: (r.frame_index, r.uptime_ms))
    return records


def estimate_fps(records: Sequence[FrameRecord], default_fps: float = 5.0) -> float:
    """Compute effective capture FPS from manifest uptime deltas."""
    if len(records) < 2:
        return default_fps
    span_ms = records[-1].uptime_ms - records[0].uptime_ms
    if span_ms <= 0:
        return default_fps
    fps = ((len(records) - 1) * 1000.0) / float(span_ms)
    return max(0.1, min(60.0, fps))


def build_ffconcat_script(session_dir: Path, records: Sequence[FrameRecord], default_fps: float = 5.0) -> str:
    """Generate an ffmpeg concat demuxer script preserving per-frame capture timing."""
    if not records:
        raise ValueError("Cannot build ffconcat script from empty frame list")

    fallback_dt = 1.0 / max(0.1, default_fps)
    lines = ["ffconcat version 1.0"]

    for i, rec in enumerate(records):
        img_path = (session_dir / rec.filename).resolve().as_posix()
        escaped = img_path.replace("'", "'\\''")
        lines.append(f"file '{escaped}'")

        if i + 1 < len(records):
            delta_ms = records[i + 1].uptime_ms - rec.uptime_ms
            duration = (delta_ms / 1000.0) if delta_ms > 0 else fallback_dt
            duration = max(0.02, min(3600.0, duration))
        else:
            duration = fallback_dt

        lines.append(f"duration {duration:.4f}")

    # Repeat final file entry as required by ffmpeg concat demuxer to honor the last duration.
    last_escaped = (session_dir / records[-1].filename).resolve().as_posix().replace("'", "'\\''")
    lines.append(f"file '{last_escaped}'")
    return "\n".join(lines) + "\n"


def discover_unindexed_frames(session_dir: Path) -> List[FrameRecord]:
    """Fallback scanner when manifest.csv is absent (legacy run folders)."""
    frames = sorted(session_dir.glob("frame_*.jpg"))
    records: List[FrameRecord] = []
    for i, path in enumerate(frames):
        records.append(
            FrameRecord(
                frame_index=i,
                filename=path.name,
                uptime_ms=i * 200,
                size_bytes=path.stat().st_size,
                width=0,
                height=0,
            )
        )
    return records


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Convert an ESP32-CAM /video/run_XXXX folder to MP4 via ffmpeg.")
    parser.add_argument("session_dir", type=Path, help="Path to run_XXXX directory containing frame_*.jpg")
    parser.add_argument("-o", "--output", type=Path, default=None, help="Output MP4 path (default: <run_XXXX>.mp4)")
    parser.add_argument("--fps", type=float, default=None, help="Force constant frame rate instead of manifest timing")
    parser.add_argument("--crf", type=int, default=20, help="H.264 CRF quality (0..51, default: 20)")
    parser.add_argument("--dry-run", action="store_true", help="Print summary and ffmpeg command without encoding")
    args = parser.parse_args(argv)

    session_dir = args.session_dir.resolve()
    if not session_dir.is_dir():
        print(f"Error: session directory not found: {session_dir}", file=sys.stderr)
        return 1

    records = load_manifest(session_dir)
    used_manifest = bool(records)
    if not records:
        records = discover_unindexed_frames(session_dir)
    if not records:
        print(f"Error: no JPEG frames found in {session_dir}", file=sys.stderr)
        return 1

    output_path = (args.output or session_dir.with_suffix(".mp4")).resolve()
    avg_fps = args.fps if args.fps is not None else estimate_fps(records)
    print(
        f"Session: {session_dir.name} | frames={len(records)} | "
        f"source={'manifest.csv' if used_manifest else 'glob'} | est_fps={avg_fps:.2f}"
    )

    if args.dry_run:
        print(f"Dry run complete. Output target: {output_path}")
        return 0

    ffmpeg_bin = shutil.which("ffmpeg")
    if not ffmpeg_bin:
        print("Error: ffmpeg executable not found in PATH.", file=sys.stderr)
        return 2

    with tempfile.NamedTemporaryFile("w", suffix=".ffconcat", encoding="utf-8", delete=False) as tmp:
        concat_path = Path(tmp.name)
        if args.fps is not None:
            fixed_records = [
                FrameRecord(
                    frame_index=r.frame_index,
                    filename=r.filename,
                    uptime_ms=int(round(idx * (1000.0 / max(0.1, args.fps)))),
                    size_bytes=r.size_bytes,
                    width=r.width,
                    height=r.height,
                )
                for idx, r in enumerate(records)
            ]
            tmp.write(build_ffconcat_script(session_dir, fixed_records, default_fps=args.fps))
        else:
            tmp.write(build_ffconcat_script(session_dir, records, default_fps=avg_fps))

    try:
        cmd = [
            ffmpeg_bin,
            "-y",
            "-safe",
            "0",
            "-f",
            "concat",
            "-i",
            str(concat_path),
            "-vf",
            "pad=ceil(iw/2)*2:ceil(ih/2)*2",
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            str(max(0, min(51, args.crf))),
            str(output_path),
        ]
        subprocess.run(cmd, check=True)
    finally:
        concat_path.unlink(missing_ok=True)

    print(f"Wrote MP4: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
