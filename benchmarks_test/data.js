window.BENCHMARK_DATA = {
  "lastUpdate": 1793110603094,
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
          "id": "2b3970ae04073bae814cd9e0ebddd0aa682c3746",
          "message": "fix(e906): remove inav_e906_app and polled UART3/TWI1 classes; E906 build links again\n\nc5a1eaa broke the E906 build: two polled driver classes in platform.cpp\nadded ~14 KB to every E906 app and e906_repl_app overflowed SRAM_FAST by\n6192 B. TWI1 init also remapped UART2's PB0/PB1, and both classes waited\non hardware inside start_hardware_transfer_from_isr.\n\nINAV belongs in its own repo (AGENTS.md section 14); the E906 runs\ngeneric AbstractX firmware. UART3/TWI1 become ports by moving the\ninterrupt-driven code from e906_repl_app/src/e906_periph.cpp into the\ntarget ([SPEC-E906-HAL-04], added spec-first).\n\nEvidence (this PC):\n- cmake --preset e906 && cmake --build build_e906: exit 0, all segments [ok]\n- analyze_elf_size.sh e906_repl_app e906_ddr.ld:\n  SRAM_FAST 185.1/192.0 KB (96.4%), SRAM_A3_2 45.1/128 KB, DRAM 0.00 MB\n- ctest --test-dir build_host: 69/69 passed\n- audit_specs.py: 47/47; audit_hal_invariants.py: PASS\n- spectrace_matrix.py --changes: 0 untraced hunks\n\nNot verified: board run; ASan/TSan not re-run (E906-only change).\nOther c5a1eaa defects (asp_uart_core.sv, Zynq TLP word order) are\nlisted in engineering_log.md and not fixed here.",
          "timestamp": "2026-10-27T08:34:14-05:00",
          "tree_id": "58e0c7e45e56cd7abb1d9f4c60a3ad3c01427723",
          "url": "https://github.com/tcmichals/AbstractX/commit/2b3970ae04073bae814cd9e0ebddd0aa682c3746"
        },
        "date": 1793108452348,
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
          "id": "1f520c15e1be3b3f4547fc174363a5ec88771c69",
          "message": "fix(e906): async I2C transfers honour timeout_us ([SPEC-HAL-09])\n\nE906I2c ignored I2cRequest::timeout_us: a slave holding the bus left the\nawaiting coroutine suspended forever.\n\nThe async start records a deadline and arms a timer wake;\nplatform_poll_network() (every step) calls poll_i2c_timeouts(), which\nunder an InterruptGuard ends a late transfer with STOP and completes it\nwith Timeout exactly once. No interrupt resumes a coroutine.\n[SPEC-E906-HAL-04] rule 8 added first.\n\npoll_timeout/finish are noinline: inlined per port they cost 1,908 B of\ntext in every E906 app; now ~200 B.\n\nEvidence (this PC):\n- sim/test_e906_twi: async write+read via transfer_async + Dispatcher,\n  async timeout once with STOP, 1000 async cycles with no pool growth;\n  fails with poll_timeout disabled\n- ctest build_host 70/70; build_asan 70/70, 0 reports;\n  build_tsan (setarch -R) 70/70, 0 reports\n- cmake --build build_e906 --clean-first: 12/12 segments [ok]\n- analyze_elf_size.sh e906_repl_app: SRAM_FAST 182.3/192.0 KB (94.9%),\n  DRAM 0.00 MB\n\nNot verified: board run.",
          "timestamp": "2026-10-27T08:55:42-05:00",
          "tree_id": "afe4340c7e795149c472a191ba4c48fc9888709e",
          "url": "https://github.com/tcmichals/AbstractX/commit/1f520c15e1be3b3f4547fc174363a5ec88771c69"
        },
        "date": 1793109677311,
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
          "id": "75001f86d426be2668e9d986135add8636092a32",
          "message": "refactor(e906): one TWI driver for TWI0 and TWI1, per-target port table\n\nThe E906 had two TWI drivers (target i2c.cpp for TWI0, app\ne906_periph.cpp for TWI1) plus a private E906I2c inside the IoProcessor.\nThe target TWI0 driver never cleared INT_FLG (write-1-to-clear), ignored\nthe shared CLIC line, set no pins and waited without a timeout.\n\n- [SPEC-HAL-15] (new): silicon facts per target in hal/ports.hpp; one\n  driver per controller type takes a port; apps use get_*_port only.\n- [SPEC-E906-HAL-04]: TWI rules (INT_FLG W1C, shared line 91, CCR\n  formula, error STOP, bounded wait hook, no TWI DMA until measured).\n- E906I2c built from the board-proven TWI1 state machine; TWI0 and TWI1\n  are two objects; get_i2c_port(1) = TWI1; IoProcessor uses the TWI0\n  object. REPL TWI1 code is glue only in apps/e906_repl_app/src/i2c.cpp.\n- sim/test_e906_twi.cpp: register model + slave, 1000-cycle steady\n  state; fails with the shared-line check removed.\n\nEvidence (this PC):\n- ctest build_host 70/70; build_asan 70/70, 0 reports;\n  build_tsan (setarch -R) 70/70, 0 reports\n- cmake --build build_e906: exit 0, 12/12 segments [ok]\n- text: e906_repl_app 184854 -> 181870, gps_imu_app 103216 -> 100840,\n  serial_loopback_app 62704 -> 60328, e906_bench_app 79750 -> 77370\n- analyze_elf_size.sh e906_repl_app: SRAM_FAST 182.0/192.0 KB (94.8%),\n  SRAM_A3_2 45.1/128 KB, DRAM 0.00 MB\n- audit_specs 100%, audit_hal_invariants PASS, spectrace --changes 0\n  untraced, --check-verifies: no new unbacked claims\n\nNot verified: board run (compass on TWI1, new CCR clock values).\nCCR changes TWI1 from 0x12/0x25 (200 kHz / ~15 kHz by the Linux\nformula) to 0x11/0x2A (400/100 kHz).",
          "timestamp": "2026-10-27T08:51:29-05:00",
          "tree_id": "b8692bdfdab721dc83aeb624b7afbd2881bf29f4",
          "url": "https://github.com/tcmichals/AbstractX/commit/75001f86d426be2668e9d986135add8636092a32"
        },
        "date": 1793110056109,
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
          "id": "1ac96a3861cacab8cdbb8efc80f66b420f127872",
          "message": "docs(log): review tracker EL-1027-01..36 for c5a1eaa and the E906/FPGA review\n\nOne engineering_log.md entry listing every finding from 2026-10-27 with a\nstable ID, status, fixing commit and the check that proves it, so work\ncan continue on either PC. 13 fixed (2b3970a, 75001f8, 1f520c1,\n15f125c; host and cross-build evidence only), 23 open, with a suggested\norder. Nothing was run on hardware.",
          "timestamp": "2026-10-27T09:08:27-05:00",
          "tree_id": "5a82bea8b908116fdc32c416b1fb99a1adf0783a",
          "url": "https://github.com/tcmichals/AbstractX/commit/1ac96a3861cacab8cdbb8efc80f66b420f127872"
        },
        "date": 1793110602392,
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