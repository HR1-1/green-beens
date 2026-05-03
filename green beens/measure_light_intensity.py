#!/usr/bin/env python3
"""
Batch image light intensity measurement tool.

Measures relative light intensity by converting images to grayscale and
computing mean gray value (0-255), similar to ImageJ's Mean Gray Value.

Why one LED color can show a higher mean gray than another
----------------------------------------------------------
``convert("L")`` uses luminance weights close to ITU-R BT.601
(roughly 0.299 R + 0.587 G + 0.114 B). A mostly-red patch still gets
non-trivial luminance from the red channel; blue-heavy mixes often read
lower in grayscale even when the LED looks bright to the eye. Phone camera
spectral sensitivity and JPEG processing also differ by color. Higher mean
gray does **not** automatically mean higher optical power — use the same
camera setup and then CAL on the MCU to match levels.

Default input is the ``photos`` folder (``white`` / ``red`` / ``purple``).
Do not leave miscellaneous shots inside ``photos`` if you only want those
three groups analyzed; move extras elsewhere.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from statistics import mean
from collections import defaultdict
from typing import Iterable

from PIL import Image, ImageStat


SUPPORTED_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}


@dataclass
class Roi:
    x: int
    y: int
    w: int
    h: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Measure relative light intensity from images using mean grayscale value."
        )
    )
    parser.add_argument(
        "--input-dir",
        default="photos",
        help="Directory containing images (default: photos).",
    )
    parser.add_argument(
        "--output",
        default="light_intensity_results.csv",
        help="Output CSV file path (default: light_intensity_results.csv).",
    )
    parser.add_argument(
        "--roi",
        nargs=4,
        type=int,
        metavar=("X", "Y", "W", "H"),
        help="Optional ROI rectangle for all images: X Y W H.",
    )
    parser.add_argument(
        "--top-only",
        action="store_true",
        help="Only scan images in input folder (disable subfolder scan).",
    )
    return parser.parse_args()


def iter_images(input_dir: Path, recursive: bool) -> Iterable[Path]:
    iterator = input_dir.rglob("*") if recursive else input_dir.glob("*")
    for path in iterator:
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS:
            yield path


def validate_roi(roi: Roi, img_w: int, img_h: int) -> None:
    if roi.w <= 0 or roi.h <= 0:
        raise ValueError("ROI width and height must be > 0.")
    if roi.x < 0 or roi.y < 0:
        raise ValueError("ROI x and y must be >= 0.")
    if roi.x + roi.w > img_w or roi.y + roi.h > img_h:
        raise ValueError(
            f"ROI ({roi.x},{roi.y},{roi.w},{roi.h}) exceeds image bounds ({img_w}x{img_h})."
        )


def measure_image(path: Path, roi: Roi | None, root_dir: Path) -> dict:
    with Image.open(path) as img:
        gray = img.convert("L")
        width, height = gray.size

        measured = gray
        area = f"full({width}x{height})"
        if roi is not None:
            validate_roi(roi, width, height)
            measured = gray.crop((roi.x, roi.y, roi.x + roi.w, roi.y + roi.h))
            area = f"roi({roi.x},{roi.y},{roi.w},{roi.h})"

        stat = ImageStat.Stat(measured)
        mean_gray = float(stat.mean[0])
        min_gray, max_gray = measured.getextrema()

    try:
        relative_path = str(path.resolve().relative_to(root_dir.resolve()))
        relative_folder = str(Path(relative_path).parent)
    except ValueError:
        relative_path = path.name
        relative_folder = "."

    if relative_folder == "":
        relative_folder = "."

    return {
        "file_name": path.name,
        "file_path": str(path.resolve()),
        "relative_path": relative_path,
        "relative_folder": relative_folder,
        "width": width,
        "height": height,
        "measured_area": area,
        "mean_gray_value": round(mean_gray, 3),
        "min_gray_value": min_gray,
        "max_gray_value": max_gray,
    }


def write_csv(rows: list[dict], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    columns = [
        "file_name",
        "file_path",
        "relative_path",
        "relative_folder",
        "width",
        "height",
        "measured_area",
        "mean_gray_value",
        "min_gray_value",
        "max_gray_value",
    ]
    with output_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_path = Path(args.output).resolve()
    roi = Roi(*args.roi) if args.roi else None

    if not input_dir.exists() or not input_dir.is_dir():
        print(f"[ERROR] Input directory not found: {input_dir}")
        return 1

    recursive_scan = not args.top_only
    images = sorted(iter_images(input_dir, recursive_scan))
    if not images:
        print(f"[WARN] No supported image files found in: {input_dir}")
        print(
            "[TIP] Add images under photos/white, photos/red, photos/purple "
            "(or pass --input-dir)."
        )
        print(f"Supported formats: {', '.join(sorted(SUPPORTED_EXTENSIONS))}")
        return 0

    rows: list[dict] = []
    for path in images:
        try:
            rows.append(measure_image(path, roi, input_dir))
        except Exception as exc:  # keep batch running if one file fails
            print(f"[SKIP] {path.name}: {exc}")

    if not rows:
        print("[ERROR] No images were successfully measured.")
        return 1

    write_csv(rows, output_path)

    means = [r["mean_gray_value"] for r in rows]
    print(f"[OK] Measured {len(rows)} image(s).")
    print(f"[OK] Saved CSV: {output_path}")
    print(f"[INFO] Mean Gray Value range: {min(means):.3f} ~ {max(means):.3f}")
    print(f"[INFO] Average Mean Gray Value: {mean(means):.3f}")
    group_values: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        group_values[row["relative_folder"]].append(float(row["mean_gray_value"]))
    print("[INFO] Folder summary:")
    for folder in sorted(group_values):
        vals = group_values[folder]
        print(
            f"  - {folder}: n={len(vals)}, avg={mean(vals):.3f}, "
            f"min={min(vals):.3f}, max={max(vals):.3f}"
        )

    print(
        "[NOTE] Mean gray is approx. luminance (not equal-energy brightness). "
        "Red scenes often read higher than blue-heavy mixes on typical sensors. "
        "Firmware CAL: scales PWM toward fixed mean gray target 70 "
        "(CAL_TARGET_GRAY_MEAN in green_beans_experiment.ino)."
    )

    # Suggested CAL from photos/white, photos/red, photos/purple folder averages
    leaf_avgs: dict[str, float] = {}
    for folder, vals in group_values.items():
        leaf = Path(folder).name.lower()
        if leaf in ("white", "red", "purple"):
            leaf_avgs[leaf] = mean(vals)
    if len(leaf_avgs) == 3:
        w, r, p = leaf_avgs["white"], leaf_avgs["red"], leaf_avgs["purple"]
        print(
            "[INFO] Suggested Serial (folder averages W,R,P; MCU target mean ~70): "
            f"CAL:{w:.3f},{r:.3f},{p:.3f}"
        )
    elif leaf_avgs:
        missing = {"white", "red", "purple"} - set(leaf_avgs)
        print(
            f"[WARN] Need folders named white, red, purple under input "
            f"for CAL hint (missing: {', '.join(sorted(missing))})."
        )

    print("[TIP] Lower variance means your intensity control is more consistent.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
