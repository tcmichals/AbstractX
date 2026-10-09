#!/usr/bin/env python3
"""
AbstractX SpecTrace Memory Tracker (Backward Compatibility Wrapper)
Redirects to tools/track_memory_footprint.py
"""
import sys
from pathlib import Path

# Add repo root to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.track_memory_footprint import run_analysis, audit_zero_heap

if __name__ == "__main__":
    sys.exit(run_analysis())
