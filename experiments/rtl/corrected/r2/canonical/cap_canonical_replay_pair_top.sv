`timescale 1ns/1ps

// Canonical replay A/B wrapper for the isolated split-phase experiment.
// External names retain the established replay ABI:
//   normal_* = validated blocking R2 production baseline
//   s45_*    = tagged dual-port split-phase R2 candidate
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
    output wire [63:0] normal_issue_tep0,
    output wire [63:0] normal_issue_tep1,
    output wire [63:0] normal_issue_tep2,
    output wire [63:0] normal_issue_tep3,
    output wire [63:0] normal_issue_states0,
    output wire [63:0] normal_issue_states1,
    output wire [63:0] normal_issue_states2,
    output wire [63:0] normal_issue_states3,
    output wire [13:0] normal_issue_info0,
    output wire [13:0] normal_issue_info1,
    output wire [13:0] normal_issue_info2,
    output wire [13:0] normal_issue_info3,
    output reg  [31:0] normal_fifo_full_cycles,
    output reg  [31:0] normal_fifo_empty_cycles,
    output reg  [31:0] normal_build_normal_dp_cycles,
    output reg  [31:0] normal_build_right_dp_cycles,
    output reg  [31:0] normal_build_expansion_cycles,
    output reg  [31:0] normal_build_triangular_cycles,
    output reg  [31:0] normal_build_prefix_cycles,

    // ABI-compatible second architecture; this is R2, not S45.
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
    cap_r2_normal_top normal (
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
        .max_task_occupancy(normal_max_task_occupancy),.max_bound_waiters(normal_max_bound_waiters)
    );

    cap_r2_split_normal_top compact (
        .clk(clk),.rst(rst),
        .cfg_row_we(cfg_row_we),.cfg_row_group(cfg_row_group),.cfg_row_rank(cfg_row_rank),.cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we),.cfg_phi_group(cfg_phi_group),.cfg_phi_state(cfg_phi_state),.cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we),.cfg_info_rank(cfg_info_rank),.cfg_info_cost(cfg_info_cost),
        .cfg_base_states(cfg_base_states),.cfg_seed_best_metric(cfg_seed_best_metric),.cfg_seed_mask(cfg_seed_mask),.cfg_seed_tie(cfg_seed_tie),
        .build_start(build_start),.build_busy(s45_build_busy),.build_done(s45_build_done),.build_cycles(s45_build_cycles),
        .decode_start(decode_start),.decode_busy(s45_decode_busy),.decode_done(s45_decode_done),
        .best_metric(s45_best_metric),.best_tep(s45_best_tep),.best_tied(s45_best_tied),
        .decode_cycles(s45_decode_cycles),.score_issues(s45_score_issues_reported),
        .bound_row_cycles(s45_bound_rows),.context_wait_cycles(s45_context_wait),
        .max_task_occupancy(s45_max_task_occupancy),.max_bound_waiters(s45_max_bound_waiters)
    );

    assign normal_issue_mask = {
        normal.core.score_in_valid[3],normal.core.score_in_valid[2],
        normal.core.score_in_valid[1],normal.core.score_in_valid[0]
    };
    assign s45_issue_mask = {
        compact.core.score_in_valid[3],compact.core.score_in_valid[2],
        compact.core.score_in_valid[1],compact.core.score_in_valid[0]
    };
    assign normal_issue_tep0 = normal.core.score_in_mask[0];
    assign normal_issue_tep1 = normal.core.score_in_mask[1];
    assign normal_issue_tep2 = normal.core.score_in_mask[2];
    assign normal_issue_tep3 = normal.core.score_in_mask[3];
    assign normal_issue_states0 = normal.core.score_in_states[0];
    assign normal_issue_states1 = normal.core.score_in_states[1];
    assign normal_issue_states2 = normal.core.score_in_states[2];
    assign normal_issue_states3 = normal.core.score_in_states[3];
    assign normal_issue_info0 = normal.core.score_in_information[0];
    assign normal_issue_info1 = normal.core.score_in_information[1];
    assign normal_issue_info2 = normal.core.score_in_information[2];
    assign normal_issue_info3 = normal.core.score_in_information[3];

    always @(posedge clk) begin
        if (rst) begin
            normal_fifo_full_cycles <= 0;
            normal_fifo_empty_cycles <= 0;
            normal_build_normal_dp_cycles <= 0;
            normal_build_right_dp_cycles <= 0;
            normal_build_expansion_cycles <= 0;
            normal_build_triangular_cycles <= 0;
            normal_build_prefix_cycles <= 0;
            s45_fifo_full_cycles <= 0;
            s45_fifo_empty_cycles <= 0;
            s45_build_normal_dp_cycles <= 0;
            s45_build_right_dp_cycles <= 0;
            s45_build_expansion_cycles <= 0;
            s45_build_triangular_cycles <= 0;
            s45_build_prefix_cycles <= 0;
        end else begin
            if (build_start) begin
                normal_build_normal_dp_cycles <= 0;
                normal_build_right_dp_cycles <= 0;
                normal_build_expansion_cycles <= 0;
                normal_build_triangular_cycles <= 0;
                normal_build_prefix_cycles <= 0;
                s45_build_normal_dp_cycles <= 0;
                s45_build_right_dp_cycles <= 0;
                s45_build_expansion_cycles <= 0;
                s45_build_triangular_cycles <= 0;
                s45_build_prefix_cycles <= 0;
            end else begin
                if (normal.core.build_busy) begin
                    if (normal.core.b_state >= 1 && normal.core.b_state <= 3)
                        normal_build_normal_dp_cycles <= normal_build_normal_dp_cycles + 1;
                    else if (normal.core.b_state == 4)
                        normal_build_prefix_cycles <= normal_build_prefix_cycles + 1;
                end
                if (compact.core.build_busy) begin
                    if (compact.core.b_state >= 1 && compact.core.b_state <= 3)
                        s45_build_normal_dp_cycles <= s45_build_normal_dp_cycles + 1;
                    else if (compact.core.b_state == 4)
                        s45_build_prefix_cycles <= s45_build_prefix_cycles + 1;
                end
            end

            if (decode_start) begin
                normal_fifo_full_cycles <= 0;
                normal_fifo_empty_cycles <= 0;
                s45_fifo_full_cycles <= 0;
                s45_fifo_empty_cycles <= 0;
            end else begin
                if (normal.core.decode_busy && normal.core.d_state == 3) begin
                    if ((normal.core.task_count-normal.core.task_head)==2048)
                        normal_fifo_full_cycles <= normal_fifo_full_cycles + 1;
                    if (normal.core.task_count==normal.core.task_head)
                        normal_fifo_empty_cycles <= normal_fifo_empty_cycles + 1;
                end
                if (compact.core.decode_busy && compact.core.d_state == 3) begin
                    if ((compact.core.task_count-compact.core.task_head)==2048)
                        s45_fifo_full_cycles <= s45_fifo_full_cycles + 1;
                    if (compact.core.task_count==compact.core.task_head)
                        s45_fifo_empty_cycles <= s45_fifo_empty_cycles + 1;
                end
            end
        end
    end
endmodule
