# Third-Party Code in AbstractX

AbstractX is offered under GPL-3.0 and a commercial license (AGENTS.md section 11). Third-party code
it uses keeps its own license; each entry says how AbstractX complies, for both licensing routes.

| Path | Project | License | Pinned by | Use |
| :--- | :--- | :--- | :--- | :--- |
| `third_party/etl` | <https://github.com/ETLCPP/etl> | MIT | submodule | embedded containers |
| `third_party/micropython` | <https://github.com/micropython/micropython> | MIT | submodule | E906 REPL |
| `third_party/FreeRTOS-Kernel` | <https://github.com/FreeRTOS/FreeRTOS-Kernel> | MIT | submodule | E906 FreeRTOS app |
| `rtl/pio` | <https://github.com/tcmichals/pio2> | GPL-3.0-or-later (same author) | submodule | PIO tile |
| `rtl/pio/third_party/wb2axip` | <https://github.com/ZipCPU/wb2axip> (Dan Gisselquist, ZipCPU) | Apache-2.0 (file headers, README) | nested submodule (pio2 pins it) | `rtl/wbm2axilite.v`: Wishbone to AXI4-Lite bridge in `rtl/asp_pio_wrap.sv` ([SPEC-TLP-05]) |
| `rtl/pio/third_party/verilog-axi`, `verilog-axis` | <https://github.com/alexforencich> (Alex Forencich) | MIT | nested submodules (pio2) | not instantiated by AbstractX yet |

## Compliance

* **MIT**: keep the copyright and permission notice with the code (the submodules carry it); include it
  with any distribution that contains the files, GPL or commercial.
* **Apache-2.0 (wb2axip)**: distribute the license text with the files (`rtl/pio/LICENSES/Apache-2.0.txt`),
  keep their copyright headers, mark any modified file (AbstractX modifies none), carry a `NOTICE` file
  if the project has one (none at pio2's pinned commit). Apache-2.0 allows use in the GPL-3.0 build and
  in a commercially licensed build alike; it gives no trademark rights.
* **Pins**: pio2's `docs/third_party.md` lists the exact commits; re-check the license when a pin moves.
