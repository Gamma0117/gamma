#!/usr/bin/env python3
"""Whole-run section update latency from an `aurora --latency-log <file.csv>` record.

    python tools/latency_report.py <file.csv>

Prints the number of samples and their nearest-rank p50, p95 and maximum (milliseconds). The game keeps only a
window of recent completions, so whole-run percentiles come from this record. A record counts as a whole run only
if every row's latency is a finite number >= 0 (0 is fine) and it ends with the game's closing line and that line
agrees: as many rows as completions, none dropped. Otherwise the numbers are printed as INCOMPLETE and must not be
reported as the run's percentiles; rows with a bad latency are named and left out of them.

Exit codes: 0 complete record, 2 incomplete or unreadable record, 1 wrong arguments.
"""

import math
import re
import sys


def nearest_rank(sorted_values, fraction):
    index = math.ceil(fraction * len(sorted_values))
    return sorted_values[min(max(index, 1), len(sorted_values)) - 1]


def main(argv):
    if len(argv) != 2:
        print("usage: latency_report.py <file.csv>", file=sys.stderr)
        return 1
    try:
        with open(argv[1], encoding="utf-8") as stream:
            lines = stream.read().splitlines()
    except OSError as error:
        print(f"INCOMPLETE: cannot read {argv[1]}: {error}")
        return 2

    values = []
    rows = 0
    closing = None
    problems = []
    for number, line in enumerate(lines, 1):
        if line.startswith("#"):
            match = re.fullmatch(r"# end rows=(\d+) completed=(\d+) dropped=(\d+)", line)
            if match:
                if closing is not None:
                    problems.append(f"line {number}: a second closing line")
                closing = tuple(int(group) for group in match.groups())
            continue
        if line == "taken_ms,latency_ms,chunk_x,chunk_z,section":
            continue
        if closing is not None:
            problems.append(f"line {number}: a row after the closing line")
        rows += 1
        fields = line.split(",")
        try:
            if len(fields) != 5:
                raise ValueError("expected 5 fields")
            latency = float(fields[1])
            if not math.isfinite(latency) or latency < 0:
                raise ValueError(f"latency {fields[1]} is not a finite number >= 0")
            values.append(latency)
        except ValueError as error:
            problems.append(f"line {number}: {error}")

    if closing is None:
        problems.append("no closing line (the run did not end normally, or the file was cut)")
    else:
        closing_rows, completed, dropped = closing
        if closing_rows != rows:
            problems.append(f"the closing line says {closing_rows} rows, the file has {rows}")
        if completed != closing_rows:
            problems.append(f"{completed} completions but {closing_rows} rows")
        if dropped != 0:
            problems.append(f"{dropped} completions dropped before they were written")

    if values:
        ordered = sorted(values)
        summary = (f"{len(values)} samples: p50 {nearest_rank(ordered, 0.50):.1f} ms, "
                   f"p95 {nearest_rank(ordered, 0.95):.1f} ms, max {ordered[-1]:.1f} ms")
    else:
        summary = "0 samples"
    if problems:
        print(f"INCOMPLETE ({summary})")
        for problem in problems:
            print(f"  {problem}")
        return 2
    print(f"whole run: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
