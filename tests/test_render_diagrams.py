#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-16] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/render_diagrams.py. Runs with plain python3 or pytest."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import render_diagrams as rd  # noqa: E402

SPEC = {
    "title": "Fixture",
    "nodes": [
        {"id": "a", "label": "Sensor", "text": ["`DRDY` edge"], "at": [0, 0], "kind": "hw"},
        {"id": "b", "label": "E906", "at": [1, 0]},
        {"id": "c", "label": "Linux", "at": [1, 1], "kind": "accent"},
    ],
    "groups": [{"label": "SoC", "contains": ["b", "c"], "kind": "muted"}],
    "edges": [{"from": "a", "to": "b", "label": "SPI"}, ["b", "c", "RPMsg"]],
}


def expect_error(spec, fragment):
    try:
        rd.render(spec, "fixture")
    except rd.DiagramError as e:
        assert fragment in str(e), str(e)
        return
    raise AssertionError(f"expected DiagramError containing '{fragment}'")


def test_render_has_title_desc_text_and_background():
    svg = rd.render(SPEC, "fixture")
    assert svg.startswith("<svg") and "<title" in svg and "<desc" in svg
    assert 'fill="#0f1a30"' in svg  # explicit background for light and dark viewers
    assert ">Sensor</text>" in svg and "RPMsg" in svg and "SOC" in svg
    assert "<tspan" in svg  # inline `code` is monospace
    assert "<script" not in svg and "http://" not in svg.replace("http://www.w3.org/2000/svg", "")


def test_output_is_identical_across_processes():
    # Marker ids must not depend on Python's per-process hash seed.
    code = ("import sys; sys.path.insert(0, %r); import render_diagrams as rd, json; "
            "print(rd.render(json.loads(%r)))") % (str(TOOLS), __import__("json").dumps(SPEC))
    outs = {subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, check=True,
                           env={**os.environ, "PYTHONHASHSEED": seed}).stdout for seed in ("1", "2")}
    assert len(outs) == 1


def test_layout_problems_are_errors():
    expect_error({"nodes": [{"id": "a", "label": "A very long label for a box", "at": [0, 0]}]},
                 "wider than its box")
    expect_error({"nodes": [{"id": "a", "at": [0, 0]}, {"id": "b", "at": [1, 0]},
                            {"id": "c", "at": [2, 0]}], "edges": [["a", "c"]]},
                 "runs through node 'b'")
    expect_error({"grid": {"gap_x": 20}, "nodes": [{"id": "a", "at": [0, 0]}, {"id": "b", "at": [1, 0]}],
                  "edges": [["a", "b", "a long label"]]}, "covers node")
    expect_error({"nodes": [{"id": "a", "at": [0, 0]}], "edges": [["a", "zz"]]}, "unknown node")
    expect_error({"nodes": [{"id": "a", "at": [0, 0], "kind": "pink"}]}, "unknown kind")


def scan(md_text):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "x.md"
        p.write_text(md_text)
        return rd.ascii_fences(p)


def test_ascii_detection():
    box = "```text\n┌───┐\n│ A │\n└───┘\n```\n"
    plus = "```\n+-----+\n| A   |\n+-----+\n```\n"
    tree = "```\nsrc/\n├── a.cpp\n└── b.cpp\n```\n"
    mermaid = "```mermaid\nflowchart LR\n  A[┌x] --> B\n```\n"
    marked = "<!-- ascii-ok: register layout -->\n" + plus
    assert len(scan(box)) == 1 and len(scan(plus)) == 1
    assert scan(tree) == [] and scan(mermaid) == [] and scan(marked) == []
    assert scan(box)[0][0] == 1  # 1-based line of the opening fence
    # Arrows-only flow drawings have no box corner; they missed the first version of the scan.
    flow = "```text\n[1. Observe]\n     │\n     ▼\n[2. Log it]\n```\n"
    assert len(scan(flow)) == 1


def test_plain_line_edges_have_no_arrowhead():
    spec = {"nodes": [{"id": "a", "at": [0, 0]}, {"id": "b", "at": [1, 0]}],
            "edges": [{"from": "a", "to": "b", "arrow": False}]}
    path = [l for l in rd.render(spec).splitlines() if l.startswith("<path d=")]
    assert len(path) == 1 and "marker-end" not in path[0]
    spec["edges"][0]["arrow"] = True
    assert "marker-end" in rd.render(spec)


def test_diagram_sources_are_docs_not_code():
    # The change review must not ask for @impl tags in drawing sources.
    import spectrace_matrix as sm
    assert not sm.is_code("docs/assets/x.diagram.yaml")
    assert sm.is_code("tools/render_diagrams.py")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"PASS {name}")
