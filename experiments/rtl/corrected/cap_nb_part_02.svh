        B_FINISH=11, B_PREFIX=12;
    reg [3:0] b_state;
    reg [6:0] b_suffix;
    reg [2:0] b_weight;
    reg [4:0] b_value_state;
    reg [3:0] b_pair;
    reg [6:0] b_prefix_rank;
    integer bg;
    reg [9:0] build_min;

    // ------------------------------------------------------------------
    // Bound query engine row list and owner bookkeeping.
    // ------------------------------------------------------------------
    reg q_active;
    reg q_force_full;
    reg q_debug_owner;
    reg [1:0] q_context;
    reg q_context_depth3;
    reg [5:0] q_suffix;
    reg [2:0] q_budget;
    reg [METRIC_W-1:0] q_information;
    reg [63:0] q_states;
    reg [METRIC_W-1:0] q_threshold;
    reg [3:0] q_count;
    reg [3:0] q_issue_index;
    reg [3:0] q_pair_rows [0:14];
    reg [2:0] q_weight_rows [0:14];
    reg [METRIC_W-1:0] q_info_rows [0:14];
    reg q_eval_valid;
    reg q_eval_last;
    reg [METRIC_W-1:0] q_eval_information;
    reg [METRIC_W-1:0] q_minimum_seen;
    reg q_result_valid;
    reg q_result_pass;
    reg [1:0] q_result_context;
    reg q_result_depth3;
    reg q_issue_now;
    reg q_issue_compact;
    reg [3:0] q_issue_pair;
    reg [2:0] q_issue_weight;
    reg [METRIC_W-1:0] q_issue_information;
    integer qi;
    integer qj;
    integer qfill;
    reg [METRIC_W+4:0] q_sum;

    // ------------------------------------------------------------------
    // Four five-cycle scorer lanes.
    // ------------------------------------------------------------------
    reg score_in_valid [0:CONTEXTS-1];
    reg [63:0] score_in_mask [0:CONTEXTS-1];
    reg [63:0] score_in_states [0:CONTEXTS-1];
    reg [METRIC_W-1:0] score_in_information [0:CONTEXTS-1];
    reg score_v [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [63:0] score_m [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [METRIC_W-1:0] score_d [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [METRIC_W+4:0] parity_sum_comb [0:CONTEXTS-1];
    integer sg;
    integer sl;
    integer sp;
    reg score_pipe_nonempty;

    // ------------------------------------------------------------------
    // Shared depth-2 prefix task FIFO.  Every pair i<j is one independently
    // stealable task; singletons are scored during generation.
    // ------------------------------------------------------------------
    reg task_we;
    reg [TASK_AW-1:0] task_waddr;
    reg [TASK_W-1:0] task_wdata;
    reg task_re;
    reg [TASK_AW-1:0] task_raddr;
    wire [TASK_W-1:0] task_rdata;
    cap_task_ram #(.DEPTH(TASK_DEPTH),.ADDR_W(TASK_AW),.DATA_W(TASK_W)) task_mem (
        .clk(clk),.we(task_we),.waddr(task_waddr),.wdata(task_wdata),
        .re(task_re),.raddr(task_raddr),.rdata(task_rdata)
    );
    reg [11:0] task_count;
    reg [11:0] task_head;
    reg [1:0] pop_phase;
    reg [1:0] pop_context;

    localparam [3:0]
        C_IDLE=0, C_BOUND2_REQ=1, C_BOUND2_WAIT=2,
        C_DEPTH3=3, C_BOUND3_REQ=4, C_BOUND3_WAIT=5,
        C_LEAF=6;
    reg [3:0] ctx_state [0:CONTEXTS-1];
    reg [63:0] ctx_mask [0:CONTEXTS-1];
    reg [63:0] ctx_states [0:CONTEXTS-1];
    reg [METRIC_W-1:0] ctx_info [0:CONTEXTS-1];
    reg [6:0] ctx_next [0:CONTEXTS-1];
    reg [6:0] ctx_r3 [0:CONTEXTS-1];
    reg [63:0] ctx_d3_mask [0:CONTEXTS-1];
    reg [63:0] ctx_d3_states [0:CONTEXTS-1];
    reg [METRIC_W-1:0] ctx_d3_info [0:CONTEXTS-1];
    reg [6:0] ctx_leaf [0:CONTEXTS-1];

    localparam [2:0] D_IDLE=0, D_SINGLE=1, D_PAIR=2, D_RUN=3, D_DRAIN=4;
    reg [2:0] d_state;
    reg [6:0] gen_i;
    reg [6:0] gen_j;
    reg [1:0] grant_ctx;
    reg grant_valid;
    reg [2:0] waiter_count;
    reg all_contexts_idle;
    reg steal_valid;
    reg [1:0] steal_ctx;
    reg [2:0] issue_count_comb;
    reg [63:0] tmp_states;
    reg [METRIC_W-1:0] tmp_info;
    reg [63:0] tmp_mask;

    // Debug read pipeline.
    reg dbg_re_d;
    reg dbg_sel_d;
    reg [3:0] dbg_group_d;

    // ------------------------------------------------------------------
    // Combinational memory-port arbitration, query issue, scorer inputs, and
    // scheduler actions.  Build has highest priority, then debug read/query.
    // ------------------------------------------------------------------
    integer cg;
    always @* begin
        for (cg=0; cg<GROUPS; cg=cg+1) begin
            normal_a_en[cg]=0; normal_a_we[cg]=0; normal_a_addr[cg]=0; normal_a_wdata[cg]=0;
            normal_b_en[cg]=0; normal_b_we[cg]=0; normal_b_addr[cg]=0; normal_b_wdata[cg]=0;
            right_a_en[cg]=0; right_a_we[cg]=0; right_a_addr[cg]=0; right_a_wdata[cg]=0;
            right_b_en[cg]=0; right_b_we[cg]=0; right_b_addr[cg]=0; right_b_wdata[cg]=0;
            compact_a_en[cg]=0; compact_a_we[cg]=0; compact_a_addr[cg]=0; compact_a_wdata[cg]=0;
            compact_b_en[cg]=0; compact_b_we[cg]=0; compact_b_addr[cg]=0; compact_b_wdata[cg]=0;
        end

        // Builder ports.
        if (build_busy) begin
            for (cg=0; cg<GROUPS; cg=cg+1) begin
                if (!COMPACT) begin
                    if (b_state==B_N_INIT) begin
                        normal_a_en[cg]=1; normal_a_we[cg]=1;
                        normal_a_addr[cg]=normal_addr(K,b_weight,b_value_state);
                        normal_a_wdata[cg]=(b_weight==0)?phi_cost[cg][b_value_state]:INF10;
                    end else if (b_state==B_N_READ) begin
                        normal_a_en[cg]=1;
                        normal_a_addr[cg]=normal_addr(b_suffix+1,b_weight,b_value_state);
                        if (b_weight!=0) begin
                            normal_b_en[cg]=1;
                            normal_b_addr[cg]=normal_addr(b_suffix+1,b_weight-1,
                                b_value_state ^ row_effect[cg][b_suffix]);
                        end
                    end else if (b_state==B_N_WRITE) begin
                        normal_a_en[cg]=1; normal_a_we[cg]=1;
                        normal_a_addr[cg]=normal_addr(b_suffix,b_weight,b_value_state);
                        normal_a_wdata[cg]=(b_weight==0 || normal_a_q[cg]<=normal_b_q[cg])?
                            normal_a_q[cg]:normal_b_q[cg];
                    end
                end else begin
                    if (b_state==B_R_INIT) begin
                        right_a_en[cg]=1; right_a_we[cg]=1;
                        right_a_addr[cg]=right_addr(K,b_weight,b_value_state);
                        right_a_wdata[cg]=(b_weight==0)?phi_cost[cg][b_value_state]:INF10;
                    end else if (b_state==B_R_READ) begin
                        right_a_en[cg]=1;
                        right_a_addr[cg]=right_addr(b_suffix+1,b_weight,b_value_state);
                        if (b_weight!=0) begin
                            right_b_en[cg]=1;
                            right_b_addr[cg]=right_addr(b_suffix+1,b_weight-1,
                                b_value_state ^ row_effect[cg][b_suffix]);
                        end
                    end else if (b_state==B_R_WRITE) begin
                        right_a_en[cg]=1; right_a_we[cg]=1;
                        right_a_addr[cg]=right_addr(b_suffix,b_weight,b_value_state);
                        right_a_wdata[cg]=(b_weight==0 || right_a_q[cg]<=right_b_q[cg])?
                            right_a_q[cg]:right_b_q[cg];
                    end else if (b_state==B_EXP_READ) begin
                        if (pair_left(b_pair)==0) begin
                            right_a_en[cg]=1;
                            right_a_addr[cg]=right_addr(SPLIT,pair_right(b_pair),b_value_state);
                        end
                    end else if (b_state==B_EXP_WRITE) begin
                        compact_a_en[cg]=1; compact_a_we[cg]=1;
                        compact_a_addr[cg]=compact_addr(SPLIT,b_pair,b_value_state);
                        compact_a_wdata[cg]=(pair_left(b_pair)==0)?right_a_q[cg]:INF10;
                    end else if (b_state==B_L_READ) begin
                        compact_a_en[cg]=1;
                        compact_a_addr[cg]=compact_addr(b_suffix+1,b_pair,b_value_state);
                        if (pair_left(b_pair)!=0) begin
                            compact_b_en[cg]=1;
                            compact_b_addr[cg]=compact_addr(b_suffix+1,
                                pair_index(pair_left(b_pair)-1,pair_right(b_pair)),
                                b_value_state ^ row_effect[cg][b_suffix]);
                        end
                    end else if (b_state==B_L_WRITE) begin