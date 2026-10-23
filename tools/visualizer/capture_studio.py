#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-STUDIO-15] [SPEC-STUDIO-11] [SPEC-STUDIO-17] [SPEC-STUDIO-13] tools/visualizer/SPECIFICATION.md
"""Capture Studio screenshots and short animations from the live board (article images).

Runs the real Studio (live link to the board, sensor stream running), walks through the CPU,
source, timeline and CLI tabs and saves, into OUT_DIR:
  studio-cpu.png, studio-source.png, studio-timeline.png, studio-cli.png
  studio-cpu.gif, studio-timeline.gif   (animated, for Hackster)
  studio-live.mp4                       (the same frames as a video, if ffmpeg is installed)

Stop any running Studio first (UDP port 9870 and the board's REPL are single-user).
Usage: python3 tools/visualizer/capture_studio.py OUT_DIR [--board-ip 192.168.3.3]
Frames are read from the OpenGL back buffer (glReadPixels via ctypes) just before each swap.
"""
import argparse
import ctypes
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import abstractx_studio as studio  # noqa: E402
import flight_plugin as fp  # noqa: E402
from imgui_bundle import hello_imgui, imgui, immapp, implot  # noqa: E402
from PIL import Image  # noqa: E402

GL_RGB, GL_UNSIGNED_BYTE, GL_PACK_ALIGNMENT, GL_BACK = 0x1907, 0x1401, 0x0D05, 0x0405

# (start s, action, tab, end s): the schedule after the link is up
WARMUP_S = 25.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out_dir")
    ap.add_argument("--board-ip", default="192.168.3.3")
    ap.add_argument("--host-ip", default="192.168.3.1")
    ap.add_argument("--port", type=int, default=9870)
    ap.add_argument("--size", default="1600x900")
    args = ap.parse_args()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    width, height = (int(v) for v in args.size.split("x"))

    gl = ctypes.CDLL("libGL.so.1")
    st = studio.g_state
    st.board_ip, st.host_ip, st.udp_port = args.board_ip, args.host_ip, args.port
    st.cli_sender = fp.send_cli
    fp.g_cli_handler = st.on_cli_line
    fp.start_board_bridge(args.board_ip, args.host_ip, args.port)
    threading.Thread(target=studio.udp_receiver_thread, args=(args.port, False), daemon=True).start()

    stills = {}
    gifs = {"cpu": [], "timeline": []}
    video = []
    t0 = time.monotonic()
    state = {"step": None}

    def grab() -> Image.Image:
        io = imgui.get_io()
        w = int(io.display_size.x * io.display_framebuffer_scale.x)
        h = int(io.display_size.y * io.display_framebuffer_scale.y)
        buf = (ctypes.c_ubyte * (w * h * 3))()
        gl.glPixelStorei(GL_PACK_ALIGNMENT, 1)
        gl.glReadBuffer(GL_BACK)
        gl.glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf)
        return Image.frombytes("RGB", (w, h), bytes(buf)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)

    last_frame = [0.0]

    def before_swap():
        t = time.monotonic() - t0
        step = None
        if t < 2.0:
            step = "layout"
        elif t < WARMUP_S:
            step = "warmup"
        elif t < WARMUP_S + 11:
            step = "cpu"
        elif t < WARMUP_S + 16:
            step = "source"
        elif t < WARMUP_S + 26:
            step = "timeline"
        elif t < WARMUP_S + 33:
            step = "cli"
        else:
            hello_imgui.get_runner_params().app_shall_exit = True
            return
        if step != state["step"]:  # entering a step: select its tab once
            state["step"] = step
            state["entered"] = t
            if step == "layout":
                studio.apply_docking_layout("core_focus")
            elif step in ("cpu", "source", "timeline", "cli"):
                st.requested_studio_tab = step
            if step == "source":
                agg = st.profile_aggregate(0)
                hot = next(((f, ln) for _, _, f, ln in (agg["funcs"] if agg else []) if f), None)
                if hot:
                    studio._jump_to_source(*hot)
            if step == "timeline":
                st.timeline_window_ms = 100.0
            if step == "cli":
                st.cli_mode = "AbstractX"
                for cmd in ("ps", "drivers", "abstractx.version()", "list(abstractx.i2c_read(0x1E, 0x0A, 3))"):
                    st.execute_cli_command(cmd)
        since = t - state["entered"]
        if step in ("cpu", "timeline") and since >= 1.0 and t - last_frame[0] >= 0.2:
            last_frame[0] = t
            img = grab()
            gifs[step].append(img)
            video.append(img)
        if step in ("cpu", "source", "timeline", "cli") and step not in stills and since >= 4.0:
            stills[step] = grab()

    runner = studio.create_docking_runner_params()
    runner.app_window_params.window_geometry.size = (width, height)
    runner.app_window_params.restore_previous_geometry = False
    runner.ini_filename = "studio_capture.ini"  # never touch the user's own layout file
    runner.callbacks.before_swap = before_swap
    runner.fps_idling.enable_idling = False  # keep rendering at full rate: nobody moves the mouse
    implot.create_context()
    try:
        immapp.run(runner)
    finally:
        implot.destroy_context()
        fp.stop_board_bridge()

    for name, img in stills.items():
        img.save(out / f"studio-{name}.png", optimize=True)
        print(f"saved {out / f'studio-{name}.png'} {img.size}")
    for name, frames in gifs.items():
        if frames:
            small = [f.resize((f.width * 3 // 4, f.height * 3 // 4)) for f in frames]
            small[0].save(out / f"studio-{name}.gif", save_all=True, append_images=small[1:],
                          duration=200, loop=0, optimize=True)
            print(f"saved {out / f'studio-{name}.gif'} ({len(frames)} frames)")
    if video and shutil.which("ffmpeg"):
        with tempfile.TemporaryDirectory() as d:
            for i, f in enumerate(video):
                f.save(Path(d) / f"f{i:04d}.png")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "5", "-i", str(Path(d) / "f%04d.png"),
                            "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2", "-pix_fmt", "yuv420p",
                            str(out / "studio-live.mp4")], check=True)
        print(f"saved {out / 'studio-live.mp4'} ({len(video)} frames at 5 fps)")
    missing = {"cpu", "source", "timeline", "cli"} - set(stills)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
