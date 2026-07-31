                    end
                end
            end
        end else begin
            // Pipelined bound row issue.
            if (q_active && q_issue_index < q_count) begin
                for (cg=0; cg<GROUPS; cg=cg+1) begin
                    if (COMPACT && q_suffix < SPLIT) begin
                        compact_a_en[cg]=1;
                        compact_a_addr[cg]=compact_addr(q_suffix,
                            q_pair_rows[q_issue_index],packed_group_state(q_states,cg));
                    end else if (COMPACT) begin
                        right_a_en[cg]=1;
                        right_a_addr[cg]=right_addr(q_suffix,
                            q_weight_rows[q_issue_index],packed_group_state(q_states,cg));
                    end else begin
                        normal_a_en[cg]=1;
                        normal_a_addr[cg]=normal_addr(q_suffix,
                            q_weight_rows[q_issue_index],packed_group_state(q_states,cg));
                    end
                end
            end else if (dbg_table_re && !q_active && !decode_busy) begin
                for (cg=0; cg<GROUPS; cg=cg+1) begin
                    if (dbg_table_sel) begin
                        compact_a_en[cg]=1;
                        compact_a_addr[cg]=dbg_table_addr[COMPACT_AW-1:0];
                    end else if (COMPACT) begin
                        right_a_en[cg]=1;
                        right_a_addr[cg]=dbg_table_addr[RIGHT_AW-1:0];
                    end else begin
                        normal_a_en[cg]=1;
                        normal_a_addr[cg]=dbg_table_addr[NORMAL_AW-1:0];
                    end
                end
            end
        end

        // Scorer exact parity sums.
        for (sl=0; sl<CONTEXTS; sl=sl+1) begin
            parity_sum_comb[sl]=score_in_information[sl];
            for (sg=0; sg<GROUPS; sg=sg+1)
                parity_sum_comb[sl]=parity_sum_comb[sl] +
                    phi_cost[sg][packed_group_state(score_in_states[sl],sg)];
        end

        score_pipe_nonempty=0;
        for (sp=0;sp<SCORE_LATENCY;sp=sp+1)
            for (sl=0;sl<CONTEXTS;sl=sl+1)
                score_pipe_nonempty=score_pipe_nonempty | score_v[sp][sl];

        all_contexts_idle=1;
        waiter_count=0;
        grant_valid=0;
        grant_ctx=0;
        for (sl=0;sl<CONTEXTS;sl=sl+1) begin
            if (ctx_state[sl]!=C_IDLE) all_contexts_idle=0;
            if (ctx_state[sl]==C_BOUND2_REQ || ctx_state[sl]==C_BOUND3_REQ) begin
                waiter_count=waiter_count+1;
                if (!grant_valid && !q_active) begin
                    grant_valid=1;
                    grant_ctx=sl[1:0];
                end
            end
        end
    end

    // ------------------------------------------------------------------
    // Sequential control.
    // ------------------------------------------------------------------
    integer k;
    integer lane;
    integer stage;
    integer sumg;
    reg [METRIC_W-1:0] reduce_metric;
    reg [63:0] reduce_mask;
    reg reduce_tie;
    reg [METRIC_W-1:0] candidate_metric;
    reg [63:0] candidate_mask;
    reg [METRIC_W+4:0] query_total;
    reg query_pass_now;

    always @(posedge clk) begin
        if (rst) begin
            build_busy<=0; build_done<=0; build_cycles<=0; b_state<=B_IDLE;
            decode_busy<=0; decode_done<=0; decode_cycles<=0;
            best_metric<=INF_METRIC; best_tep<=0; best_tied<=0;
            score_issues<=0; bound_row_cycles<=0; context_wait_cycles<=0;
            max_task_occupancy<=0; max_bound_waiters<=0;
            q_active<=0; q_eval_valid<=0; q_result_valid<=0;
            dbg_query_done<=0; dbg_query_pass<=0; dbg_query_minimum<=INF_METRIC;
            dbg_score_done<=0; dbg_score_metric<=0;
            dbg_table_valid<=0; dbg_re_d<=0; dbg_sel_d<=0; dbg_group_d<=0;
            task_we<=0; task_re<=0; task_count<=0; task_head<=0;
            pop_pending<=0; d_state<=D_IDLE; gen_i<=0; gen_j<=1;
            base_states_latched<=0; seed_metric_latched<=0;
            seed_mask_latched<=0; seed_tie_latched<=0;
            for (k=0;k<=K;k=k+1) info_prefix[k]<=0;
            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                ctx_state[lane]<=C_IDLE;
                score_in_valid[lane]<=0;
                for (stage=0;stage<SCORE_LATENCY;stage=stage+1) begin
                    score_v[stage][lane]<=0;
                    score_m[stage][lane]<=0;
                    score_d[stage][lane]<=0;
                end
            end
        end else begin
            build_done<=0; decode_done<=0; dbg_query_done<=0; dbg_score_done<=0;
            task_we<=0; task_re<=0; q_result_valid<=0;
            for (lane=0;lane<CONTEXTS;lane=lane+1) score_in_valid[lane]<=0;

            // Latch frame-wide configuration at build start and derive prefix sums.
            if (build_start && !build_busy && !decode_busy) begin
                build_busy<=1; build_cycles<=0;
                base_states_latched<=cfg_base_states;
                seed_metric_latched<=cfg_seed_best_metric;
                seed_mask_latched<=cfg_seed_mask;
                seed_tie_latched<=cfg_seed_tie;
                info_prefix[0]<=0;
                for (k=0;k<K;k=k+1)
                    info_prefix[k+1]<=info_prefix[k]+info_cost[k];
                b_weight<=0; b_value_state<=0; b_pair<=0;
                if (COMPACT) begin b_state<=B_R_INIT; b_suffix<=K; end
                else begin b_state<=B_N_INIT; b_suffix<=K; end
            end else if (build_busy) begin
                build_cycles<=build_cycles+1;
                case (b_state)
                    B_N_INIT: begin
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==ORDER) begin
                                b_weight<=0; b_suffix<=K-1; b_state<=B_N_READ;
                            end else b_weight<=b_weight+1;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_N_READ: b_state<=B_N_WRITE;
                    B_N_WRITE: begin
                        b_state<=B_N_READ;
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==ORDER) begin
                                b_weight<=0;
                                if (b_suffix==0) b_state<=B_FINISH;
                                else b_suffix<=b_suffix-1;
                            end else b_weight<=b_weight+1;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_R_INIT: begin
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==ORDER) begin
                                b_weight<=0; b_suffix<=K-1; b_state<=B_R_READ;
                            end else b_weight<=b_weight+1;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_R_READ: b_state<=B_R_WRITE;
                    B_R_WRITE: begin
                        b_state<=B_R_READ;
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==ORDER) begin
                                b_weight<=0;
                                if (b_suffix==SPLIT) begin
                                    b_pair<=0; b_state<=B_EXP_READ;
                                end else b_suffix<=b_suffix-1;
                            end else b_weight<=b_weight+1;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_EXP_READ: b_state<=B_EXP_WRITE;
                    B_EXP_WRITE: begin
                        b_state<=B_EXP_READ;
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_pair==PAIRS-1) begin
                                b_pair<=0; b_suffix<=SPLIT-1; b_state<=B_L_READ;
                            end else b_pair<=b_pair+1;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_L_READ: b_state<=B_L_WRITE;
                    B_L_WRITE: begin
                        b_state<=B_L_READ;
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_pair==PAIRS-1) begin
                                b_pair<=0;
                                if (b_suffix==0) b_state<=B_FINISH;
                                else b_suffix<=b_suffix-1;
                            end else b_pair<=b_pair+1;
                        end else b_value_state<=b_value_state+1;
                    end