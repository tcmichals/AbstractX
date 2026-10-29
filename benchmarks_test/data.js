window.BENCHMARK_DATA = {
  "lastUpdate": 1793272790135,
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
          "id": "15f125c60068bab91a61c0c75cef5225ecf0a466",
          "message": "refactor(e906): one UART driver (UART3), UART2 removed, e906_periph.cpp split\n\nThe E906 had two UART drivers: target Uart2 (interrupt never attached,\nso RX was polled and a full TX queue stalled) and the board-proven UART3\ncode in the REPL app. UART2's PB0/PB1 are not connected on the Cubie\nA5E (overlay: NC on the PCB), yet Pio::init, get_uart_driver(),\nserial_loopback_app and the silicon atlas rule all pointed at it.\n\n- [SPEC-E906-HAL-04] first: UART3 = ports 0 and 3, no UART2, UART\n  driver rules, one driver_wait hook; rule 4 states the measured link\n  cost instead of claiming unused ports are not linked.\n- E906Uart in targets/allwinner_e906/src/uart.cpp from the UART3 code:\n  circular DMA RX into UartRxRingBase (read_async works), IRQ fallback,\n  configure-at-busy baud, line errors, TX ring + THRE, TX DMA > FIFO.\n  kUart3 in hal/ports.hpp, buffers in .sram_c.\n- hal/driver_wait: one wait hook for TWI and UART.\n- REPL glue split by subject (src/uart.cpp, i2c.cpp, spi.cpp,\n  system.cpp); e906_periph.cpp deleted; CMake updated.\n- serial_loopback_app (E906) uses get_uart_port(3); atlas rule, HOWTOs\n  and memory_map.h corrected.\n- sim/test_e906_uart.cpp: register model + fake DMAC; 1000 DMA frames,\n  TX ring and TX DMA deadline; fails with the RX tail sync removed.\n\nEvidence (this PC):\n- ctest build_host 71/71; build_asan 71/71, 0 reports;\n  build_tsan (setarch -R) 71/71, 0 reports\n- cmake --build build_e906 --clean-first: 12/12 segments [ok]\n- text: e906_repl_app 182078 -> 182866; serial_loopback_app 60524 ->\n  64416; gps_imu_app 101032 -> 104944; e906_bench_app 77570 -> 81458\n- analyze_elf_size.sh e906_repl_app: SRAM_FAST 182.1/192.0 KB (94.8%),\n  SRAM_A3_2 45.1/128 KB, DRAM 0.00 MB\n- audit_specs 100%, audit_hal_invariants/boundaries/interfaces PASS,\n  spectrace --changes 0 untraced, --check-drift 0, --check-verifies\n  64 (unchanged)\n\nNot verified: board run (GPS on UART3, serial loopback on UART3,\ncompass on TWI1). Not covered by the host test: interrupt-mode RX and\nthe async write request.",
          "timestamp": "2026-10-27T09:05:11-05:00",
          "tree_id": "f77334ec74acbac59be3109cdde4bf1a8608b42c",
          "url": "https://github.com/tcmichals/AbstractX/commit/15f125c60068bab91a61c0c75cef5225ecf0a466"
        },
        "date": 1793110970943,
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
          "id": "7639a980ae45cae219b80280af86d157fd084900",
          "message": "fix(pio): RP2040-conformant state machine, SMx_INSTR, pioasm program library (EL-1027-37)\n\npull block on an empty FIFO advanced the PC: the stall flags were\nnon-blocking assignments to automatic locals. Also wrong: pull noblock,\nautopush (pre-shift ISR), autopull stall, mov isr/osr counts, mov\nstatus, side-set count with opt, irq wait, irq rel, and private\nEXECCTRL/PINCTRL/CLKDIV layouts.\n\n- asp_pio_sm.sv rewritten to the [SPEC-PIO-02] conformance list, RP2040\n  register layouts, SMx_INSTR (write 0x118+0x20*N); restart releases pins\n- tile: SMs see IRQ set/clear one clock after the instruction\n- rtl/pio/programs + tools/pio_programs.py: pioasm (Pico SDK 2.1.1)\n  images checked in, --check compares ([SPEC-PIO-11])\n- 15 conformance tests running pioasm output; autopush mutation fails 3\n\nEvidence (this PC):\n- tools/run_cocotb.py: 18/18 targets (pio-tile 5/5, conformance 15/15)\n- tools/pio_programs.py --check: 0 problems\n- verilator lint clean except top_qmtech_zynq7020 (EL-1027-13, existing)\n- audit_specs 100%, --check-drift 0, ctest build_host 71/71\n\nNot verified: fpga-fit with the larger SM, hardware.",
          "timestamp": "2026-10-27T09:32:52-05:00",
          "tree_id": "07f919b506fb18fd58c92aa15124766e538e75cc",
          "url": "https://github.com/tcmichals/AbstractX/commit/7639a980ae45cae219b80280af86d157fd084900"
        },
        "date": 1793112293523,
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
          "id": "67278320c6f205cc5fe22faecbd364a02a24c76f",
          "message": "feat(pio): bidirectional DShot600 and ESC passthrough as PIO programs; fix Gowin build (EL-1027-20, -40, -41)\n\nNo protocol logic in RTL ([SPEC-PIO-00]): both are pioasm programs the\nhost loads; only the divider changes per board clock.\n\n- dshot_bidir.pio (27 words): inverted DShot600 frame per TX word (no\n  repeat = failsafe), wire released, eRPM reply captured as 64 raw\n  samples; host decode reference tools/dshot_ref.py (Betaflight\n  algorithm)\n- esc_serial.pio (27 words): half-duplex 8N1 19200, one-bit guard then\n  release, glitch-rejecting RX, break by the host via SMx_INSTR\n- Gowin build broken since the PIO tile entered asp_top: synth script\n  never read rtl/pio; Yosys reader rejected return / unpacked ports\n  (fixed). First measured fit: LUT4 16126/20736 (77 %) with one tile\n  plus all old cores (EL-1027-41).\n- tools/run_cocotb.py --log DIR\n\nEvidence (this PC):\n- test_asp_pio_dshot_cocotb 5/5: timing within 1 clock + 3 % at 12/27/\n  50 MHz, eRPM decoded at nominal and +-3 % ESC clock; mutation fails 4/5\n- test_asp_pio_esc_serial_cocotb 3/3: 19200 8N1, guard, no echo, ESC\n  +-2 %, glitch, break; guard mutation fails\n- tools/run_cocotb.py: 20/20 targets; pio_programs --check 0 problems\n- verilator lint asp_pio_tile clean; ctest build_host 71/71\n- nextpnr utilisation (top_tang_nano20k): LUT4 77 %, DFF 56 %\n\nNot verified: placement/Fmax (running), hardware.",
          "timestamp": "2026-10-27T09:47:40-05:00",
          "tree_id": "45631ad24d95f6bf728d981476379980bc3fd03e",
          "url": "https://github.com/tcmichals/AbstractX/commit/67278320c6f205cc5fe22faecbd364a02a24c76f"
        },
        "date": 1793112963306,
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
          "id": "1910cacf5f77d9ece0ab6de9b862523ccaffd6cb",
          "message": "docs(log): EL-1027-41 measured: PIO tile 30,674 LUT4, does not fit the Tang Nano 20K\n\nyosys synth_gowin -noflatten on asp_pio_tile: ~7,000 LUT4 per state\nmachine (seven 32x32 pin crossbars each), 2,361 tile glue. The 77 %\nboard fit was low only because asp_top leaves the tile pins unconnected.\nBoard placement hung and was stopped. Plan recorded; no RTL change.",
          "timestamp": "2026-10-27T09:58:43-05:00",
          "tree_id": "2eee28c747464d5bb53b35e25f20de5f0322f621",
          "url": "https://github.com/tcmichals/AbstractX/commit/1910cacf5f77d9ece0ab6de9b862523ccaffd6cb"
        },
        "date": 1793113573871,
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
          "id": "05ffd6f3c95b4008a8055e8befa9c13bb77b7c13",
          "message": "perf(pio): tile 30,674 -> 7,701 LUT4: shared pin writers, LUT-RAM instruction memory (EL-1027-41)\n\nSeven 32x32 pin crossbars per state machine replaced by one\nrotate-and-mask writer for data and one for side-set (side-set wins on a\nshared pin), plus one shared input rotator. Instruction memory moved to\nLUT RAM (no reset; 40 RAM16SDP4).\n\nEvidence (this PC):\n- yosys synth_gowin -noflatten: SM ~7,000 -> ~1,760 LUT4; tile glue\n  2,361 -> 648 + 40 RAM16SDP4; tile 7,701 LUT4 (37 % of GW2AR-18)\n- tools/run_cocotb.py 20/20 (conformance 15/15, DShot 5/5, ESC 3/3)\n- verilator lint clean; spectrace --changes 0 untraced\n\nNot verified: board-level fit/Fmax, hardware. Tang Nano 9K does not fit\nthe full design (EL-1027-42).",
          "timestamp": "2026-10-27T10:03:56-05:00",
          "tree_id": "c5550b4d1c028c3e8322fe99535a33c0473b3ebe",
          "url": "https://github.com/tcmichals/AbstractX/commit/05ffd6f3c95b4008a8055e8befa9c13bb77b7c13"
        },
        "date": 1793114488866,
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
          "id": "ac489c40f2bb77090bbfdd70f534e31f630120cf",
          "message": "feat(pio): generic TLP data path: 32-slot ingress queue, egress rounds; area figure corrected (EL-1027-38, -41)\n\nSpec first: rtl/pio/SPECIFICATION.md [SPEC-PIO-10] wire format (SM mask in flags[3:0],\npayload word k at [351-32k -: 32]), group dealing, rounds, burst/full/idle-timeout send,\nregisters OUT_CFG 0x0C, OUT_TIMEOUT 0x90, IN_CFG 0x94, IN_STATUS 0x98; new 32-slot\ningress queue because asp_spi_frontend drops the next TLP while ingress is held.\n\n- rtl/pio/asp_pio_tlp_ingress.sv (new): queue in one block RAM (SDPX9B), ingress held\n  <= 11 clocks per TLP, a group is written to all selected TX FIFOs in one clock.\n- asp_pio_tlp_shim.sv: egress rounds from the SM mask, first-word timestamp, frame sent\n  from the word registers. Fix: data/trace arbitration changed tdata while tvalid waited\n  (AXI-Stream violation); test_trace_and_data_share_egress fails on the old rule\n  (\"egress tdata/tvalid changed before tready\") and passes now.\n- asp_pio_tile.sv: s_tlp_* port, registers, shared FIFO ports. asp_top: ingress tied off\n  until the router has PIO channels (EL-1027-39).\n\nEvidence (this PC):\n  python3 tools/run_cocotb.py -> 21/21 targets pass (new sim-test-pio-tlp-path 7/7;\n    conformance 15/15, dshot 5/5, esc-serial 3/3, tile 5/5)\n  verilator --lint-only -Wall (tile + SM + shim + ingress) -> clean\n  yosys ... hierarchy -top asp_pio_tile; proc; check -assert -> rc 0\n  audit_specs 100 %; spectrace_matrix --changes: 0 untraced; --check-drift: 0\n  synth_gowin -top asp_pio_tile (flattened), 05ffd6f -> this:\n    LUT1-4 22,461 -> 23,602; FF 3,726 -> 4,033; + 1 SDPX9B\n\nCorrection: the 7,701 LUT4 / \"37 % of GW2AR-18\" in 05ffd6f is wrong (the same\n-noflatten command gives 10,671 LUT4 today and counts no LUT1-3). One tile is\n~22-23k Yosys LUTs: it does not fit the Tang Nano 20K yet. Logged in engineering_log.md.\n\nNot verified: nextpnr fit / Fmax, asp_top with the tile connected, hardware.",
          "timestamp": "2026-10-27T12:05:36-05:00",
          "tree_id": "8fdceda92641107d56bb2d9fb1225f87a8bdee40",
          "url": "https://github.com/tcmichals/AbstractX/commit/ac489c40f2bb77090bbfdd70f534e31f630120cf"
        },
        "date": 1793121275537,
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
          "id": "e9b57c05dba34b9ac6cbf5ffb8ddb615947becfe",
          "message": "fix(pio): FIFO join and EXEC_STALLED; RP2040 datasheet checklist; vendor pico-examples (EL-1027-43..48)\n\nSpec first: [SPEC-PIO-02] rules 8 (FJOIN_TX/RX, flush on change) and 9 (latched SMx_INSTR,\nEXECCTRL.EXEC_STALLED); [SPEC-PIO-14] third-party program conformance; [SPEC-PIO-15]\nRP2040 feature table from the datasheet register list (done / missing / wrong / not planned).\n\n- asp_pio_sm.sv: TX and RX share 8 words per SM (FJOIN gives 8 deep, no extra FIFO flops);\n  FJOIN change empties both FIFOs; o_exec_latched. asp_pio_tile.sv: EXECCTRL[31] reads it.\n  Found by Betaflight's PIO UART (FJOIN) and OSD driver (pio_sm_exec_wait_blocking).\n- rtl/pio/programs/examples/pico/: 26 pico-examples .pio files (BSD-3, LICENSE.TXT,\n  raspberrypi/pico-examples @ 0d62f75b) + pioasm images.\n- tools/pio_examples.py: Betaflight programs (GPL-3.0) are read from $BETAFLIGHT_DIR\n  (default ../flightcode/betaflight, @ 49679ccd), never copied here (dual licence).\n  tools/pio_programs.py: assemble() takes a root.\n\nDatasheet findings logged, not yet fixed: pin latch per SM instead of per block\n(EL-1027-43); SM_RESTART changes PC/FIFOs/pins, rule 7 contradicts the datasheet\n(EL-1027-44); missing FDEBUG, FLEVEL, IRQ_FORCE, INTE/INTF/INTS, DBG_*, OUT_STICKY,\nINLINE_OUT_EN, CLKDIV 0 = 65536 (EL-1027-45).\n\nEvidence (this PC):\n  new tests fail on the previous RTL (test_fifo_join_tx_and_flush, test_fifo_join_rx_8_words,\n    test_exec_stalled_latches_until_done: FAIL) and pass now\n  python3 tools/run_cocotb.py -> 21/21 targets (conformance 18/18)\n  tools/pio_programs.py --check -> 29 sources, 0 problems; tools/pio_examples.py --list ->\n    41 programs assemble, all <= 32 words\n  verilator -Wall lint clean; yosys proc; check -assert rc 0\n  audit_specs OK; spectrace --changes 0 untraced; drift 0\n\nNot verified: functional tests of the 41 example programs (EL-1027-46), area of the\nFIFO change, nextpnr fit, hardware.",
          "timestamp": "2026-10-27T12:21:38-05:00",
          "tree_id": "af7fdc694174f8d92adaaff4b877e6c1680f27b9",
          "url": "https://github.com/tcmichals/AbstractX/commit/e9b57c05dba34b9ac6cbf5ffb8ddb615947becfe"
        },
        "date": 1793122502202,
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
          "id": "559022f70a16bd5e08602f2eb31dfef37956373e",
          "message": "Added support for better PIO support",
          "timestamp": "2026-10-27T12:48:12-05:00",
          "tree_id": "11d615f030b558f2e781d2f9b2add17493cc4223",
          "url": "https://github.com/tcmichals/AbstractX/commit/559022f70a16bd5e08602f2eb31dfef37956373e"
        },
        "date": 1793123879605,
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
          "id": "291ed11362d3af501e3c9e71b827987ec5444e7a",
          "message": "fix(core,targets): review fixes: one IoEngine, coroutine leaks, lost wakes, dead asserts, broken E906/Pico builds\n\nReview of AbstractX and all targets (engineering_log.md 2026-10-28, four entries).\n\nCoroutines [SPEC-ARCH-09/10/11]\n- when_any leaked a never-started loser (~470 calls emptied the pool, then a\n  null-handle segfault): Task::detach() destroys never-started tasks.\n- Semaphore: a woken waiter also took the next permit (two releases woke once).\n- Awaiting an empty Task (pool exhausted) reports via set_pool_exhausted_hook()\n  and traps instead of dereferencing null.\n\nI/O processor [SPEC-ARCH-13]\n- New include/abstractx/hal/io_engine.hpp: IoEngine + IIoBackend replace three\n  copied io_processor.cpp decoders (Linux, E906, Pico). Fixed with it: 32 B tx\n  buffer clocked for 64 B; host tx_len used as rx offset; one buffer shared by\n  all channels; DMA restarted in flight; E906/Pico swallowed every SPI/I2C/GPIO\n  bridge request; Linux SPI window could never match; GPIO 1 << pin >= 32.\n- Linux SpiWorker: unequal tx/rx rejected, atomic speed, stop() completes\n  queued requests as Cancelled (also I2cWorker).\n- E906 Spi1 DMA: second caller refused instead of overwriting the in-flight\n  descriptor and callback. E906 IoEngine back end uses the shared TWI0 driver\n  (get_e906_i2c(), upstream 75001f8 / EL-1027-06).\n\nLost wakes [SPEC-HAL-10] [SPEC-HAL-13]\n- UartRxRing and TriggerRing: wait/wake handshake was store->load on both\n  sides with release/acquire only; both could miss, reader never woken.\n  sim_test_uart_rx_ring hung in 3/300 runs at c5a1eaa (TSan silent).\n  seq_cst fence between publish and check on every path.\n- Dead E906 Timer::set_compare path removed\n  (its deadline was overwritten by the ISR); e906_freertos_app enabled the soft\n  IRQ line before its flag; Trace::putc S_UART0 spin bounded.\n\nBuilds\n- E906 Release was -O3 (CMake appends its default after *_RELEASE_INIT).\n  Toolchain seeds -O2 (CMakeCache CMAKE_CXX_FLAGS_RELEASE=-O2 -g -DNDEBUG).\n- Pico 2 W did not compile (MpscRingBuffer<uint8_t,512> vs ETL small model):\n  memory model follows capacity.\n\nChecks that missed all this, fixed [SPEC-AUDIT-15]\n- boundary_review.py now scans waiter slots incl. class members and fails on\n  HANDSHAKE (store->load with no seq_cst fence); `// handshake: <why>` waiver.\n  On c5a1eaa: 13 HANDSHAKE; now 0. All 22 unnoted shared variables answered.\n- sim/tests/examples build with -UNDEBUG: 11 sim tests (~276 asserts) had their\n  checks compiled out in the Release host build. sim_test_asserts_live guards it.\n- audit_ipc_invariants.py: comment \"nothing to return\" read as a return (false\n  IPC-STALL); bridge lease retry waived with reason. Audit is green again.\n- sim/test_coro_failures.cpp: <iostream> -> <cstdio>.\n\nRegression tests (each fails on the old code)\n- sim/test_coro_combinators.cpp case 6 (old: segfault)\n- tests/test_coro_primitives.cpp SemaphoreHandoffKeepsEveryPermit,\n  DetachNeverStartedTaskFreesFrame (old: 2 failures)\n- sim/test_coro_failures.cpp pool-exhausted death test (old: no hook)\n- sim/test_io_engine.cpp (new engine; clamps, overrun, lockout, slot pool,\n  bridge routing, 1000 rounds flat)\n- sim/test_linux_hal_completion.cpp [6][7] (old: status 0, 9/200 stranded)\n- sim/test_wake_handshake.cpp: 300,000 lockstep rounds per ring with watchdog.\n  Old headers 10/10 fail (\"UartRxRing: lost wake after 1237 of 300000 rounds\",\n  \"TriggerRing: lost wake after 41389 ...\"); new 0/20; uart_rx_ring 0/300.\n- tests/test_boundary_review.py, tests/test_ipc_invariants.py (old tool fails),\n  sim/test_asserts_live.cpp (g++ -DNDEBUG: #error)\n\nResults (rebased onto 559022f, this tree)\n- ctest host 76/76, ASan/UBSan 76/76, TSan 76/76 (no setarch needed on this\n  PC), 0 sanitizer reports (trac.py test --ctest debug/asan/tsan --record)\n- ctest --repeat until-fail:20 on uart_rx_ring|wake_handshake|bus_protocols|\n  linux_hal_completion|io_engine: host/ASan/TSan 5/5\n- E906 fresh build -O2 (riscv-none-elf-gcc 15.2.0), analyze_elf_size.sh\n  e906_repl_app: SRAM 0x3FFC0000 192.0 KB used 174.4 KB free 17.6 KB 90.8%\n  (upstream at -O3: 94.8%); .text 143,964 B, .bss 24,528 B, stack 8 KB.\n  No LOAD segment at 0x48000000+ (DRAM 0) and check_e906_layout ok for\n  e906_repl_app, gps_imu_app, serial_loopback_app, e906_bench_app,\n  e906_freertos_app. size (text data bss): e906_repl_app 173720 408 50676;\n  gps_imu_app 95318 424 49436; serial_loopback_app 55994 380 45760.\n- Pico 2 W build: gps_imu_app.elf, serial_loopback_app.elf\n- audit_specs, --check-drift, --check-skills, audit_boundaries,\n  audit_interfaces, audit_coro_contracts, boundary_review --check (24 shared,\n  0 missing, 0 handshake), audit_ipc_invariants: all rc 0.\n  --check-verifies: 65 unbacked, same as origin/main (EL-1027-32).\n  Matrix: 34 verified, 136 stale, 0 failing of 232.\n\nboundary-race-review checklist (touched boundaries)\n- UartRxRing / TriggerRing: one producer (ISR/thread), one reader; atomics +\n  seq_cst fence on both sides; check-then-act closed by the fence; reader\n  re-checks after publishing; test drives a real producer thread.\n- E906 timer deadlines: main under InterruptGuard, ISR with IRQs masked, single\n  hart, 64-bit values never read unguarded.\n- RPMsg doorbell ISR: waiter CAS-publishes then unmasks the level IRQ (same\n  hart), so no fence needed; recorded as `// handshake:`.\n- E906 UART/SPI DMA busy flags: test-and-set under InterruptGuard; ISR clears.\n\nNot verified\n- No board run: E906 is now -O2 (silicon was tested at -O3); run\n  e906-board-selftest. No Pico or ESP32-P4 hardware run.\n- E906 and Pico IoEngine I2C still block inside start_transfer() (the new E906\n  TWI driver has an async path: next); E906 DRDY hook not attached.\n- Upstream's new UART3 driver and split REPL files were only re-annotated\n  (boundary notes), not re-reviewed line by line.\n- Pico cross-core: bare-metal InterruptLock has one global depth/saved state\n  and excludes no other core (next commit). No RTL/cocotb run (no RTL changed).",
          "timestamp": "2026-10-28T17:45:13-05:00",
          "tree_id": "d6cbf62a88d2d50842ce0955df12cf1484749688",
          "url": "https://github.com/tcmichals/AbstractX/commit/291ed11362d3af501e3c9e71b827987ec5444e7a"
        },
        "date": 1793227948066,
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
            "value": 76,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan test_time_s",
            "value": 6.19,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan tests",
            "value": 76,
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
            "value": 76,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug test_time_s",
            "value": 9.68,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug tests",
            "value": 76,
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
            "value": 76,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan test_time_s",
            "value": 6.65,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan tests",
            "value": 76,
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
            "value": 31,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix partial",
            "value": 5,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix spec_only",
            "value": 26,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix stale",
            "value": 136,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix total",
            "value": 232,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified",
            "value": 34,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified_pct",
            "value": 14.7,
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
          "id": "7be72793ecd382e91b416996a5039abc017cef8a",
          "message": "fix(e906,core): RPMsg never announced since d8bcbac; awaiters without a waker; cross-core lock; first board run\n\nE906 RPMsg [SPEC-E906-IPC-02] (found on the board)\n- Since d8bcbac every e906_repl_app booted but Linux created no channel:\n  translate_buf() accepted only the SRAM pool 0x072E0000, while the board's\n  cubie-a5e-flight-stack-e906 overlay puts the vrings at vdev@48000000 (DDR,\n  1 MB; resource table vring0 0x48000000, vring1 0x48002000), so setup_vq()\n  refused them. Bisected on the board: d8bcbac~1 announces, d8bcbac and every\n  later main (2dfa62a 2b3970a 75001f8 15f125c 559022f) do not.\n- s_windows[] = both overlay pools, nothing else.\n- tests/test_e906_rpmsg_windows.py: every overlay shared-dma-pool must be a\n  firmware window (old rpmsg.cpp: \"firmware refuses these RPMsg pools: ...\n  flight-stack-e906.dtso: pool 0x48000000+0x100000; ...\").\n\nCoroutines [SPEC-ARCH-10]\n- delay_* / sleep_* / wait_until / AsyncDelayAwaiter kept no handle (only a\n  hand-written resume could continue them): moved to examples/hand_clocked.hpp.\n  Unused TimerService (dropped handles when full) removed. Skill and article\n  pointed at a non-existent coro::sleep_for(chrono): now the timer driver.\n- AsyncTimerDriver::SleepAwaiter: refused submit reported Ok, now Error\n  (TimerSubmitFailureIsAnError; old header: Failure in that TEST).\n- audit_coro_contracts.py check 5: public await_suspend that drops its handle\n  (tests/test_coro_contracts.py; old asp_coro.hpp: 4 hits).\n\nCross-core lock [SPEC-ARCH-01]\n- Bare-metal InterruptLock had one global depth/saved state and excluded no\n  other core. SmpInterruptLock<Platform, Cores> (per-core depth/saved state +\n  shared spinlock); _from_isr shortcuts take the lock on SMP\n  (ABSTRACTX_LOCKED_ISR_PATHS); Pico 2 W ABSTRACTX_SMP_CORES=2,\n  abstractx_core_id() = get_core_num(). E906 unchanged (single core).\n- sim/test_smp_lock.cpp, two threads as cores. Old algorithm\n  (-DABSTRACTX_TEST_LEGACY_LOCK): \"exclusion: counter 398348 of 400000,\n  overlap 1\", \"shared queue: 57001 of 100000 items, in order 0\". New: pass.\n\nAlso: two examples relied on <unordered_map> from the removed header; TODO\nreconciled with the other PC's tracker EL-1027-NN.\n\nResults (this tree)\n- Board (Cubie A5E, kernel #10 SMP PREEMPT_RT, overlays\n  cubie-a5e-flight-stack-e906 cubie-a5e-msgbox-txirq), e906_repl_app from this\n  tree at -O2: run_board_tests.py 31 passed, 2 failed, 2 skipped (bench\n  firmware not loaded); docs/verification/evidence/e906_selftest_20261028.json.\n  The 2 failures also fail on the board's previous firmware (2026-10-24):\n  test_streamer_keeps_running_with_heartbeat (\"NMEA sentences failed their\n  checksum\"), test_profiler_and_timeline_when_idle (\"1021 idle spans: sample\n  interrupts woke the loop\"; old firmware 1028). Open in TODO.\n  Board restored afterwards (riscv-firmware.elf, original e906_repl_app.elf).\n- ctest host 79/79, ASan/UBSan 79/79, TSan 79/79, 0 sanitizer reports;\n  --repeat until-fail:20 smp_lock|uart_rx_ring|wake_handshake|coro_primitives\n  host/ASan/TSan 4/4 (TSan 896 s).\n- E906 -O2: analyze_elf_size.sh e906_repl_app SRAM 192.0 KB used 174.5 KB\n  free 17.5 KB 90.9%; no LOAD segment at 0x48000000+ (DRAM 0); layout ok for\n  e906_repl_app 173828/408/50676, gps_imu_app 95438/424/49436,\n  serial_loopback_app 56098/380/45760, e906_bench_app 61636/384/47164,\n  e906_freertos_app 66482/392/58852 (text/data/bss).\n- Pico 2 W build ok with ABSTRACTX_SMP_CORES=2.\n- audit_specs, --check-drift, --check-skills, audit_boundaries,\n  audit_interfaces, audit_coro_contracts, boundary_review --check,\n  audit_ipc_invariants: rc 0. --check-verifies 64 (main: 65). 0 untraced.\n  Matrix 57 verified, 113 stale, 0 failing of 232.\n\nboundary-race-review: SmpInterruptLock flag_ shared by all cores\n(test_and_set acquire / clear release); depth_/saved_ per core, written by\ntheir own core with its IRQs masked; nested lock never re-enables early\n(tested); ISR on one core vs consumer on the other now both lock.\n\nNot verified\n- SMP lock on RP2350 silicon (Core 1 not launched yet); ESP32-P4 has no SMP\n  define. SRAM-pool overlay on silicon. Bench / FreeRTOS firmware board tests.",
          "timestamp": "2026-10-28T18:51:47-05:00",
          "tree_id": "d9175c56495ce1540c0b4c9904cf60be3c23b1dc",
          "url": "https://github.com/tcmichals/AbstractX/commit/7be72793ecd382e91b416996a5039abc017cef8a"
        },
        "date": 1793232009198,
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
            "value": 79,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan test_time_s",
            "value": 25.71,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan tests",
            "value": 79,
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
            "value": 79,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug test_time_s",
            "value": 26.63,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug tests",
            "value": 79,
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
            "value": 79,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan test_time_s",
            "value": 41.23,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan tests",
            "value": 79,
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
            "value": 31,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix partial",
            "value": 5,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix spec_only",
            "value": 26,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix stale",
            "value": 113,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix total",
            "value": 232,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified",
            "value": 57,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified_pct",
            "value": 24.6,
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
          "id": "4289a958ca8e7fc4b4c16e0354cb9fe897833978",
          "message": "fix(coro,esp32p4): CoroutineIoEngine resumed inline; every .resume() needs a reason; ESP32-P4 stubs refuse\n\n[SPEC-ARCH-10] who may call .resume()\n- CoroutineIoEngine::poll_completions() resumed awaiters inline; now\n  Dispatcher::push(). A second stream waiter on a channel overwrote the first\n  handle; now refused (empty TLP). sim/sitl_coro_sim.cpp pumps the Dispatcher.\n- audit_coro_contracts.py check 6: every .resume() in include/ and src/ needs a\n  `// resume: <why>` (the old check only looked at lines with await_/notify_).\n  8 legitimate starts annotated; the 2 inline wakes were the only others.\n\n[SPEC-ESP32P4-HAL-00] not implemented = NotSupported\n- Esp32p4Spi init false, transfers NotSupported/0 bytes (was Ok with tx size);\n  Esp32p4Mailbox send false, doorbells NotSupported (was Ok);\n  Esp32p4Timer completes sleeps with Error (was abstract: could not be\n  instantiated). MailboxStatus::NotSupported added.\n\nRegression tests (fail on the old code)\n- sim/test_coroutine_io_engine.cpp: old header \"Assertion `resumes == 0 &&\n  !t.done()' failed\"; posted completion, refused second waiter, 1000 rounds flat.\n- tests/test_coro_contracts.py resume cases (old tree: 10 unannotated sites).\n- sim/test_esp32p4_stubs.cpp: old stubs \"cannot declare variable 'timer' to be\n  of abstract type\", \"Assertion `!spi.init(SpiConfig{})' failed\".\n\nResults (this tree)\n- ctest host 81/81, ASan/UBSan 81/81, TSan 81/81, 0 sanitizer reports\n  (trac.py --record)\n- E906 -O2 e906_repl_app SRAM 174.5/192.0 KB (90.9%), DRAM 0, layout ok;\n  text/data/bss 173828/408/50676. Pico 2 W build ok.\n- audit_specs, drift, skills, boundaries, interfaces, coro contracts,\n  boundary_review --check, ipc audit: rc 0; --check-verifies 64.\n\nNot verified: ESP32-P4 under ESP-IDF (not built here); no board run for this\ncommit (E906 code unchanged by it).",
          "timestamp": "2026-10-29T06:11:48-05:00",
          "tree_id": "bda6f5645077a56516303486453fd0d541fdcce6",
          "url": "https://github.com/tcmichals/AbstractX/commit/4289a958ca8e7fc4b4c16e0354cb9fe897833978"
        },
        "date": 1793272789538,
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
            "value": 81,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan test_time_s",
            "value": 6.68,
            "unit": "bytes"
          },
          {
            "name": "ctest:asan tests",
            "value": 81,
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
            "value": 81,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug test_time_s",
            "value": 6.53,
            "unit": "bytes"
          },
          {
            "name": "ctest:debug tests",
            "value": 81,
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
            "value": 81,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan test_time_s",
            "value": 7.47,
            "unit": "bytes"
          },
          {
            "name": "ctest:tsan tests",
            "value": 81,
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
            "value": 31,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix partial",
            "value": 5,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix spec_only",
            "value": 26,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix stale",
            "value": 114,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix total",
            "value": 233,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified",
            "value": 57,
            "unit": "bytes"
          },
          {
            "name": "spectrace:matrix verified_pct",
            "value": 24.5,
            "unit": "%"
          }
        ]
      }
    ]
  }
}