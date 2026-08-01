        .cfg_base_states(cfg_base_states),.cfg_seed_best_metric(cfg_seed_best_metric),.cfg_seed_mask(cfg_seed_mask),.cfg_seed_tie(cfg_seed_tie),
        .build_start(build_start),.build_busy(s45_build_busy),.build_done(s45_build_done),.build_cycles(s45_build_cycles),
        .decode_start(decode_start),.decode_busy(s45_decode_busy),.decode_done(s45_decode_done),
        .best_metric(s45_best_metric),.best_tep(s45_best_tep),.best_tied(s45_best_tied),
        .decode_cycles(s45_decode_cycles),.score_issues(s45_score_issues_reported),
        .bound_row_cycles(s45_bound_rows),.context_wait_cycles(s45_context_wait),
        .max_task_occupancy(s45_max_task_occupancy),.max_bound_waiters(s45_max_bound_waiters),
        .dbg_table_re(1'b0),.dbg_table_sel(1'b0),.dbg_table_group(4'b0),.dbg_table_addr(15'b0),
        .dbg_table_valid(unused_table_valid_c),.dbg_table_data(unused_table_data_c),
        .dbg_query_start(1'b0),.dbg_query_force_full(1'b0),.dbg_query_suffix(6'b0),.dbg_query_budget(3'b0),
        .dbg_query_information_cost(14'b0),.dbg_query_states(64'b0),.dbg_query_threshold(14'b0),
        .dbg_query_done(unused_query_done_c),.dbg_query_pass(unused_query_pass_c),.dbg_query_minimum(unused_query_min_c),
        .dbg_score_start(1'b0),.dbg_score_mask(64'b0),.dbg_score_information_cost(14'b0),.dbg_score_states(64'b0),
        .dbg_score_done(unused_score_done_c),.dbg_score_metric(unused_score_metric_c)
    );

    assign normal_issue_mask = {
        normal.core.score_in_valid[3],normal.core.score_in_valid[2],
        normal.core.score_in_valid[1],normal.core.score_in_valid[0]
    };
    assign s45_issue_mask = {
        compact.core.score_in_valid[3],compact.core.score_in_valid[2],
        compact.core.score_in_valid[1],compact.core.score_in_valid[0]
    };

    // Build-state encodings are fixed in cap_nonblocking_core:
    // normal DP 1..3; right DP 4..6; expansion 7..8;
    // triangular DP 9..10; sequential prefix/finalization 11.
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
                    else if (normal.core.b_state >= 4 && normal.core.b_state <= 6)
                        normal_build_right_dp_cycles <= normal_build_right_dp_cycles + 1;
                    else if (normal.core.b_state >= 7 && normal.core.b_state <= 8)
                        normal_build_expansion_cycles <= normal_build_expansion_cycles + 1;
                    else if (normal.core.b_state >= 9 && normal.core.b_state <= 10)
                        normal_build_triangular_cycles <= normal_build_triangular_cycles + 1;
                    else if (normal.core.b_state == 11)
                        normal_build_prefix_cycles <= normal_build_prefix_cycles + 1;
                end
                if (compact.core.build_busy) begin
                    if (compact.core.b_state >= 1 && compact.core.b_state <= 3)
                        s45_build_normal_dp_cycles <= s45_build_normal_dp_cycles + 1;
                    else if (compact.core.b_state >= 4 && compact.core.b_state <= 6)
                        s45_build_right_dp_cycles <= s45_build_right_dp_cycles + 1;
                    else if (compact.core.b_state >= 7 && compact.core.b_state <= 8)
                        s45_build_expansion_cycles <= s45_build_expansion_cycles + 1;
                    else if (compact.core.b_state >= 9 && compact.core.b_state <= 10)
                        s45_build_triangular_cycles <= s45_build_triangular_cycles + 1;
                    else if (compact.core.b_state == 11)
                        s45_build_prefix_cycles <= s45_build_prefix_cycles + 1;
                end
            end

            if (decode_start) begin
                normal_fifo_full_cycles <= 0;
                normal_fifo_empty_cycles <= 0;
                s45_fifo_full_cycles <= 0;
                s45_fifo_empty_cycles <= 0;
            end else begin
                // Count queue pressure only during the work-stealing phase (D_RUN=3).
                if (normal.core.decode_busy && normal.core.d_state == 3) begin
                    if ((normal.core.task_count - normal.core.task_head) == 2048)
                        normal_fifo_full_cycles <= normal_fifo_full_cycles + 1;
                    if (normal.core.task_count == normal.core.task_head)
                        normal_fifo_empty_cycles <= normal_fifo_empty_cycles + 1;
                end
                if (compact.core.decode_busy && compact.core.d_state == 3) begin
                    if ((compact.core.task_count - compact.core.task_head) == 2048)
                        s45_fifo_full_cycles <= s45_fifo_full_cycles + 1;
                    if (compact.core.task_count == compact.core.task_head)
                        s45_fifo_empty_cycles <= s45_fifo_empty_cycles + 1;
                end
            end
        end
    end
