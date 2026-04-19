# hardware build for Tang boards
# Run from project ROOT: make -f hw/Makefile tang9k

TOOLS_DIR ?= $(HOME)/.tools/oss-cad-suite/bin
YOSYS  = $(TOOLS_DIR)/yosys
NEXTPNR = $(TOOLS_DIR)/nextpnr-himbaechel
GOWIN_PACK = $(TOOLS_DIR)/gowin_pack

# Build directories
BUILD_DIR = build_hw

.PHONY: all tang9k clean primer20k

all: tang9k primer20k

# --- Tang Nano 9K ---
T9_FAMILY = GW1N-9
T9_DEVICE = GW1NR-LV9QN88PC6/I5

tang9k: $(BUILD_DIR)/tang9k.fs

$(BUILD_DIR)/tang9k.json: hw/tang9k/top_tang9k.sv rtl/asp_top.sv
	mkdir -p $(BUILD_DIR)
	TOP_MODULE=top_tang9k \
	BOARD_TOP_FILE=hw/tang9k/top_tang9k.sv \
	OUTPUT_JSON=$@ \
	$(YOSYS) -p "tcl hw/common/gowin_synth.tcl"

$(BUILD_DIR)/tang9k_pnr.json: $(BUILD_DIR)/tang9k.json hw/tang9k/tang9k.cst
	$(NEXTPNR) --device $(T9_DEVICE) \
		--json $< \
		--write $@ \
		--vopt family=$(T9_FAMILY) \
		--vopt cst=hw/tang9k/tang9k.cst

$(BUILD_DIR)/tang9k.fs: $(BUILD_DIR)/tang9k_pnr.json
	$(GOWIN_PACK) -d $(T9_DEVICE) -o $@ $<

# --- Tang Primer 20K ---
TP20_FAMILY = GW2A-18
TP20_DEVICE = GW2A-LV18PG256C8/I7

primer20k: $(BUILD_DIR)/primer20k.fs

$(BUILD_DIR)/primer20k.json: hw/primer20k/top_primer20k.sv rtl/asp_top.sv
	mkdir -p $(BUILD_DIR)
	TOP_MODULE=top_primer20k \
	BOARD_TOP_FILE=hw/primer20k/top_primer20k.sv \
	OUTPUT_JSON=$@ \
	$(YOSYS) -p "tcl hw/common/gowin_synth.tcl"

$(BUILD_DIR)/primer20k_pnr.json: $(BUILD_DIR)/primer20k.json hw/primer20k/primer20k.cst
	$(NEXTPNR) --device $(TP20_DEVICE) \
		--json $< \
		--write $@ \
		--vopt family=$(TP20_FAMILY) \
		--vopt cst=hw/primer20k/primer20k.cst

$(BUILD_DIR)/primer20k.fs: $(BUILD_DIR)/primer20k_pnr.json
	$(GOWIN_PACK) -d $(TP20_DEVICE) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
