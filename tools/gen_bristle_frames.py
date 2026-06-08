#!/usr/bin/env python3
"""Generate bristle sway frames from res/Duster.svg.

For each <line> inside a <g id="bristles-*"> group, shifts the top endpoint's
x coordinate by `displacement * (lineLength / maxLineLength)`. Taller bristles
sway more; the base where they meet the handle stays anchored.

Emits res/Duster_frame_0.svg ... res/Duster_frame_4.svg.
"""

import os
import xml.etree.ElementTree as ET

SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(REPO, "res", "Duster.svg")
OUT_TEMPLATE = os.path.join(REPO, "res", "Duster_frame_{}.svg")

DISPLACEMENTS_MM = [-2.4, -1.2, 0.0, 1.2, 2.4]
BRISTLE_GROUP_IDS = {"bristles-back", "bristles-main", "bristles-highlight"}


def collect_bristle_lines(root):
    out = []
    for g in root.iter(f"{{{SVG_NS}}}g"):
        if g.get("id") in BRISTLE_GROUP_IDS:
            out.extend(g.findall(f"{{{SVG_NS}}}line"))
    return out


def max_bristle_length(lines):
    return max(abs(float(l.get("y2")) - float(l.get("y1"))) for l in lines)


def shift_top(line, amount_mm, max_len):
    y1 = float(line.get("y1"))
    y2 = float(line.get("y2"))
    length = abs(y2 - y1)
    shift = amount_mm * (length / max_len)
    if y1 <= y2:
        line.set("x1", f"{float(line.get('x1')) + shift:.3f}")
    else:
        line.set("x2", f"{float(line.get('x2')) + shift:.3f}")


def write_frame(displacement, out_path):
    tree = ET.parse(SRC)
    root = tree.getroot()
    lines = collect_bristle_lines(root)
    if not lines:
        raise RuntimeError(
            f"No bristle <line> elements found in {SRC}. "
            f"Expected groups with id in {BRISTLE_GROUP_IDS}."
        )
    max_len = max_bristle_length(lines)
    for line in lines:
        shift_top(line, displacement, max_len)
    tree.write(out_path, xml_declaration=True, encoding="UTF-8")


def main():
    for i, d in enumerate(DISPLACEMENTS_MM):
        out = OUT_TEMPLATE.format(i)
        write_frame(d, out)
        print(f"wrote {os.path.relpath(out, REPO)} (top shift {d:+.2f} mm)")


if __name__ == "__main__":
    main()
