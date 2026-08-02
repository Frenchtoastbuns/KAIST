`timescale 1ns/1ps

// Production residual-order-two normal CAP certification core.
//
// The search scheduler scores every order-0, order-1 and order-2 candidate
// before certification. Consequently, production bound requests only need
// residual budgets one and two and begin at suffix rank two or later. This
// core therefore stores only F_g(s,w,u) for s=2..64 and w in {1,2}.
// Residual weight zero is evaluated directly as phi_g(u).
//
// Memory per parity group: 63 suffixes * 2 weights * 32 states = 4032 words.
// Pair tasks store only the two six-bit ranks; mask, information cost and
// parity state are reconstructed after the synchronous FIFO response.

module cap_r2_split_tdp_ram #(
    parameter integer DEPTH = 4032,
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

module cap_r2_split_rank_pair_ram #(
    parameter integer DEPTH = 2048,
    parameter integer ADDR_W = $clog2(DEPTH)
) (
    input  wire                 clk,
    input  wire                 we,
    input  wire [ADDR_W-1:0]    waddr,
    input  wire [11:0]          wdata,
    input  wire                 re,
    input  wire [ADDR_W-1:0]    raddr,
    output reg  [11:0]          rdata
);
    (* ram_style = "block" *) reg [11:0] mem [0:DEPTH-1];
    always @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
        if (re) rdata <= mem[raddr];
    end
endmodule

module cap_r2_split_normal_core #(
    parameter integer K = 64,
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
    output reg [2:0]  max_bound_waiters
);
    localparam [9:0] INF10 = 10'h3ff;
    localparam [METRIC_W-1:0] INF_METRIC = {METRIC_W{1'b1}};
    localparam integer R2_DEPTH = (K-2+1) * 2 * STATES;
    localparam integer R2_AW = $clog2(R2_DEPTH);
    localparam integer TASK_AW = $clog2(TASK_DEPTH);

    reg [4:0] row_effect [0:GROUPS-1][0:K-1];
    reg [9:0] phi_cost [0:GROUPS-1][0:STATES-1];
    reg [6:0] info_cost [0:K-1];
    reg [METRIC_W-1:0] info_prefix [0:K];
    reg [63:0] base_states_latched;
    reg [METRIC_W-1:0] seed_metric_latched;
    reg [63:0] seed_mask_latched;
    reg seed_tie_latched;

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

    function automatic [63:0] xor_row_states;
        input [63:0] packed_states;
        input [5:0] rank;
        integer g;
        reg [63:0] value;
        begin
            value = packed_states;
            for (g=0; g<12; g=g+1)
                value[g*5 +: 5] = value[g*5 +: 5] ^ row_effect[g][rank];
            value[63:60] = value[63:60] ^ row_effect[12][rank][3:0];
            xor_row_states = value;
        end
    endfunction

    function automatic [R2_AW-1:0] r2_addr;
        input [6:0] suffix;
        input [1:0] weight;
        input [4:0] state;
        begin
            r2_addr = ((((suffix - 2) * 2) + (weight - 1)) * STATES) + state;
        end
    endfunction

    function automatic integer popcount64;
        input [63:0] value;
        integer i;
        begin
            popcount64 = 0;
            for (i=0;i<64;i=i+1) popcount64 = popcount64 + value[i];
        end
    endfunction

    function automatic canonical_before;
        input [63:0] left;
        input [63:0] right;
        integer i;
        integer lw;
        integer rw;
        reg decided;
        begin
            lw = popcount64(left);
            rw = popcount64(right);
            if (lw < rw) canonical_before = 1'b1;
            else if (lw > rw) canonical_before = 1'b0;
            else begin
                canonical_before = 1'b0;
                decided = 1'b0;
                for (i=0;i<64;i=i+1) begin
                    if (!decided && left[i] != right[i]) begin
                        canonical_before = left[i];
                        decided = 1'b1;
                    end
                end
            end
        end
    endfunction

    wire [9:0] r2_a_q [0:GROUPS-1];
    wire [9:0] r2_b_q [0:GROUPS-1];
    reg r2_a_en [0:GROUPS-1];
    reg r2_a_we [0:GROUPS-1];
    reg [R2_AW-1:0] r2_a_address [0:GROUPS-1];
    reg [9:0] r2_a_wdata [0:GROUPS-1];
    reg r2_b_en [0:GROUPS-1];
    reg r2_b_we [0:GROUPS-1];
    reg [R2_AW-1:0] r2_b_address [0:GROUPS-1];
    reg [9:0] r2_b_wdata [0:GROUPS-1];

    genvar mg;
    generate
        for (mg=0; mg<GROUPS; mg=mg+1) begin: g_r2_mem
            cap_r2_split_tdp_ram #(.DEPTH(R2_DEPTH),.ADDR_W(R2_AW),.DATA_W(10)) mem (
                .clk(clk),
                .a_en(r2_a_en[mg]),.a_we(r2_a_we[mg]),
                .a_addr(r2_a_address[mg]),.a_wdata(r2_a_wdata[mg]),.a_rdata(r2_a_q[mg]),
                .b_en(r2_b_en[mg]),.b_we(r2_b_we[mg]),
                .b_addr(r2_b_address[mg]),.b_wdata(r2_b_wdata[mg]),.b_rdata(r2_b_q[mg])
            );
        end
    endgenerate

    localparam [2:0] B_IDLE=0, B_INIT=1, B_READ=2, B_WRITE=3, B_PREFIX=4;
    reg [2:0] b_state;
    reg [6:0] b_suffix;
    reg [1:0] b_weight;
    reg [4:0] b_value_state;
    reg [6:0] b_prefix_index;

    // Split-phase query pipeline.  A request consumes both RAM ports in one
    // cycle: port A reads residual weight one and port B, when applicable,
    // reads residual weight two.  Tags and the captured incumbent travel with
    // the synchronous RAM response, allowing a new context request every
    // cycle while preserving conservative stale-threshold semantics.
    reg [1:0] q_rr;
    reg q_mem_valid;
    reg q_mem_has_w1;
    reg q_mem_has_w2;
    reg [1:0] q_mem_context;
    reg q_mem_depth3;
    reg [METRIC_W-1:0] q_mem_threshold;
    reg [METRIC_W-1:0] q_mem_info_w1;
    reg [METRIC_W-1:0] q_mem_info_w2;
    reg q_sum_valid;
    reg q_sum_has_w1;
    reg q_sum_has_w2;
    reg [1:0] q_sum_context;
    reg q_sum_depth3;
    reg [METRIC_W-1:0] q_sum_threshold;
    reg [METRIC_W+4:0] q_sum_w1;
    reg [METRIC_W+4:0] q_sum_w2;
    wire q_sum_pass =
        (q_sum_has_w1 && q_sum_w1<=q_sum_threshold) ||
        (q_sum_has_w2 && q_sum_w2<=q_sum_threshold);
    integer sumg;
    integer arb_index;
    reg [METRIC_W+4:0] query_total_w1;
    reg [METRIC_W+4:0] query_total_w2;
    reg [6:0] grant_suffix;
    reg [63:0] grant_states;
    reg [METRIC_W-1:0] grant_information;
    reg grant_depth3;
    reg grant_has_w1;
    reg grant_has_w2;

    reg score_in_valid [0:CONTEXTS-1];
    reg [63:0] score_in_mask [0:CONTEXTS-1];
    reg [63:0] score_in_states [0:CONTEXTS-1];
    reg [METRIC_W-1:0] score_in_information [0:CONTEXTS-1];
    reg score_v [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [63:0] score_m [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [METRIC_W-1:0] score_d [0:SCORE_LATENCY-1][0:CONTEXTS-1];
    reg [METRIC_W+4:0] parity_sum_comb [0:CONTEXTS-1];
    reg score_pipe_nonempty;
    integer sg;
    integer sl;
    integer sp;

    reg task_we;
    reg [TASK_AW-1:0] task_waddr;
    reg [11:0] task_wdata;
    reg task_re;
    reg [TASK_AW-1:0] task_raddr;
    wire [11:0] task_rdata;
    cap_r2_split_rank_pair_ram #(.DEPTH(TASK_DEPTH),.ADDR_W(TASK_AW)) task_mem (
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
    reg [63:0] tmp_states;
    reg [METRIC_W-1:0] tmp_info;
    reg [63:0] tmp_mask;
    reg [5:0] tmp_rank_i;
    reg [5:0] tmp_rank_j;

    integer cg;
    always @* begin
        for (cg=0; cg<GROUPS; cg=cg+1) begin
            r2_a_en[cg]=0; r2_a_we[cg]=0; r2_a_address[cg]=0; r2_a_wdata[cg]=0;
            r2_b_en[cg]=0; r2_b_we[cg]=0; r2_b_address[cg]=0; r2_b_wdata[cg]=0;
        end

        if (build_busy) begin
            for (cg=0; cg<GROUPS; cg=cg+1) begin
                if (b_state==B_INIT) begin
                    r2_a_en[cg]=1; r2_a_we[cg]=1;
                    r2_a_address[cg]=r2_addr(K,b_weight,b_value_state);
                    r2_a_wdata[cg]=INF10;
                end else if (b_state==B_READ) begin
                    r2_a_en[cg]=1;
                    r2_a_address[cg]=r2_addr(b_suffix+1,b_weight,b_value_state);
                    if (b_weight==2) begin
                        r2_b_en[cg]=1;
                        r2_b_address[cg]=r2_addr(b_suffix+1,1,
                            b_value_state ^ row_effect[cg][b_suffix]);
                    end
                end else if (b_state==B_WRITE) begin
                    r2_a_en[cg]=1; r2_a_we[cg]=1;
                    r2_a_address[cg]=r2_addr(b_suffix,b_weight,b_value_state);
                    if (b_weight==1)
                        r2_a_wdata[cg]=(r2_a_q[cg] <=
                            phi_cost[cg][b_value_state ^ row_effect[cg][b_suffix]]) ?
                            r2_a_q[cg] : phi_cost[cg][b_value_state ^ row_effect[cg][b_suffix]];
                    else
                        r2_a_wdata[cg]=(r2_a_q[cg] <= r2_b_q[cg]) ? r2_a_q[cg] : r2_b_q[cg];
                end
            end
        end else if (grant_valid) begin
            for (cg=0; cg<GROUPS; cg=cg+1) begin
                if (grant_has_w1) begin
                    r2_a_en[cg]=1;
                    r2_a_address[cg]=r2_addr(grant_suffix,1,
                        packed_group_state(grant_states,cg));
                end
                if (grant_has_w2) begin
                    r2_b_en[cg]=1;
                    r2_b_address[cg]=r2_addr(grant_suffix,2,
                        packed_group_state(grant_states,cg));
                end
            end
        end

        for (sl=0; sl<CONTEXTS; sl=sl+1) begin
            parity_sum_comb[sl]=score_in_information[sl];
            for (sg=0; sg<GROUPS; sg=sg+1)
                parity_sum_comb[sl]=parity_sum_comb[sl]+
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
        grant_suffix=0;
        grant_states=0;
        grant_information=0;
        grant_depth3=0;
        grant_has_w1=0;
        grant_has_w2=0;
        for (sl=0;sl<CONTEXTS;sl=sl+1) begin
            if (ctx_state[sl]!=C_IDLE) all_contexts_idle=0;
            if (ctx_state[sl]==C_BOUND2_REQ || ctx_state[sl]==C_BOUND3_REQ) begin
                waiter_count=waiter_count+1;
            end
        end
        // Round-robin selection prevents a busy low-index context from
        // monopolising the one-request-per-cycle query front end.
        for (sl=0;sl<CONTEXTS;sl=sl+1) begin
            arb_index=(q_rr+sl)%CONTEXTS;
            if (!grant_valid &&
                (ctx_state[arb_index]==C_BOUND2_REQ ||
                 ctx_state[arb_index]==C_BOUND3_REQ)) begin
                grant_valid=1;
                grant_ctx=arb_index[1:0];
            end
        end
        if (grant_valid) begin
            grant_depth3=ctx_state[grant_ctx]==C_BOUND3_REQ;
            grant_states=grant_depth3?ctx_d3_states[grant_ctx]:ctx_states[grant_ctx];
            grant_suffix=grant_depth3?ctx_leaf[grant_ctx]:ctx_next[grant_ctx];
            grant_information=grant_depth3?ctx_d3_info[grant_ctx]:ctx_info[grant_ctx];
            grant_has_w1=grant_suffix<K;
            grant_has_w2=!grant_depth3 && (grant_suffix+2<=K);
        end
    end

    integer k;
    integer lane;
    integer stage;
    reg [METRIC_W-1:0] reduce_metric;
    reg [63:0] reduce_mask;
    reg reduce_tie;
    reg [METRIC_W-1:0] candidate_metric;
    reg [63:0] candidate_mask;

    always @(posedge clk) begin
        if (rst) begin
            build_busy<=0; build_done<=0; build_cycles<=0; b_state<=B_IDLE;
            b_suffix<=K; b_weight<=1; b_value_state<=0; b_prefix_index<=0;
            decode_busy<=0; decode_done<=0; decode_cycles<=0;
            best_metric<=INF_METRIC; best_tep<=0; best_tied<=0;
            score_issues<=0; bound_row_cycles<=0; context_wait_cycles<=0;
            max_task_occupancy<=0; max_bound_waiters<=0;
            q_rr<=0; q_mem_valid<=0; q_sum_valid<=0;
            task_we<=0; task_re<=0; task_count<=0; task_head<=0;
            pop_phase<=0; pop_context<=0;
            d_state<=D_IDLE; gen_i<=0; gen_j<=1;
            base_states_latched<=0; seed_metric_latched<=0;
            seed_mask_latched<=0; seed_tie_latched<=0;
            // Blocking assignment is committed source so simulation and
            // synthesis consume identical RTL.
            for (k=0;k<=K;k=k+1) info_prefix[k]=0;
            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                ctx_state[lane]<=C_IDLE;
                score_in_valid[lane]<=0;
                score_in_mask[lane]<=0;
                score_in_states[lane]<=0;
                score_in_information[lane]<=0;
                for (stage=0;stage<SCORE_LATENCY;stage=stage+1) begin
                    score_v[stage][lane]<=0;
                    score_m[stage][lane]<=0;
                    score_d[stage][lane]<=0;
                end
            end
        end else begin
            build_done<=0; decode_done<=0;
            task_we<=0; task_re<=0;
            for (lane=0;lane<CONTEXTS;lane=lane+1) score_in_valid[lane]<=0;

            if (build_start && !build_busy && !decode_busy) begin
                build_busy<=1; build_cycles<=0;
                base_states_latched<=cfg_base_states;
                seed_metric_latched<=cfg_seed_best_metric;
                seed_mask_latched<=cfg_seed_mask;
                seed_tie_latched<=cfg_seed_tie;
                b_state<=B_INIT; b_suffix<=K; b_weight<=1;
                b_value_state<=0; b_prefix_index<=0;
            end else if (build_busy) begin
                build_cycles<=build_cycles+1;
                case (b_state)
                    B_INIT: begin
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==2) begin
                                b_weight<=1; b_suffix<=K-1; b_state<=B_READ;
                            end else b_weight<=2;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_READ: b_state<=B_WRITE;
                    B_WRITE: begin
                        b_state<=B_READ;
                        if (b_value_state==STATES-1) begin
                            b_value_state<=0;
                            if (b_weight==2) begin
                                b_weight<=1;
                                if (b_suffix==2) begin
                                    b_state<=B_PREFIX; b_prefix_index<=0;
                                end else b_suffix<=b_suffix-1;
                            end else b_weight<=2;
                        end else b_value_state<=b_value_state+1;
                    end
                    B_PREFIX: begin
                        if (b_prefix_index==0) begin
                            info_prefix[0]<=0;
                            b_prefix_index<=1;
                        end else begin
                            info_prefix[b_prefix_index]<=
                                info_prefix[b_prefix_index-1]+info_cost[b_prefix_index-1];
                            if (b_prefix_index==K) begin
                                build_busy<=0; build_done<=1; b_state<=B_IDLE;
                                b_prefix_index<=0;
                            end else b_prefix_index<=b_prefix_index+1;
                        end
                    end
                    default: b_state<=B_IDLE;
                endcase
            end

            // Registered adder stage for both residual rows.  The RAM outputs
            // observed here correspond to q_mem_* from the preceding cycle.
            q_sum_valid<=q_mem_valid;
            if (q_mem_valid) begin
                query_total_w1=q_mem_info_w1;
                query_total_w2=q_mem_info_w2;
                for (sumg=0;sumg<GROUPS;sumg=sumg+1) begin
                    query_total_w1=query_total_w1+r2_a_q[sumg];
                    query_total_w2=query_total_w2+r2_b_q[sumg];
                end
                q_sum_w1<=query_total_w1;
                q_sum_w2<=query_total_w2;
                q_sum_has_w1<=q_mem_has_w1;
                q_sum_has_w2<=q_mem_has_w2;
                q_sum_context<=q_mem_context;
                q_sum_depth3<=q_mem_depth3;
                q_sum_threshold<=q_mem_threshold;
            end

            // One tagged sum becomes available every cycle.  The context
            // consumes it directly below, avoiding a redundant response
            // register.  A stale captured U may only admit extra work; it
            // cannot prune a valid winner.
            if (q_sum_valid) begin
                bound_row_cycles<=bound_row_cycles+q_sum_has_w1+q_sum_has_w2;
            end

            // Capture request metadata at the same edge that launches the two
            // synchronous BRAM reads.
            q_mem_valid<=decode_busy && d_state==D_RUN && grant_valid;
            if (decode_busy && d_state==D_RUN && grant_valid) begin
                q_mem_has_w1<=grant_has_w1;
                q_mem_has_w2<=grant_has_w2;
                q_mem_context<=grant_ctx;
                q_mem_depth3<=grant_depth3;
                q_mem_threshold<=best_metric;
                if (grant_has_w1)
                    q_mem_info_w1<=grant_information+
                        (info_prefix[grant_suffix+1]-info_prefix[grant_suffix]);
                else
                    q_mem_info_w1<=0;
                if (grant_has_w2)
                    q_mem_info_w2<=grant_information+
                        (info_prefix[grant_suffix+2]-info_prefix[grant_suffix]);
                else
                    q_mem_info_w2<=0;
            end

            for (lane=0;lane<CONTEXTS;lane=lane+1) begin
                score_v[0][lane]<=score_in_valid[lane];
                score_m[0][lane]<=score_in_mask[lane];
                score_d[0][lane]<=parity_sum_comb[lane][METRIC_W-1:0];
                for (stage=1;stage<SCORE_LATENCY;stage=stage+1) begin
                    score_v[stage][lane]<=score_v[stage-1][lane];
                    score_m[stage][lane]<=score_m[stage-1][lane];
                    score_d[stage][lane]<=score_d[stage-1][lane];
                end
            end

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

            if (decode_start && !decode_busy && !build_busy &&
                !q_mem_valid && !q_sum_valid) begin
                decode_busy<=1; decode_cycles<=0; d_state<=D_SINGLE;
                best_metric<=seed_metric_latched; best_tep<=seed_mask_latched;
                best_tied<=seed_tie_latched;
                score_issues<=0; bound_row_cycles<=0; context_wait_cycles<=0;
                max_task_occupancy<=0; max_bound_waiters<=0;
                task_count<=0; task_head<=0; pop_phase<=0;
                gen_i<=0; gen_j<=1;
                q_rr<=0; q_mem_valid<=0; q_sum_valid<=0;
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
                        task_wdata<={gen_i[5:0],gen_j[5:0]};
                        task_count<=task_count+1;
                        if (gen_j==K-1) begin
                            if (gen_i==K-2) begin pop_phase<=0; d_state<=D_RUN; end
                            else begin gen_i<=gen_i+1; gen_j<=gen_i+2; end
                        end else gen_j<=gen_j+1;
                    end
                    D_RUN: begin
                        if (pop_phase==0 && task_head<task_count) begin
                            if (ctx_state[0]==C_IDLE) begin
                                task_re<=1; task_raddr<=task_head[TASK_AW-1:0];
                                task_head<=task_head+1; pop_phase<=1; pop_context<=0;
                            end else if (ctx_state[1]==C_IDLE) begin
                                task_re<=1; task_raddr<=task_head[TASK_AW-1:0];
                                task_head<=task_head+1; pop_phase<=1; pop_context<=1;
                            end else if (ctx_state[2]==C_IDLE) begin
                                task_re<=1; task_raddr<=task_head[TASK_AW-1:0];
                                task_head<=task_head+1; pop_phase<=1; pop_context<=2;
                            end else if (ctx_state[3]==C_IDLE) begin
                                task_re<=1; task_raddr<=task_head[TASK_AW-1:0];
                                task_head<=task_head+1; pop_phase<=1; pop_context<=3;
                            end
                        end else if (pop_phase==1) begin
                            pop_phase<=2;
                        end else if (pop_phase==2) begin
                            tmp_rank_i=task_rdata[11:6];
                            tmp_rank_j=task_rdata[5:0];
                            tmp_mask=(64'b1<<tmp_rank_i)|(64'b1<<tmp_rank_j);
                            tmp_states=xor_row_states(xor_row_states(base_states_latched,tmp_rank_i),tmp_rank_j);
                            tmp_info=info_cost[tmp_rank_i]+info_cost[tmp_rank_j];
                            ctx_mask[pop_context]<=tmp_mask;
                            ctx_next[pop_context]<=tmp_rank_j+7'd1;
                            ctx_info[pop_context]<=tmp_info;
                            ctx_states[pop_context]<=tmp_states;
                            if (tmp_rank_j+1>=K) ctx_state[pop_context]<=C_IDLE;
                            else ctx_state[pop_context]<=C_BOUND2_REQ;
                            pop_phase<=0;
                        end

                        if (grant_valid) begin
                            q_rr<=grant_ctx+1'b1;
                            if (grant_depth3)
                                ctx_state[grant_ctx]<=C_BOUND3_WAIT;
                            else ctx_state[grant_ctx]<=C_BOUND2_WAIT;
                        end

                        if (q_sum_valid) begin
                            if (q_sum_depth3) begin
                                if (q_sum_pass) ctx_state[q_sum_context]<=C_LEAF;
                                else ctx_state[q_sum_context]<=C_DEPTH3;
                            end else begin
                                if (q_sum_pass) begin
                                    ctx_r3[q_sum_context]<=ctx_next[q_sum_context];
                                    ctx_state[q_sum_context]<=C_DEPTH3;
                                end else ctx_state[q_sum_context]<=C_IDLE;
                            end
                        end

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

                        if (task_head==task_count && pop_phase==0 && all_contexts_idle &&
                            !q_mem_valid && !q_sum_valid)
                            d_state<=D_DRAIN;
                    end
                    D_DRAIN: begin
                        if (!score_pipe_nonempty) begin
                            decode_busy<=0; decode_done<=1; d_state<=D_IDLE;
                        end
                    end
                    default: d_state<=D_IDLE;
                endcase

                score_issues<=score_issues+
                    score_in_valid[0]+score_in_valid[1]+
                    score_in_valid[2]+score_in_valid[3];
            end
        end
    end
endmodule

module cap_r2_split_normal_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire [3:0] cfg_row_group,input wire [5:0] cfg_row_rank,input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we,input wire [3:0] cfg_phi_group,input wire [4:0] cfg_phi_state,input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we,input wire [5:0] cfg_info_rank,input wire [6:0] cfg_info_cost,
    input wire [63:0] cfg_base_states,input wire [13:0] cfg_seed_best_metric,input wire [63:0] cfg_seed_mask,input wire cfg_seed_tie,
    input wire build_start,output wire build_busy,output wire build_done,output wire [31:0] build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire [13:0] best_metric,output wire [63:0] best_tep,output wire best_tied,
    output wire [31:0] decode_cycles,output wire [31:0] score_issues,output wire [31:0] bound_row_cycles,output wire [31:0] context_wait_cycles,
    output wire [11:0] max_task_occupancy,output wire [2:0] max_bound_waiters
);
    cap_r2_split_normal_core core(.*);
endmodule
