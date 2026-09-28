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
    assert len(state.simple_trace_events) > 0, "Expected initial seeded trace events"
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
    assert len(state.coro_frames) >= 5, "Expected active coroutine frames in TelemetryState"

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
    expected_keys = {"coro", "cpu", "tlp", "log", "timeline", "trace", "source", "fpga", "memory"}
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




