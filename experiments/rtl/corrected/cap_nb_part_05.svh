                    D_RUN: begin
                        // One shared synchronous FIFO pop per cycle; whichever
                        // context is idle first steals the next prefix task.
                        if (!pop_pending && task_head<task_count) begin
                            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                                if (!pop_pending && ctx_state[lane]==C_IDLE) begin
                                    task_re<=1; task_raddr<=task_head[TASK_AW-1:0];
                                    task_head<=task_head+1; pop_pending<=1;
                                    pop_context<=lane[1:0];
                                end
                            end
                        end else if (pop_pending) begin
                            ctx_mask[pop_context]<=task_rdata[TASK_W-1 -: 64];
                            ctx_next[pop_context]<=task_rdata[TASK_W-65 -: 7];
                            ctx_info[pop_context]<=task_rdata[METRIC_W+63 -: METRIC_W];
                            ctx_states[pop_context]<=task_rdata[63:0];
                            // A pair ending at rank K-1 has no order-3 or
                            // order-4 descendants.  Do not start a zero-row
                            // bound query, which would otherwise leave
                            // q_active asserted forever.
                            if (task_rdata[TASK_W-65 -: 7] >= K)
                                ctx_state[pop_context]<=C_IDLE;
                            else
                                ctx_state[pop_context]<=C_BOUND2_REQ;
                            pop_pending<=0;
                        end

                        // Query arbiter grant.
                        if (grant_valid && !q_active) begin
                            q_active<=1; q_force_full<=0; q_debug_owner<=0;
                            q_context<=grant_ctx;
                            q_states<=ctx_state[grant_ctx]==C_BOUND3_REQ?
                                ctx_d3_states[grant_ctx]:ctx_states[grant_ctx];
                            q_information<=ctx_state[grant_ctx]==C_BOUND3_REQ?
                                ctx_d3_info[grant_ctx]:ctx_info[grant_ctx];
                            q_suffix<=ctx_state[grant_ctx]==C_BOUND3_REQ?
                                ctx_leaf[grant_ctx]:ctx_next[grant_ctx];
                            q_budget<=ctx_state[grant_ctx]==C_BOUND3_REQ?1:2;
                            q_threshold<=best_metric; q_issue_index<=0;
                            q_minimum_seen<=INF_METRIC; q_eval_valid<=0;
                            q_context_depth3<=ctx_state[grant_ctx]==C_BOUND3_REQ;
                            if (ctx_state[grant_ctx]==C_BOUND3_REQ)
                                ctx_state[grant_ctx]<=C_BOUND3_WAIT;
                            else ctx_state[grant_ctx]<=C_BOUND2_WAIT;
                            qfill=0;
                            if (COMPACT && (ctx_state[grant_ctx]==C_BOUND3_REQ?
                                ctx_leaf[grant_ctx]:ctx_next[grant_ctx])<SPLIT) begin
                                for (qi=1;qi<=ORDER;qi=qi+1) begin
                                    if (qi<=(ctx_state[grant_ctx]==C_BOUND3_REQ?1:2)) begin
                                        for (qj=0;qj<=ORDER;qj=qj+1) begin
                                            if (qj<=qi &&
                                                (ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                    ctx_leaf[grant_ctx]:ctx_next[grant_ctx])+qj<=SPLIT &&
                                                SPLIT+(qi-qj)<=K) begin
                                                q_pair_rows[qfill]<=pair_index(qj,qi-qj);
                                                q_weight_rows[qfill]<=qi;
                                                q_info_rows[qfill]<=(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                    ctx_d3_info[grant_ctx]:ctx_info[grant_ctx])+
                                                    (info_prefix[(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                        ctx_leaf[grant_ctx]:ctx_next[grant_ctx])+qj]-
                                                     info_prefix[(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                        ctx_leaf[grant_ctx]:ctx_next[grant_ctx])])+
                                                    (info_prefix[SPLIT+(qi-qj)]-info_prefix[SPLIT]);
                                                qfill=qfill+1;
                                            end
                                        end
                                    end
                                end
                            end else begin
                                for (qi=1;qi<=ORDER;qi=qi+1) begin
                                    if (qi<=(ctx_state[grant_ctx]==C_BOUND3_REQ?1:2) &&
                                        (ctx_state[grant_ctx]==C_BOUND3_REQ?
                                            ctx_leaf[grant_ctx]:ctx_next[grant_ctx])+qi<=K) begin
                                        q_pair_rows[qfill]<=0; q_weight_rows[qfill]<=qi;
                                        q_info_rows[qfill]<=(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                            ctx_d3_info[grant_ctx]:ctx_info[grant_ctx])+
                                            (info_prefix[(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                ctx_leaf[grant_ctx]:ctx_next[grant_ctx])+qi]-
                                             info_prefix[(ctx_state[grant_ctx]==C_BOUND3_REQ?
                                                ctx_leaf[grant_ctx]:ctx_next[grant_ctx])]);
                                        qfill=qfill+1;
                                    end
                                end
                            end
                            q_count<=qfill;
                            if (qfill==0) begin
                                q_active<=0;
                                q_result_valid<=1;
                                q_result_pass<=0;
                                q_result_context<=grant_ctx;
                                q_result_depth3<=ctx_state[grant_ctx]==C_BOUND3_REQ;
                            end
                        end

                        if (q_result_valid && !q_debug_owner) begin
                            if (q_result_depth3) begin
                                if (q_result_pass) ctx_state[q_result_context]<=C_LEAF;
                                else ctx_state[q_result_context]<=C_DEPTH3;
                            end else begin
                                if (q_result_pass) begin
                                    ctx_r3[q_result_context]<=ctx_next[q_result_context];
                                    ctx_state[q_result_context]<=C_DEPTH3;
                                end else ctx_state[q_result_context]<=C_IDLE;
                            end
                        end

                        // Context issue paths.  Each context owns one scorer lane.
                        for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                            if (ctx_state[lane]==C_DEPTH3) begin
                                if (ctx_r3[lane]>=K ||
                                    ctx_info[lane]+info_cost[ctx_r3[lane]]>best_metric) begin
                                    ctx_state[lane]<=C_IDLE;
                                end else begin
                                    tmp_states=xor_row_states(ctx_states[lane],ctx_r3[lane]);
                                    tmp_info=ctx_info[lane]+info_cost[ctx_r3[lane]];
                                    tmp_mask=ctx_mask[lane]|(64'b1<<ctx_r3[lane]);
                                    score_in_valid[lane]<=1; score_in_mask[lane]<=tmp_mask;
                                    score_in_states[lane]<=tmp_states; score_in_information[lane]<=tmp_info;
                                    ctx_d3_states[lane]<=tmp_states; ctx_d3_info[lane]<=tmp_info;
                                    ctx_d3_mask[lane]<=tmp_mask; ctx_leaf[lane]<=ctx_r3[lane]+1;
                                    ctx_r3[lane]<=ctx_r3[lane]+1;
                                    if (ctx_r3[lane]+1<K) ctx_state[lane]<=C_BOUND3_REQ;
                                end
                            end else if (ctx_state[lane]==C_LEAF) begin
                                if (ctx_leaf[lane]>=K ||
                                    ctx_d3_info[lane]+info_cost[ctx_leaf[lane]]>best_metric) begin
                                    ctx_state[lane]<=C_DEPTH3;
                                end else begin
                                    tmp_states=xor_row_states(ctx_d3_states[lane],ctx_leaf[lane]);
                                    tmp_info=ctx_d3_info[lane]+info_cost[ctx_leaf[lane]];
                                    tmp_mask=ctx_d3_mask[lane]|(64'b1<<ctx_leaf[lane]);
                                    score_in_valid[lane]<=1; score_in_mask[lane]<=tmp_mask;
                                    score_in_states[lane]<=tmp_states; score_in_information[lane]<=tmp_info;
                                    ctx_leaf[lane]<=ctx_leaf[lane]+1;
                                end
                            end
                        end

                        if (task_head==task_count && !pop_pending && all_contexts_idle &&
                            !q_active) d_state<=D_DRAIN;
                    end
                    D_DRAIN: begin
                        if (!score_pipe_nonempty) begin
                            decode_busy<=0; decode_done<=1; d_state<=D_IDLE;
                        end
                    end
                    default: d_state<=D_IDLE;
                endcase
                // The earlier per-lane nonblocking increments collapse when
                // multiple lanes issue together.  This later assignment wins
                // and accumulates the exact number of accepted lane issues.
                score_issues<=score_issues+
                    score_in_valid[0]+score_in_valid[1]+
                    score_in_valid[2]+score_in_valid[3];
            end
        end
    end
endmodule

module cap_nonblocking_normal_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire [3:0] cfg_row_group,input wire [5:0] cfg_row_rank,input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we,input wire [3:0] cfg_phi_group,input wire [4:0] cfg_phi_state,input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we,input wire [5:0] cfg_info_rank,input wire [6:0] cfg_info_cost,
    input wire [63:0] cfg_base_states,input wire [13:0] cfg_seed_best_metric,input wire [63:0] cfg_seed_mask,input wire cfg_seed_tie,
    input wire build_start,output wire build_busy,output wire build_done,output wire [31:0] build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire [13:0] best_metric,output wire [63:0] best_tep,output wire best_tied,
    output wire [31:0] decode_cycles,output wire [31:0] score_issues,output wire [31:0] bound_row_cycles,output wire [31:0] context_wait_cycles,
    output wire [11:0] max_task_occupancy,output wire [2:0] max_bound_waiters,
    input wire dbg_table_re,input wire dbg_table_sel,input wire [3:0] dbg_table_group,input wire [14:0] dbg_table_addr,output wire dbg_table_valid,output wire [9:0] dbg_table_data,
    input wire dbg_query_start,input wire dbg_query_force_full,input wire [5:0] dbg_query_suffix,input wire [2:0] dbg_query_budget,input wire [13:0] dbg_query_information_cost,input wire [63:0] dbg_query_states,input wire [13:0] dbg_query_threshold,
    output wire dbg_query_done,output wire dbg_query_pass,output wire [13:0] dbg_query_minimum,
    input wire dbg_score_start,input wire [63:0] dbg_score_mask,input wire [13:0] dbg_score_information_cost,input wire [63:0] dbg_score_states,output wire dbg_score_done,output wire [13:0] dbg_score_metric
);
    cap_nonblocking_core #(.COMPACT(0),.SPLIT(45)) core(.*);
endmodule

module cap_nonblocking_s45_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire [3:0] cfg_row_group,input wire [5:0] cfg_row_rank,input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we,input wire [3:0] cfg_phi_group,input wire [4:0] cfg_phi_state,input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we,input wire [5:0] cfg_info_rank,input wire [6:0] cfg_info_cost,
    input wire [63:0] cfg_base_states,input wire [13:0] cfg_seed_best_metric,input wire [63:0] cfg_seed_mask,input wire cfg_seed_tie,
    input wire build_start,output wire build_busy,output wire build_done,output wire [31:0] build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire [13:0] best_metric,output wire [63:0] best_tep,output wire best_tied,
    output wire [31:0] decode_cycles,output wire [31:0] score_issues,output wire [31:0] bound_row_cycles,output wire [31:0] context_wait_cycles,
    output wire [11:0] max_task_occupancy,output wire [2:0] max_bound_waiters,
    input wire dbg_table_re,input wire dbg_table_sel,input wire [3:0] dbg_table_group,input wire [14:0] dbg_table_addr,output wire dbg_table_valid,output wire [9:0] dbg_table_data,
    input wire dbg_query_start,input wire dbg_query_force_full,input wire [5:0] dbg_query_suffix,input wire [2:0] dbg_query_budget,input wire [13:0] dbg_query_information_cost,input wire [63:0] dbg_query_states,input wire [13:0] dbg_query_threshold,
    output wire dbg_query_done,output wire dbg_query_pass,output wire [13:0] dbg_query_minimum,
    input wire dbg_score_start,input wire [63:0] dbg_score_mask,input wire [13:0] dbg_score_information_cost,input wire [63:0] dbg_score_states,output wire dbg_score_done,output wire [13:0] dbg_score_metric
);
    cap_nonblocking_core #(.COMPACT(1),.SPLIT(45)) core(.*);
endmodule
