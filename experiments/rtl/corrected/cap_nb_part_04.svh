                    B_FINISH: begin
                        build_busy<=0; build_done<=1; b_state<=B_IDLE;
                    end
                    default: b_state<=B_IDLE;
                endcase
            end

            // Debug table output one cycle after request.
            dbg_re_d<=dbg_table_re && !build_busy && !q_active && !decode_busy;
            dbg_sel_d<=dbg_table_sel;
            dbg_group_d<=dbg_table_group;
            dbg_table_valid<=dbg_re_d;
            if (dbg_re_d) begin
                if (dbg_sel_d) dbg_table_data<=compact_a_q[dbg_group_d];
                else if (COMPACT) dbg_table_data<=right_a_q[dbg_group_d];
                else dbg_table_data<=normal_a_q[dbg_group_d];
            end

            // Query engine: evaluate previous row while issuing the next row.
            q_issue_now = q_active && (q_issue_index < q_count);
            if (q_eval_valid) begin
                query_total = q_eval_information;
                for (sumg=0;sumg<GROUPS;sumg=sumg+1) begin
                    if (COMPACT && q_suffix<SPLIT) query_total=query_total+compact_a_q[sumg];
                    else if (COMPACT) query_total=query_total+right_a_q[sumg];
                    else query_total=query_total+normal_a_q[sumg];
                end
                if (query_total < q_minimum_seen) q_minimum_seen<=query_total[METRIC_W-1:0];
                query_pass_now = query_total <= q_threshold;
                bound_row_cycles<=bound_row_cycles+1;
                if ((!q_force_full && query_pass_now) || q_eval_last) begin
                    q_active<=0;
                    q_eval_valid<=0;
                    q_result_valid<=1;
                    q_result_pass<=query_pass_now || (q_minimum_seen<=q_threshold);
                    q_result_context<=q_context;
                    q_result_depth3<=q_context_depth3;
                    if (q_debug_owner) begin
                        dbg_query_done<=1;
                        dbg_query_pass<=query_pass_now || (q_minimum_seen<=q_threshold);
                        dbg_query_minimum<=query_total<q_minimum_seen?
                            query_total[METRIC_W-1:0]:q_minimum_seen;
                    end
                end else begin
                    q_eval_valid<=q_issue_now;
                    q_eval_last<=q_issue_now && (q_issue_index==q_count-1);
                    if (q_issue_now) begin
                        q_eval_information<=q_info_rows[q_issue_index];
                        q_issue_index<=q_issue_index+1;
                    end
                end
            end else if (q_issue_now) begin
                q_eval_valid<=1;
                q_eval_last<=q_issue_index==q_count-1;
                q_eval_information<=q_info_rows[q_issue_index];
                q_issue_index<=q_issue_index+1;
            end

            // External debug query can start only when decoder is idle.
            if (dbg_query_start && !q_active && !decode_busy && !build_busy) begin
                q_active<=1; q_force_full<=dbg_query_force_full; q_debug_owner<=1;
                q_context<=0; q_context_depth3<=0;
                q_suffix<=dbg_query_suffix; q_budget<=dbg_query_budget;
                q_information<=dbg_query_information_cost; q_states<=dbg_query_states;
                q_threshold<=dbg_query_threshold; q_issue_index<=0;
                q_minimum_seen<=INF_METRIC; q_eval_valid<=0;
                qfill=0;
                if (COMPACT && dbg_query_suffix<SPLIT) begin
                    for (qi=1;qi<=ORDER;qi=qi+1) begin
                        if (qi<=dbg_query_budget) begin
                            for (qj=0;qj<=ORDER;qj=qj+1) begin
                                if (qj<=qi && dbg_query_suffix+qj<=SPLIT && SPLIT+(qi-qj)<=K) begin
                                    q_pair_rows[qfill]<=pair_index(qj,qi-qj);
                                    q_weight_rows[qfill]<=qi;
                                    q_info_rows[qfill]<=dbg_query_information_cost+
                                        (info_prefix[dbg_query_suffix+qj]-info_prefix[dbg_query_suffix])+
                                        (info_prefix[SPLIT+(qi-qj)]-info_prefix[SPLIT]);
                                    qfill=qfill+1;
                                end
                            end
                        end
                    end
                end else begin
                    for (qi=1;qi<=ORDER;qi=qi+1) begin
                        if (qi<=dbg_query_budget && dbg_query_suffix+qi<=K) begin
                            q_pair_rows[qfill]<=0; q_weight_rows[qfill]<=qi;
                            q_info_rows[qfill]<=dbg_query_information_cost+
                                (info_prefix[dbg_query_suffix+qi]-info_prefix[dbg_query_suffix]);
                            qfill=qfill+1;
                        end
                    end
                end
                q_count<=qfill;
            end

            // Score pipeline shift and exact metric insertion.
            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                score_v[0][lane]<=score_in_valid[lane];
                score_m[0][lane]<=score_in_mask[lane];
                score_d[0][lane]<=parity_sum_comb[lane][METRIC_W-1:0];
                if (score_in_valid[lane]) score_issues<=score_issues+1;
                for (stage=1;stage<SCORE_LATENCY;stage=stage+1) begin
                    score_v[stage][lane]<=score_v[stage-1][lane];
                    score_m[stage][lane]<=score_m[stage-1][lane];
                    score_d[stage][lane]<=score_d[stage-1][lane];
                end
            end

            // Canonical reduction of all score completions in this cycle.
            reduce_metric=best_metric; reduce_mask=best_tep; reduce_tie=best_tied;
            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                if (score_v[SCORE_LATENCY-1][lane]) begin
                    candidate_metric=score_d[SCORE_LATENCY-1][lane];
                    candidate_mask=score_m[SCORE_LATENCY-1][lane];
                    if (candidate_mask!=reduce_mask) begin
                        if (candidate_metric<reduce_metric) begin
                            reduce_metric=candidate_metric;
                            reduce_mask=candidate_mask;
                            reduce_tie=0;
                        end else if (candidate_metric==reduce_metric) begin
                            reduce_tie=1;
                            if (canonical_before(candidate_mask,reduce_mask))
                                reduce_mask=candidate_mask;
                        end
                    end
                end
            end
            if (decode_busy) begin
                best_metric<=reduce_metric; best_tep<=reduce_mask; best_tied<=reduce_tie;
            end

            // Debug scorer uses lane 0 while decode is idle.
            if (dbg_score_start && !decode_busy) begin
                score_in_valid[0]<=1;
                score_in_mask[0]<=dbg_score_mask;
                score_in_states[0]<=dbg_score_states;
                score_in_information[0]<=dbg_score_information_cost;
            end
            if (!decode_busy && score_v[SCORE_LATENCY-1][0]) begin
                dbg_score_done<=1;
                dbg_score_metric<=score_d[SCORE_LATENCY-1][0];
            end

            // Decoder start/reset.
            if (decode_start && !decode_busy && !build_busy && !q_active) begin
                decode_busy<=1; decode_cycles<=0; d_state<=D_SINGLE;
                best_metric<=seed_metric_latched; best_tep<=seed_mask_latched;
                best_tied<=seed_tie_latched;
                score_issues<=0; bound_row_cycles<=0; context_wait_cycles<=0;
                max_task_occupancy<=0; max_bound_waiters<=0;
                task_count<=0; task_head<=0; pop_pending<=0;
                gen_i<=0; gen_j<=1;
                for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                    ctx_state[lane]<=C_IDLE;
                    for (stage=0;stage<SCORE_LATENCY;stage=stage+1)
                        score_v[stage][lane]<=0;
                end
            end else if (decode_busy) begin
                decode_cycles<=decode_cycles+1;
                if (waiter_count>max_bound_waiters) max_bound_waiters<=waiter_count;
                if ((task_count-task_head)>max_task_occupancy)
                    max_task_occupancy<=task_count-task_head;
                for (lane=0;lane<CONTEXTS;lane=lane+1)
                    if ((ctx_state[lane]==C_BOUND2_REQ || ctx_state[lane]==C_BOUND3_REQ) &&
                        !(grant_valid && grant_ctx==lane[1:0]))
                        context_wait_cycles<=context_wait_cycles+1;

                case (d_state)
                    D_SINGLE: begin
                        score_in_valid[0]<=1;
                        score_in_mask[0]<=64'b1<<gen_i;
                        score_in_states[0]<=xor_row_states(base_states_latched,gen_i);
                        score_in_information[0]<=info_cost[gen_i];
                        if (gen_i==K-1) begin gen_i<=0; gen_j<=1; d_state<=D_PAIR; end
                        else gen_i<=gen_i+1;
                    end
                    D_PAIR: begin
                        tmp_states=xor_row_states(xor_row_states(base_states_latched,gen_i),gen_j);
                        tmp_info=info_cost[gen_i]+info_cost[gen_j];
                        tmp_mask=(64'b1<<gen_i)|(64'b1<<gen_j);
                        score_in_valid[0]<=1; score_in_mask[0]<=tmp_mask;
                        score_in_states[0]<=tmp_states; score_in_information[0]<=tmp_info;
                        task_we<=1; task_waddr<=task_count[TASK_AW-1:0];
                        task_wdata<={tmp_mask,gen_j+1,tmp_info,tmp_states};
                        task_count<=task_count+1;
                        if (gen_j==K-1) begin
                            if (gen_i==K-2) d_state<=D_RUN;
                            else begin gen_i<=gen_i+1; gen_j<=gen_i+2; end
                        end else gen_j<=gen_j+1;
                    end