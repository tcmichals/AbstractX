// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// One map of 64-byte TLP fields in the 512-bit vector ([SPEC-TLP-04] one field map, [SPEC-TLP-01]).
// Every RTL block that reads or builds a TLP uses these functions, so the byte order lives here only.
// No `return` statements: Yosys (the Gowin flow) does not parse them; assign to the function name.
// Current layout: byte 0 at [511:504], multi-byte fields most significant byte first.
// @impl [SPEC-TLP-04] [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md
package asp_tlp_pkg;
    localparam logic [7:0] TLP_MEM_RD = 8'h01;
    localparam logic [7:0] TLP_MEM_WR = 8'h02;
    localparam logic [7:0] TLP_CPL_D  = 8'h03;
    localparam logic [7:0] CH_CONTROL = 8'h01;
    localparam int         MAX_WORDS  = 10;     // payload words (40 bytes)

    function automatic logic [7:0] tlp_type(input logic [511:0] t); tlp_type = t[511:504]; endfunction
    function automatic logic [7:0] tlp_flags(input logic [511:0] t); tlp_flags = t[503:496]; endfunction
    function automatic logic [7:0] tlp_tag(input logic [511:0] t); tlp_tag = t[495:488]; endfunction
    function automatic logic [7:0] tlp_channel(input logic [511:0] t); tlp_channel = t[487:480]; endfunction
    function automatic logic [31:0] tlp_addr(input logic [511:0] t); tlp_addr = t[479:448]; endfunction
    function automatic logic [15:0] tlp_len(input logic [511:0] t); tlp_len = t[447:432]; endfunction
    function automatic logic [15:0] tlp_seq(input logic [511:0] t); tlp_seq = t[431:416]; endfunction
    function automatic logic [63:0] tlp_ts(input logic [511:0] t); tlp_ts = t[415:352]; endfunction
    // Payload word k (0..9)
    function automatic logic [31:0] tlp_word(input logic [511:0] t, input int k);
        tlp_word = t[351 - 32 * k -: 32];
    endfunction

    // Header of a TLP; payload words are added with tlp_set_word.
    function automatic logic [511:0] tlp_header(input logic [7:0] typ, input logic [7:0] flags,
                                                input logic [7:0] tag, input logic [7:0] channel,
                                                input logic [31:0] addr, input logic [15:0] len,
                                                input logic [15:0] seq, input logic [63:0] ts);
        logic [511:0] t;
        t = '0;
        t[511:504] = typ;
        t[503:496] = flags;
        t[495:488] = tag;
        t[487:480] = channel;
        t[479:448] = addr;
        t[447:432] = len;
        t[431:416] = seq;
        t[415:352] = ts;
        tlp_header = t;
    endfunction

    function automatic logic [511:0] tlp_set_word(input logic [511:0] t, input int k, input logic [31:0] w);
        logic [511:0] r;
        r = t;
        r[351 - 32 * k -: 32] = w;
        tlp_set_word = r;
    endfunction
endpackage
`default_nettype wire
