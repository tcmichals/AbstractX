// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// One map of 64-byte TLP fields in the 512-bit vector ([SPEC-TLP-04] one field map, [SPEC-TLP-01]).
// Every RTL block that reads or builds a TLP uses these functions, so the byte order lives here only.
// No `return` statements: Yosys (the Gowin flow) does not parse them; assign to the function name.
// Little-endian (owner decision 2026-11-05): wire byte k at bits [8k+7:8k], multi-byte fields and payload
// words least significant byte first, so the vector is the asp_tlp64_t bytes read as one LE number.
// @impl [SPEC-TLP-04] [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md
package asp_tlp_pkg;
    localparam logic [7:0] TLP_MEM_RD = 8'h01;
    localparam logic [7:0] TLP_MEM_WR = 8'h02;
    localparam logic [7:0] TLP_CPL_D  = 8'h03;
    localparam logic [7:0] CH_CONTROL = 8'h01;
    localparam int         MAX_WORDS  = 10;     // payload words (40 bytes, wire bytes 20..59)

    function automatic logic [7:0] tlp_type(input logic [511:0] t); tlp_type = t[7:0]; endfunction
    function automatic logic [7:0] tlp_flags(input logic [511:0] t); tlp_flags = t[15:8]; endfunction
    function automatic logic [7:0] tlp_tag(input logic [511:0] t); tlp_tag = t[23:16]; endfunction
    function automatic logic [7:0] tlp_channel(input logic [511:0] t); tlp_channel = t[31:24]; endfunction
    function automatic logic [31:0] tlp_addr(input logic [511:0] t); tlp_addr = t[63:32]; endfunction
    function automatic logic [15:0] tlp_len(input logic [511:0] t); tlp_len = t[79:64]; endfunction
    function automatic logic [15:0] tlp_seq(input logic [511:0] t); tlp_seq = t[95:80]; endfunction
    function automatic logic [63:0] tlp_ts(input logic [511:0] t); tlp_ts = t[159:96]; endfunction
    // Register value k (0..9, CONTROL MemWrite / CplD): wire bytes 20+4k .. 23+4k, little-endian
    function automatic logic [31:0] tlp_word(input logic [511:0] t, input int k);
        tlp_word = t[160 + 32 * k +: 32];
    endfunction

    // Header of a TLP; payload words are added with tlp_set_word.
    function automatic logic [511:0] tlp_header(input logic [7:0] typ, input logic [7:0] flags,
                                                input logic [7:0] tag, input logic [7:0] channel,
                                                input logic [31:0] addr, input logic [15:0] len,
                                                input logic [15:0] seq, input logic [63:0] ts);
        logic [511:0] t;
        t = '0;
        t[7:0]    = typ;
        t[15:8]   = flags;
        t[23:16]  = tag;
        t[31:24]  = channel;
        t[63:32]  = addr;
        t[79:64]  = len;
        t[95:80]  = seq;
        t[159:96] = ts;
        tlp_header = t;
    endfunction

    function automatic logic [511:0] tlp_set_word(input logic [511:0] t, input int k, input logic [31:0] w);
        logic [511:0] r;
        r = t;
        r[160 + 32 * k +: 32] = w;
        tlp_set_word = r;
    endfunction

    // Stream data (PIO FIFO words, [SPEC-TLP-05]): a byte stream in arrival order, word k most significant
    // byte first at payload bytes 4k..4k+3, so sensor bytes keep the sensor's format ([SPEC-TLP-01] payload rule).
    function automatic logic [31:0] bswap32(input logic [31:0] w);
        bswap32 = {w[7:0], w[15:8], w[23:16], w[31:24]};
    endfunction
    function automatic logic [31:0] tlp_stream_word(input logic [511:0] t, input int k);
        tlp_stream_word = bswap32(t[160 + 32 * k +: 32]);
    endfunction
    function automatic logic [511:0] tlp_set_stream_word(input logic [511:0] t, input int k, input logic [31:0] w);
        logic [511:0] r;
        r = t;
        r[160 + 32 * k +: 32] = bswap32(w);
        tlp_set_stream_word = r;
    endfunction

    // Wire byte order of a serial link (byte 0 first, each byte MSB first) <-> this vector:
    // a shift register that shifts left holds byte 0 at [511:504]; reversing the bytes gives byte 0 at [7:0].
    function automatic logic [511:0] tlp_bswap(input logic [511:0] t);
        for (int k = 0; k < 64; k++) tlp_bswap[8 * k +: 8] = t[8 * (63 - k) +: 8];
    endfunction
endpackage
`default_nettype wire
