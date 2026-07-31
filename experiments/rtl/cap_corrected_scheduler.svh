    // ------------------------------------------------------------------
    // Prefix-task FIFO and four nonblocking DFS contexts.
    // ------------------------------------------------------------------
    localparam [3:0]
        CP_IDLE       = 0,
        CP_EMIT       = 1,
        CP_SCAN       = 2,
        CP_BOUND_WAIT = 3,
        CP_PICK       = 4,
        CP_LEAF       = 5,
        CP_BACK       = 6;

    reg [63:0] task_states [0:TASK_DEPTH-1];
    reg [13:0] task_info [0:TASK_DEPTH-1];
    reg [63:0] task_mask [0:TASK_DEPTH-1];
    reg [6:0] task_suffix [0:TASK_DEPTH-1];
    reg [6:0] task_count;
    reg [5:0] task_rptr;
    reg [5:0] seed_rank;
    reg seeding;

    reg [3:0] cphase [0:3];
    reg [2:0] cdepth [0:3];
    reg [63:0] cstates [0:3][0:4];
    reg [13:0] cinfo [0:3][0:4];
    reg [63:0] cmask [0:3][0:4];
    reg [6:0] ccursor [0:3][0:4];
    reg [6:0] csuffix [0:3][0:4];
    reg [3:0] calloc [0:3];
    reg [1:0] rr_ctx;

    reg idle_found;
    reg [1:0] idle_ctx;
    reg grant_found;
    reg [1:0] grant_ctx;
    reg grant_valid_alloc;
    reg [2:0] grant_weight, grant_left, grant_right;
    reg [6:0] grant_suffix;
    reg [13:0] grant_base;
    reg [63:0] leaf_states [0:3];
    reg [13:0] leaf_info [0:3];
    reg [63:0] leaf_mask [0:3];

    reg [13:0] root_metric;
    reg [63:0] root_state_tmp;
    integer c, d;

    always @* begin
        root_metric = 0;
        for (bg = 0; bg < 12; bg = bg + 1)
            root_metric = root_metric +
                phi_cost[bg][cfg_base_states[bg*5 +: 5]];
        root_metric = root_metric +
            phi_cost[12][{1'b0, cfg_base_states[60 +: 4]}];
    end

    always @* begin
        idle_found = 0;
        idle_ctx = 0;
        for (c = 0; c < 4; c = c + 1) begin
            if (!idle_found && cphase[c] == CP_IDLE) begin
                idle_found = 1;
                idle_ctx = c;
            end
        end
    end

    always @* begin
        grant_found = 0;
        grant_ctx = rr_ctx;
        grant_weight = 0;
        grant_left = 0;
        grant_right = 0;
        grant_suffix = 0;
        grant_base = 0;
        grant_valid_alloc = 0;

        for (c = 0; c < 4; c = c + 1) begin
            if (!grant_found &&
                cphase[(rr_ctx + c) & 3] == CP_SCAN) begin
                grant_found = 1;
                grant_ctx = (rr_ctx + c) & 3;
            end
        end

        if (grant_found) begin
            grant_weight = alloc_weight(calloc[grant_ctx]);
            grant_left = alloc_left(calloc[grant_ctx]);
            grant_right = grant_weight - grant_left;
            grant_suffix = csuffix[grant_ctx][cdepth[grant_ctx]];
            grant_valid_alloc =
                (calloc[grant_ctx] != 0) &&
                (grant_weight <= O - cdepth[grant_ctx]) &&
                (grant_suffix + grant_weight <= K) &&
                ((grant_suffix < SPLIT &&
                  grant_left <= SPLIT - grant_suffix &&
                  grant_right <= K - SPLIT) ||
                 (grant_suffix >= SPLIT &&
                  grant_left == 0 &&
                  grant_right <= K - grant_suffix));

            if (grant_suffix < SPLIT)
                grant_base = cinfo[grant_ctx][cdepth[grant_ctx]] +
                    range_info_min(grant_suffix[5:0], grant_left) +
                    range_info_min(SPLIT, grant_right);
            else
                grant_base = cinfo[grant_ctx][cdepth[grant_ctx]] +
                    range_info_min(grant_suffix[5:0], grant_right);
        end
    end

    // Decode-bound request, with debug-bound request only while idle.
    always @* begin
        q_issue_valid = 0;
        q_issue_row = 0;
        q_issue_state5 = 0;
        q_issue_state4 = 0;
        q_issue_base = 0;
        q_issue_inc = best_metric;
        q_issue_ctx = 0;
        q_issue_debug = 0;

        if (decode_busy && grant_found && grant_valid_alloc) begin
            q_issue_valid = 1;
            q_issue_ctx = grant_ctx;
            q_issue_state5 =
                cstates[grant_ctx][cdepth[grant_ctx]][59:0];
            q_issue_state4 =
                cstates[grant_ctx][cdepth[grant_ctx]][63:60];
            q_issue_base = grant_base;
            if (grant_suffix < SPLIT)
                q_issue_row = compact_row_id(
                    grant_suffix[5:0], grant_left, grant_right
                );
            else
                q_issue_row = normal_row_id(
                    grant_suffix[5:0], grant_right
                );
        end else if (!decode_busy && !build_busy && dbg_bound_req) begin
            q_issue_valid = 1;
            q_issue_debug = 1;
            q_issue_row = dbg_bound_row;
            q_issue_state5 = dbg_bound_state5;
            q_issue_state4 = dbg_bound_state4;
            q_issue_base = dbg_bound_base;
            q_issue_inc = dbg_bound_incumbent;
        end
    end

    // Candidate issue generation: one dedicated lane per context.
    always @* begin
        s_req_valid = 0;
        s_req_debug = 0;
        for (c = 0; c < 4; c = c + 1) begin
            s_req_states[c] = cstates[c][cdepth[c]];
            s_req_info[c] = cinfo[c][cdepth[c]];
            s_req_mask[c] = cmask[c][cdepth[c]];

            leaf_states[c] = cstates[c][cdepth[c]] ^
                packed_row(ccursor[c][cdepth[c]][5:0]);
            leaf_info[c] = cinfo[c][cdepth[c]] +
                info_cost[ccursor[c][cdepth[c]][5:0]];
            leaf_mask[c] = cmask[c][cdepth[c]] |
                (64'd1 << ccursor[c][cdepth[c]][5:0]);

            if (decode_busy && cphase[c] == CP_EMIT) begin
                s_req_valid[c] = 1;
            end else if (decode_busy && cphase[c] == CP_LEAF &&
                         ccursor[c][cdepth[c]] < K) begin
                s_req_valid[c] = 1;
                s_req_states[c] = leaf_states[c];
                s_req_info[c] = leaf_info[c];
                s_req_mask[c] = leaf_mask[c];
            end
        end

        if (!decode_busy && !build_busy && dbg_score_req) begin
            s_req_valid[0] = 1;
            s_req_debug[0] = 1;
            s_req_states[0] = dbg_score_states;
            s_req_info[0] = dbg_score_info;
            s_req_mask[0] = dbg_score_mask;
        end
    end

    // Deterministic response reduction and canonical numeric-mask tie rule.
    reg [2:0] score_rsp_count;
    reg [13:0] score_rsp_min;
    reg [63:0] score_rsp_min_mask;
    reg [2:0] score_rsp_min_count;
    reg [2:0] score_issue_count;
    reg score_issue_any;

    always @* begin
        score_rsp_count = 0;
        score_rsp_min = INF14;
        score_rsp_min_mask = {64{1'b1}};
        score_rsp_min_count = 0;
        score_issue_count = 0;
        score_issue_any = 0;

        for (c = 0; c < 4; c = c + 1) begin
            if (s_req_valid[c] && !s_req_debug[c]) begin
                score_issue_count = score_issue_count + 1;
                score_issue_any = 1;
            end
            if (s_rsp_valid[c] && !s_rsp_debug[c]) begin
                score_rsp_count = score_rsp_count + 1;
                if (s_rsp_metric[c] < score_rsp_min) begin
                    score_rsp_min = s_rsp_metric[c];
                    score_rsp_min_mask = s_rsp_mask[c];
                    score_rsp_min_count = 1;
                end else if (s_rsp_metric[c] == score_rsp_min) begin
                    score_rsp_min_count = score_rsp_min_count + 1;
                    if (s_rsp_mask[c] < score_rsp_min_mask)
                        score_rsp_min_mask = s_rsp_mask[c];
                end
            end
        end
    end

    wire q_pipe_busy = qmem_valid || qv1 || qv2 || qv3 || q_rsp_valid;
    wire s_pipe_busy = (|sv1) || (|sv2) || (|sv3) || (|s_rsp_valid);
    reg all_contexts_idle;
    always @* begin
        all_contexts_idle = 1;
        for (c = 0; c < 4; c = c + 1)
            if (cphase[c] != CP_IDLE) all_contexts_idle = 0;
    end

    always @(posedge clk) begin
        if (rst) begin
            decode_busy <= 0;
            decode_done <= 0;
            best_metric <= INF14;
            best_tep <= 0;
            best_tied <= 0;
            decode_cycles <= 0;
            scored_teps <= 0;
            bound_rows <= 0;
            score_issue_slots <= 0;
            score_active_cycles <= 0;
            task_count <= 0;
            task_rptr <= 0;
            seed_rank <= 0;
            seeding <= 0;
            rr_ctx <= 0;
            for (c = 0; c < 4; c = c + 1) begin
                cphase[c] <= CP_IDLE;
                cdepth[c] <= 0;
                calloc[c] <= 1;
                for (d = 0; d < 5; d = d + 1) begin
                    cstates[c][d] <= 0;
                    cinfo[c][d] <= 0;
                    cmask[c][d] <= 0;
                    ccursor[c][d] <= 0;
                    csuffix[c][d] <= 0;
                end
            end
        end else begin
            decode_done <= 0;

            if (decode_start && tables_ready && !decode_busy) begin
                decode_busy <= 1;
                decode_cycles <= 0;
                best_metric <= root_metric;
                best_tep <= 0;
                best_tied <= 0;
                scored_teps <= 1;
                bound_rows <= 0;
                score_issue_slots <= 0;
                score_active_cycles <= 0;
                task_count <= 0;
                task_rptr <= 0;
                seed_rank <= 0;
                seeding <= 1;
                rr_ctx <= 0;
                for (c = 0; c < 4; c = c + 1) begin
                    cphase[c] <= CP_IDLE;
                    cdepth[c] <= 0;
                    calloc[c] <= 1;
                end
            end else if (decode_busy) begin
                decode_cycles <= decode_cycles + 1;
                score_issue_slots <= score_issue_slots + score_issue_count;
                if (score_issue_any)
                    score_active_cycles <= score_active_cycles + 1;

                // Build 64 dynamic depth-1 prefix tasks. Heavy early prefixes
                // enter first; any idle context later steals the next task.
                if (seeding) begin
                    task_states[seed_rank] <=
                        cfg_base_states ^ packed_row(seed_rank);
                    task_info[seed_rank] <= info_cost[seed_rank];
                    task_mask[seed_rank] <= 64'd1 << seed_rank;
                    task_suffix[seed_rank] <= seed_rank + 1;
                    if (seed_rank == K - 1) begin
                        task_count <= K;
                        task_rptr <= 0;
                        seeding <= 0;
                    end else seed_rank <= seed_rank + 1;
                end else if (idle_found && task_count != 0) begin
                    cdepth[idle_ctx] <= 1;
                    cstates[idle_ctx][1] <= task_states[task_rptr];
                    cinfo[idle_ctx][1] <= task_info[task_rptr];
                    cmask[idle_ctx][1] <= task_mask[task_rptr];
                    csuffix[idle_ctx][1] <= task_suffix[task_rptr];
                    ccursor[idle_ctx][1] <= task_suffix[task_rptr];
                    calloc[idle_ctx] <= 1;
                    cphase[idle_ctx] <= CP_EMIT;
                    task_rptr <= task_rptr + 1;
                    task_count <= task_count - 1;
                end

                // Continuous scorer reduction. Equal metric candidates are
                // tracked and the numerically smallest TEP mask is canonical.
                if (score_rsp_count != 0) begin
                    scored_teps <= scored_teps + score_rsp_count;
                    if (score_rsp_min < best_metric) begin
                        best_metric <= score_rsp_min;
                        best_tep <= score_rsp_min_mask;
                        best_tied <= (score_rsp_min_count > 1);
                    end else if (score_rsp_min == best_metric) begin
                        if (score_rsp_min_mask != best_tep)
                            best_tied <= 1;
                        if (score_rsp_min_mask < best_tep)
                            best_tep <= score_rsp_min_mask;
                    end
                end

                // Score requests are fire-and-forget: contexts advance on the
                // request cycle and never wait for scorer response latency.
                for (c = 0; c < 4; c = c + 1) begin
                    if (cphase[c] == CP_EMIT) begin
                        if (cdepth[c] >= O ||
                            csuffix[c][cdepth[c]] >= K)
                            cphase[c] <= CP_BACK;
                        else begin
                            calloc[c] <= 1;
                            cphase[c] <= CP_SCAN;
                        end
                    end else if (cphase[c] == CP_LEAF) begin
                        if (ccursor[c][cdepth[c]] < K)
                            ccursor[c][cdepth[c]] <=
                                ccursor[c][cdepth[c]] + 1;
                        else cphase[c] <= CP_BACK;
                    end
                end

                // Invalid allocation rows are skipped locally without using
                // the shared bound pipe.
                for (c = 0; c < 4; c = c + 1) begin
                    if (cphase[c] == CP_SCAN &&
                        !(grant_found && grant_ctx == c &&
                          grant_valid_alloc)) begin
                        if (alloc_weight(calloc[c]) > O - cdepth[c] ||
                            calloc[c] == 0 ||
                            (csuffix[c][cdepth[c]] < SPLIT &&
                             (alloc_left(calloc[c]) >
                                SPLIT - csuffix[c][cdepth[c]] ||
                              alloc_weight(calloc[c]) -
                                alloc_left(calloc[c]) > K - SPLIT)) ||
                            (csuffix[c][cdepth[c]] >= SPLIT &&
                             (alloc_left(calloc[c]) != 0 ||
                              alloc_weight(calloc[c]) >
                                K - csuffix[c][cdepth[c]]))) begin
                            if (calloc[c] == 14)
                                cphase[c] <= CP_BACK;
                            else calloc[c] <= calloc[c] + 1;
                        end
                    end
                end

                // One bound request per cycle, round-robin across contexts.
                if (q_issue_valid && !q_issue_debug) begin
                    cphase[grant_ctx] <= CP_BOUND_WAIT;
                    rr_ctx <= grant_ctx + 1;
                    bound_rows <= bound_rows + 1;
                end

                if (q_rsp_valid && !q_rsp_debug &&
                    cphase[q_rsp_ctx] == CP_BOUND_WAIT) begin
                    if (q_rsp_may_improve) begin
                        if (cdepth[q_rsp_ctx] == O - 1)
                            cphase[q_rsp_ctx] <= CP_LEAF;
                        else
                            cphase[q_rsp_ctx] <= CP_PICK;
                    end else if (calloc[q_rsp_ctx] == 14) begin
                        cphase[q_rsp_ctx] <= CP_BACK;
                    end else begin
                        calloc[q_rsp_ctx] <= calloc[q_rsp_ctx] + 1;
                        cphase[q_rsp_ctx] <= CP_SCAN;
                    end
                end

                // Non-leaf DFS child generation and backtracking.
                for (c = 0; c < 4; c = c + 1) begin
                    if (cphase[c] == CP_PICK) begin
                        if (ccursor[c][cdepth[c]] >= K) begin
                            cphase[c] <= CP_BACK;
                        end else begin
                            root_state_tmp = cstates[c][cdepth[c]] ^
                                packed_row(ccursor[c][cdepth[c]][5:0]);
                            cstates[c][cdepth[c] + 1] <= root_state_tmp;
                            cinfo[c][cdepth[c] + 1] <=
                                cinfo[c][cdepth[c]] +
                                info_cost[ccursor[c][cdepth[c]][5:0]];
                            cmask[c][cdepth[c] + 1] <=
                                cmask[c][cdepth[c]] |
                                (64'd1 << ccursor[c][cdepth[c]][5:0]);
                            csuffix[c][cdepth[c] + 1] <=
                                ccursor[c][cdepth[c]] + 1;
                            ccursor[c][cdepth[c] + 1] <=
                                ccursor[c][cdepth[c]] + 1;
                            ccursor[c][cdepth[c]] <=
                                ccursor[c][cdepth[c]] + 1;
                            cdepth[c] <= cdepth[c] + 1;
                            cphase[c] <= CP_EMIT;
                        end
                    end else if (cphase[c] == CP_BACK) begin
                        if (cdepth[c] <= 1) begin
                            cdepth[c] <= 0;
                            cphase[c] <= CP_IDLE;
                        end else begin
                            cdepth[c] <= cdepth[c] - 1;
                            cphase[c] <= CP_PICK;
                        end
                    end
                end

                // Exact completion waits for all requests and responses.
                if (!seeding && task_count == 0 && all_contexts_idle &&
                    !q_pipe_busy && !s_pipe_busy && !q_issue_valid &&
                    !(|s_req_valid)) begin
                    decode_busy <= 0;
                    decode_done <= 1;
                end
            end
        end
    end
