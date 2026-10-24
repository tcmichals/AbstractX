#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Standalone Minimal Multi-Viewport Pop-out Demo
==============================================
Demonstrates Dear ImGui + HelloImGui multi-viewport pop-out behavior
governed by [SPEC-STUDIO-01] in tools/visualizer/SPECIFICATION.md.

Key mechanisms demonstrated:
1. io.config_viewports_no_auto_merge = True:
   Prevents popped-out floating windows overlapping the main window
   from automatically merging back into the main OS window.
2. io.config_viewports_no_decoration = False:
   Ensures popped-out OS windows receive standard OS title bars & borders.
3. imgui.set_window_size() vs hello_imgui.change_window_size():
   Demonstrates that floating window size buttons and sliders must resize
   ONLY the floating window and not the main workbench.

Usage:
  python3 tools/visualizer/demo_viewport_popout.py
  python3 tools/visualizer/demo_viewport_popout.py --no-viewports
"""

import sys
import argparse
from imgui_bundle import imgui, hello_imgui, immapp

# State for tracking window docking/pop-out status
class DemoState:
    def __init__(self):
        self.pop_out_requested = False
        self.reset_layout_requested = False
        self.counter = 0

g_state = DemoState()
g_dockable_windows = {}

def configure_viewports():
    """Configure Dear ImGui viewport behavior in post_init callback."""
    # @impl [SPEC-STUDIO-01] tools/visualizer/SPECIFICATION.md
    io = imgui.get_io()
    # Without no_auto_merge, a pop-out over the main window merges back into it.
    io.config_viewports_no_auto_merge = True
    # Ensure OS decorations (borders, titlebar) are visible on popped-out windows.
    io.config_viewports_no_decoration = False
    print("[Demo] Viewports configured: no_auto_merge = True, no_decoration = False")

def render_main_controller_window():
    """Main workbench window containing status and controls."""
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "=== Multi-Viewport Pop-out Verification ===")
    imgui.spacing()

    io = imgui.get_io()
    has_viewports = bool(io.config_flags & imgui.ConfigFlags_.viewports_enable.value)
    
    imgui.text("Viewports Flag Enabled: ")
    imgui.same_line()
    if has_viewports:
        imgui.text_colored(imgui.ImVec4(0.2, 0.9, 0.3, 1.0), "YES (Multi-Viewport Active)")
    else:
        imgui.text_colored(imgui.ImVec4(1.0, 0.4, 0.4, 1.0), "NO (Single-Window Mode)")

    imgui.text(f"io.config_viewports_no_auto_merge: {io.config_viewports_no_auto_merge}")
    imgui.text(f"io.config_viewports_no_decoration: {io.config_viewports_no_decoration}")
    imgui.separator()

    imgui.text("Manual Verification Checklist:")
    imgui.bullet_text("1. Drag the 'Secondary Pop-out Window' tab outside the main window bounds.")
    imgui.bullet_text("2. Verify it becomes an independent OS window (taskbar icon / OS borders).")
    imgui.bullet_text("3. Drag it back over the main window: it should NOT automatically merge.")
    imgui.bullet_text("4. Use the size buttons/sliders in the secondary window:")
    imgui.bullet_text("   -> Only the secondary window should change size (NOT the main window).")
    imgui.bullet_text("5. Click 'Dock Back to Studio' or 'Reset Layout' to dock it back.")

    imgui.separator()
    imgui.spacing()

    if imgui.button("🗗 Reset Layout (Dock All Windows)", imgui.ImVec2(260, 32)):
        params = hello_imgui.get_runner_params()
        params.docking_params.layout_reset = True

    imgui.spacing()
    g_state.counter += 1
    imgui.text_disabled(f"Frame counter: {g_state.counter}")

def render_secondary_window():
    """Secondary window that can be docked or popped out to its own OS window."""
    is_docked = imgui.is_window_docked()
    cur_size = imgui.get_window_size()

    if not is_docked:
        imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "● STATUS: POPPED-OUT (Separate OS Window)")
    else:
        imgui.text_colored(imgui.ImVec4(0.4, 0.8, 1.0, 1.0), "◌ STATUS: DOCKED in Main Workbench")

    imgui.same_line(0, 15)
    imgui.text(f"Current Size: {int(cur_size.x)} x {int(cur_size.y)} px")
    imgui.spacing()

    imgui.text("Size Presets (using imgui.set_window_size):")
    presets = [
        ("640x480", 640, 480),
        ("800x600", 800, 600),
        ("1024x720", 1024, 720),
        ("1280x800", 1280, 800),
    ]
    for lbl, w, h in presets:
        if imgui.button(lbl):
            # Resizes only this window, not the main application window
            imgui.set_window_size("Secondary Pop-out Window", imgui.ImVec2(float(w), float(h)), imgui.Cond_.always)
        imgui.same_line()
    imgui.new_line()

    imgui.spacing()
    imgui.push_item_width(120)
    ch_w, new_w = imgui.slider_int("Width", int(cur_size.x), 400, 1920)
    imgui.same_line()
    ch_h, new_h = imgui.slider_int("Height", int(cur_size.y), 300, 1200)
    imgui.pop_item_width()

    if ch_w or ch_h:
        imgui.set_window_size("Secondary Pop-out Window", imgui.ImVec2(float(new_w), float(new_h)), imgui.Cond_.always)

    imgui.separator()
    imgui.spacing()

    if not is_docked:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.7, 0.4, 0.9))
        if imgui.button("🗗 Dock Back to Main Workbench (Pop In)", imgui.ImVec2(280, 30)):
            params = hello_imgui.get_runner_params()
            params.docking_params.layout_reset = True
        imgui.pop_style_color()
    else:
        imgui.text_wrapped("Hint: You can undock this tab by clicking and dragging its tab bar out into the desktop.")

def create_demo_runner_params(enable_viewports: bool = True) -> hello_imgui.RunnerParams:
    """Build HelloImGui RunnerParams with docking and optional viewports."""
    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Viewport Pop-Out Demo"
    runner_params.app_window_params.window_geometry.size = (1100, 700)
    runner_params.app_window_params.resizable = True

    # Enable full screen docking layout
    runner_params.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.provide_full_screen_dock_space
    )
    runner_params.imgui_window_params.enable_viewports = enable_viewports
    if enable_viewports:
        runner_params.callbacks.post_init = configure_viewports

    runner_params.imgui_window_params.show_menu_bar = True
    runner_params.imgui_window_params.show_status_bar = True

    # Windows
    win_main = hello_imgui.DockableWindow()
    win_main.label = "Controller & Checklist"
    win_main.dock_space_name = "MainDockSpace"
    win_main.gui_function = render_main_controller_window

    win_sec = hello_imgui.DockableWindow()
    win_sec.label = "Secondary Pop-out Window"
    win_sec.dock_space_name = "RightSpace"
    win_sec.gui_function = render_secondary_window

    # Split: 50% left (MainDockSpace), 50% right (RightSpace)
    split_right = hello_imgui.DockingSplit()
    split_right.initial_dock = "MainDockSpace"
    split_right.new_dock = "RightSpace"
    split_right.direction = imgui.Dir_.right
    split_right.ratio = 0.45

    runner_params.docking_params.docking_splits = [split_right]
    runner_params.docking_params.dockable_windows = [win_main, win_sec]

    return runner_params

def main():
    parser = argparse.ArgumentParser(description="AbstractX Multi-Viewport Pop-out Demo")
    parser.add_argument(
        "--viewports",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Enable multi-viewports (default: True). Use --no-viewports for single window.",
    )
    args = parser.parse_args()

    print("=====================================================")
    print("  AbstractX Minimal Viewport Pop-Out Demo")
    print(f"  Mode: {'Multi-Viewport' if args.viewports else 'Single Window (--no-viewports)'}")
    print("=====================================================")

    runner_params = create_demo_runner_params(enable_viewports=args.viewports)
    immapp.run(runner_params)

if __name__ == "__main__":
    main()
