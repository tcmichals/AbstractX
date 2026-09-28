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
