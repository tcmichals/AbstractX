"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

@impl [SPEC-STUDIO-08] [SPEC-STUDIO-04] tools/visualizer/SPECIFICATION.md

AbstractX Visualizer Studio: AsyncIO High-Throughput Bridge & Telemetry Ingress Engine
-------------------------------------------------------------------------------------
Provides an asynchronous, non-blocking telemetry and subprocess communication pipeline:
1. TelemetryDatagramProtocol: Event-driven UDP datagram receiver consuming 64-byte TLPs
   and dynamic CTF 1.8 streams with zero thread polling.
2. AsyncBridgeController: Asynchronous process lifecycle manager for board SSH bridges
   (board_studio_bridge.py and trace endpoints) with streaming stdout/stderr parsing.
3. StudioAsyncEngine: Dedicated background asyncio event loop worker with threadsafe
   entrypoints for Dear ImGui.
"""

from __future__ import annotations

import asyncio
import collections
import socket
import struct
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional


class TelemetryDatagramProtocol(asyncio.DatagramProtocol):
    """
    Event-driven UDP datagram receiver for AbstractX Studio.
    Processes incoming 64-byte TLPs and CTF trace packets without busy-waiting.
    """

    def __init__(self, state: Any, runtime_stats_module: Any = None):
        self.state = state
        self.rs = runtime_stats_module
        self.transport: Optional[asyncio.DatagramTransport] = None
        self.start_t = time.time()

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if not data:
            return

        with self.state.lock:
            self.state.connected = True
            self.state.packet_count += 1
            self.state.bytes_received += len(data)
            self.state.rate_counter += 1

        # Check if CTF trace stream (stream index at offset 4)
        if self.rs and self.rs.is_ctf(data):
            self.state.ingest_ctf(data)
            return

        # Sliced 64-byte TLP processing
        if len(data) >= 64:
            for off in range(0, len(data) - 63, 64):
                frame = data[off : off + 64]
                try:
                    le_type, _flags, le_tag, le_ch, _addr, _len, le_seq, le_ts = struct.unpack(
                        "<BBBBIHHQ", frame[:20]
                    )
                    self.state.add_tlp_packet(frame, le_type, le_tag, le_ch, le_seq, le_ts, True)
                except struct.error:
                    continue

    def error_received(self, exc: Exception) -> None:
        if hasattr(self.state, "add_log"):
            self.state.add_log("WARN", "async_udp", f"UDP socket error: {exc}")

    def connection_lost(self, exc: Optional[Exception]) -> None:
        self.transport = None


class AsyncBridgeController:
    """
    Manages background SSH telemetry bridge subprocesses with asynchronous streaming I/O.
    Eliminates pipe-filling stalls and ensures zero UI hitching during command dispatch.
    """

    def __init__(
        self,
        state: Any,
        log_deque: Optional[collections.deque] = None,
        cli_handler: Optional[Callable[[str], None]] = None,
    ):
        self.state = state
        self.log_deque = log_deque if log_deque is not None else collections.deque(maxlen=200)
        self.cli_handler = cli_handler
        self.bridge_proc: Optional[asyncio.subprocess.Process] = None
        self.trace_proc: Optional[asyncio.subprocess.Process] = None
        self.drain_tasks: list[asyncio.Task] = []
        self._tx_queue: asyncio.Queue[str] = asyncio.Queue(maxsize=256)
        self._tx_task: Optional[asyncio.Task] = None
        self._running = False

    async def _drain_stream(self, stream: asyncio.StreamReader, is_trace: bool = False) -> None:
        try:
            while not stream.at_eof():
                line_bytes = await stream.readline()
                if not line_bytes:
                    break
                line = line_bytes.decode(errors="replace").rstrip()
                if not line:
                    continue

                if line.startswith("!CLI:") and self.cli_handler is not None:
                    self.cli_handler(line)
                else:
                    self.log_deque.append(line)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            if hasattr(self.state, "add_log"):
                self.state.add_log("WARN", "bridge_drain", f"Drain error: {e}")

    async def _tx_worker(self) -> None:
        """Asynchronously writes queued CLI commands to the bridge process stdin."""
        while self._running:
            try:
                line = await self._tx_queue.get()
                if line is None:
                    break
                if self.bridge_proc and self.bridge_proc.stdin:
                    self.bridge_proc.stdin.write(line.encode())
                    await self.bridge_proc.stdin.drain()
            except asyncio.CancelledError:
                break
            except (BrokenPipeError, ConnectionResetError, OSError):
                break
            finally:
                self._tx_queue.task_done()

    async def start(
        self,
        board_ip: str = "192.168.3.3",
        host_ip: str = "192.168.3.1",
        port: int = 9870,
    ) -> bool:
        await self.stop()
        self._running = True
        self._tx_task = asyncio.create_task(self._tx_worker())

        bridge_cmd = [
            "ssh", "-o", "StrictHostKeyChecking=no", f"root@{board_ip}",
            f"EXIT_ON_STDIN_EOF=1 STUDIO_HOST_IP={host_ip} STUDIO_PORT={port} STREAM_DURATION=0 python3 -u /root/board_studio_bridge.py"
        ]
        trace_cmd = [
            "ssh", "-o", "StrictHostKeyChecking=no", f"root@{board_ip}",
            f"python3 -u /root/e906_rpmsg_bridge.py trace --udp-host {host_ip} --udp-port {port} --exit-on-stdin-eof"
        ]

        try:
            self.bridge_proc = await asyncio.create_subprocess_exec(
                *bridge_cmd,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            if self.bridge_proc.stdout:
                self.drain_tasks.append(asyncio.create_task(self._drain_stream(self.bridge_proc.stdout)))

            self.trace_proc = await asyncio.create_subprocess_exec(
                *trace_cmd,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            if self.trace_proc.stdout:
                self.drain_tasks.append(asyncio.create_task(self._drain_stream(self.trace_proc.stdout, is_trace=True)))

            if hasattr(self.state, "add_log"):
                self.state.add_log("BRIDGE", "asyncio", f"Async board bridge connected to {board_ip}")
            return True
        except Exception as e:
            if hasattr(self.state, "add_log"):
                self.state.add_log("ERR", "async_bridge", f"Failed to start bridge: {e}")
            await self.stop()
            return False

    async def send_cli(self, cid: str, code_hex: str) -> bool:
        if not self._running or self.bridge_proc is None or self.bridge_proc.returncode is not None:
            return False
        try:
            self._tx_queue.put_nowait(f"CLI {cid} {code_hex}\n")
            return True
        except asyncio.QueueFull:
            return False

    def is_running(self) -> bool:
        return (
            self._running
            and self.bridge_proc is not None
            and self.bridge_proc.returncode is None
        )

    async def stop(self) -> None:
        self._running = False

        if self._tx_task:
            self._tx_task.cancel()
            self._tx_task = None

        for t in self.drain_tasks:
            t.cancel()
        self.drain_tasks.clear()

        for proc in (self.bridge_proc, self.trace_proc):
            if proc and proc.returncode is None:
                try:
                    proc.terminate()
                    await asyncio.wait_for(proc.wait(), timeout=1.0)
                except (asyncio.TimeoutError, ProcessLookupError, Exception):
                    try:
                        proc.kill()
                    except Exception:
                        pass

        self.bridge_proc = None
        self.trace_proc = None


class StudioAsyncEngine:
    """
    Dedicated background asyncio worker thread for AbstractX Studio.
    Hosts event-driven UDP ingestion and non-blocking board bridges.
    """

    def __init__(self, state: Any, runtime_stats_module: Any = None):
        self.state = state
        self.rs = runtime_stats_module
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run_event_loop, daemon=True, name="StudioAsyncEngine")
        self.udp_transport: Optional[asyncio.DatagramTransport] = None
        self.udp_protocol: Optional[TelemetryDatagramProtocol] = None
        self.bridge_controller = AsyncBridgeController(state)
        self.thread.start()

    def _run_event_loop(self) -> None:
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def start_udp_listener(self, port: int = 9870) -> bool:
        """Starts the event-driven UDP datagram server on the background asyncio loop."""
        fut = asyncio.run_coroutine_threadsafe(self._async_start_udp(port), self.loop)
        try:
            return fut.result(timeout=3.0)
        except Exception:
            return False

    async def _async_start_udp(self, port: int) -> bool:
        await self._async_stop_udp()
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                # 2 MB buffer to absorb 8 kHz bursts without kernel drops
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
            except OSError:
                pass
            sock.bind(("0.0.0.0", port))
            sock.setblocking(False)

            self.udp_transport, self.udp_protocol = await self.loop.create_datagram_endpoint(
                lambda: TelemetryDatagramProtocol(self.state, self.rs),
                sock=sock,
            )
            return True
        except Exception as e:
            if hasattr(self.state, "add_log"):
                self.state.add_log("ERR", "async_udp", f"Failed to bind async UDP :{port}: {e}")
            return False

    def stop_udp_listener(self) -> None:
        fut = asyncio.run_coroutine_threadsafe(self._async_stop_udp(), self.loop)
        try:
            fut.result(timeout=2.0)
        except Exception:
            pass

    async def _async_stop_udp(self) -> None:
        if self.udp_transport:
            self.udp_transport.close()
            self.udp_transport = None
            self.udp_protocol = None

    def start_board_bridge(
        self,
        board_ip: str = "192.168.3.3",
        host_ip: str = "192.168.3.1",
        port: int = 9870,
    ) -> bool:
        fut = asyncio.run_coroutine_threadsafe(
            self.bridge_controller.start(board_ip, host_ip, port), self.loop
        )
        try:
            return fut.result(timeout=3.0)
        except Exception:
            return False

    def stop_board_bridge(self) -> None:
        fut = asyncio.run_coroutine_threadsafe(self.bridge_controller.stop(), self.loop)
        try:
            fut.result(timeout=2.0)
        except Exception:
            pass

    def send_cli(self, cid: str, code_hex: str) -> bool:
        """Threadsafe non-blocking CLI dispatch to the async bridge."""
        fut = asyncio.run_coroutine_threadsafe(
            self.bridge_controller.send_cli(cid, code_hex), self.loop
        )
        try:
            return fut.result(timeout=0.5)
        except Exception:
            return False

    def get_bridge_status(self) -> bool:
        return self.bridge_controller.is_running()

    def shutdown(self) -> None:
        """Cleanly shuts down all async transports and stops the background loop."""
        try:
            self.stop_board_bridge()
            self.stop_udp_listener()
            self.loop.call_soon_threadsafe(self.loop.stop)
            self.thread.join(timeout=1.0)
        except Exception:
            pass
