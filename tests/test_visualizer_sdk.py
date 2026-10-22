"""
Test Suite: AbstractX Studio SDK & Visualizer Plugins
-----------------------------------------------------
Verifies that:
1. The AbstractXStudioPlugin base class provides standard lifecycle methods.
2. Domain plugins (e.g. FlightVisualizerPlugin) implement the SDK interface cleanly.
3. Dear ImGui & ImPlot GUI tabs (flight, platform, memory) render without exceptions.
"""

import pytest
from tools.visualizer.sdk.plugin import AbstractXStudioPlugin
from tools.visualizer.flight_plugin import FlightVisualizerPlugin
import tools.visualizer.abstractx_studio as studio
from imgui_bundle import hello_imgui, implot, immapp

def test_studio_plugin_sdk_contract():
    """Validates AbstractXStudioPlugin base class methods and public contracts."""
    class ConcretePlugin(AbstractXStudioPlugin):
        def render_ui(self, delta_time: float = 0.0, state=None) -> None:
            pass

    plugin = ConcretePlugin("test_instrument", "1.0")

    # Base class should have standard lifecycle methods
    assert plugin.name == "test_instrument"
    assert plugin.version == "1.0"
    assert hasattr(plugin, "on_init")
    assert hasattr(plugin, "on_tlp_packet")
    assert hasattr(plugin, "render_ui")
    assert hasattr(plugin, "render_menu_items")

    # Default methods execute as no-ops without error
    plugin.on_init({})
    plugin.on_tlp_packet({}, {})
    plugin.render_ui(0.016)
    plugin.render_menu_items()


def test_flight_visualizer_plugin_conformance():
    """Verifies that FlightVisualizerPlugin subclasses AbstractXStudioPlugin."""
    plugin = FlightVisualizerPlugin()
    assert isinstance(plugin, AbstractXStudioPlugin)

    # Verify lifecycle invocations
    plugin.on_init(None)
    plugin.on_tlp_packet({"type": 0x10}, {"roll_cdeg": {"value": 10.0}})
    plugin.render_menu_items()

def test_studio_gui_render_flight_tab():
    """Renders 3 frames of the Level 2 Flight Tab in headless/app mode."""
    studio.g_initial_tab = "flight"
    frame_count = 0

    def step_gui():
        nonlocal frame_count
        frame_count += 1
        studio.render_gui()
        if frame_count >= 3:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "Pytest Studio - Flight"
    runner.app_window_params.window_geometry.size = (800, 600)
    runner.callbacks.show_gui = step_gui

    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()

    assert frame_count >= 3, f"Expected at least 3 frames, rendered {frame_count}"

def test_studio_gui_render_platform_tab():
    """Renders 3 frames of the Level 1 Platform Topology & Timeline Tab."""
    studio.g_initial_tab = "platform"
    frame_count = 0

    def step_gui():
        nonlocal frame_count
        frame_count += 1
        studio.render_gui()
        if frame_count >= 3:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "Pytest Studio - Platform"
    runner.app_window_params.window_geometry.size = (800, 600)
    runner.callbacks.show_gui = step_gui

    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()

    assert frame_count >= 3, f"Expected at least 3 frames, rendered {frame_count}"

def test_studio_gui_render_memory_tab():
    """Renders 3 frames of the Level 1 MemBrowse Memory Tab."""
    studio.g_initial_tab = "memory"
    frame_count = 0

    def step_gui():
        nonlocal frame_count
        frame_count += 1
        studio.render_gui()
        if frame_count >= 3:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "Pytest Studio - Memory"
    runner.app_window_params.window_geometry.size = (800, 600)
    runner.callbacks.show_gui = step_gui

    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()

    assert frame_count >= 3, f"Expected at least 3 frames, rendered {frame_count}"


def test_studio_docking_workbench_configuration():
    """Verifies that create_docking_runner_params sets up the 6-window dynamic workbench."""
    runner = studio.create_docking_runner_params()
    assert runner is not None
    assert runner.app_window_params.window_title == "AbstractX Studio & User Domain Workbench"
    assert runner.imgui_window_params.enable_viewports is False, "Single dedicated window mode by default for cross-platform portability"
    runner_multi = studio.create_docking_runner_params(enable_viewports=True)
    assert runner_multi.imgui_window_params.enable_viewports is True, "Multi-viewport enabled when requested"

    # Check 6 dockable windows
    windows = runner.docking_params.dockable_windows
    assert len(windows) == 6, f"Expected 6 dockable windows, found {len(windows)}"
    labels = {w.label: w.dock_space_name for w in windows}
    assert "AbstractX Core Studio" in labels
    assert labels["AbstractX Core Studio"] == "LeftSpace"
    assert "User Domain Instruments" in labels
    assert labels["User Domain Instruments"] == "MainDockSpace"
    assert "TLP Bus Debugger" in labels
    assert labels["TLP Bus Debugger"] == "BottomSpace"
    assert "System Event Log" in labels
    assert labels["System Event Log"] == "BottomRightSpace"
    assert "Source Code & Performance Inspector" in labels
    assert labels["Source Code & Performance Inspector"] == "MainDockSpace"
    assert "FPGA & Hardware Peripherals" in labels
    assert labels["FPGA & Hardware Peripherals"] == "BottomSpace"

    # Check 3 docking split ratios
    splits = runner.docking_params.docking_splits
    assert len(splits) == 3
    assert splits[0].new_dock == "LeftSpace"
    assert splits[1].new_dock == "BottomSpace"
    assert splits[2].new_dock == "BottomRightSpace"

    # Check that TLP and Log are visible by default as dedicated windows
    tlp_win = [w for w in windows if w.label == "TLP Bus Debugger"][0]
    log_win = [w for w in windows if w.label == "System Event Log"][0]
    assert tlp_win.is_visible is True
    assert log_win.is_visible is True

    # Test dynamic layout switching
    studio.apply_docking_layout("source_focus")
    assert studio.g_state.active_layout_preset == "source_focus"
    assert studio.g_dockable_windows["source"].is_visible is True
    assert studio.g_dockable_windows["user"].is_visible is False

    studio.apply_docking_layout("fpga_focus")
    assert studio.g_state.active_layout_preset == "fpga_focus"
    assert studio.g_dockable_windows["fpga"].is_visible is True
    assert studio.g_dockable_windows["user"].is_visible is False

    studio.apply_docking_layout("user_focus")
    assert studio.g_state.active_layout_preset == "user_focus"
    assert studio.g_dockable_windows["user"].is_visible is True
    assert studio.g_dockable_windows["core"].is_visible is False
    assert studio.g_dockable_windows["tlp"].is_visible is False
    assert studio.g_dockable_windows["log"].is_visible is False

    studio.apply_docking_layout("balanced")
    assert studio.g_state.active_layout_preset == "balanced"
    assert studio.g_dockable_windows["core"].is_visible is True
    assert studio.g_dockable_windows["user"].is_visible is True
    assert studio.g_dockable_windows["tlp"].is_visible is True
    assert studio.g_dockable_windows["log"].is_visible is True


def test_studio_docking_workbench_headless_run():
    """Runs 3 frames of the full 4-window docking workbench in headless mode."""
    frame_count = 0
    runner = studio.create_docking_runner_params()
    runner.app_window_params.window_geometry.size = (900, 600)

    # Inject frame exit callback into CustomBackground or BeforeImGuiRender
    def on_before_imgui():
        nonlocal frame_count
        frame_count += 1
        if frame_count >= 3:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner.callbacks.before_imgui_render = on_before_imgui

    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()

    assert frame_count >= 3, f"Expected at least 3 frames in docking mode, rendered {frame_count}"


def test_studio_coroutine_and_simple_trace_render():
    """Renders AbstractX Studio Coroutine Inspector and Simple Trace views in headless mode."""
    frame_count = 0

    def step_gui():
        nonlocal frame_count
        frame_count += 1
        studio._render_coroutine_inspector()
        studio._render_simple_trace_view()
        if frame_count >= 2:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "Pytest Studio - AbstractX Coroutine & Simple Trace"
    runner.app_window_params.window_geometry.size = (900, 700)
    runner.callbacks.show_gui = step_gui

    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()

    assert frame_count >= 2, f"Expected at least 2 frames, rendered {frame_count}"


def test_studio_simple_trace_event_lifecycle():
    """Validates the Simple Trace and AbstractX Studio buffer APIs in TelemetryState."""
    state = studio.TelemetryState()
    state.enable_sim()
    assert len(state.simple_trace_events) > 0, "Expected seeded trace events in --sim mode"
    first = state.simple_trace_events[0]
    assert "time_us" in first
    assert "core" in first
    assert "primitive" in first
    assert "symbol" in first
    assert "details" in first
    assert "file" in first
    assert "line" in first

    # Add a custom event
    state.add_trace_event(150.0, "Core 1", "co_await", "test_task()", "token=await_test", 1.5, "main.cpp", 50)
    assert any(ev["symbol"] == "test_task()" for ev in state.simple_trace_events)

    # Test pushing chart metrics
    state.push_chart_metrics(1.0, 25.0, 35.0, 15.0, 1.1, 4.2, 2.7, 0.9, 18.0, 14.0)
    assert state.chart_cpu_c0[-1] == 25.0
    assert state.chart_cpu_c1[-1] == 35.0
    assert state.chart_cpu_spu[-1] == 15.0
    assert state.chart_lat_imu[-1] == 1.1


def test_studio_coroutine_inspector_and_watchdog_model():
    """Validates C++20 Coroutine Inspector state, frame memory pool, watchdog budget limits, and topology."""
    state = studio.TelemetryState()
    state.enable_sim()
    assert len(state.coro_frames) >= 5, "Expected seeded coroutine frames in --sim mode"

    # Verify frame structure matches C++20 coroutine inspection contract
    for f in state.coro_frames:
        assert "task_id" in f
        assert "name" in f
        assert "state" in f
        assert "awaiter" in f
        assert "duration_us" in f
        assert "budget_us" in f
        assert "file" in f
        assert "line" in f

    # Verify static bump-pool allocation metrics (zero dynamic heap)
    assert state.coro_pool_used_bytes > 0
    assert state.coro_pool_capacity_bytes >= state.coro_pool_used_bytes
    assert state.coro_pool_capacity_bytes == 61440  # 60 KB static pool

    # Verify parent-child spawn topology
    assert len(state.coro_topology) >= 5
    assert ("app_main", "imu_pipeline") in state.coro_topology
    assert ("imu_pipeline", "sensor_fusion") in state.coro_topology


def test_studio_membrowse_ci_integration():
    """Validates that MemBrowse CI metrics are ingested, loaded, and audited for zero dynamic heap."""
    state = studio.TelemetryState()
    state.enable_sim()
    assert hasattr(state, "load_membrowse_metrics")
    assert hasattr(state, "run_membrowse_analysis")
    assert state.membrowse_status_msg != ""
    assert len(state.mem_targets) > 0

    # Validate that all targets have zero-heap compliance audited
    for t_name, t_info in state.mem_targets.items():
        assert "ram_used_bytes" in t_info
        assert "flash_used_bytes" in t_info
        assert "zero_heap_compliant" in t_info
        assert "sections" in t_info


def test_studio_floating_canvas_windows_and_user_canvas():
    """Validates movable floating canvas windows and independent User domain instruments canvas."""
    state = studio.TelemetryState()
    assert hasattr(state, "canvas_windows")
    expected_keys = {"coro", "cpu", "tlp", "log", "timeline", "trace", "source", "fpga", "memory", "cli"}
    assert expected_keys.issubset(set(state.canvas_windows.keys()))

    # Verify that all windows start docked as tabs inside Studio
    for k in expected_keys:
        assert state.canvas_windows[k] is False

    # Simulate popping Coroutine Inspector and TLP Debugger to canvas windows
    state.canvas_windows["coro"] = True
    state.canvas_windows["tlp"] = True
    assert state.canvas_windows["coro"] is True
    assert state.canvas_windows["tlp"] is True

    # Simulate popping back into Studio tabs
    state.canvas_windows["coro"] = False
    assert state.canvas_windows["coro"] is False

    # Verify runner params has post_render_dockable_windows configured for canvas windows
    runner = studio.create_docking_runner_params()
    assert runner.callbacks.post_render_dockable_windows is not None


def test_studio_cli_console_and_repl():
    """Validates [SPEC-STUDIO-13] Interactive CLI Console & MicroPython Hardware REPL (--sim)."""
    state = studio.TelemetryState()
    state.enable_sim()
    assert hasattr(state, "cli_history")
    assert hasattr(state, "cli_mode")
    assert state.cli_mode == "AbstractX"
    assert len(state.cli_history) > 0

    # 1. Test AbstractX commands
    state.execute_cli_command("help")
    assert any("Available AbstractX CLI Commands" in line[1] for line in state.cli_history)

    state.execute_cli_command("ps")
    assert any("app_main" in line[1] for line in state.cli_history)

    state.execute_cli_command("tasks")
    assert any("bench_a" in line[1] for line in state.cli_history)

    state.execute_cli_command("peek 0x07090000")
    assert any("0x07090000" in line[1] for line in state.cli_history)

    state.execute_cli_command("poke 0x07130000 0x12345678")
    assert state.cli_simulated_regs[0x07130000] == 0x12345678

    # 2. Test switching to MicroPython REPL mode
    state.execute_cli_command("mode micropython")
    assert state.cli_mode == "MicroPython"

    state.execute_cli_command("2 + 2")
    assert any(line[1] == "4" for line in state.cli_history)

    state.execute_cli_command("abstractx.ping()")
    assert any("1.85" in line[1] for line in state.cli_history)

    state.execute_cli_command("len(abstractx.coros())")
    assert any(line[1] == str(len(state.coro_frames)) for line in state.cli_history)


def test_studio_hardware_profile_and_fpga_filtering():
    """
    # @impl [SPEC-STUDIO-02] tools/visualizer/SPECIFICATION.md
    # @impl [SPEC-STUDIO-07] tools/visualizer/SPECIFICATION.md
    Validates dynamic hardware profiles, FPGA window suppression, and barectf schema loading.
    """
    state = studio.TelemetryState()
    assert state.has_fpga is False, "Pure Silicon profile should be default (no FPGA)"
    assert not any("FPGA" in c.get("role", "") for c in state.active_cores), "FPGA core should not be active in Pure Silicon mode"

    # 1. Test profile loading: Pure Silicon
    ok = state.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_pure.json")
    assert ok is True
    assert state.has_fpga is False
    assert state.profile_id == "cubie_a5e_pure"

    # 2. Test profile loading: FPGA X-Fabric
    ok = state.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_fpga.json")
    assert ok is True
    assert state.has_fpga is True
    assert state.profile_id == "cubie_a5e_fpga"
    assert any("FPGA" in c.get("role", "") for c in state.active_cores), "FPGA core should be active in FPGA profile"

    # 3. Test explicit set_has_fpga toggle
    state.set_has_fpga(False)
    assert state.has_fpga is False
    assert state.canvas_windows["fpga"] is False

    # 4. Test dynamic barectf schema loading
    ok = state.load_barectf_schema("apps/gps_imu_app/trace_schema.json")
    assert ok is True
    assert state.compiled_schema is not None
    assert len(state.compiled_schema.streams) > 0







LIVE_PANELS = (
    "_render_core_cpu_and_topology", "_render_core_timeline", "_render_flow_integrity_and_eye_diagram",
    "_render_fpga_peripherals_window", "_render_source_inspector_window", "_render_coroutine_inspector",
    "_render_simple_trace_view", "_render_level1_memory", "_render_cli_console",
    "_render_tlp_debugger_window", "_render_event_log_window", "_render_user_domain_window",
)


def _render_panels(state, frames=2):
    saved = studio.g_state
    studio.g_state = state
    count = 0

    def step_gui():
        nonlocal count
        count += 1
        for name in LIVE_PANELS:
            getattr(studio, name)()
        if count >= frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_geometry.size = (1200, 800)
    runner.callbacks.show_gui = step_gui
    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()
        studio.g_state = saved
    return count


def test_studio_starts_live_without_demo_data():
    """[SPEC-STUDIO-14] Without --sim, Studio state holds no invented data."""
    state = studio.TelemetryState()
    assert state.sim_mode is False
    assert state.coro_frames == [] and state.coro_topology == []
    assert state.coro_names == [] and state.io_driver_events == []
    assert state.simple_trace_events == []
    assert state.coro_pool_used_bytes == 0
    assert (state.gps_lat, state.gps_lon, state.gps_sats, state.gps_fix_type) == (0.0, 0.0, 0, 0)
    assert state.linux_total_cpu == 0.0 and state.e906_active_duty_pct == 0.0
    assert not state.chart_cpu_c0.any() and not state.chart_transit_delay_us.any()
    assert state.cli_simulated_regs == {}
    assert not any(e["level"] in ("TLP", "CORO", "MEM", "ISR") for e in state.logs), state.logs
    metrics = studio.Path(studio.__file__).resolve().parent / "memory_metrics.json"
    if not metrics.exists():
        assert state.mem_targets == {}


def test_studio_cli_without_target_link_does_not_invent_answers():
    """[SPEC-STUDIO-14] Target commands say there is no REPL link instead of answering from fake state."""
    state = studio.TelemetryState()
    for cmd in ("peek 0x07090000", "poke 0x07130000 0x1", "ping", "tasks"):
        state.execute_cli_command(cmd)
    text = "\n".join(line[1] for line in state.cli_history)
    assert "bench_a" not in text and "0x18FA" not in text
    errs = [line for line in state.cli_history if line[0] == "ERR" and "no REPL link" in line[1]]
    assert len(errs) == 4, text
    state.execute_cli_command("mode micropython")
    state.execute_cli_command("2 + 2")
    assert not any(line[1] == "4" for line in state.cli_history), "local eval() is not the E906 REPL"
    assert "no REPL link" in state.cli_history[-1][1]


def test_studio_enable_sim_seeds_demo_data():
    """[SPEC-STUDIO-09] [SPEC-STUDIO-14] Demo data appears only after enable_sim()."""
    state = studio.TelemetryState()
    state.enable_sim()
    assert state.sim_mode is True
    assert len(state.coro_frames) >= 5 and len(state.simple_trace_events) > 0
    assert state.gps_sats > 0 and state.cli_simulated_regs


def test_studio_panels_render_live_and_sim():
    """[SPEC-STUDIO-14] Every panel renders with empty live state (placeholders) and with --sim data."""
    assert _render_panels(studio.TelemetryState()) >= 2
    sim = studio.TelemetryState()
    sim.enable_sim()
    assert _render_panels(sim) >= 2


def _status_packet(ts_us, cycles, idle, run, events, isr):
    from tools.visualizer import runtime_stats as rs
    return rs.encode({"timestamp_us": ts_us, "cycle_hz": 200_000_000, "cycles": cycles, "idle_cycles": idle,
                      "steps": 0, "pool_used": 192, "pool_high_water": 192, "pool_capacity": 4096,
                      "dropped_resumes": 0, "queue_depth": 0, "queue_capacity": 32, "trace_discarded": 0,
                      "tasks": [{"name": "repl_rx", "frame": 0x3FFC1000, "resumes": 1, "run_cycles": run,
                                 "max_cycles": 10}],
                      "drivers": [{"name": "twi1", "id": 91, "events": events, "cycles": isr, "errors": 0}]})


def test_studio_live_runtime_status_panel():
    """[SPEC-STUDIO-15]: two status reports give rates; the live CPU panel and TLP debugger render."""
    # @verifies [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
    state = studio.TelemetryState()
    studio.g_state = state
    assert not state.sim_mode
    state.ingest_ctf(_status_packet(1_000_000, 0, 0, 0, 0, 0))
    assert state.runtime_rates is None  # one report has no rate yet
    state.ingest_ctf(_status_packet(2_000_000, 200_000_000, 100_000_000, 90_000_000, 2600, 720_000))
    r = state.runtime_rates
    assert abs(r["busy_pct"] - 50.0) < 1e-6 and abs(r["tasks"][0]["cpu_pct"] - 45.0) < 1e-6
    assert abs(r["drivers"][0]["irq_per_s"] - 2600) < 1e-6
    assert any(e["source"] == "status" and "busy 50.0%" in e["message"] for e in state.logs)
    for i in range(80):
        state.add_tlp_packet(bytes(64), 0x10, 2, 2, i, i, True)
    frames = 0

    def step_gui():
        nonlocal frames
        frames += 1
        studio._render_live_runtime_status()
        studio._render_tlp_debugger_window()
        if frames >= 3:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_title = "Pytest Studio - Runtime Status"
    runner.app_window_params.window_geometry.size = (900, 700)
    runner.callbacks.show_gui = step_gui
    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()
    assert frames >= 3
    assert state.tlp_view and state.tlp_tag_counts[2] == 80


def test_studio_busy_plot_auto_scale():
    """[SPEC-STUDIO-15]: the busy % axis follows the data (idle and loaded both readable), fixed 0-100 when off."""
    # @verifies [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
    assert studio.runtime_plot_y_max([0.01, 0.008]) == 0.02
    assert studio.runtime_plot_y_max([0.31, 0.12]) == 0.5
    assert studio.runtime_plot_y_max([3.0]) == 5.0
    assert studio.runtime_plot_y_max([90.0, 0.3]) == 100.0
    assert studio.runtime_plot_y_max([]) == 0.01
    assert studio.runtime_plot_y_max([0.3], autoscale=False) == 100.0
    state = studio.TelemetryState()
    assert state.runtime_plot_autoscale and state.runtime_plot_window_s == 300


def _run_frames(show, frames=2, size=(1400, 900)):
    count = 0

    def step_gui():
        nonlocal count
        count += 1
        show()
        if count >= frames:
            hello_imgui.get_runner_params().app_shall_exit = True

    runner = hello_imgui.RunnerParams()
    runner.app_window_params.window_geometry.size = size
    runner.callbacks.show_gui = step_gui
    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()
    return count


def test_studio_tlp_and_log_are_not_also_tabs(monkeypatch):
    """[SPEC-STUDIO-02]: TLP debugger and event log are docked windows only; no tab or pop-out copy."""
    # @verifies [SPEC-STUDIO-02] tools/visualizer/SPECIFICATION.md
    studio.g_state = studio.TelemetryState()
    tabs = []
    real = studio.imgui.begin_tab_item

    def spy(label, *args):
        tabs.append(label)
        return real(label, *args)

    monkeypatch.setattr(studio.imgui, "begin_tab_item", spy)
    assert _run_frames(studio._render_core_studio_window) >= 2
    assert "Coroutine Inspector" in tabs and "Source Code & RTL Inspector" in tabs
    assert "TLP Bus Debugger" not in tabs and "System Event Log" not in tabs, sorted(set(tabs))
    assert not studio.g_state.canvas_windows.get("tlp") and not studio.g_state.canvas_windows.get("log")


def test_studio_source_browser_reads_real_files_with_live_badges():
    """[SPEC-STUDIO-11]: real repository files, and lines naming a live task/driver carry its numbers."""
    # @verifies [SPEC-STUDIO-11] tools/visualizer/SPECIFICATION.md
    files = studio.list_source_files("CPP")
    main = "apps/e906_repl_app/src/main.cpp"
    assert main in files and "targets/allwinner_e906/src/irq_dispatcher.cpp" in files
    assert not any(f.startswith("third_party") or "/build" in f for f in files)
    assert any(f.endswith(".sv") for f in studio.list_source_files("RTL"))
    lines = studio.load_source_lines(main)
    assert lines and len(lines) == len((studio.REPO_ROOT / main).read_text().splitlines())
    rates = {"tasks": [{"name": "repl_rx", "cpu_pct": 90.0, "resumes_per_s": 12.0}],
             "drivers": [{"name": "uart3", "irq_per_s": 30.0, "isr_pct": 0.117, "errors": 0}]}
    badges = studio.live_source_badges(lines, rates)
    assert any("repl_rx: 90.0% CPU" in b and '"repl_rx"' in lines[i] for i, b in badges.items()), badges
    periph = studio.load_source_lines("apps/e906_repl_app/src/e906_periph.cpp")
    assert any("uart3: 30 IRQ/s, 39.00 µs/IRQ" in b for b in studio.live_source_badges(periph, rates).values())
    assert studio.live_source_badges(lines, None) == {}

    state = studio.TelemetryState()
    studio.g_state = state
    state.runtime_rates = rates
    state.source_search = "spawn"
    assert _run_frames(studio._render_source_inspector_window) >= 2
    assert state.selected_source_file in files


def test_studio_gauge_row_clears_its_labels():
    """[SPEC-STUDIO-15]: the gauge row is tall enough for dial + two label lines (no overlap below)."""
    # @verifies [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
    heights = []
    _run_frames(lambda: heights.append(studio.imgui.get_text_line_height()))
    # draw_radial_gauge: centre at +40, radius 32, sublabel drawn at centre + radius + 26
    assert studio.RUNTIME_GAUGE_ROW_H >= 40 + 32 + 26 + heights[-1]
