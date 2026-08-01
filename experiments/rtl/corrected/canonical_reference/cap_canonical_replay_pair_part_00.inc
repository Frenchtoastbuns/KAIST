`timescale 1ns/1ps

// Simulation-only canonical replay wrapper.  The corrected normal and S=45
// cores are unchanged; this wrapper exposes internal scheduler/build telemetry
// through hierarchical observation for deterministic trace replay.
module cap_canonical_replay_pair_top (
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
    input  wire [13:0] cfg_seed_best_metric,
    input  wire [63:0] cfg_seed_mask,
    input  wire        cfg_seed_tie,
    input  wire build_start,
    input  wire decode_start,

    output wire normal_build_busy,
    output wire normal_build_done,
    output wire [31:0] normal_build_cycles,
    output wire normal_decode_busy,
    output wire normal_decode_done,
    output wire [13:0] normal_best_metric,
    output wire [63:0] normal_best_tep,
    output wire normal_best_tied,
    output wire [31:0] normal_decode_cycles,
    output wire [31:0] normal_score_issues_reported,
    output wire [31:0] normal_bound_rows,
    output wire [31:0] normal_context_wait,
    output wire [11:0] normal_max_task_occupancy,
    output wire [2:0]  normal_max_bound_waiters,
    output wire [3:0]  normal_issue_mask,
    output reg  [31:0] normal_fifo_full_cycles,
    output reg  [31:0] normal_fifo_empty_cycles,
    output reg  [31:0] normal_build_normal_dp_cycles,
    output reg  [31:0] normal_build_right_dp_cycles,
    output reg  [31:0] normal_build_expansion_cycles,
    output reg  [31:0] normal_build_triangular_cycles,
    output reg  [31:0] normal_build_prefix_cycles,

    output wire s45_build_busy,
    output wire s45_build_done,
    output wire [31:0] s45_build_cycles,
    output wire s45_decode_busy,
    output wire s45_decode_done,
    output wire [13:0] s45_best_metric,
    output wire [63:0] s45_best_tep,
    output wire s45_best_tied,
    output wire [31:0] s45_decode_cycles,
    output wire [31:0] s45_score_issues_reported,
    output wire [31:0] s45_bound_rows,
    output wire [31:0] s45_context_wait,
    output wire [11:0] s45_max_task_occupancy,
    output wire [2:0]  s45_max_bound_waiters,
    output wire [3:0]  s45_issue_mask,
    output reg  [31:0] s45_fifo_full_cycles,
    output reg  [31:0] s45_fifo_empty_cycles,
    output reg  [31:0] s45_build_normal_dp_cycles,
    output reg  [31:0] s45_build_right_dp_cycles,
    output reg  [31:0] s45_build_expansion_cycles,
    output reg  [31:0] s45_build_triangular_cycles,
    output reg  [31:0] s45_build_prefix_cycles
);
    wire unused_table_valid_n, unused_query_done_n, unused_query_pass_n, unused_score_done_n;
    wire [9:0] unused_table_data_n;
    wire [13:0] unused_query_min_n, unused_score_metric_n;
    wire unused_table_valid_c, unused_query_done_c, unused_query_pass_c, unused_score_done_c;
    wire [9:0] unused_table_data_c;
    wire [13:0] unused_query_min_c, unused_score_metric_c;

    cap_nonblocking_normal_top normal (
        .clk(clk),.rst(rst),
        .cfg_row_we(cfg_row_we),.cfg_row_group(cfg_row_group),.cfg_row_rank(cfg_row_rank),.cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we),.cfg_phi_group(cfg_phi_group),.cfg_phi_state(cfg_phi_state),.cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we),.cfg_info_rank(cfg_info_rank),.cfg_info_cost(cfg_info_cost),
        .cfg_base_states(cfg_base_states),.cfg_seed_best_metric(cfg_seed_best_metric),.cfg_seed_mask(cfg_seed_mask),.cfg_seed_tie(cfg_seed_tie),
        .build_start(build_start),.build_busy(normal_build_busy),.build_done(normal_build_done),.build_cycles(normal_build_cycles),
        .decode_start(decode_start),.decode_busy(normal_decode_busy),.decode_done(normal_decode_done),
        .best_metric(normal_best_metric),.best_tep(normal_best_tep),.best_tied(normal_best_tied),
        .decode_cycles(normal_decode_cycles),.score_issues(normal_score_issues_reported),
        .bound_row_cycles(normal_bound_rows),.context_wait_cycles(normal_context_wait),
        .max_task_occupancy(normal_max_task_occupancy),.max_bound_waiters(normal_max_bound_waiters),
        .dbg_table_re(1'b0),.dbg_table_sel(1'b0),.dbg_table_group(4'b0),.dbg_table_addr(15'b0),
        .dbg_table_valid(unused_table_valid_n),.dbg_table_data(unused_table_data_n),
        .dbg_query_start(1'b0),.dbg_query_force_full(1'b0),.dbg_query_suffix(6'b0),.dbg_query_budget(3'b0),
        .dbg_query_information_cost(14'b0),.dbg_query_states(64'b0),.dbg_query_threshold(14'b0),
        .dbg_query_done(unused_query_done_n),.dbg_query_pass(unused_query_pass_n),.dbg_query_minimum(unused_query_min_n),
        .dbg_score_start(1'b0),.dbg_score_mask(64'b0),.dbg_score_information_cost(14'b0),.dbg_score_states(64'b0),
        .dbg_score_done(unused_score_done_n),.dbg_score_metric(unused_score_metric_n)
    );

    cap_nonblocking_s45_top compact (
        .clk(clk),.rst(rst),
        .cfg_row_we(cfg_row_we),.cfg_row_group(cfg_row_group),.cfg_row_rank(cfg_row_rank),.cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we),.cfg_phi_group(cfg_phi_group),.cfg_phi_state(cfg_phi_state),.cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we),.cfg_info_rank(cfg_info_rank),.cfg_info_cost(cfg_info_cost),
