---
name: doc-diagrams
runs_on: host
verifies: "Block diagrams in Markdown are SVGs generated from *.diagram.yaml sources and up to date; no ASCII box art is left in code fences ([SPEC-AUDIT-16])."
description: "Check skill. Block diagrams in Markdown are SVGs generated from *.diagram.yaml sources and up to date; timing is Mermaid sequenceDiagram, lifecycles Mermaid stateDiagram-v2; no ASCII box art in code fences ([SPEC-AUDIT-16]). Run when adding or changing a diagram or a Markdown file that has one."
---

# Skill: doc-diagrams

**What it verifies**: `[SPEC-AUDIT-16]` in `docs/SPECTRACE.md`.

## Which kind of drawing

| Drawing | Use |
| :--- | :--- |
| Block diagram: boxes, arrows, nesting (architecture, dataflow, topology, memory layout, pipeline) | SVG from `assets/<name>.diagram.yaml` |
| Timing: who does what, when (ISR -> ring -> dispatcher -> coroutine, handshakes) | Mermaid `sequenceDiagram` in the Markdown |
| Lifecycle, modes | Mermaid `stateDiagram-v2` in the Markdown |
| Directory tree, register or packet bit layout, terminal output, table | Text; mark a flagged fence with `<!-- ascii-ok: <reason> -->` |

## How to run

```bash
python3 tools/render_diagrams.py path/to/assets/<name>.diagram.yaml   # write the SVG next to it
python3 tools/render_diagrams.py                                      # re-render all
python3 tools/render_diagrams.py --check                              # stale SVGs, ASCII box art
python3 tests/test_render_diagrams.py                                 # renderer unit tests
```

Look at the result, do not trust the YAML alone: render to PNG and open it.

```bash
chromium --headless --disable-gpu --screenshot=$HOME/d.png --window-size=1400,900 file://$PWD/<svg>
```

(Snap Chromium can only read and write under `$HOME`.) The format is documented at the top of
`tools/render_diagrams.py`. Lay the nodes on the grid (`at: [col, row]`, `span`) so arrows run
straight; the renderer refuses text wider than its box, an edge through another node and a label
on top of a node.

## Pass criteria

`--check` prints `OK`, the unit tests pass, and the PNG shows every box, arrow and label of the
drawing it replaced with nothing clipped or overlapping.

## Evidence

The `--check` line and test output in the commit message.
