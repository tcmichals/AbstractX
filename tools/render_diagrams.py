#!/usr/bin/env python3
"""Render *.diagram.yaml block diagrams to SVG; flag ASCII box art in Markdown.

# @impl [SPEC-AUDIT-16] docs/SPECTRACE.md

Usage:
  python3 tools/render_diagrams.py                 # render every *.diagram.yaml in the repo
  python3 tools/render_diagrams.py a.diagram.yaml  # render one (or several)
  python3 tools/render_diagrams.py --check         # stale SVGs + ASCII box art -> exit 1

Diagram source (all coordinates in grid cells; fractions allowed):

  title: Shown at the top and as <title>
  desc: Screen-reader description (<desc>)
  grid: {col: 200, row: 90, gap_x: 110, gap_y: 60}  # optional
  nodes:
    - id: e906
      label: XuanTie E906
      text: ["bare metal", "`run_imu()`"]   # optional lines; `x` renders monospace
      at: [0, 1]                            # column, row
      span: [1, 1]                          # optional width, height in cells
                                            # (height = h*row + (h-1)*gap_y, so 0.5 < half a cell)
      kind: default | accent | hw | mem | soft | muted | warn
  groups:                                   # drawn behind; may contain earlier groups
    - id: soc
      label: Allwinner T527
      contains: [e906, a55]
      kind: muted
  edges:
    - {from: a, to: b, label: RPMsg, dashed: false, both: false,
       arrow: true,   # false: plain line (wire, bus) with no arrowhead
       route: auto | hv | vh | hvh | vhv, kind: default}
  notes: ["one line per note under the drawing"]
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape

import yaml

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "ascii_diagram_baseline.txt"

BG = "#0f1a30"
TEXT = "#f2f6ff"
SUB = "#aebcda"
EDGE = "#9fb3d9"
KINDS = {  # stroke, fill
    "default": ("#42d8f5", "#14233f"),
    "accent": ("#b79cff", "#1d1a3f"),
    "hw": ("#f5b942", "#2a2416"),
    "mem": ("#4ade80", "#122b22"),
    "soft": ("#7dd3fc", "#16263b"),
    "muted": ("#64748b", "#151f33"),
    "warn": ("#f87171", "#2d1720"),
}
SANS = "Inter, 'Segoe UI', Helvetica, Arial, sans-serif"
MONO = "'JetBrains Mono', 'DejaVu Sans Mono', Menlo, Consolas, monospace"
LABEL_PX, SUB_PX, EDGE_PX, GROUP_PX = 17, 13, 12, 12
PAD = 30


class DiagramError(Exception):
    pass


@dataclass
class Box:
    x: float
    y: float
    w: float
    h: float

    @property
    def cx(self) -> float:
        return self.x + self.w / 2

    @property
    def cy(self) -> float:
        return self.y + self.h / 2


def text_width(s: str, px: float, mono: bool = False) -> float:
    parts = s.split("`")
    if len(parts) > 1 and len(parts) % 2 == 1:
        return sum(text_width(seg, px, k % 2 == 1) for k, seg in enumerate(parts))
    if mono:
        return len(s) * px * 0.61
    wide = sum(1 for c in s if c.isupper() or c in "MWmw@%")
    return (len(s) - wide) * px * 0.53 + wide * px * 0.68


def f(v: float) -> str:
    r = round(v, 1)
    return str(int(r)) if r == int(r) else str(r)


def kind_of(item: dict, where: str) -> tuple[str, str]:
    k = item.get("kind", "default")
    if k not in KINDS:
        raise DiagramError(f"{where}: unknown kind '{k}' (use {', '.join(KINDS)})")
    return KINDS[k]


def lines_of(v) -> list[str]:
    if v is None:
        return []
    return [str(x) for x in v] if isinstance(v, list) else str(v).split("\n")


def text_el(x: float, y: float, s: str, px: int, fill: str, anchor: str = "middle",
            weight: int = 400, spacing: float = 0) -> str:
    """One line of text; `code` spans render monospace."""
    extra = f' letter-spacing="{f(spacing)}"' if spacing else ""
    parts = s.split("`")
    if len(parts) % 2 == 0:  # unbalanced backtick: show it literally
        parts = [s]
    if len(parts) == 3 and not parts[0] and not parts[2]:
        inner = escape(parts[1])
        fam = MONO
    else:
        inner = "".join(escape(seg) if k % 2 == 0 else
                        f'<tspan font-family="{MONO}">{escape(seg)}</tspan>'
                        for k, seg in enumerate(parts) if seg)
        fam = SANS
    return (f'<text x="{f(x)}" y="{f(y)}" font-family="{fam}" font-size="{px}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{extra}>'
            f"{inner}</text>")


def render(spec: dict, name: str = "diagram") -> str:
    grid = {"col": 200, "row": 90, "gap_x": 110, "gap_y": 60, **(spec.get("grid") or {})}
    cw, rh, gx, gy = grid["col"], grid["row"], grid["gap_x"], grid["gap_y"]
    title = str(spec.get("title", ""))
    desc = str(spec.get("desc", title))
    boxes: dict[str, Box] = {}
    body: list[str] = []
    problems: list[str] = []

    nodes = spec.get("nodes") or []
    node_ids = {str(n.get("id", "")) for n in nodes}
    for n in nodes:
        nid = str(n.get("id", ""))
        if not nid or nid in boxes:
            raise DiagramError(f"{name}: node id missing or duplicated: '{nid}'")
        col, row = n["at"]
        sw, sh = n.get("span", [1, 1])
        boxes[nid] = Box(col * (cw + gx), row * (rh + gy),
                         sw * cw + (sw - 1) * gx, sh * rh + (sh - 1) * gy)

    group_boxes: list[tuple[dict, Box]] = []
    for g in spec.get("groups") or []:
        members = [boxes[m] for m in g.get("contains", []) if m in boxes]
        missing = [m for m in g.get("contains", []) if m not in boxes]
        if missing or not members:
            raise DiagramError(f"{name}: group '{g.get('label')}' names unknown ids {missing}")
        pad = 18
        top = 30 if g.get("label") else pad
        x0 = min(b.x for b in members) - pad
        y0 = min(b.y for b in members) - top
        x1 = max(b.x + b.w for b in members) + pad
        y1 = max(b.y + b.h for b in members) + pad
        gb = Box(x0, y0, x1 - x0, y1 - y0)
        if g.get("id"):
            boxes[str(g["id"])] = gb
        group_boxes.append((g, gb))

    # canvas bounds
    all_boxes = list(boxes.values())
    min_x = min(b.x for b in all_boxes) if all_boxes else 0
    min_y = min(b.y for b in all_boxes) if all_boxes else 0
    max_x = max(b.x + b.w for b in all_boxes) if all_boxes else 400
    max_y = max(b.y + b.h for b in all_boxes) if all_boxes else 100
    head = 64 if title else 0
    ox, oy = PAD - min_x, PAD + head - min_y
    notes = lines_of(spec.get("notes"))
    width = max_x - min_x + 2 * PAD
    if title:
        width = max(width, text_width(title, 22) + 2 * PAD)
    for s in notes:
        width = max(width, text_width(s, SUB_PX) + 2 * PAD)
    height = max_y - min_y + 2 * PAD + head + (len(notes) * 20 + 10 if notes else 0)

    def at(b: Box) -> Box:
        return Box(b.x + ox, b.y + oy, b.w, b.h)

    groups_svg: list[str] = []
    for g, gb in group_boxes:
        stroke, _ = kind_of(g, f"{name}: group")
        b = at(gb)
        groups_svg.append(f'<rect x="{f(b.x)}" y="{f(b.y)}" width="{f(b.w)}" height="{f(b.h)}" '
                    f'rx="16" fill="{stroke}" fill-opacity="0.05" stroke="{stroke}" '
                    f'stroke-opacity="0.7" stroke-width="1.5" stroke-dasharray="6 5"/>')
        if g.get("label"):
            lab = str(g["label"]).upper()
            groups_svg.append(text_el(b.x + 14, b.y + 20, lab, GROUP_PX, stroke, "start", 700, 1.2))
            if text_width(lab, GROUP_PX) * 1.12 + 28 > b.w:
                problems.append(f"group label '{g['label']}' wider than the group")

    edge_svg: list[str] = []
    label_svg: list[str] = []
    for i, e in enumerate(spec.get("edges") or []):
        if isinstance(e, list):
            e = {"from": e[0], "to": e[1], **({"label": e[2]} if len(e) > 2 else {})}
        a, b = e.get("from"), e.get("to")
        if a not in boxes or b not in boxes:
            raise DiagramError(f"{name}: edge {i} names unknown node '{a if a not in boxes else b}'")
        pts = route(at(boxes[a]), at(boxes[b]), e.get("route", "auto"))
        for nid in node_ids - {a, b}:
            if any(seg_hits(p0, p1, at(boxes[nid])) for p0, p1 in zip(pts, pts[1:])):
                problems.append(f"edge {a}->{b} runs through node '{nid}' (move it or set route:)")
        stroke = KINDS[e["kind"]][0] if e.get("kind") in KINDS and e.get("kind") != "default" else EDGE
        mid = f"a{stroke[1:]}"
        d = "M" + " L".join(f"{f(x)} {f(y)}" for x, y in pts)
        dash = ' stroke-dasharray="7 5"' if e.get("dashed") else ""
        start = f' marker-start="url(#{mid}s)"' if e.get("both") else ""
        end = f' marker-end="url(#{mid})"' if e.get("arrow", True) else ""
        edge_svg.append(f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="2"'
                        f'{dash}{end}{start}/>')
        if e.get("label"):
            lx, ly, horizontal = label_point(pts)
            lines = lines_of(e["label"])
            w = max(text_width(s, EDGE_PX) for s in lines) + 12
            h = len(lines) * 15 + 6
            if horizontal:  # sit above the line, not on it
                ly -= h / 2 + 3
            lb = Box(lx - w / 2, ly - h / 2, w, h)
            for nid in node_ids:
                if overlaps(lb, at(boxes[nid])):
                    problems.append(f"label '{lines[0]}' on edge {a}->{b} covers node '{nid}' "
                                    "(widen grid gap_x/gap_y or shorten it)")
            label_svg.append(f'<rect x="{f(lx - w / 2)}" y="{f(ly - h / 2)}" width="{f(w)}" '
                             f'height="{f(h)}" rx="5" fill="{BG}" fill-opacity="0.92"/>')
            for j, s in enumerate(lines):
                label_svg.append(text_el(lx, ly - h / 2 + 15 + j * 15, s, EDGE_PX, SUB))

    for n in nodes:
        stroke, fill = kind_of(n, f"{name}: node '{n['id']}'")
        b = at(boxes[str(n["id"])])
        body.append(f'<rect x="{f(b.x)}" y="{f(b.y)}" width="{f(b.w)}" height="{f(b.h)}" '
                    f'rx="12" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
        label = lines_of(n.get("label", n["id"]))
        sub = lines_of(n.get("text"))
        block = len(label) * 21 + len(sub) * 17
        y = b.cy - block / 2 + 15
        for s in label:
            body.append(text_el(b.cx, y, s, LABEL_PX, TEXT, weight=700))
            if text_width(s, LABEL_PX) * 1.05 + 16 > b.w:
                problems.append(f"node '{n['id']}' label '{s}' wider than its box")
            y += 21
        for s in sub:
            body.append(text_el(b.cx, y, s, SUB_PX, SUB))
            if text_width(s, SUB_PX) + 16 > b.w:
                problems.append(f"node '{n['id']}' text '{s}' wider than its box")
            y += 17
        if block + 12 > b.h:
            problems.append(f"node '{n['id']}' text taller than its box")

    if problems:
        raise DiagramError(f"{name}: " + "; ".join(problems))

    markers = []
    for color in sorted({EDGE, *(v[0] for v in KINDS.values())}):
        mid = f"a{color[1:]}"
        markers.append(f'<marker id="{mid}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" '
                       f'markerHeight="8" orient="auto"><path d="M0 0 L10 5 L0 10 Z" fill="{color}"/></marker>')
        markers.append(f'<marker id="{mid}s" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="8" '
                       f'markerHeight="8" orient="auto"><path d="M10 0 L0 5 L10 10 Z" fill="{color}"/></marker>')

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {f(width)} {f(height)}" '
           f'width="{f(width)}" height="{f(height)}" role="img" aria-labelledby="t d">',
           f'<title id="t">{escape(title or name)}</title>',
           f'<desc id="d">{escape(desc)}</desc>',
           "<defs>", *markers, "</defs>",
           f'<rect width="100%" height="100%" rx="18" fill="{BG}"/>']
    if title:
        out.append(text_el(PAD, PAD + 22, title, 22, TEXT, "start", 800))
    out += groups_svg + edge_svg + body + label_svg
    for j, s in enumerate(notes):
        out.append(text_el(PAD, height - PAD - (len(notes) - 1 - j) * 20 + 6, s, SUB_PX, SUB, "start"))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def route(a: Box, b: Box, mode: str) -> list[tuple[float, float]]:
    """Orthogonal path from box a to box b, ending on b's border."""
    ox0, ox1 = max(a.x, b.x), min(a.x + a.w, b.x + b.w)
    oy0, oy1 = max(a.y, b.y), min(a.y + a.h, b.y + b.h)
    if mode == "auto" and ox1 - ox0 > 10:  # stacked: straight vertical
        x = (ox0 + ox1) / 2
        return [(x, a.y + a.h), (x, b.y)] if b.cy > a.cy else [(x, a.y), (x, b.y + b.h)]
    if mode == "auto" and oy1 - oy0 > 10:  # side by side: straight horizontal
        y = (oy0 + oy1) / 2
        return [(a.x + a.w, y), (b.x, y)] if b.cx > a.cx else [(a.x, y), (b.x + b.w, y)]
    if mode in ("auto", "hv"):  # leave sideways, enter top/bottom
        sx = a.x + a.w if b.cx > a.cx else a.x
        ey = b.y if b.cy > a.cy else b.y + b.h
        return [(sx, a.cy), (b.cx, a.cy), (b.cx, ey)]
    if mode == "vh":  # leave top/bottom, enter sideways
        sy = a.y + a.h if b.cy > a.cy else a.y
        ex = b.x if b.cx > a.cx else b.x + b.w
        return [(a.cx, sy), (a.cx, b.cy), (ex, b.cy)]
    if mode == "hvh":  # sideways, vertical in the gap, sideways
        sx = a.x + a.w if b.cx > a.cx else a.x
        ex = b.x if b.cx > a.cx else b.x + b.w
        mx = (sx + ex) / 2
        return [(sx, a.cy), (mx, a.cy), (mx, b.cy), (ex, b.cy)]
    if mode == "vhv":  # vertical, sideways in the gap, vertical
        sy = a.y + a.h if b.cy > a.cy else a.y
        ey = b.y if b.cy > a.cy else b.y + b.h
        my = (sy + ey) / 2
        return [(a.cx, sy), (a.cx, my), (b.cx, my), (b.cx, ey)]
    raise DiagramError(f"unknown route '{mode}' (auto, hv, vh, hvh, vhv)")


def overlaps(a: Box, b: Box, shrink: float = 2) -> bool:
    return (a.x < b.x + b.w - shrink and b.x + shrink < a.x + a.w
            and a.y < b.y + b.h - shrink and b.y + shrink < a.y + a.h)


def seg_hits(p0: tuple[float, float], p1: tuple[float, float], b: Box) -> bool:
    """Orthogonal segment p0-p1 passes through the inside of box b."""
    x0, x1 = sorted((p0[0], p1[0]))
    y0, y1 = sorted((p0[1], p1[1]))
    return overlaps(Box(x0, y0, max(x1 - x0, 0.1), max(y1 - y0, 0.1)), b)


def label_point(pts: list[tuple[float, float]]) -> tuple[float, float, bool]:
    segs = list(zip(pts, pts[1:]))
    (x0, y0), (x1, y1) = max(segs, key=lambda s: abs(s[1][0] - s[0][0]) + abs(s[1][1] - s[0][1]))
    return (x0 + x1) / 2, (y0 + y1) / 2, y0 == y1


def svg_path(src: Path) -> Path:
    return src.with_name(src.name[: -len(".diagram.yaml")] + ".svg")


def render_file(src: Path) -> str:
    spec = yaml.safe_load(src.read_text())
    if not isinstance(spec, dict):
        raise DiagramError(f"{src}: not a mapping")
    return render(spec, str(src.relative_to(ROOT) if src.is_relative_to(ROOT) else src))


def tracked(pattern: str) -> list[Path]:
    out = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", pattern],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [ROOT / p for p in out.split() if not p.startswith("third_party/")]


FENCE = re.compile(r"^(\s*)(```|~~~)(.*)$")
# Vertical arrow glyphs or a connector line on its own: an arrows-only flow drawing.
ARROWS = re.compile(r"[▼▲]|^\s*│\s*$", re.M)


def ascii_fences(md: Path) -> list[tuple[int, str]]:
    """(line, sha) for every unmarked code fence that looks like a box diagram."""
    lines = md.read_text(errors="ignore").splitlines()
    found, i = [], 0
    while i < len(lines):
        m = FENCE.match(lines[i])
        if not m:
            i += 1
            continue
        start, lang = i, m.group(3).strip().lower()
        j = i + 1
        while j < len(lines) and not lines[j].strip().startswith(m.group(2)):
            j += 1
        content = "\n".join(lines[start + 1 : j])
        marked = any("ascii-ok:" in lines[k] for k in range(max(0, start - 2), start))
        boxy = (bool(re.search(r"[┌╔╭]", content))
                or (bool(re.search(r"\+[-=]{3,}", content)) and "|" in content)
                or bool(ARROWS.search(content)))
        if lang != "mermaid" and boxy and not marked:
            found.append((start + 1, hashlib.sha1(content.encode()).hexdigest()[:12]))
        i = j + 1
    return found


def load_baseline() -> set[str]:
    if not BASELINE.exists():
        return set()
    return {l.split("#")[0].strip() for l in BASELINE.read_text().splitlines()
            if l.split("#")[0].strip()}


def check() -> int:
    bad = 0
    for src in tracked("*.diagram.yaml"):
        rel = src.relative_to(ROOT)
        try:
            want = render_file(src)
        except DiagramError as e:
            print(f"ERROR {e}")
            bad += 1
            continue
        out = svg_path(src)
        if not out.exists() or out.read_text() != want:
            print(f"STALE {out.relative_to(ROOT)} (run: python3 tools/render_diagrams.py {rel})")
            bad += 1
    baseline, seen = load_baseline(), set()
    for md in tracked("*.md"):
        rel = md.relative_to(ROOT)
        for line, sha in ascii_fences(md):
            key = f"{rel}:{sha}"
            seen.add(key)
            if key not in baseline:
                print(f"ASCII {rel}:{line}: box diagram in a code fence; make it a "
                      f"*.diagram.yaml SVG or mark it '<!-- ascii-ok: <reason> -->'")
                bad += 1
    for key in sorted(baseline - seen):
        print(f"BASELINE {key} no longer present: delete it from {BASELINE.relative_to(ROOT)}")
        bad += 1
    print("render_diagrams --check:", "FAIL" if bad else "OK", f"({bad} problem(s))")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", type=Path)
    ap.add_argument("--check", action="store_true", help="fail on stale SVGs or ASCII box art")
    args = ap.parse_args()
    if args.check:
        return check()
    srcs = [p.resolve() for p in args.files] or tracked("*.diagram.yaml")
    rc = 0
    for src in srcs:
        try:
            svg_path(src).write_text(render_file(src))
            print(f"wrote {svg_path(src)}")
        except DiagramError as e:
            print(f"ERROR {e}", file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
