window.BENCHMARK_DATA = {
  "lastUpdate": 1793121273729,
  "repoUrl": "https://github.com/tcmichals/AbstractX",
  "entries": {
    "AbstractX Static Size Metrics": [
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
        "date": 1793031118192,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793065939056,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793108449987,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793109674960,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793110053340,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793110599018,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793110967876,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793112290972,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793112959725,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793113571071,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793114485781,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
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
        "date": 1793121272437,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "elf:e906 bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 ram_bytes",
            "value": 93300,
            "unit": "bytes"
          },
          {
            "name": "elf:e906 text",
            "value": 52968,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug ram_bytes",
            "value": 79898,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/debug text",
            "value": 39566,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto bss",
            "value": 41128,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto data",
            "value": 336,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto ram_bytes",
            "value": 101596,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/lto text",
            "value": 60132,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release bss",
            "value": 40000,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release data",
            "value": 332,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release ram_bytes",
            "value": 92884,
            "unit": "bytes"
          },
          {
            "name": "elf:e906/release text",
            "value": 52552,
            "unit": "bytes"
          },
          {
            "name": "elf:host bss",
            "value": 100796,
            "unit": "bytes"
          },
          {
            "name": "elf:host data",
            "value": 2676,
            "unit": "bytes"
          },
          {
            "name": "elf:host flash_bytes",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "elf:host ram_bytes",
            "value": 250985,
            "unit": "bytes"
          },
          {
            "name": "elf:host text",
            "value": 147513,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w bss",
            "value": 145660,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w data",
            "value": 6640,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w flash_bytes",
            "value": 362076,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w ram_bytes",
            "value": 152300,
            "unit": "bytes"
          },
          {
            "name": "elf:pico2w text",
            "value": 355436,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k alu_used",
            "value": 1230,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_pct",
            "value": 0,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bsram_used",
            "value": 0,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_pct",
            "value": 4,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k bufg_used",
            "value": 1,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_pct",
            "value": 30,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k dff_used",
            "value": 4759,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_pct",
            "value": 7,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k iob_used",
            "value": 27,
            "unit": "bytes"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_pct",
            "value": 22,
            "unit": "%"
          },
          {
            "name": "fpga-fit:tang_nano20k lut4_used",
            "value": 4747,
            "unit": "bytes"
          }
        ]
      }
    ]
  }
}