`include "experiments/rtl/cap_corrected_memory.sv"

module cap_full_compiled_decoder_corrected #(
    parameter integer SPLIT = 45,
    parameter integer COMPACT_ROWS = (SPLIT == 45) ? 445 : 5,
    parameter integer ROWS = (SPLIT == 45) ? 511 : 251,
    parameter integer ROW_W = $clog2(ROWS),
    parameter integer GROUPS5 = 12,
    parameter integer CONTEXTS = 4,
    parameter integer K = 64,
    parameter integer O = 4,
    parameter integer SCRATCH_DEPTH = 480,
    parameter integer SCRATCH_AW = $clog2(SCRATCH_DEPTH),
    parameter integer DEPTH5 = ROWS * 32,
    parameter integer DEPTH4 = ROWS * 16,
    parameter integer ADDR5_W = $clog2(DEPTH5),
    parameter integer ADDR4_W = $clog2(DEPTH4),
    parameter integer TASK_DEPTH = 64
) (
    input wire clk,
    input wire rst,

    input wire cfg_row_we,
    input wire [3:0] cfg_row_group,
    input wire [5:0] cfg_row_rank,
    input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we,
    input wire [3:0] cfg_phi_group,
    input wire [4:0] cfg_phi_state,
    input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we,
    input wire [5:0] cfg_info_rank,
    input wire [6:0] cfg_info_cost,
    input wire [63:0] cfg_base_states,

    input wire build_start,
    output reg build_busy,
    output reg build_done,
    output reg [31:0] build_cycles,

    input wire decode_start,
    output reg decode_busy,
    output reg decode_done,
    output reg [13:0] best_metric,
    output reg [63:0] best_tep,
    output reg best_tied,
    output reg [31:0] decode_cycles,
    output reg [31:0] scored_teps,
    output reg [31:0] bound_rows,
    output reg [31:0] score_issue_slots,
    output reg [31:0] score_active_cycles,

    // Builder-table differential debug port. Use only while idle.
    input wire dbg_table_req,
    input wire [ROW_W-1:0] dbg_table_row,
    input wire [4:0] dbg_table_state,
    output reg dbg_table_valid,
    output wire [GROUPS5*10-1:0] dbg_table_cost5,
    output wire [8:0] dbg_table_cost4,

    // Bound-pipeline differential debug port. Use only while idle.
    input wire dbg_bound_req,
    input wire [ROW_W-1:0] dbg_bound_row,
    input wire [59:0] dbg_bound_state5,
    input wire [3:0] dbg_bound_state4,
    input wire [13:0] dbg_bound_base,
    input wire [13:0] dbg_bound_incumbent,
    output reg dbg_bound_valid,
    output reg [13:0] dbg_bound_value,
    output reg dbg_bound_may_improve,

    // Scorer-pipeline differential debug port. Use only while idle.
    input wire dbg_score_req,
    input wire [63:0] dbg_score_states,
    input wire [13:0] dbg_score_info,
    input wire [63:0] dbg_score_mask,
    output wire dbg_score_valid,
    output wire [13:0] dbg_score_metric,
    output wire [63:0] dbg_score_mask_out
);
    localparam [9:0] INF10 = 10'h3ff;
    localparam [8:0] INF9 = 9'h1ff;
    localparam [13:0] INF14 = 14'h3fff;

    reg tables_ready;
    reg [4:0] row_effect [0:12][0:63];
    reg [9:0] phi_cost [0:12][0:31];
    reg [6:0] info_cost [0:63];
    integer gi, si;

    always @(posedge clk) begin
        if (cfg_row_we)
            row_effect[cfg_row_group][cfg_row_rank] <= cfg_row_effect;
        if (cfg_phi_we)
            phi_cost[cfg_phi_group][cfg_phi_state] <= cfg_phi_cost;
        if (cfg_info_we)
            info_cost[cfg_info_rank] <= cfg_info_cost;
    end

    // Complete triangular allocation encoding:
    // index = weight*(weight+1)/2 + left, weight=left+right.
    // 0:(0,0)
    // 1:(0,1) 2:(1,0)
    // 3:(0,2) 4:(1,1) 5:(2,0)
    // 6:(0,3) 7:(1,2) 8:(2,1) 9:(3,0)
    // 10:(0,4) 11:(1,3) 12:(2,2) 13:(3,1) 14:(4,0)
    function automatic [2:0] alloc_weight(input [3:0] idx);
        begin
            if (idx == 0) alloc_weight = 0;
            else if (idx <= 2) alloc_weight = 1;
            else if (idx <= 5) alloc_weight = 2;
            else if (idx <= 9) alloc_weight = 3;
            else alloc_weight = 4;
        end
    endfunction

    function automatic [2:0] alloc_left(input [3:0] idx);
        begin
            case (idx)
                0,1,3,6,10: alloc_left = 0;
                2,4,7,11:   alloc_left = 1;
                5,8,12:     alloc_left = 2;
                9,13:       alloc_left = 3;
                default:    alloc_left = 4;
            endcase
        end
    endfunction

    function automatic [3:0] alloc_index(
        input [2:0] left, input [2:0] right
    );
        reg [2:0] weight;
        begin
            weight = left + right;
            case (weight)
                0: alloc_index = 0;
                1: alloc_index = 1 + left;
                2: alloc_index = 3 + left;
                3: alloc_index = 6 + left;
                default: alloc_index = 10 + left;
            endcase
        end
    endfunction

    function automatic integer compact_prefix(input integer n);
        begin
            if (n <= 0) compact_prefix = 0;
            else if (n == 1) compact_prefix = 4;
            else if (n == 2) compact_prefix = 11;
            else if (n == 3) compact_prefix = 20;
            else compact_prefix = 20 + 10 * (n - 3);
        end
    endfunction

    function automatic [ROW_W-1:0] compact_row_id(
        input [5:0] suffix, input [2:0] left, input [2:0] right
    );
        integer avail, off;
        begin
            if (left == 0) compact_row_id = right;
            else begin
                avail = SPLIT - suffix;
                case (left)
                    1: off = right;
                    2: off = 4 + right;
                    3: off = 7 + right;
                    default: off = 9 + right;
                endcase
                compact_row_id = 5 + compact_prefix(avail - 1) + off;
            end
        end
    endfunction

    function automatic integer normal_prefix(input integer n);
        begin
            if (n <= 0) normal_prefix = 0;
            else if (n == 1) normal_prefix = 1;
            else if (n == 2) normal_prefix = 3;
            else if (n == 3) normal_prefix = 6;
            else normal_prefix = 6 + 4 * (n - 3);
        end
    endfunction

    function automatic [ROW_W-1:0] normal_row_id(
        input [5:0] suffix, input [2:0] weight
    );
        integer rem;
        begin
            if (suffix == SPLIT) normal_row_id = weight;
            else begin
                rem = K - suffix;
                normal_row_id = COMPACT_ROWS +
                    normal_prefix(rem - 1) + (weight - 1);
            end
        end
    endfunction

    function automatic [63:0] packed_row(input [5:0] rank);
        integer g;
        reg [63:0] tmp;
        begin
            tmp = 64'd0;
            for (g = 0; g < 12; g = g + 1)
                tmp[g*5 +: 5] = row_effect[g][rank];
            tmp[60 +: 4] = row_effect[12][rank][3:0];
            packed_row = tmp;
        end
    endfunction

    function automatic [13:0] range_info_min(
        input [5:0] begin_rank, input [2:0] count
    );
        integer j;
        reg [13:0] sum;
        begin
            sum = 0;
            for (j = 0; j < 4; j = j + 1)
                if (j < count) sum = sum + info_cost[begin_rank + j];
            range_info_min = sum;
        end
    endfunction

    // ------------------------------------------------------------------
    // Main compiled/normal table banks.
    // ------------------------------------------------------------------
    wire [GROUPS5*10-1:0] table_q5;
    wire [8:0] table_q4;
    reg table_re;
    reg [ROW_W-1:0] table_rrow;
    reg [59:0] table_rstate5;
    reg [3:0] table_rstate4;
    reg table_we;
    reg [ROW_W-1:0] table_wrow;
    reg [4:0] table_wstate;
    reg [GROUPS5*10-1:0] build_value5;
    reg [8:0] build_value4;

    wire [ADDR5_W-1:0] table_rbase5 = table_rrow * 32;
    wire [ADDR4_W-1:0] table_rbase4 = table_rrow * 16;
    wire [ADDR5_W-1:0] table_waddr5 = table_wrow * 32 + table_wstate;
    wire [ADDR4_W-1:0] table_waddr4 = table_wrow * 16 +
        table_wstate[3:0];

    genvar gg;
    generate
        for (gg = 0; gg < GROUPS5; gg = gg + 1) begin: main5
            cap_sp_ram #(
                .DEPTH(DEPTH5), .ADDR_W(ADDR5_W), .DATA_W(10)
            ) ram (
                .clk(clk), .we(table_we), .waddr(table_waddr5),
                .wdata(build_value5[gg*10 +: 10]),
                .re(table_re),
                .raddr(table_rbase5 + table_rstate5[gg*5 +: 5]),
                .rdata(table_q5[gg*10 +: 10])
            );
        end
    endgenerate

    cap_sp_ram #(
        .DEPTH(DEPTH4), .ADDR_W(ADDR4_W), .DATA_W(9)
    ) main4 (
        .clk(clk),
        .we(table_we && table_wstate < 16),
        .waddr(table_waddr4), .wdata(build_value4),
        .re(table_re), .raddr(table_rbase4 + table_rstate4),
        .rdata(table_q4)
    );

    assign dbg_table_cost5 = table_q5;
    assign dbg_table_cost4 = table_q4;

`include "experiments/rtl/cap_corrected_builder.svh"
`include "experiments/rtl/cap_corrected_pipelines.svh"
`include "experiments/rtl/cap_corrected_scheduler.svh"
endmodule

// Synthesis wrappers intentionally retain only normal and S=45.
module cap_full_normal_corrected_top (
    input wire clk, input wire rst,
    input wire cfg_row_we, input wire [3:0] cfg_row_group,
    input wire [5:0] cfg_row_rank, input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we, input wire [3:0] cfg_phi_group,
    input wire [4:0] cfg_phi_state, input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we, input wire [5:0] cfg_info_rank,
    input wire [6:0] cfg_info_cost, input wire [63:0] cfg_base_states,
    input wire build_start, output wire build_busy, output wire build_done,
    output wire [31:0] build_cycles,
    input wire decode_start, output wire decode_busy, output wire decode_done,
    output wire [13:0] best_metric, output wire [63:0] best_tep,
    output wire best_tied, output wire [31:0] decode_cycles,
    output wire [31:0] scored_teps, output wire [31:0] bound_rows,
    output wire [31:0] score_issue_slots,
    output wire [31:0] score_active_cycles
);
    wire unused_tv, unused_bv, unused_bmi, unused_sv;
    wire [119:0] unused_t5;
    wire [8:0] unused_t4;
    wire [13:0] unused_bval, unused_smetric;
    wire [63:0] unused_smask;
    cap_full_compiled_decoder_corrected #(
        .SPLIT(0), .COMPACT_ROWS(5), .ROWS(251), .ROW_W(8),
        .DEPTH5(8032), .DEPTH4(4016), .ADDR5_W(13), .ADDR4_W(12)
    ) core (
        .clk(clk), .rst(rst),
        .cfg_row_we(cfg_row_we), .cfg_row_group(cfg_row_group),
        .cfg_row_rank(cfg_row_rank), .cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we), .cfg_phi_group(cfg_phi_group),
        .cfg_phi_state(cfg_phi_state), .cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we), .cfg_info_rank(cfg_info_rank),
        .cfg_info_cost(cfg_info_cost), .cfg_base_states(cfg_base_states),
        .build_start(build_start), .build_busy(build_busy),
        .build_done(build_done), .build_cycles(build_cycles),
        .decode_start(decode_start), .decode_busy(decode_busy),
        .decode_done(decode_done), .best_metric(best_metric),
        .best_tep(best_tep), .best_tied(best_tied),
        .decode_cycles(decode_cycles), .scored_teps(scored_teps),
        .bound_rows(bound_rows), .score_issue_slots(score_issue_slots),
        .score_active_cycles(score_active_cycles),
        .dbg_table_req(1'b0), .dbg_table_row(8'd0),
        .dbg_table_state(5'd0), .dbg_table_valid(unused_tv),
        .dbg_table_cost5(unused_t5), .dbg_table_cost4(unused_t4),
        .dbg_bound_req(1'b0), .dbg_bound_row(8'd0),
        .dbg_bound_state5(60'd0), .dbg_bound_state4(4'd0),
        .dbg_bound_base(14'd0), .dbg_bound_incumbent(14'd0),
        .dbg_bound_valid(unused_bv), .dbg_bound_value(unused_bval),
        .dbg_bound_may_improve(unused_bmi),
        .dbg_score_req(1'b0), .dbg_score_states(64'd0),
        .dbg_score_info(14'd0), .dbg_score_mask(64'd0),
        .dbg_score_valid(unused_sv), .dbg_score_metric(unused_smetric),
        .dbg_score_mask_out(unused_smask)
    );
endmodule

module cap_full_s45_corrected_top (
    input wire clk, input wire rst,
    input wire cfg_row_we, input wire [3:0] cfg_row_group,
    input wire [5:0] cfg_row_rank, input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we, input wire [3:0] cfg_phi_group,
    input wire [4:0] cfg_phi_state, input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we, input wire [5:0] cfg_info_rank,
    input wire [6:0] cfg_info_cost, input wire [63:0] cfg_base_states,
    input wire build_start, output wire build_busy, output wire build_done,
    output wire [31:0] build_cycles,
    input wire decode_start, output wire decode_busy, output wire decode_done,
    output wire [13:0] best_metric, output wire [63:0] best_tep,
    output wire best_tied, output wire [31:0] decode_cycles,
    output wire [31:0] scored_teps, output wire [31:0] bound_rows,
    output wire [31:0] score_issue_slots,
    output wire [31:0] score_active_cycles
);
    wire unused_tv, unused_bv, unused_bmi, unused_sv;
    wire [119:0] unused_t5;
    wire [8:0] unused_t4;
    wire [13:0] unused_bval, unused_smetric;
    wire [63:0] unused_smask;
    cap_full_compiled_decoder_corrected #(
        .SPLIT(45), .COMPACT_ROWS(445), .ROWS(511), .ROW_W(9)
    ) core (
        .clk(clk), .rst(rst),
        .cfg_row_we(cfg_row_we), .cfg_row_group(cfg_row_group),
        .cfg_row_rank(cfg_row_rank), .cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we), .cfg_phi_group(cfg_phi_group),
        .cfg_phi_state(cfg_phi_state), .cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we), .cfg_info_rank(cfg_info_rank),
        .cfg_info_cost(cfg_info_cost), .cfg_base_states(cfg_base_states),
        .build_start(build_start), .build_busy(build_busy),
        .build_done(build_done), .build_cycles(build_cycles),
        .decode_start(decode_start), .decode_busy(decode_busy),
        .decode_done(decode_done), .best_metric(best_metric),
        .best_tep(best_tep), .best_tied(best_tied),
        .decode_cycles(decode_cycles), .scored_teps(scored_teps),
        .bound_rows(bound_rows), .score_issue_slots(score_issue_slots),
        .score_active_cycles(score_active_cycles),
        .dbg_table_req(1'b0), .dbg_table_row(9'd0),
        .dbg_table_state(5'd0), .dbg_table_valid(unused_tv),
        .dbg_table_cost5(unused_t5), .dbg_table_cost4(unused_t4),
        .dbg_bound_req(1'b0), .dbg_bound_row(9'd0),
        .dbg_bound_state5(60'd0), .dbg_bound_state4(4'd0),
        .dbg_bound_base(14'd0), .dbg_bound_incumbent(14'd0),
        .dbg_bound_valid(unused_bv), .dbg_bound_value(unused_bval),
        .dbg_bound_may_improve(unused_bmi),
        .dbg_score_req(1'b0), .dbg_score_states(64'd0),
        .dbg_score_info(14'd0), .dbg_score_mask(64'd0),
        .dbg_score_valid(unused_sv), .dbg_score_metric(unused_smetric),
        .dbg_score_mask_out(unused_smask)
    );
endmodule
