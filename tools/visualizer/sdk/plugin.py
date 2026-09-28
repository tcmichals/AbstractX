"""
AbstractX Studio Plugin SDK
---------------------------
Base class and interfaces for building Level 2 extensible domain instruments
(e.g., flight deck, ESC configurator, rover telemetry, gimbal control)
on top of the AbstractX Studio Dear ImGui framework.
"""

from abc import ABC, abstractmethod
from typing import Dict, Any, Optional

class AbstractXStudioPlugin(ABC):
    """
    # @impl [SPEC-STUDIO-06] tools/visualizer/sdk/plugin.py
    Base class for AbstractX Studio domain plugins.
    
    Subclasses implement this interface to render custom domain instruments,
    consume decoded CTF 1.8 / 64-byte TLP packets, and add custom menu items
    without needing any knowledge of OpenGL/GLFW/windowing boilerplate.
    """
    
    def __init__(self, name: str, version: str = "1.0"):
        self.name = name
        self.version = version

    def on_init(self, context: Dict[str, Any]) -> None:
        """
        Called once during studio initialization.
        Use this to register fonts, color schemes, or setup ImPlot contexts.
        """
        pass

    def on_tlp_packet(self, tlp_header: Dict[str, Any], payload_dict: Dict[str, Any]) -> None:
        """
        Called by the CTF Schema Loader when a new 64-byte TLP matching
        this domain or application arrives over UDP / shared SRAM.
        """
        pass

    @abstractmethod
    def render_ui(self, delta_time: float = 0.0, state: Optional[Any] = None) -> None:
        """
        Called per-frame (at 120 FPS) to render domain-specific ImGui / ImPlot widgets.
        Can receive an optional shared application state dictionary/object.
        """
        pass

    def render_menu_items(self) -> None:
        """
        Optional: Called inside the main Studio menu bar to inject plugin-specific tools or actions.
        """
        pass
