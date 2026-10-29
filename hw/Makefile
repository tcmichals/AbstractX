# hardware build for Tang boards
# Run from project ROOT: make -f hw/Makefile tang9k

TOOLS_DIR ?= $(HOME)/.tools/oss-cad-suite/bin
YOSYS  = $(TOOLS_DIR)/yosys
NEXTPNR = $(TOOLS_DIR)/nextpnr-himbaechel
GOWIN_PACK = $(TOOLS_DIR)/gowin_pack

# Build directories
BUILD_DIR = build_hw

.PHONY: all tang9k clean primer20k tang_nano20k tang_nano20k_fit

all: tang9k primer20k tang_nano20k

# --- Tang Nano 9K ---
T9_FAMILY = GW1N-9
T9_DEVICE = GW1NR-LV9QN88PC6/I5

tang9k: $(BUILD_DIR)/tang9k.fs

$(BUILD_DIR)/tang9k.json: hw/tang9k/top_tang9k.sv rtl/asp_top.sv rtl/pwm/asp_pwm_decoder.sv
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

$(BUILD_DIR)/primer20k.json: hw/primer20k/top_primer20k.sv rtl/asp_top.sv rtl/pwm/asp_pwm_decoder.sv
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

# --- Tang Nano 20K (Cubie A5E test target, [SPEC-TN20K-01]) ---
TN20_FAMILY = GW2A-18C
TN20_DEVICE = GW2AR-LV18QN88C8/I7
TN20_RTL = $(wildcard rtl/*.sv rtl/*/*.sv rtl/pio/src/rtl/*.sv)

tang_nano20k: $(BUILD_DIR)/tang_nano20k.fs

$(BUILD_DIR)/tang_nano20k.json: hw/tang_nano20k/top_tang_nano20k.sv $(TN20_RTL)
	mkdir -p $(BUILD_DIR)
	TOP_MODULE=top_tang_nano20k \
	BOARD_TOP_FILE=hw/tang_nano20k/top_tang_nano20k.sv \
	OUTPUT_JSON=$@ \
	$(YOSYS) -p "tcl hw/common/gowin_synth.tcl"

$(BUILD_DIR)/tang_nano20k_pnr.json: $(BUILD_DIR)/tang_nano20k.json hw/tang_nano20k/tang_nano20k.cst
	$(NEXTPNR) --device $(TN20_DEVICE) \
		--json $< \
		--write $@ \
		--freq 27 \
		--vopt family=$(TN20_FAMILY) \
		--vopt cst=hw/tang_nano20k/tang_nano20k.cst

$(BUILD_DIR)/tang_nano20k.fs: $(BUILD_DIR)/tang_nano20k_pnr.json
	$(GOWIN_PACK) -d $(TN20_DEVICE) -o $@ $<

# Fit/timing measurement only: same design, arbitrary plain pins
# (hw/tang_nano20k/gen_fitonly_cst.py). Never wire hardware to this bitstream.
tang_nano20k_fit: $(BUILD_DIR)/tang_nano20k_fit_pnr.json

$(BUILD_DIR)/tang_nano20k_fit_pnr.json: $(BUILD_DIR)/tang_nano20k.json hw/tang_nano20k/tang_nano20k_fitonly.cst
	$(NEXTPNR) --device $(TN20_DEVICE) \
		--json $< \
		--write $@ \
		--freq 27 \
		--vopt family=$(TN20_FAMILY) \
		--vopt cst=hw/tang_nano20k/tang_nano20k_fitonly.cst

clean:
	rm -rf $(BUILD_DIR)
