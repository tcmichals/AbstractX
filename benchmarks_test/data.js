window.BENCHMARK_DATA = {
  "lastUpdate": 1793065941796,
  "repoUrl": "https://github.com/tcmichals/AbstractX",
  "entries": {
    "AbstractX Test Execution Metrics": [
      {
        "commit": {
          "author": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "committer": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "distinct": true,
          "id": "2dfa62a272b0e184c105b6093760ce396c5b622b",
          "message": "fix(ci): SPEC-PIO-05 named an uncommitted skill, failing the matrix step\n\ntrac.py test --matrix fails on any BROKEN requirement. [SPEC-PIO-05] listed\nhw-sw-register-parity, a skill (and tools/check_register_parity.py) that exists\nonly on the other PC, so CI has been red since 527ae15. It now names\ncocotb-regression and records the parity check as an open item, to restore\nwhen that work is pushed.\n\nEvidence (this PC):\n  ctest build_host 67/67 passed; ctest build_asan 67/67 passed\n  trac.py test --ctest debug=build_host --ctest asan=build_asan --matrix: exit 0\n  audit_specs.py exit 0; pytest tests/test_adversarial_invariants.py 14 passed\n  track_memory_membrowse.py / track_quality_trends.py exit 0 in a clean worktree\nNot run: TSan locally (passed in CI on a0791a0), the advisory steps' real\nresults, E906 cross-build, board.",
          "timestamp": "2026-10-26T10:55:52-05:00",
          "tree_id": "c0b195b902f97293859e3d827368c1bc499f480b",
          "url": "https://github.com/tcmichals/AbstractX/commit/2dfa62a272b0e184c105b6093760ce396c5b622b"
        },
        "date": 1793031123621,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "board:e906:repl-system announce_abstractx-repl",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system announce_abstractx-tlp",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system announce_abstractx-trace",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system idle_no_recipient",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system new_no_recipient",
            "value": 8,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system no_recipient_bounded",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system no_recipient_idle_silent",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system plane_isolation",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system repl_text",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlp_64B_all_received",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlp_payload_sequence",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlps",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlps_valid",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system trace_ctf_received",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system trace_datagrams",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors duration_s",
            "value": 5.22,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors gps_3d_fix_acquired",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors gps_serial_115200_active",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors i2c_compass_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors vector_synthesis_verified",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_received",
            "value": 84,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_attitude",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_gps",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_mag",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_valid_64b",
            "value": 84,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout seq_monotonic_rate",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout valid_gps_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout valid_yaw_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "cocotb:all tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "cocotb:all tests_failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan passed",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan test_time_s",
            "value": 1.12,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan passed",
            "value": 32,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan test_time_s",
            "value": 0.52,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan tests",
            "value": 32,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug passed",
            "value": 46,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug test_time_s",
            "value": 2.2,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug tests",
            "value": 46,
            "unit": "bytes"
          },
          {
            "name": "ctest:host failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:host passed",
            "value": 38,
            "unit": "bytes"
          },
          {
            "name": "ctest:host test_time_s",
            "value": 1.09,
            "unit": "bytes"
          },
          {
            "name": "ctest:host tests",
            "value": 38,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan passed",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan test_time_s",
            "value": 1.13,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "host:ctest failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-asan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-asan passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-tsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-tsan passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer passed",
            "value": 13,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer tests",
            "value": 13,
            "unit": "bytes"
          },
          {
            "name": "rtl:lint critical_warnings",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "rtl:lint modules",
            "value": 14,
            "unit": "bytes"
          },
          {
            "name": "rtl:synth-check modules",
            "value": 14,
            "unit": "bytes"
          },
          {
            "name": "rtl:synth-check multi_driver_modules",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix broken",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix failing",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix implemented",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix partial",
            "value": 6,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix spec_only",
            "value": 22,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix stale",
            "value": 74,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix total",
            "value": 179,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified",
            "value": 40,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified_pct",
            "value": 22.3,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "committer": {
            "email": "tcmichals@gmail.com",
            "name": "Tim Michals",
            "username": "tcmichals"
          },
          "distinct": true,
          "id": "c5a1eaa4ec6f9110982b3fe8d33d04e404188ccf",
          "message": "feat(zynq,e906,drivers): add Zynq target HAL tests, register parity audit, and E906 INAV/compass drivers\n\n- Add Zynq-7000 Target HAL & IoProcessor simulation test (sim/test_zynq_target.cpp)\n- Add Hardware-Software Register Parity auditor (tools/check_register_parity.py) and skill\n- Add Control TLP Wishbone access to zynq_diagnostics.py via UioRegion\n- Add HMC5883L / Compass driver abstractions and SPEC-MAG-01 in DESIGN_SPECIFICATION.md\n- Add E906 UART3 and TWI1 HAL drivers for GPS and compass\n- Add INAV E906 sensor coprocessor application (apps/inav_e906_app/)\n- Fix signal connection in asp_top.sv and add rtl/pio include to rtl-lint skill\n\nVerification:\n- build_host: 69/69 tests passed (100%)\n- build_asan: 69/69 tests passed (100%)\n- build_tsan: 69/69 tests passed (100%)\n- tools/spectrace.py --review: PASS FOR COMMIT (0 issues)\n- tools/audit_specs.py: 47/47 tagged (100.0% coverage)\n- tools/audit_boundaries.py: PASS (100% core purity)\n- tools/audit_interfaces.py: PASS (100% compliance)\n- tools/check_queue_concurrency.py: PASS (232 files checked, 0 defects)\n- tools/check_register_parity.py: PASS (100% register parity verified)\n- verilator lint: PASS (all RTL modules clean)\n\nNOT VERIFIED: Physical FPGA bitstream flashing on hardware, Cubie A5E remote hardware execution.",
          "timestamp": "2026-10-26T20:45:52-05:00",
          "tree_id": "5c7bb586977076a555d5e4e9a45fca7ce1314169",
          "url": "https://github.com/tcmichals/AbstractX/commit/c5a1eaa4ec6f9110982b3fe8d33d04e404188ccf"
        },
        "date": 1793065941365,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "board:e906:repl-system announce_abstractx-repl",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system announce_abstractx-tlp",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system announce_abstractx-trace",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system idle_no_recipient",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system new_no_recipient",
            "value": 8,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system no_recipient_bounded",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system no_recipient_idle_silent",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system plane_isolation",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system repl_text",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlp_64B_all_received",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlp_payload_sequence",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlps",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system tlps_valid",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system trace_ctf_received",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:repl-system trace_datagrams",
            "value": 10,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors duration_s",
            "value": 5.22,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors gps_3d_fix_acquired",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors gps_serial_115200_active",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors i2c_compass_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:e906:sensors vector_synthesis_verified",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_received",
            "value": 84,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_attitude",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_gps",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_tag_mag",
            "value": 28,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout packets_valid_64b",
            "value": 84,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout seq_monotonic_rate",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout valid_gps_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "board:system:checkout valid_yaw_detected",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "cocotb:all tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "cocotb:all tests_failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan passed",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan test_time_s",
            "value": 1.12,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan passed",
            "value": 32,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan test_time_s",
            "value": 0.52,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan-ubsan tests",
            "value": 32,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug passed",
            "value": 46,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug test_time_s",
            "value": 2.2,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug tests",
            "value": 46,
            "unit": "bytes"
          },
          {
            "name": "ctest:host failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:host passed",
            "value": 38,
            "unit": "bytes"
          },
          {
            "name": "ctest:host test_time_s",
            "value": 1.09,
            "unit": "bytes"
          },
          {
            "name": "ctest:host tests",
            "value": 38,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan passed",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan test_time_s",
            "value": 1.13,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan tests",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "host:ctest failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-asan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-asan passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-tsan failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "host:ctest-tsan passed",
            "value": 48,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer failed",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer passed",
            "value": 13,
            "unit": "bytes"
          },
          {
            "name": "pytest:visualizer tests",
            "value": 13,
            "unit": "bytes"
          },
          {
            "name": "rtl:lint critical_warnings",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "rtl:lint modules",
            "value": 14,
            "unit": "bytes"
          },
          {
            "name": "rtl:synth-check modules",
            "value": 14,
            "unit": "bytes"
          },
          {
            "name": "rtl:synth-check multi_driver_modules",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix broken",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix failing",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix implemented",
            "value": 37,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix partial",
            "value": 6,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix spec_only",
            "value": 22,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix stale",
            "value": 74,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix total",
            "value": 179,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified",
            "value": 40,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified_pct",
            "value": 22.3,
            "unit": "%"
          }
        ]
      }
    ]
  }
}