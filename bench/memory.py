#!/usr/bin/env python3

import argparse
import csv
import json
import math
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path


MIB = 1024 * 1024


def settings(quick):
    if quick:
        bloom = [(2, MIB), (6, MIB), (10, MIB)]
        memtable = [(10, MIB // 2), (10, MIB), (10, 2 * MIB)]
    else:
        bloom = [(2, 16 * MIB), (6, 16 * MIB), (10, 16 * MIB), (14, 16 * MIB)]
        memtable = [(10, 4 * MIB), (10, 8 * MIB), (10, 16 * MIB), (10, 32 * MIB)]

    result = []
    for bits, memtable_size in bloom:
        result.append({"variable": "bloom", "setting": bits, "bits": bits, "memtable": memtable_size})
    for bits, memtable_size in memtable:
        result.append({"variable": "memtable", "setting": memtable_size / MIB, "bits": bits, "memtable": memtable_size})
    return result


def run(binary, db_path, result_path, config, args):
    command = [
        str(binary),
        "--db", str(db_path),
        "--operations", str(args.operations),
        "--warmup-operations", str(args.warmup),
        "--keys", str(args.keys),
        "--reads", "95",
        "--writes", "4",
        "--deletes", "1",
        "--distribution", "zipfian",
        "--zipf-theta", "0.99",
        "--value-size", str(args.value_size),
        "--seed", str(args.seed),
        "--sync-mode", "off",
        "--memory-budget", str(args.budget),
        "--memtable-size", str(config["memtable"]),
        "--bloom-bits-per-key", str(config["bits"]),
        "--drop-page-cache",
        "--output", str(result_path),
        "--format", "json",
    ]
    subprocess.run(command, check=True)
    with result_path.open() as file:
        return json.load(file)


def write_row(path, row, write_header):
    fields = ["variable", "setting", "repeat"] + [key for key in row if key not in {"variable", "setting", "repeat"}]
    with path.open("a", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields, lineterminator="\n")
        if write_header:
            writer.writeheader()
        writer.writerow(row)


def values(rows, variable, metric, scale):
    groups = {}
    for row in rows:
        if row["variable"] != variable:
            continue
        setting = float(row["setting"])
        groups.setdefault(setting, []).append(float(row[metric]) * scale)
    return [(setting, points, statistics.median(points)) for setting, points in sorted(groups.items())]


def panel(svg, groups, left, top, width, height, title, x_label, y_min, y_max, color, decimals):
    if not groups:
        return
    count = len(groups)

    def x_position(index):
        if count == 1:
            return left + width / 2
        return left + index * width / (count - 1)

    def y_position(value):
        return top + height - (value - y_min) * height / (y_max - y_min)

    svg.append(f'<rect class="panel" x="{left - 38}" y="{top - 48}" width="{width + 76}" height="{height + 108}" rx="14"/>')
    svg.append(f'<text class="panel-title" x="{left + width / 2}" y="{top - 18}" text-anchor="middle">{title}</text>')
    svg.append(f'<text class="axis-label" x="{left + width / 2}" y="{top + height + 56}" text-anchor="middle">{x_label}</text>')

    for i in range(6):
        value = y_min + (y_max - y_min) * i / 5
        y = y_position(value)
        svg.append(f'<line class="grid" x1="{left}" y1="{y:.2f}" x2="{left + width}" y2="{y:.2f}"/>')
        svg.append(f'<text class="tick" x="{left - 14}" y="{y + 5:.2f}" text-anchor="end">{value:.{decimals}f}</text>')

    svg.append(f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + height}"/>')
    svg.append(f'<line class="axis" x1="{left}" y1="{top + height}" x2="{left + width}" y2="{top + height}"/>')

    coordinates = []
    for index, (setting, points, median) in enumerate(groups):
        x = x_position(index)
        coordinates.append(f"{x:.2f},{y_position(median):.2f}")
        svg.append(f'<text class="tick" x="{x:.2f}" y="{top + height + 26}" text-anchor="middle">{setting:g}</text>')
        for point_index, point in enumerate(points):
            offset = (point_index - (len(points) - 1) / 2) * 9
            svg.append(f'<circle cx="{x + offset:.2f}" cy="{y_position(point):.2f}" r="5" fill="{color}" opacity="0.22"/>')

    svg.append(f'<polyline points="{" ".join(coordinates)}" fill="none" stroke="{color}" stroke-width="4" stroke-linejoin="round" stroke-linecap="round"/>')
    for index, (_, _, median) in enumerate(groups):
        x = x_position(index)
        y = y_position(median)
        label_x = x
        if index == 0:
            label_x += 18
        elif index == len(groups) - 1:
            label_x -= 18
        svg.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="7" fill="white" stroke="{color}" stroke-width="4"/>')
        svg.append(f'<text class="value" x="{label_x:.2f}" y="{y - 16:.2f}" text-anchor="middle">{median:.{decimals}f}</text>')


def plot(rows, metric, title, output, scale=1.0, decimals=1):
    bloom = values(rows, "bloom", metric, scale)
    memtable = values(rows, "memtable", metric, scale)
    all_values = [point for _, points, _ in bloom + memtable for point in points]
    y_min = min(all_values)
    y_max = max(all_values)
    padding = (y_max - y_min) * 0.18
    if padding == 0:
        padding = max(abs(y_min) * 0.1, 1)
    y_min = math.floor((y_min - padding) * 10) / 10
    y_max = math.ceil((y_max + padding) * 10) / 10

    svg = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="620" viewBox="0 0 1400 620">',
        '<style>',
        'text { font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; fill: #172033; }',
        '.title { font-size: 28px; font-weight: 700; }',
        '.subtitle { font-size: 16px; fill: #64748b; }',
        '.panel-title { font-size: 20px; font-weight: 650; }',
        '.axis-label { font-size: 17px; font-weight: 600; fill: #334155; }',
        '.tick { font-size: 15px; fill: #475569; }',
        '.value { font-size: 15px; font-weight: 700; }',
        '.panel { fill: white; stroke: #e2e8f0; stroke-width: 1.5; }',
        '.grid { stroke: #e8edf3; stroke-width: 1; }',
        '.axis { stroke: #94a3b8; stroke-width: 1.5; }',
        '</style>',
        '<rect width="100%" height="100%" fill="#f5f7fb"/>',
        f'<text class="title" x="700" y="42" text-anchor="middle">{title}</text>',
        '<text class="subtitle" x="700" y="70" text-anchor="middle">Median of three runs, 64 MiB allocation budget</text>',
        '<text class="subtitle" x="700" y="602" text-anchor="middle">Faded points show individual runs</text>'
    ]
    panel(svg, bloom, 125, 145, 455, 340, "Bloom filter", "Bits per key", y_min, y_max, "#0f766e", decimals)
    panel(svg, memtable, 820, 145, 455, 340, "MemTable", "Size (MiB)", y_min, y_max, "#e05a33", decimals)
    svg.append("</svg>")
    output.write_text("\n".join(svg) + "\n")


def make_plots(csv_path, figure_path):
    with csv_path.open(newline="") as file:
        rows = list(csv.DictReader(file))
    figure_path.mkdir(parents=True, exist_ok=True)
    plot(rows, "read_p99_ns", "Read p99 latency (µs)", figure_path / "p99.svg", 1 / 1000)
    plot(rows, "throughput_ops_per_second", "Throughput (k ops/s)", figure_path / "throughput.svg", 1 / 1000)
    plot(rows, "cache_hit_rate", "Block cache hit rate (%)", figure_path / "cache.svg", 100)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build/lsmkv_bench"))
    parser.add_argument("--output", type=Path, default=Path("bench/memory.csv"))
    parser.add_argument("--figures", type=Path, default=Path("figures"))
    parser.add_argument("--budget", type=int, default=64 * MIB)
    parser.add_argument("--keys", type=int, default=500000)
    parser.add_argument("--operations", type=int, default=500000)
    parser.add_argument("--warmup", type=int, default=50000)
    parser.add_argument("--value-size", type=int, default=256)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--plot-only", action="store_true")
    args = parser.parse_args()

    if args.plot_only:
        make_plots(args.output, args.figures)
        return
    if args.quick:
        args.budget = 4 * MIB
        args.keys = 5000
        args.operations = 10000
        args.warmup = 1000
        args.value_size = 128
        args.repeats = 1

    args.binary = args.binary.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    work = Path(tempfile.mkdtemp(prefix="lsmkv-memory-", dir="/tmp"))
    result_path = work / "result.json"
    first_row = True
    try:
        for config in settings(args.quick):
            for repeat in range(1, args.repeats + 1):
                db_path = work / f'{config["variable"]}-{config["setting"]}-{repeat}'
                print(f'{config["variable"]} {config["setting"]}, repeat {repeat}', flush=True)
                result = run(args.binary, db_path, result_path, config, args)
                result["variable"] = config["variable"]
                result["setting"] = config["setting"]
                result["repeat"] = repeat
                write_row(args.output, result, first_row)
                first_row = False
                shutil.rmtree(db_path)
    finally:
        shutil.rmtree(work)
    make_plots(args.output, args.figures)


if __name__ == "__main__":
    main()
