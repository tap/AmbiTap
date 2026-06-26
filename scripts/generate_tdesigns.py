#!/usr/bin/env python3
"""Generate the spherical t-design C++ header from the original Hardin-Sloane tables.

The point sets in `include/ambitap/math/geometry/tdesigns.h` are the "putatively
optimal" spherical t-designs computed by R. H. Hardin and N. J. A. Sloane and
published, free of charge, alongside their paper:

    R. H. Hardin and N. J. A. Sloane, "McLaren's Improved Snub Cube and Other
    New Spherical Designs in Three Dimensions," Discrete and Computational
    Geometry, 15 (1996), pp. 429-441.

The tables themselves are the authors' freely distributed research data, hosted
at Neil Sloane's site:

    http://neilsloane.com/sphdesigns/        (dimension 3: .../sphdesigns/dim3/)

This script fetches those original tables and emits the header. It deliberately
does NOT use Burkardt's `sphere_design_rule` repackaging at FSU, which is
distributed under the GNU LGPL; sourcing from Sloane's original free tables
keeps AmbiTap cleanly MIT-licensed (the coordinates are mathematical facts; the
generated header and the rest of AmbiTap are © Timothy Place under the MIT
License — see the repository LICENSE file).

Each remote file `des.3.<N>.<t>.txt` holds the 3N Cartesian coordinates of an
N-point spherical t-design, one number per line (x, y, z, x, y, z, ...).

Usage:
    python3 scripts/generate_tdesigns.py [--out PATH] [--cache DIR] [--base URL]

Defaults:
    --out    include/ambitap/math/geometry/tdesigns.h
    --cache  (none; files are fetched fresh each run)
    --base   http://neilsloane.com/sphdesigns/dim3

If --cache is given, downloaded tables are stored there and reused on later runs
so the header can be regenerated offline.

Copyright 2025-2026 Timothy Place. Distributed under the MIT License.
"""

import argparse
import os
import sys
import urllib.request
from math import sqrt

# Mapping used by AmbiTap. Each entry is (t, N): the t-design strength and the
# number of points in Hardin & Sloane's design for that strength. ALLRAD at
# ambisonics order L wants a design with t >= 2L + 1; AmbiTap selects the
# smallest tabulated t that satisfies that bound (see tdesign_for_order below).
DESIGNS = [
    (4, 14),
    (6, 26),
    (8, 36),
    (10, 60),
    (12, 84),
    (14, 108),
    (16, 144),
    (18, 180),
    (20, 216),
    (21, 240),
]

DEFAULT_BASE = "http://neilsloane.com/sphdesigns/dim3"
DEFAULT_OUT = os.path.join("include", "ambitap", "math", "geometry", "tdesigns.h")


def candidate_names(t, n):
    """Filename spellings to try, most-likely first.

    Sloane's directory has used a few conventions over the years (zero-padded
    point counts, optional .txt extension), so we probe several rather than
    hard-coding one.
    """
    names = []
    for nn in (str(n), f"{n:03d}"):
        for ext in (".txt", ""):
            names.append(f"des.3.{nn}.{t}{ext}")
    # de-duplicate while preserving order
    seen = set()
    out = []
    for name in names:
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out


def fetch_table(t, n, base, cache):
    """Return the raw text of the des.3.N.t table, using cache if available."""
    if cache:
        cached = os.path.join(cache, f"des.3.{n}.{t}.txt")
        if os.path.exists(cached):
            with open(cached, "r") as fh:
                return fh.read()

    last_err = None
    for name in candidate_names(t, n):
        url = f"{base}/{name}"
        try:
            with urllib.request.urlopen(url, timeout=60) as resp:
                text = resp.read().decode("ascii", errors="strict")
        except Exception as exc:  # noqa: BLE001 - report and try the next spelling
            last_err = exc
            continue
        if cache:
            os.makedirs(cache, exist_ok=True)
            with open(os.path.join(cache, f"des.3.{n}.{t}.txt"), "w") as fh:
                fh.write(text)
        return text

    raise RuntimeError(
        f"could not fetch the t={t}, N={n} design from {base} "
        f"(tried {', '.join(candidate_names(t, n))}); last error: {last_err}"
    )


def parse_points(text, n):
    """Parse 3N whitespace-separated floats into N unit (x, y, z) triples."""
    values = [float(tok) for tok in text.split()]
    if len(values) != 3 * n:
        raise ValueError(
            f"expected {3 * n} coordinates for a {n}-point design, got {len(values)}"
        )
    points = []
    for i in range(n):
        x, y, z = values[3 * i], values[3 * i + 1], values[3 * i + 2]
        norm = sqrt(x * x + y * y + z * z)
        if norm == 0.0:
            raise ValueError("degenerate (zero-length) point in design")
        points.append((x / norm, y / norm, z / norm))
    return points


def fmt(v):
    """Format one coordinate to match the header's existing float style."""
    return f"{v: .10e}f"


def emit_header(designs_points):
    """Build the full tdesigns.h text from {t: [points]} data."""
    lines = []
    lines.append("/// AmbiTap: target-independent ambisonics library")
    lines.append("/// T-design point sets for ALLRAD virtual loudspeaker layouts.")
    lines.append("/// Auto-generated by scripts/generate_tdesigns.py from the original,")
    lines.append("/// freely-distributed Hardin-Sloane spherical t-design tables:")
    lines.append("///   R. H. Hardin and N. J. A. Sloane, \"McLaren's Improved Snub Cube and")
    lines.append("///   Other New Spherical Designs in Three Dimensions,\" Discrete and")
    lines.append("///   Computational Geometry, 15 (1996), pp. 429-441.")
    lines.append("///   http://neilsloane.com/sphdesigns/")
    lines.append("/// The coordinates are mathematical facts from that catalogue; this header")
    lines.append("/// and the rest of AmbiTap are MIT-licensed (see LICENSE).")
    lines.append("/// Cartesian (x, y, z) on the unit sphere. For ALLRAD at order N use t >= 2*N + 1.")
    lines.append("/// Timothy Place")
    lines.append("/// Copyright 2025-2026 Timothy Place.")
    lines.append("")
    lines.append("#ifndef AMBITAP_MATH_TDESIGNS_H")
    lines.append("#define AMBITAP_MATH_TDESIGNS_H")
    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("")
    lines.append("namespace ambitap {")
    lines.append("")

    for t, n in DESIGNS:
        points = designs_points[t]
        lines.append(f"// T-design order {t}, {n} points")
        lines.append(f"constexpr float tdesign_{t}_data[][3] = {{")
        for i, (x, y, z) in enumerate(points):
            sep = "," if i + 1 < len(points) else ""
            lines.append(f"    {{{fmt(x)}, {fmt(y)}, {fmt(z)}}}{sep}")
        lines.append("};")
        lines.append(f"constexpr size_t tdesign_{t}_count = {n};")
        lines.append("")

    lines.append("// Get the appropriate t-design for a given ambisonics order.")
    lines.append("// Returns a pointer to the data and sets count.")
    lines.append("inline const float (*tdesign_for_order(int order, size_t& count))[3] {")
    lines.append("    int t_needed = 2 * order + 1;")
    branches = [t for t, _ in DESIGNS]
    for idx, t in enumerate(branches):
        kw = "if" if idx == 0 else "} else if"
        lines.append(f"    {kw} (t_needed <= {t}) {{")
        lines.append(f"        count = tdesign_{t}_count;")
        lines.append(f"        return tdesign_{t}_data;")
    last = branches[-1]
    lines.append("    } else {")
    lines.append(f"        count = tdesign_{last}_count;")
    lines.append(f"        return tdesign_{last}_data;")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace ambitap")
    lines.append("")
    lines.append("#endif // AMBITAP_MATH_TDESIGNS_H")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=DEFAULT_OUT, help="output header path")
    parser.add_argument("--cache", default=None, help="directory to cache downloaded tables")
    parser.add_argument("--base", default=DEFAULT_BASE, help="base URL of the dim-3 tables")
    args = parser.parse_args(argv)

    designs_points = {}
    for t, n in DESIGNS:
        print(f"fetching t={t}, N={n} ...", file=sys.stderr)
        text = fetch_table(t, n, args.base.rstrip("/"), args.cache)
        designs_points[t] = parse_points(text, n)

    header = emit_header(designs_points)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as fh:
        fh.write(header)
    print(f"wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
