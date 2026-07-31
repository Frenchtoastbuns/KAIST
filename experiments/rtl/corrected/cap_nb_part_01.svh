        integer g;
        reg [63:0] value;
        begin
            value = packed_states;
            for (g = 0; g < 12; g = g + 1)
                value[g*5 +: 5] = value[g*5 +: 5] ^ row_effect[g][rank];
            value[63:60] = value[63:60] ^ row_effect[12][rank][3:0];
            xor_row_states = value;
        end
    endfunction
    `undef packed

    function automatic [3:0] pair_left;
        input [3:0] pair;
        begin
            case (pair)
                0: pair_left=0;
                1: pair_left=0; 2: pair_left=1;
                3: pair_left=0; 4: pair_left=1; 5: pair_left=2;
                6: pair_left=0; 7: pair_left=1; 8: pair_left=2; 9: pair_left=3;
                10: pair_left=0; 11: pair_left=1; 12: pair_left=2;
                13: pair_left=3; 14: pair_left=4;
                default: pair_left=0;
            endcase
        end
    endfunction

    function automatic [3:0] pair_right;
        input [3:0] pair;
        begin
            case (pair)
                0: pair_right=0;
                1: pair_right=1; 2: pair_right=0;
                3: pair_right=2; 4: pair_right=1; 5: pair_right=0;
                6: pair_right=3; 7: pair_right=2; 8: pair_right=1; 9: pair_right=0;
                10: pair_right=4; 11: pair_right=3; 12: pair_right=2;
                13: pair_right=1; 14: pair_right=0;
                default: pair_right=0;
            endcase
        end
    endfunction

    function automatic [3:0] pair_index;
        input [2:0] left;
        input [2:0] right;
        reg [3:0] total;
        begin
            total = left + right;
            pair_index = (total * (total + 1)) / 2 + left;
        end
    endfunction

    function automatic [NORMAL_AW-1:0] normal_addr;
        input [6:0] suffix;
        input [2:0] weight;
        input [4:0] state;
        begin
            normal_addr = ((suffix * (ORDER+1) + weight) * STATES) + state;
        end
    endfunction

    function automatic [RIGHT_AW-1:0] right_addr;
        input [6:0] suffix;
        input [2:0] weight;
        input [4:0] state;
        begin
            right_addr = ((((suffix - SPLIT) * (ORDER+1)) + weight) * STATES) + state;
        end
    endfunction

    function automatic [COMPACT_AW-1:0] compact_addr;
        input [6:0] suffix;
        input [3:0] pair;
        input [4:0] state;
        begin
            compact_addr = ((suffix * PAIRS + pair) * STATES) + state;
        end
    endfunction

    function automatic integer popcount64;
        input [63:0] value;
        integer i;
        begin
            popcount64 = 0;
            for (i=0;i<64;i=i+1) popcount64 = popcount64 + value[i];
        end
    endfunction

    function automatic canonical_before;
        input [63:0] left;
        input [63:0] right;
        integer i;
        integer lw;
        integer rw;
        reg decided;
        begin
            lw = popcount64(left);
            rw = popcount64(right);
            if (lw < rw) canonical_before = 1'b1;
            else if (lw > rw) canonical_before = 1'b0;
            else begin
                canonical_before = 1'b0;
                decided = 1'b0;
                for (i=0;i<64;i=i+1) begin
                    if (!decided && left[i] != right[i]) begin
                        canonical_before = left[i];
                        decided = 1'b1;
                    end
                end
            end
        end
    endfunction

    // ------------------------------------------------------------------
    // DP memories.  Each parity group is an independent bank, so one query
    // reads all 13 group costs in parallel.
    // ------------------------------------------------------------------
    wire [9:0] normal_a_q [0:GROUPS-1];
    wire [9:0] normal_b_q [0:GROUPS-1];
    wire [9:0] right_a_q [0:GROUPS-1];
    wire [9:0] right_b_q [0:GROUPS-1];
    wire [9:0] compact_a_q [0:GROUPS-1];
    wire [9:0] compact_b_q [0:GROUPS-1];

    reg normal_a_en [0:GROUPS-1];
    reg normal_a_we [0:GROUPS-1];
    reg [NORMAL_AW-1:0] normal_a_addr [0:GROUPS-1];
    reg [9:0] normal_a_wdata [0:GROUPS-1];
    reg normal_b_en [0:GROUPS-1];
    reg normal_b_we [0:GROUPS-1];
    reg [NORMAL_AW-1:0] normal_b_addr [0:GROUPS-1];
    reg [9:0] normal_b_wdata [0:GROUPS-1];

    reg right_a_en [0:GROUPS-1];
    reg right_a_we [0:GROUPS-1];
    reg [RIGHT_AW-1:0] right_a_addr [0:GROUPS-1];
    reg [9:0] right_a_wdata [0:GROUPS-1];
    reg right_b_en [0:GROUPS-1];
    reg right_b_we [0:GROUPS-1];
    reg [RIGHT_AW-1:0] right_b_addr [0:GROUPS-1];
    reg [9:0] right_b_wdata [0:GROUPS-1];

    reg compact_a_en [0:GROUPS-1];
    reg compact_a_we [0:GROUPS-1];
    reg [COMPACT_AW-1:0] compact_a_addr [0:GROUPS-1];
    reg [9:0] compact_a_wdata [0:GROUPS-1];
    reg compact_b_en [0:GROUPS-1];
    reg compact_b_we [0:GROUPS-1];
    reg [COMPACT_AW-1:0] compact_b_addr [0:GROUPS-1];
    reg [9:0] compact_b_wdata [0:GROUPS-1];

    genvar mg;
    generate
        for (mg=0; mg<GROUPS; mg=mg+1) begin: g_normal_mem
            cap_dp_tdp_ram #(.DEPTH(NORMAL_DEPTH),.ADDR_W(NORMAL_AW),.DATA_W(10)) mem (
                .clk(clk),
                .a_en(normal_a_en[mg]),.a_we(normal_a_we[mg]),
                .a_addr(normal_a_addr[mg]),.a_wdata(normal_a_wdata[mg]),.a_rdata(normal_a_q[mg]),
                .b_en(normal_b_en[mg]),.b_we(normal_b_we[mg]),
                .b_addr(normal_b_addr[mg]),.b_wdata(normal_b_wdata[mg]),.b_rdata(normal_b_q[mg])
            );
        end
        for (mg=0; mg<GROUPS; mg=mg+1) begin: g_right_mem
            cap_dp_tdp_ram #(.DEPTH(RIGHT_DEPTH),.ADDR_W(RIGHT_AW),.DATA_W(10)) mem (
                .clk(clk),
                .a_en(right_a_en[mg]),.a_we(right_a_we[mg]),
                .a_addr(right_a_addr[mg]),.a_wdata(right_a_wdata[mg]),.a_rdata(right_a_q[mg]),
                .b_en(right_b_en[mg]),.b_we(right_b_we[mg]),
                .b_addr(right_b_addr[mg]),.b_wdata(right_b_wdata[mg]),.b_rdata(right_b_q[mg])
            );
        end
        for (mg=0; mg<GROUPS; mg=mg+1) begin: g_compact_mem
            cap_dp_tdp_ram #(.DEPTH(COMPACT_DEPTH),.ADDR_W(COMPACT_AW),.DATA_W(10)) mem (
                .clk(clk),
                .a_en(compact_a_en[mg]),.a_we(compact_a_we[mg]),
                .a_addr(compact_a_addr[mg]),.a_wdata(compact_a_wdata[mg]),.a_rdata(compact_a_q[mg]),
                .b_en(compact_b_en[mg]),.b_we(compact_b_we[mg]),
                .b_addr(compact_b_addr[mg]),.b_wdata(compact_b_wdata[mg]),.b_rdata(compact_b_q[mg])
            );
        end
    endgenerate

    // ------------------------------------------------------------------
    // Builder.
    // ------------------------------------------------------------------
    localparam [3:0]
        B_IDLE=0,
        B_N_INIT=1, B_N_READ=2, B_N_WRITE=3,
        B_R_INIT=4, B_R_READ=5, B_R_WRITE=6,
        B_EXP_READ=7, B_EXP_WRITE=8,
        B_L_READ=9, B_L_WRITE=10,