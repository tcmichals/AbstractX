# Prompt: Finish AbstractX Studio multi-viewport pop-outs

Paste everything below the line into Claude Code (or Gemini) from the repo root.

---

Goal: in AbstractX Studio (`tools/visualizer/abstractx_studio.py`), every popped-out
window must become its own movable OS window (so it can go on a second monitor),
and this must be the default. Follow `AGENTS.md` section 0: governing requirement is
`[SPEC-STUDIO-01]` in `tools/visualizer/SPECIFICATION.md`; change the spec first,
tag code `# @impl [SPEC-STUDIO-01] tools/visualizer/SPECIFICATION.md`, log lessons
with `tools/log_mistake.py`, commit as `Tim Michals <tcmichals@gmail.com>` with no
AI trailers, and push `main`.

## What is already done (commits c61e444 and the one after it)

- Viewports are ON by default (`enable_viewports=True`); `--no-viewports` turns them off.
- `--demo` is an alias of `--sim`.
- `_configure_viewports()` runs in `callbacks.post_init` and sets
  `io.config_viewports_no_auto_merge = True` (ImGui's default `False` merged every
  pop-out back into the main window, because pop-outs open inside it) and
  `io.config_viewports_no_decoration = False` (native title bar and borders).
- The size presets and sliders on a floating window (`_render_window_sizing_bar`) call
  only `imgui.set_window_size`, not `hello_imgui.change_window_size` (which resizes the
  main Studio window).
- Test: `tests/test_visualizer_sdk.py::test_viewport_popouts_become_os_windows`.
- Python suite: `python3 -m pytest -q tests --ignore=tests/board` -> 107 passed, 4 skipped.

**On screen (2026-10-23, X11, Tim):** pop-out to a separate OS window and Pop In both work (3a, 3d).
Still to check: 3b, 3c, 3e, 3f, 3g.

## Steps

1. Check the session: `echo $XDG_SESSION_TYPE`. ImGui viewports with GLFW need X11
   (or XWayland). On native Wayland GLFW cannot position windows; if that is the
   session, try `XDG_SESSION_TYPE=x11` / `SDL_VIDEODRIVER=x11` or log in to an X11 session,
   and record the result.
2. Run: `cd tools/visualizer && python3 abstractx_studio.py --demo`
3. Check by hand and report each one as works or fails:
   a. "🗖 Float Canvas" on User Domain Instruments -> separate OS window with a title bar?
   b. Drag that window off the Studio window and onto a second monitor -> it stays separate?
   c. Size presets (800x600 ... 1920x1080) on the floating window -> resize only it, not Studio?
   d. "🗗 Pop In (Dock to Studio)" -> docks back?
   e. Core Studio tab "Pop to Canvas" (e.g. Timeline) -> separate OS window; tab shows the placeholder?
   f. Drag a docked window's tab out of the dock -> becomes its own OS window?
   g. `--no-viewports` -> everything stays in one OS window like before?
4. If something fails, check these first:
   - Stale layout: HelloImGui restores the saved layout/ini (`restore_previous_geometry = True`
     and the ini file next to the app). Delete the `*.ini` that HelloImGui wrote, or use the
     View menu's layout reset, and try again.
   - `app_window_params.borderless_resizable = True` and `handle_edge_insets = True` on the
     main window: try turning them off when viewports are on, and see if pop-outs behave.
   - `decouple_window()` queues an undock and calls `set_window_size` in the same frame;
     the undock only happens next frame. If the window does not resize or move, set the size
     on the next frame (e.g. a pending-resize dict that is applied when the window begins)
     and, if needed, `imgui.set_next_window_pos` with a small offset from the main viewport.
   - Rendering glitches or crashes in secondary windows: note the GPU driver and backend
     (`hello_imgui` default is GLFW + OpenGL3) in the log.
5. For every fix: update `[SPEC-STUDIO-01]` first, add or extend a test in
   `tests/test_visualizer_sdk.py` that fails on the old code and passes on the new, run
   `python3 -m pytest -q tests --ignore=tests/board`, `python3 tools/audit_specs.py`,
   `python3 tools/spectrace_matrix.py --changes`, then commit with the command outputs and
   the results of step 3 (a-g) in the commit message.
6. Finish with a list of what was not checked (e.g. Wayland, Windows, a second monitor if
   there isn't one).
