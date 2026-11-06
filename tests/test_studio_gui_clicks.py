"""
Studio GUI behaviour driven by clicks (Dear ImGui Test Engine, headless): the full-audit "gui" checklist
([SPEC-STUDIO-16], [SPEC-STUDIO-01]). Before 2026-11-06 the GUI tests only rendered frames.

Each case clicks through the real widgets: every available Core Studio tab opens; "Pop to Canvas" moves
a tool to its floating canvas window and "Pop In" brings it back (one copy at a time); the View menu
hides and reopens the TLP Bus Debugger; the Connection menu offers Connect (not clicked: it opens a
socket). Failures are collected and asserted after the run.

@verifies [SPEC-STUDIO-16] tools/visualizer/SPECIFICATION.md (available_tools _render_floating_canvas_windows)
"""

from imgui_bundle import hello_imgui, imgui, immapp, implot

import tools.visualizer.abstractx_studio as studio

te = imgui.test_engine


def _run(cases):
    """Run the Studio headless with the test engine; cases = [(name, fn(ctx, fails))]. Returns fails."""
    fails: list[str] = []
    done: list[str] = []
    runner = studio.create_docking_runner_params(enable_viewports=False)
    runner.app_window_params.window_geometry.size = (2560, 1440)
    runner.use_imgui_test_engine = True
    runner.ini_disable = True   # never read or write a layout file: each run starts from the default layout

    def register():
        eng = hello_imgui.get_imgui_test_engine()
        for name, fn in cases:
            t = te.register_test(eng, "studio", name)

            def body(ctx, fn=fn, name=name):
                try:
                    fn(ctx, fails)
                except Exception as e:  # a Python error inside the engine must not vanish
                    fails.append(f"{name}: {type(e).__name__}: {e}")
                done.append(name)
            t.test_func = body

    runner.callbacks.register_tests = register
    st = {"frames": 0, "queued": False}

    def before():
        eng = hello_imgui.get_imgui_test_engine()
        st["frames"] += 1
        if not st["queued"] and st["frames"] > 3:
            io = te.get_io(eng)
            io.config_run_speed = te.TestRunSpeed.fast
            te.queue_tests(eng, te.TestGroup.tests)
            st["queued"] = True
        elif (st["queued"] and te.is_test_queue_empty(eng)) or st["frames"] > 3000:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner.callbacks.before_imgui_render = before
    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()
    missing = [n for n, _ in cases if n not in done]
    if missing:
        fails.append(f"cases never ran: {missing}")
    return fails


def _check(fails, cond, msg):
    if not cond:
        fails.append(msg)


def case_tabs(ctx, fails):
    for key, label, title, _, _ in studio.available_tools():
        ctx.item_click(f"**/{label}")
        ctx.yield_(2)
        _check(fails, ctx.item_exists(f"**/🗖 Pop to Canvas##pop_{key}"), f"tab {label}: no Pop to Canvas after opening it")


def case_pop_out_and_in(ctx, fails):
    ctx.item_click("**/Timeline")
    ctx.yield_(2)
    ctx.item_click("**/🗖 Pop to Canvas##pop_timeline")
    ctx.yield_(3)
    _check(fails, studio.g_state.canvas_windows.get("timeline") is True, "Pop to Canvas did not set canvas_windows[timeline]")
    _check(fails, ctx.item_exists("**/🗗 Pop In##dock_timeline"), "the tab does not show the Pop In placeholder")
    _check(fails, not ctx.item_exists("**/🗖 Pop to Canvas##pop_timeline"), "tool still in the tab while popped out (two copies)")
    ctx.item_click("**/🗗 Pop In##dock_timeline")
    ctx.yield_(3)
    _check(fails, studio.g_state.canvas_windows.get("timeline") is False, "Pop In did not dock the tool back")
    _check(fails, ctx.item_exists("**/🗖 Pop to Canvas##pop_timeline"), "tool not back in its tab after Pop In")


# hello_imgui's View menu: window ###Menu_00, the window list under the ID scope DockableWindows##asldqsl
VIEW_TLP_ITEM = "//###Menu_00/DockableWindows##asldqsl/TLP Bus Debugger"


def _real_window(label):
    """The DockableWindow hello_imgui draws (the runner holds copies of the Python objects)."""
    return hello_imgui.get_runner_params().docking_params.dockable_window_of_name(label)


def case_view_menu_reopens_tlp_debugger(ctx, fails):
    _check(fails, _real_window("TLP Bus Debugger").is_visible, "TLP Bus Debugger not visible at start")
    for want in (False, True):
        ctx.set_ref("//##MainMenuBar")
        ctx.menu_click("View##kdsflmkdflm")
        ctx.yield_(2)
        ctx.item_click(VIEW_TLP_ITEM)
        ctx.yield_(3)
        ctx.key_press(imgui.Key.escape)   # leave no menu open for the next case
        ctx.yield_(2)
        _check(fails, _real_window("TLP Bus Debugger").is_visible == want,
               f"View > TLP Bus Debugger did not {'reopen' if want else 'hide'} it")


def case_show_log_button_reopens_log(ctx, fails):
    """'Show Log' (a jump request, [SPEC-STUDIO-02]) shows a closed System Event Log; it used to crash."""
    _real_window("System Event Log").is_visible = False
    ctx.yield_(2)
    ctx.item_click("**/Show Log##hdr")
    ctx.yield_(3)
    _check(fails, _real_window("System Event Log").is_visible, "Show Log did not reopen the System Event Log")
    ctx.item_click("**/Show TLP##hdr")
    ctx.yield_(3)
    _check(fails, _real_window("TLP Bus Debugger").is_visible, "Show TLP: TLP Bus Debugger not visible")


def case_layout_presets_change_windows(ctx, fails):
    ctx.item_click("**/✈️ Flight Focus")
    ctx.yield_(3)
    vis = {k: _real_window(v).is_visible for k, v in studio.DOCK_LABELS.items()}
    _check(fails, vis == {"core": False, "user": True, "tlp": False, "log": False}, f"Flight Focus: {vis}")
    ctx.item_click("**/🗗 Balanced")
    ctx.yield_(3)
    vis = {k: _real_window(v).is_visible for k, v in studio.DOCK_LABELS.items()}
    _check(fails, all(vis.values()), f"Balanced: {vis}")


def case_bring_canvas_to_front(ctx, fails):
    ctx.item_click("**/Timeline")
    ctx.yield_(2)
    ctx.item_click("**/🗖 Pop to Canvas##pop_timeline")
    ctx.yield_(3)
    ctx.item_click("**/Bring to Front##focus_timeline")
    ctx.yield_(3)
    _check(fails, studio.g_state.focus_canvas is None, "focus request not consumed by the canvas window")
    ctx.item_click("**/🗗 Pop In##dock_timeline")
    ctx.yield_(3)


def case_connection_menu_offers_connect(ctx, fails):
    ctx.set_ref("//##MainMenuBar")
    ctx.menu_click("Connection")
    ctx.yield_(2)
    ip = studio.g_state.board_ip
    _check(fails, ctx.item_exists(f"//##Menu_00/▶ Connect to Board ({ip}:{studio.g_state.udp_port})")
           or ctx.item_exists(f"**/▶ Connect to Board ({ip}:{studio.g_state.udp_port})"),
           "Connection menu has no Connect item")
    ctx.key_press(imgui.Key.escape)


def test_studio_gui_clicks():
    fails = _run([("tabs", case_tabs), ("pop_out_and_in", case_pop_out_and_in),
                  ("view_menu", case_view_menu_reopens_tlp_debugger),
                  ("show_log", case_show_log_button_reopens_log), ("layout", case_layout_presets_change_windows),
                  ("bring_to_front", case_bring_canvas_to_front),
                  ("connect_menu", case_connection_menu_offers_connect)])
    assert not fails, "\n".join(fails)
