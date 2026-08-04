`timescale 1ns/1ps

// Corrected nonblocking CAP-PDB certification core for eBCH(128,64), OSD-4.
//
// This module intentionally excludes reliability sorting, Gaussian elimination,
// and incumbent discovery.  It consumes an already-systematicized frame plus an
// exact seed candidate.  It implements:
//   * normal CAP suffix-DP or S=45 compiled two-block CAP;
//   * dedicated 15-state triangular (left,right) encoding, including (0,0);
//   * explicit right-DP -> allocation-table expansion;
//   * a shared depth-2 prefix-task FIFO with dynamic context work stealing;
//   * fire-and-forget 4-lane scoring (five-cycle result latency);
//   * a depth-4 leaf loop capable of one leaf/context/cycle;
//   * strict L > U pruning and canonical tie tracking.
//
// All metric values are unsigned integer weighted-Hamming costs.  Group 0..11
// have five parity bits; group 12 has four parity bits in packed bits [63:60].

module cap_dp_tdp_ram #(
    parameter integer DEPTH = 10400,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 10
) (
    input  wire                  clk,
    input  wire                  a_en,
    input  wire                  a_we,
    input  wire [ADDR_W-1:0]     a_addr,
    input  wire [DATA_W-1:0]     a_wdata,
    output reg  [DATA_W-1:0]     a_rdata,
    input  wire                  b_en,
    input  wire                  b_we,
    input  wire [ADDR_W-1:0]     b_addr,
    input  wire [DATA_W-1:0]     b_wdata,
    output reg  [DATA_W-1:0]     b_rdata
);
    (* ram_style = "block" *) reg [DATA_W-1:0] mem [0:DEPTH-1];
    always @(posedge clk) begin
        if (a_en) begin
            if (a_we) mem[a_addr] <= a_wdata;
            else      a_rdata <= mem[a_addr];
        end
        if (b_en) begin
            if (b_we) mem[b_addr] <= b_wdata;
            else      b_rdata <= mem[b_addr];
        end
    end
endmodule

module cap_task_ram #(
    parameter integer DEPTH = 2048,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 149
) (
    input  wire                 clk,
    input  wire                 we,
    input  wire [ADDR_W-1:0]    waddr,
    input  wire [DATA_W-1:0]    wdata,
    input  wire                 re,
    input  wire [ADDR_W-1:0]    raddr,
    output reg  [DATA_W-1:0]    rdata
);
    (* ram_style = "block" *) reg [DATA_W-1:0] mem [0:DEPTH-1];
    always @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
        if (re) rdata <= mem[raddr];
    end
endmodule

module cap_nonblocking_core #(
    parameter integer COMPACT = 0,
    parameter integer SPLIT = 45,
    parameter integer K = 64,
    parameter integer ORDER = 4,
    parameter integer GROUPS = 13,
    parameter integer STATES = 32,
    parameter integer CONTEXTS = 4,
    parameter integer SCORE_LATENCY = 5,
    parameter integer TASK_DEPTH = 2048,
    parameter integer METRIC_W = 14
) (
    input  wire clk,
    input  wire rst,

    input  wire        cfg_row_we,
    input  wire [3:0]  cfg_row_group,
    input  wire [5:0]  cfg_row_rank,
    input  wire [4:0]  cfg_row_effect,
    input  wire        cfg_phi_we,
    input  wire [3:0]  cfg_phi_group,
    input  wire [4:0]  cfg_phi_state,
    input  wire [9:0]  cfg_phi_cost,
    input  wire        cfg_info_we,
    input  wire [5:0]  cfg_info_rank,
    input  wire [6:0]  cfg_info_cost,
    input  wire [63:0] cfg_base_states,
    input  wire [METRIC_W-1:0] cfg_seed_best_metric,
    input  wire [63:0] cfg_seed_mask,
    input  wire        cfg_seed_tie,

    input  wire build_start,
    output reg  build_busy,
    output reg  build_done,
    output reg [31:0] build_cycles,

    input  wire decode_start,
    output reg  decode_busy,
    output reg  decode_done,
    output reg [METRIC_W-1:0] best_metric,
    output reg [63:0] best_tep,
    output reg best_tied,
    output reg [31:0] decode_cycles,
    output reg [31:0] score_issues,
    output reg [31:0] bound_row_cycles,
    output reg [31:0] context_wait_cycles,
    output reg [11:0] max_task_occupancy,
    output reg [2:0]  max_bound_waiters,

    // Raw table debug read.  dbg_table_sel: 0=normal/right, 1=compact.
    input  wire        dbg_table_re,
    input  wire        dbg_table_sel,
    input  wire [3:0]  dbg_table_group,
    input  wire [14:0] dbg_table_addr,
    output reg         dbg_table_valid,
    output reg  [9:0]  dbg_table_data,

    // Debug bound query.  Set force_full to return the exact minimum row value;
    // otherwise the engine may finish on the first row <= threshold.
    input  wire        dbg_query_start,
    input  wire        dbg_query_force_full,
    input  wire [5:0]  dbg_query_suffix,
    input  wire [2:0]  dbg_query_budget,
    input  wire [METRIC_W-1:0] dbg_query_information_cost,
    input  wire [63:0] dbg_query_states,
    input  wire [METRIC_W-1:0] dbg_query_threshold,
    output reg         dbg_query_done,
    output reg         dbg_query_pass,
    output reg [METRIC_W-1:0] dbg_query_minimum,

    // Debug exact scorer.
    input  wire        dbg_score_start,
    input  wire [63:0] dbg_score_mask,
    input  wire [METRIC_W-1:0] dbg_score_information_cost,
    input  wire [63:0] dbg_score_states,
    output reg         dbg_score_done,
    output reg [METRIC_W-1:0] dbg_score_metric
);
    localparam [9:0] INF10 = 10'h3ff;
    localparam [METRIC_W-1:0] INF_METRIC = {METRIC_W{1'b1}};
    localparam integer NORMAL_DEPTH = (K + 1) * (ORDER + 1) * STATES; // 10400
    localparam integer RIGHT_DEPTH = (K - SPLIT + 1) * (ORDER + 1) * STATES; // 3200
    localparam integer PAIRS = 15;
    localparam integer COMPACT_DEPTH = (SPLIT + 1) * PAIRS * STATES; // 22080
    localparam integer NORMAL_AW = $clog2(NORMAL_DEPTH);
    localparam integer RIGHT_AW = $clog2(RIGHT_DEPTH);
    localparam integer COMPACT_AW = $clog2(COMPACT_DEPTH);
    localparam integer TASK_AW = $clog2(TASK_DEPTH);
    localparam integer TASK_W = 64 + 7 + METRIC_W + 64;

    reg [4:0] row_effect [0:GROUPS-1][0:K-1];
    reg [9:0] phi_cost [0:GROUPS-1][0:STATES-1];
    reg [6:0] info_cost [0:K-1];
    reg [METRIC_W-1:0] info_prefix [0:K];
    reg [63:0] base_states_latched;
    reg [METRIC_W-1:0] seed_metric_latched;
    reg [63:0] seed_mask_latched;
    reg seed_tie_latched;

    integer cfg_i;
    always @(posedge clk) begin
        if (cfg_row_we)
            row_effect[cfg_row_group][cfg_row_rank] <=
                cfg_row_group == 12 ? {1'b0,cfg_row_effect[3:0]} : cfg_row_effect;
        if (cfg_phi_we)
            phi_cost[cfg_phi_group][cfg_phi_state] <= cfg_phi_cost;
        if (cfg_info_we)
            info_cost[cfg_info_rank] <= cfg_info_cost;
    end

    function automatic [4:0] packed_group_state;
        input [63:0] packed_states;
        input integer group;
        begin
            if (group == 12) packed_group_state = {1'b0,packed_states[63:60]};
            else packed_group_state = packed_states[group*5 +: 5];
        end
    endfunction

    `define packed packed_states
    function automatic [63:0] xor_row_states;
        input [63:0] packed_states;
        input [5:0] rank;