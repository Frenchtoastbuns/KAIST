// Full threshold-compiled CAP accelerator core for eBCH(128,64), OSD-4.
//
// Scope:
//  * frame-local parity-cost and row-effect loading;
//  * frame-local right-suffix normal PDB construction;
//  * allocation-compiled two-block PDB construction;
//  * compact row compaction including right-suffix normal rows;
//  * four partitioned DFS contexts covering every non-empty OSD-4 TEP;
//  * one threshold-bound issue per cycle;
//  * four exact candidate scorers and continuous incumbent reduction.
//
// Inputs are already reliability ordered and systematicized.  Gaussian
// elimination/reliability sorting and final codeword materialization are outside
// this accelerator core.  The core returns the exact best TEP mask and metric.

module cap_sp_ram #(
    parameter integer DEPTH = 16352,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 10
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

module cap_tdp_ram #(
    parameter integer DEPTH = 480,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 10
) (
    input wire clk,
    input wire a_we, input wire [ADDR_W-1:0] a_addr,
    input wire [DATA_W-1:0] a_wdata, output reg [DATA_W-1:0] a_rdata,
    input wire b_we, input wire [ADDR_W-1:0] b_addr,
    input wire [DATA_W-1:0] b_wdata, output reg [DATA_W-1:0] b_rdata
);
    (* ram_style = "block" *) reg [DATA_W-1:0] mem [0:DEPTH-1];
    always @(posedge clk) begin
        if (a_we) mem[a_addr] <= a_wdata;
        else      a_rdata <= mem[a_addr];
        if (b_we) mem[b_addr] <= b_wdata;
        else      b_rdata <= mem[b_addr];
    end
endmodule

module cap_scratch_pair #(
    parameter integer DEPTH = 480,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 10
) (
    input wire clk,
    input wire old_sel,
    input wire init_we,
    input wire [ADDR_W-1:0] init_addr,
    input wire [DATA_W-1:0] init_data,
    input wire rd_en,
    input wire [ADDR_W-1:0] rd_addr_a,
    input wire [ADDR_W-1:0] rd_addr_b,
    output wire [DATA_W-1:0] rd_data_a,
    output wire [DATA_W-1:0] rd_data_b,
    input wire new_we,
    input wire [ADDR_W-1:0] new_addr,
    input wire [DATA_W-1:0] new_data
);
    wire [DATA_W-1:0] b0_a_r, b0_b_r, b1_a_r, b1_b_r;
    wire b0_a_we = init_we || (old_sel && new_we);
    wire b1_a_we = (!old_sel) && new_we;
    wire [ADDR_W-1:0] b0_a_addr = init_we ? init_addr :
        (old_sel ? new_addr : rd_addr_a);
    wire [ADDR_W-1:0] b1_a_addr = old_sel ? rd_addr_a : new_addr;
    wire [DATA_W-1:0] b0_a_data = init_we ? init_data : new_data;
    wire [DATA_W-1:0] b1_a_data = new_data;

    cap_tdp_ram #(.DEPTH(DEPTH), .ADDR_W(ADDR_W), .DATA_W(DATA_W)) bank0 (
        .clk(clk),
        .a_we(b0_a_we), .a_addr(b0_a_addr), .a_wdata(b0_a_data), .a_rdata(b0_a_r),
        .b_we(1'b0), .b_addr(rd_addr_b), .b_wdata({DATA_W{1'b0}}), .b_rdata(b0_b_r)
    );
    cap_tdp_ram #(.DEPTH(DEPTH), .ADDR_W(ADDR_W), .DATA_W(DATA_W)) bank1 (
        .clk(clk),
        .a_we(b1_a_we), .a_addr(b1_a_addr), .a_wdata(b1_a_data), .a_rdata(b1_a_r),
        .b_we(1'b0), .b_addr(rd_addr_b), .b_wdata({DATA_W{1'b0}}), .b_rdata(b1_b_r)
    );

    assign rd_data_a = old_sel ? b1_a_r : b0_a_r;
    assign rd_data_b = old_sel ? b1_b_r : b0_b_r;
endmodule

module cap_full_compiled_decoder #(
    parameter integer SPLIT = 45,
    parameter integer COMPACT_ROWS = (SPLIT == 45) ? 445 : 475,
    parameter integer ROWS = (SPLIT == 45) ? 511 : 529,
    parameter integer ROW_W = $clog2(ROWS),
    parameter integer GROUPS5 = 12,
    parameter integer CONTEXTS = 4,
    parameter integer K = 64,
    parameter integer O = 4,
    parameter integer SCRATCH_DEPTH = 480,
    parameter integer SCRATCH_AW = $clog2(SCRATCH_DEPTH),
    parameter integer DEPTH5 = ROWS * 32,
    parameter integer DEPTH4 = ROWS * 16,
    parameter integer ADDR5_W = $clog2(DEPTH5),
    parameter integer ADDR4_W = $clog2(DEPTH4)
) (
    input wire clk,
    input wire rst,

    // Frame configuration after reliability ordering/systematicization.
    input wire cfg_row_we,
    input wire [3:0] cfg_row_group,
    input wire [5:0] cfg_row_rank,
    input wire [4:0] cfg_row_effect,
    input wire cfg_phi_we,
    input wire [3:0] cfg_phi_group,
    input wire [4:0] cfg_phi_state,
    input wire [9:0] cfg_phi_cost,
    input wire cfg_info_we,
    input wire [5:0] cfg_info_rank,
    input wire [6:0] cfg_info_cost,
    input wire [63:0] cfg_base_states,

    input wire build_start,
    output reg build_busy,
    output reg build_done,
    output reg [31:0] build_cycles,

    input wire decode_start,
    output reg decode_busy,
    output reg decode_done,
    output reg [13:0] best_metric,
    output reg [63:0] best_tep,
    output reg [31:0] decode_cycles,
    output reg [31:0] scored_teps,
    output reg [31:0] bound_rows
);
    localparam [9:0] INF10 = 10'h3ff;
    localparam [13:0] INF14 = 14'h3fff;

    reg [4:0] row_effect [0:12][0:63];
    reg [9:0] phi_cost [0:12][0:31];
    reg [6:0] info_cost [0:63];
    integer gi, si;

    always @(posedge clk) begin
        if (cfg_row_we)
            row_effect[cfg_row_group][cfg_row_rank] <= cfg_row_effect;
        if (cfg_phi_we)
            phi_cost[cfg_phi_group][cfg_phi_state] <= cfg_phi_cost;
        if (cfg_info_we)
            info_cost[cfg_info_rank] <= cfg_info_cost;
    end

    function automatic [3:0] alloc_left(input [3:0] idx);
        begin
            case (idx)
                0,2,5,9: alloc_left=0;
                1,3,6,10: alloc_left=1;
                4,7,11: alloc_left=2;
                8,12: alloc_left=3;
                default: alloc_left=4;
            endcase
        end
    endfunction
    function automatic [2:0] alloc_weight(input [3:0] idx);
        begin
            if (idx < 2) alloc_weight=1;
            else if (idx < 5) alloc_weight=2;
            else if (idx < 9) alloc_weight=3;
            else alloc_weight=4;
        end
    endfunction
    function automatic [3:0] alloc_index(input [2:0] left, input [2:0] right);
        reg [2:0] weight;
        begin
            weight=left+right;
            case(weight)
                0: alloc_index=0;
                1: alloc_index=left;
                2: alloc_index=2+left;
                3: alloc_index=5+left;
                default: alloc_index=9+left;
            endcase
        end
    endfunction
    function automatic integer compact_prefix(input integer n);
        begin
            if (n <= 0) compact_prefix=0;
            else if (n == 1) compact_prefix=4;
            else if (n == 2) compact_prefix=11;
            else if (n == 3) compact_prefix=20;
            else compact_prefix=20+10*(n-3);
        end
    endfunction
    function automatic [ROW_W-1:0] compact_row_id(
        input [5:0] suffix, input [2:0] left, input [2:0] right
    );
        integer avail, off;
        begin
            if (left == 0) compact_row_id=right;
            else begin
                avail=SPLIT-suffix;
                case(left)
                    1: off=0+right;
                    2: off=4+right;
                    3: off=7+right;
                    default: off=9+right;
                endcase
                compact_row_id=5+compact_prefix(avail-1)+off;
            end
        end
    endfunction
    function automatic integer normal_prefix(input integer n);
        begin
            if (n <= 0) normal_prefix=0;
            else if (n == 1) normal_prefix=1;
            else if (n == 2) normal_prefix=3;
            else if (n == 3) normal_prefix=6;
            else normal_prefix=6+4*(n-3);
        end
    endfunction
    function automatic [ROW_W-1:0] normal_row_id(
        input [5:0] suffix, input [2:0] weight
    );
        integer rem;
        begin
            if (suffix == SPLIT) normal_row_id=weight;
            else begin
                rem=K-suffix;
                normal_row_id=COMPACT_ROWS+normal_prefix(rem-1)+(weight-1);
            end
        end
    endfunction
    function automatic [63:0] packed_row(input [5:0] rank);
        integer g;
        reg [63:0] tmp;
        begin
            tmp=64'd0;
            for (g=0; g<12; g=g+1)
                tmp[g*5 +: 5]=row_effect[g][rank];
            tmp[60 +: 4]=row_effect[12][rank][3:0];
            packed_row=tmp;
        end
    endfunction
    function automatic [13:0] left_info_min(input [5:0] suffix, input [2:0] count);
        integer j;
        reg [13:0] sum;
        begin
            sum=0;
            for (j=0;j<4;j=j+1)
                if (j<count) sum=sum+info_cost[suffix+j];
            left_info_min=sum;
        end
    endfunction
    function automatic [13:0] right_info_min(input [5:0] suffix, input [2:0] count);
        integer j;
        reg [13:0] sum;
        begin
            sum=0;
            for (j=0;j<4;j=j+1)
                if (j<count) sum=sum+info_cost[suffix+j];
            right_info_min=sum;
        end
    endfunction

    // ------------------------------------------------------------------
    // Main compiled/normal table banks.
    // ------------------------------------------------------------------
    wire [GROUPS5*10-1:0] table_q5;
    wire [8:0] table_q4;
    reg table_re;
    reg [ROW_W-1:0] table_rrow;
    reg [59:0] table_rstate5;
    reg [3:0] table_rstate4;
    reg table_we;
    reg [ROW_W-1:0] table_wrow;
    reg [4:0] table_wstate;
    wire [GROUPS5*10-1:0] table_wdata5;
    wire [8:0] table_wdata4;
    reg [GROUPS5*10-1:0] build_value5;
    reg [8:0] build_value4;
    assign table_wdata5=build_value5;
    assign table_wdata4=build_value4;

    wire [ADDR5_W-1:0] table_rbase5 = table_rrow*32;
    wire [ADDR4_W-1:0] table_rbase4 = table_rrow*16;
    wire [ADDR5_W-1:0] table_waddr5 = table_wrow*32 + table_wstate;
    wire [ADDR4_W-1:0] table_waddr4 = table_wrow*16 + table_wstate[3:0];

    genvar gg;
    generate for (gg=0;gg<GROUPS5;gg=gg+1) begin: main5
        cap_sp_ram #(.DEPTH(DEPTH5),.ADDR_W(ADDR5_W),.DATA_W(10)) ram (
            .clk(clk), .we(table_we), .waddr(table_waddr5),
            .wdata(table_wdata5[gg*10 +: 10]), .re(table_re),
            .raddr(table_rbase5+table_rstate5[gg*5 +: 5]),
            .rdata(table_q5[gg*10 +: 10])
        );
    end endgenerate
    cap_sp_ram #(.DEPTH(DEPTH4),.ADDR_W(ADDR4_W),.DATA_W(9)) main4 (
        .clk(clk), .we(table_we && table_wstate<16), .waddr(table_waddr4),
        .wdata(table_wdata4), .re(table_re),
        .raddr(table_rbase4+table_rstate4), .rdata(table_q4)
    );

    // ------------------------------------------------------------------
    // Two scratch DP banks per parity group, reused for right and left DP.
    // ------------------------------------------------------------------
    reg scratch_old_sel;
    reg scratch_init_we;
    reg [SCRATCH_AW-1:0] scratch_init_addr;
    reg scratch_rd_en;
    reg [SCRATCH_AW-1:0] scratch_rd_a [0:12];
    reg [SCRATCH_AW-1:0] scratch_rd_b [0:12];
    wire [9:0] scratch_q_a [0:12];
    wire [9:0] scratch_q_b [0:12];
    reg scratch_new_we;
    reg [SCRATCH_AW-1:0] scratch_new_addr;
    reg [9:0] scratch_new_data [0:12];

    generate for (gg=0;gg<13;gg=gg+1) begin: scratch
        cap_scratch_pair #(.DEPTH(SCRATCH_DEPTH),.ADDR_W(SCRATCH_AW),.DATA_W(10)) pair (
            .clk(clk), .old_sel(scratch_old_sel),
            .init_we(scratch_init_we), .init_addr(scratch_init_addr),
            .init_data((gg==12 && scratch_init_addr[4:0]>=16) ? INF10 :
                ((scratch_init_addr<32) ? phi_cost[gg][scratch_init_addr[4:0]] : INF10)),
            .rd_en(scratch_rd_en), .rd_addr_a(scratch_rd_a[gg]),
            .rd_addr_b(scratch_rd_b[gg]), .rd_data_a(scratch_q_a[gg]),
            .rd_data_b(scratch_q_b[gg]), .new_we(scratch_new_we),
            .new_addr(scratch_new_addr), .new_data(scratch_new_data[gg])
        );
    end endgenerate

    localparam [2:0] BP_IDLE=0, BP_INIT=1, BP_RIGHT=2, BP_COPY_RIGHT=3,
                     BP_COPY_BASE=4, BP_LEFT=5, BP_DONE=6;
    reg [2:0] bphase;
    reg [5:0] brank;
    reg [8:0] bcell;
    reg [2:0] bcopy_weight;
    reg [4:0] bcopy_state;
    reg pipe_valid, pipe_last;
    reg [2:0] pipe_phase;
    reg [SCRATCH_AW-1:0] pipe_dest;
    reg [4:0] pipe_state;
    reg [3:0] pipe_alloc;
    reg [5:0] pipe_suffix;
    reg [2:0] pipe_weight;
    reg pipe_skip_valid, pipe_take_valid, pipe_store_main;
    reg [ROW_W-1:0] pipe_main_row;

    wire [2:0] bweight = bcell[7:5];
    wire [4:0] bstate = bcell[4:0];
    wire [3:0] balloc = bcell[8:5];
    wire [2:0] bleft = alloc_left(balloc);
    wire [2:0] btotal = alloc_weight(balloc);
    wire [2:0] bright = btotal-bleft;
    integer bg;

    always @* begin
        scratch_init_we=1'b0; scratch_init_addr=bcell;
        scratch_rd_en=1'b0; scratch_new_we=1'b0;
        scratch_new_addr=pipe_dest;
        table_we=1'b0; table_wrow=pipe_main_row; table_wstate=pipe_state;
        build_value5={GROUPS5{INF10}}; build_value4=9'h1ff;
        for (bg=0;bg<13;bg=bg+1) begin
            scratch_rd_a[bg]=0; scratch_rd_b[bg]=0;
            scratch_new_data[bg]=INF10;
        end

        if (bphase==BP_INIT) scratch_init_we=1'b1;
        if (bphase==BP_RIGHT || bphase==BP_LEFT ||
            bphase==BP_COPY_RIGHT || bphase==BP_COPY_BASE) scratch_rd_en=1'b1;

        if (bphase==BP_RIGHT) begin
            for (bg=0;bg<13;bg=bg+1) begin
                scratch_rd_a[bg]=bcell;
                scratch_rd_b[bg]=(bweight==0) ? bcell :
                    ((bweight-1)*32 + (bstate ^ row_effect[bg][brank]));
            end
        end else if (bphase==BP_LEFT) begin
            for (bg=0;bg<13;bg=bg+1) begin
                scratch_rd_a[bg]=bcell;
                scratch_rd_b[bg]=(bleft==0) ? bcell :
                    ((balloc_index(bleft-1,bright)*32) +
                    (bstate ^ row_effect[bg][brank]));
            end
        end else if (bphase==BP_COPY_RIGHT || bphase==BP_COPY_BASE) begin
            for (bg=0;bg<13;bg=bg+1)
                scratch_rd_a[bg]=bcopy_weight*32+bcopy_state;
        end

        if (pipe_valid && (pipe_phase==BP_RIGHT || pipe_phase==BP_LEFT)) begin
            scratch_new_we=1'b1;
            for (bg=0;bg<13;bg=bg+1) begin
                if (!pipe_skip_valid && !pipe_take_valid)
                    scratch_new_data[bg]=INF10;
                else if (!pipe_skip_valid)
                    scratch_new_data[bg]=scratch_q_b[bg];
                else if (!pipe_take_valid)
                    scratch_new_data[bg]=scratch_q_a[bg];
                else
                    scratch_new_data[bg]=(scratch_q_a[bg] < scratch_q_b[bg]) ?
                        scratch_q_a[bg] : scratch_q_b[bg];
            end
        end
        if (pipe_valid && pipe_store_main) begin
            table_we=1'b1;
            for (bg=0;bg<12;bg=bg+1)
                build_value5[bg*10 +: 10] =
                    (pipe_phase==BP_LEFT) ? scratch_new_data[bg] : scratch_q_a[bg];
            if (pipe_phase==BP_LEFT) build_value4 = scratch_new_data[12][8:0];
            else build_value4 = scratch_q_a[12][8:0];
        end
    end

    // Typo-safe alias used in the combinational builder address expression.
    function automatic [3:0] balloc_index(input [2:0] l, input [2:0] r);
        begin balloc_index=alloc_index(l,r); end
    endfunction

    always @(posedge clk) begin
        if (rst) begin
            build_busy<=0; build_done<=0; build_cycles<=0; bphase<=BP_IDLE;
            bcell<=0; brank<=63; scratch_old_sel<=0; pipe_valid<=0;
            pipe_last<=0; pipe_phase<=BP_IDLE; pipe_dest<=0; pipe_state<=0;
            pipe_alloc<=0; pipe_suffix<=0; pipe_weight<=0;
            pipe_skip_valid<=0; pipe_take_valid<=0; pipe_store_main<=0;
            pipe_main_row<=0; bcopy_weight<=0; bcopy_state<=0;
        end else begin
            build_done<=0;
            if (build_start && !build_busy && !decode_busy) begin
                build_busy<=1; build_cycles<=0; bphase<=BP_INIT; bcell<=0;
                brank<=63; scratch_old_sel<=0; pipe_valid<=0;
            end else if (build_busy) begin
                build_cycles<=build_cycles+1;
                pipe_valid<=0;
                if (bphase==BP_INIT) begin
                    if (bcell==159) begin bphase<=BP_RIGHT; bcell<=0; end
                    else bcell<=bcell+1;
                end else if (bphase==BP_RIGHT) begin
                    pipe_valid<=1; pipe_phase<=BP_RIGHT; pipe_dest<=bcell;
                    pipe_state<=bstate; pipe_weight<=bweight;
                    pipe_skip_valid<=1; pipe_take_valid<=(bweight!=0);
                    pipe_store_main<=0; pipe_last<=(bcell==159);
                    if (bcell==159) bphase<=BP_IDLE; else bcell<=bcell+1;
                end else if (bphase==BP_COPY_RIGHT || bphase==BP_COPY_BASE) begin
                    pipe_valid<=1; pipe_phase<=bphase; pipe_state<=bcopy_state;
                    pipe_store_main<=1; pipe_last<=0;
                    pipe_main_row <= (bphase==BP_COPY_BASE) ? bcopy_weight :
                        normal_row_id(brank,bcopy_weight);
                    if (bcopy_state==31) begin
                        bcopy_state<=0;
                        if (bcopy_weight==((K-brank<4)?K-brank:4)) begin
                            pipe_last<=1; bphase<=BP_IDLE;
                        end else bcopy_weight<=bcopy_weight+1;
                    end else bcopy_state<=bcopy_state+1;
                end else if (bphase==BP_LEFT) begin
                    pipe_valid<=1; pipe_phase<=BP_LEFT; pipe_dest<=bcell;
                    pipe_state<=bstate; pipe_alloc<=balloc; pipe_suffix<=brank;
                    pipe_skip_valid <= (bleft==0) || (bleft <= SPLIT-brank-1);
                    pipe_take_valid <= (bleft>0) && (bleft <= SPLIT-brank);
                    pipe_store_main <= (bleft>0) && (bleft <= SPLIT-brank);
                    pipe_main_row<=compact_row_id(brank,bleft,bright);
                    pipe_last<=(bcell==479);
                    if (bcell==479) bphase<=BP_IDLE; else bcell<=bcell+1;
                end

                if (pipe_valid && pipe_last) begin
                    if (pipe_phase==BP_RIGHT) begin
                        scratch_old_sel<=~scratch_old_sel;
                        if (brank>SPLIT) begin
                            bphase<=BP_COPY_RIGHT; bcopy_weight<=1; bcopy_state<=0;
                        end else begin
                            bphase<=BP_COPY_BASE; bcopy_weight<=0; bcopy_state<=0;
                        end
                    end else if (pipe_phase==BP_COPY_RIGHT) begin
                        brank<=brank-1; bphase<=BP_RIGHT; bcell<=0;
                    end else if (pipe_phase==BP_COPY_BASE) begin
                        if (SPLIT==0) bphase<=BP_DONE;
                        else begin brank<=SPLIT-1; bphase<=BP_LEFT; bcell<=0; end
                    end else if (pipe_phase==BP_LEFT) begin
                        scratch_old_sel<=~scratch_old_sel;
                        if (brank==0) begin bphase<=BP_DONE; end
                        else begin brank<=brank-1; bphase<=BP_LEFT; bcell<=0; end
                    end
                end
                if (bphase==BP_DONE) begin
                    build_busy<=0; build_done<=1; bphase<=BP_IDLE;
                end
            end
        end
    end

    // ------------------------------------------------------------------
    // Threshold query pipeline.
    // ------------------------------------------------------------------
    reg q_req_valid;
    reg [ROW_W-1:0] q_req_row;
    reg [59:0] q_req_state5;
    reg [3:0] q_req_state4;
    reg [13:0] q_req_base, q_req_inc;
    reg [1:0] q_req_ctx;
    reg qv0,qv1,qv2,qv3;
    reg [13:0] qb0,qi0,qb1,qi1,qb2,qi2,qb3,qi3;
    reg [1:0] qc0,qc1,qc2,qc3;
    reg [10:0] qpair1 [0:5]; reg [8:0] qtail1;
    reg [11:0] qquad2 [0:2]; reg [8:0] qtail2;
    reg [12:0] qoct3; reg [11:0] qfour3; reg [8:0] qtail3;
    reg qinv1,qinv2,qinv3; reg qinvalid_comb;
    wire [13:0] qparity = {1'b0,qoct3}+{2'b0,qfour3}+{5'b0,qtail3};
    wire [14:0] qtotal = {1'b0,qb3}+{1'b0,qparity};
    wire [13:0] qbound = qtotal[14] ? INF14 : qtotal[13:0];
    reg q_rsp_valid; reg [1:0] q_rsp_ctx; reg q_rsp_cannot;
    integer qi;

    always @* begin
        qinvalid_comb=(table_q4==9'h1ff);
        for(qi=0;qi<12;qi=qi+1)
            if(table_q5[qi*10 +: 10]==INF10) qinvalid_comb=1'b1;
        table_re=q_req_valid && !build_busy;
        table_rrow=q_req_row; table_rstate5=q_req_state5;
        table_rstate4=q_req_state4;
    end
    always @(posedge clk) begin
        if (rst) begin qv0<=0;qv1<=0;qv2<=0;qv3<=0;q_rsp_valid<=0;
            qinv1<=0;qinv2<=0;qinv3<=0; end
        else begin
            qv0<=q_req_valid; qb0<=q_req_base; qi0<=q_req_inc; qc0<=q_req_ctx;
            qv1<=qv0; qb1<=qb0; qi1<=qi0; qc1<=qc0;
            for(qi=0;qi<6;qi=qi+1)
                qpair1[qi]<={1'b0,table_q5[2*qi*10 +: 10]}+
                            {1'b0,table_q5[(2*qi+1)*10 +: 10]};
            qtail1<=table_q4; qinv1<=qinvalid_comb;
            qv2<=qv1; qb2<=qb1; qi2<=qi1; qc2<=qc1;
            qquad2[0]<={1'b0,qpair1[0]}+{1'b0,qpair1[1]};
            qquad2[1]<={1'b0,qpair1[2]}+{1'b0,qpair1[3]};
            qquad2[2]<={1'b0,qpair1[4]}+{1'b0,qpair1[5]}; qtail2<=qtail1; qinv2<=qinv1;
            qv3<=qv2; qb3<=qb2; qi3<=qi2; qc3<=qc2;
            qoct3<={1'b0,qquad2[0]}+{1'b0,qquad2[1]}; qfour3<=qquad2[2]; qtail3<=qtail2; qinv3<=qinv2;
            q_rsp_valid<=qv3; q_rsp_ctx<=qc3; q_rsp_cannot<=(!qinv3) && (qbound<=qi3);
        end
    end

    // ------------------------------------------------------------------
    // Four exact scoring lanes.
    // ------------------------------------------------------------------
    reg [3:0] s_req_valid;
    reg [63:0] s_req_states [0:3];
    reg [13:0] s_req_info [0:3];
    reg [63:0] s_req_mask [0:3];
    reg [3:0] sv0,sv1,sv2,sv3;
    reg [10:0] spair1 [0:3][0:5]; reg [8:0] stail1 [0:3];
    reg [11:0] squad2 [0:3][0:2]; reg [8:0] stail2 [0:3];
    reg [12:0] soct3 [0:3]; reg [11:0] sfour3 [0:3]; reg [8:0] stail3 [0:3];
    reg [13:0] sinfo1[0:3],sinfo2[0:3],sinfo3[0:3];
    reg [63:0] smask1[0:3],smask2[0:3],smask3[0:3];
    reg [3:0] s_rsp_valid; reg [13:0] s_rsp_metric[0:3]; reg [63:0] s_rsp_mask[0:3];
    integer sl,sg;
    reg [9:0] scost [0:3][0:12];
    always @* begin
        for(sl=0;sl<4;sl=sl+1) begin
            for(sg=0;sg<12;sg=sg+1)
                scost[sl][sg]=phi_cost[sg][s_req_states[sl][sg*5 +: 5]];
            scost[sl][12]={1'b0,phi_cost[12][{1'b0,s_req_states[sl][60 +: 4]}][8:0]};
        end
    end
    always @(posedge clk) begin
        if(rst) begin sv0<=0;sv1<=0;sv2<=0;sv3<=0;s_rsp_valid<=0; end
        else begin
            sv0<=s_req_valid; sv1<=sv0; sv2<=sv1; sv3<=sv2;
            for(sl=0;sl<4;sl=sl+1) begin
                sinfo1[sl]<=s_req_info[sl]; smask1[sl]<=s_req_mask[sl];
                for(sg=0;sg<6;sg=sg+1)
                    spair1[sl][sg]<={1'b0,scost[sl][2*sg]}+{1'b0,scost[sl][2*sg+1]};
                stail1[sl]<=scost[sl][12][8:0];
                sinfo2[sl]<=sinfo1[sl]; smask2[sl]<=smask1[sl];
                squad2[sl][0]<={1'b0,spair1[sl][0]}+{1'b0,spair1[sl][1]};
                squad2[sl][1]<={1'b0,spair1[sl][2]}+{1'b0,spair1[sl][3]};
                squad2[sl][2]<={1'b0,spair1[sl][4]}+{1'b0,spair1[sl][5]}; stail2[sl]<=stail1[sl];
                sinfo3[sl]<=sinfo2[sl]; smask3[sl]<=smask2[sl];
                soct3[sl]<={1'b0,squad2[sl][0]}+{1'b0,squad2[sl][1]};
                sfour3[sl]<=squad2[sl][2]; stail3[sl]<=stail2[sl];
                s_rsp_valid[sl]<=sv2[sl];
                s_rsp_metric[sl]<=sinfo3[sl]+soct3[sl]+sfour3[sl]+stail3[sl];
                s_rsp_mask[sl]<=smask3[sl];
            end
        end
    end

    // ------------------------------------------------------------------
    // Four partitioned DFS contexts.
    // ------------------------------------------------------------------
    localparam [2:0] CP_IDLE=0,CP_PICK=1,CP_SCORE=2,CP_AFTER_SCORE=3,
                     CP_SCAN=4,CP_BOUND_WAIT=5,CP_BACK=6,CP_DONE=7;
    reg [2:0] cphase[0:3]; reg [2:0] cdepth[0:3];
    reg [63:0] cstates[0:3][0:4]; reg [13:0] cinfo[0:3][0:4];
    reg [63:0] cmask[0:3][0:4]; reg [6:0] ccursor[0:3][0:4];
    reg [6:0] csuffix[0:3][0:4]; reg [3:0] calloc[0:3];
    reg [1:0] rr_ctx;
    integer c,d;
    reg [63:0] child_states, child_mask; reg [13:0] child_info;
    reg [5:0] pick_rank;
    reg [13:0] root_metric;
    always @* begin
        root_metric=0;
        for(bg=0;bg<12;bg=bg+1)
            root_metric=root_metric+phi_cost[bg][cfg_base_states[bg*5 +: 5]];
        root_metric=root_metric+phi_cost[12][{1'b0,cfg_base_states[60 +: 4]}];
    end

    // Bound arbitration and allocation decoding.
    reg grant_found; reg [1:0] grant_ctx;
    reg [2:0] grant_w,grant_l,grant_r; reg grant_valid_alloc;
    reg [6:0] grant_suffix;
    reg [13:0] grant_base;
    always @* begin
        grant_found=0; grant_ctx=rr_ctx; grant_w=0;grant_l=0;grant_r=0;
        grant_valid_alloc=0; grant_suffix=0; grant_base=0;
        q_req_valid=0;q_req_row=0;q_req_state5=0;q_req_state4=0;
        q_req_base=0;q_req_inc=best_metric;q_req_ctx=0;
        for(c=0;c<4;c=c+1) begin
            if(!grant_found && cphase[(rr_ctx+c)&3]==CP_SCAN) begin
                grant_ctx=(rr_ctx+c)&3; grant_found=1;
            end
        end
        if(grant_found) begin
            grant_w=alloc_weight(calloc[grant_ctx]);
            grant_l=alloc_left(calloc[grant_ctx]); grant_r=grant_w-grant_l;
            grant_suffix=csuffix[grant_ctx][cdepth[grant_ctx]];
            grant_valid_alloc=(grant_w <= O-cdepth[grant_ctx]) &&
                ((grant_suffix<SPLIT && grant_l<=SPLIT-grant_suffix) ||
                 (grant_suffix>=SPLIT && grant_l==0 && grant_r<=K-grant_suffix));
            if(grant_valid_alloc) begin
                q_req_valid=1; q_req_ctx=grant_ctx;
                if(grant_suffix<SPLIT) begin
                    q_req_row=compact_row_id(grant_suffix,grant_l,grant_r);
                    grant_base=cinfo[grant_ctx][cdepth[grant_ctx]]+
                        left_info_min(grant_suffix,grant_l)+right_info_min(SPLIT,grant_r);
                end else begin
                    q_req_row=normal_row_id(grant_suffix,grant_r);
                    grant_base=cinfo[grant_ctx][cdepth[grant_ctx]]+
                        right_info_min(grant_suffix,grant_r);
                end
                q_req_state5=cstates[grant_ctx][cdepth[grant_ctx]][59:0];
                q_req_state4=cstates[grant_ctx][cdepth[grant_ctx]][63:60];
                q_req_base=grant_base;
            end
        end
    end

    always @* begin
        s_req_valid=0;
        for(c=0;c<4;c=c+1) begin
            s_req_states[c]=cstates[c][cdepth[c]];
            s_req_info[c]=cinfo[c][cdepth[c]];
            s_req_mask[c]=cmask[c][cdepth[c]];
            if(cphase[c]==CP_SCORE) s_req_valid[c]=1;
        end
    end

    reg [2:0] score_rsp_count;
    reg [13:0] score_rsp_min;
    reg [63:0] score_rsp_min_mask;
    always @* begin
        score_rsp_count=0; score_rsp_min=best_metric; score_rsp_min_mask=best_tep;
        for(c=0;c<4;c=c+1) if(s_rsp_valid[c]) begin
            score_rsp_count=score_rsp_count+1;
            if(s_rsp_metric[c]<score_rsp_min) begin
                score_rsp_min=s_rsp_metric[c]; score_rsp_min_mask=s_rsp_mask[c];
            end
        end
    end

    always @(posedge clk) begin
        if(rst) begin
            decode_busy<=0;decode_done<=0;best_metric<=INF14;best_tep<=0;
            decode_cycles<=0;scored_teps<=0;bound_rows<=0;rr_ctx<=0;
            for(c=0;c<4;c=c+1) begin cphase[c]<=CP_IDLE;cdepth[c]<=0;calloc[c]<=0;
                for(d=0;d<5;d=d+1) begin cstates[c][d]<=0;cinfo[c][d]<=0;
                    cmask[c][d]<=0;ccursor[c][d]<=0;csuffix[c][d]<=0; end
            end
        end else begin
            decode_done<=0;
            if(decode_start && build_done && !decode_busy) begin
                decode_busy<=1;decode_cycles<=0;scored_teps<=1;bound_rows<=0;
                best_metric<=root_metric;best_tep<=0;rr_ctx<=0;
                for(c=0;c<4;c=c+1) begin
                    cphase[c]<=CP_PICK;cdepth[c]<=0;cstates[c][0]<=cfg_base_states;
                    cinfo[c][0]<=0;cmask[c][0]<=0;ccursor[c][0]<=c*16;
                    csuffix[c][0]<=0;calloc[c]<=0;
                end
            end else if(decode_busy) begin
                decode_cycles<=decode_cycles+1;
                // Continuous incumbent reduction from all four score lanes.
                if(score_rsp_count!=0) begin
                    scored_teps<=scored_teps+score_rsp_count;
                    if(score_rsp_min<best_metric) begin
                        best_metric<=score_rsp_min; best_tep<=score_rsp_min_mask;
                    end
                end
                for(c=0;c<4;c=c+1) if(s_rsp_valid[c]) begin
                    if(cphase[c]==CP_AFTER_SCORE) begin
                        if(cdepth[c]>=O || csuffix[c][cdepth[c]]>=K) cphase[c]<=CP_BACK;
                        else begin calloc[c]<=0;cphase[c]<=CP_SCAN; end
                    end
                end

                // Launching a score request moves the context to wait state.
                for(c=0;c<4;c=c+1) if(cphase[c]==CP_SCORE) cphase[c]<=CP_AFTER_SCORE;

                // Bound request arbitration.
                if(q_req_valid) begin
                    cphase[grant_ctx]<=CP_BOUND_WAIT;rr_ctx<=grant_ctx+1;bound_rows<=bound_rows+1;
                end else if(grant_found && !grant_valid_alloc) begin
                    if(calloc[grant_ctx]==13) cphase[grant_ctx]<=CP_BACK;
                    else calloc[grant_ctx]<=calloc[grant_ctx]+1;
                end
                if(q_rsp_valid && cphase[q_rsp_ctx]==CP_BOUND_WAIT) begin
                    if(q_rsp_cannot) cphase[q_rsp_ctx]<=CP_PICK;
                    else if(calloc[q_rsp_ctx]==13) cphase[q_rsp_ctx]<=CP_BACK;
                    else begin calloc[q_rsp_ctx]<=calloc[q_rsp_ctx]+1;cphase[q_rsp_ctx]<=CP_SCAN; end
                end

                // DFS child generation/backtracking.
                for(c=0;c<4;c=c+1) begin
                    if(cphase[c]==CP_PICK) begin
                        if(cdepth[c]==0) begin
                            if(ccursor[c][0] > c*16+15) cphase[c]<=CP_DONE;
                            else begin
                                pick_rank=ccursor[c][0][5:0];ccursor[c][0]<=ccursor[c][0]+1;
                                child_states=cstates[c][0]^packed_row(pick_rank);
                                child_info=cinfo[c][0]+info_cost[pick_rank];
                                child_mask=64'd1<<pick_rank;cdepth[c]<=1;
                                cstates[c][1]<=child_states;cinfo[c][1]<=child_info;
                                cmask[c][1]<=child_mask;csuffix[c][1]<=pick_rank+1;
                                ccursor[c][1]<=pick_rank+1;cphase[c]<=CP_SCORE;
                            end
                        end else if(cdepth[c]<O) begin
                            if(ccursor[c][cdepth[c]]>=K) cphase[c]<=CP_BACK;
                            else begin
                                pick_rank=ccursor[c][cdepth[c]][5:0];
                                ccursor[c][cdepth[c]]<=ccursor[c][cdepth[c]]+1;
                                child_states=cstates[c][cdepth[c]]^packed_row(pick_rank);
                                child_info=cinfo[c][cdepth[c]]+info_cost[pick_rank];
                                child_mask=cmask[c][cdepth[c]]|(64'd1<<pick_rank);
                                cdepth[c]<=cdepth[c]+1;
                                cstates[c][cdepth[c]+1]<=child_states;
                                cinfo[c][cdepth[c]+1]<=child_info;
                                cmask[c][cdepth[c]+1]<=child_mask;
                                csuffix[c][cdepth[c]+1]<=pick_rank+1;
                                ccursor[c][cdepth[c]+1]<=pick_rank+1;cphase[c]<=CP_SCORE;
                            end
                        end else cphase[c]<=CP_BACK;
                    end else if(cphase[c]==CP_BACK) begin
                        if(cdepth[c]==0) cphase[c]<=CP_DONE;
                        else begin cdepth[c]<=cdepth[c]-1;cphase[c]<=CP_PICK; end
                    end
                end
                if(cphase[0]==CP_DONE && cphase[1]==CP_DONE &&
                   cphase[2]==CP_DONE && cphase[3]==CP_DONE) begin
                    decode_busy<=0;decode_done<=1;
                end
            end
        end
    end
endmodule


module cap_full_normal_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire[3:0]cfg_row_group,input wire[5:0]cfg_row_rank,input wire[4:0]cfg_row_effect,
    input wire cfg_phi_we,input wire[3:0]cfg_phi_group,input wire[4:0]cfg_phi_state,input wire[9:0]cfg_phi_cost,
    input wire cfg_info_we,input wire[5:0]cfg_info_rank,input wire[6:0]cfg_info_cost,input wire[63:0]cfg_base_states,
    input wire build_start,output wire build_busy,output wire build_done,output wire[31:0]build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire[13:0]best_metric,
    output wire[63:0]best_tep,output wire[31:0]decode_cycles,output wire[31:0]scored_teps,output wire[31:0]bound_rows
);
 cap_full_compiled_decoder #(.SPLIT(0),.COMPACT_ROWS(5),.ROWS(251),.ROW_W(8),
    .DEPTH5(8032),.DEPTH4(4016),.ADDR5_W(13),.ADDR4_W(12)) core(.*);
endmodule

module cap_full_s45_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire[3:0]cfg_row_group,input wire[5:0]cfg_row_rank,input wire[4:0]cfg_row_effect,
    input wire cfg_phi_we,input wire[3:0]cfg_phi_group,input wire[4:0]cfg_phi_state,input wire[9:0]cfg_phi_cost,
    input wire cfg_info_we,input wire[5:0]cfg_info_rank,input wire[6:0]cfg_info_cost,input wire[63:0]cfg_base_states,
    input wire build_start,output wire build_busy,output wire build_done,output wire[31:0]build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire[13:0]best_metric,
    output wire[63:0]best_tep,output wire[31:0]decode_cycles,output wire[31:0]scored_teps,output wire[31:0]bound_rows
);
 cap_full_compiled_decoder #(.SPLIT(45),.COMPACT_ROWS(445),.ROWS(511)) core(.*);
endmodule

module cap_full_s48_top (
    input wire clk,input wire rst,
    input wire cfg_row_we,input wire[3:0]cfg_row_group,input wire[5:0]cfg_row_rank,input wire[4:0]cfg_row_effect,
    input wire cfg_phi_we,input wire[3:0]cfg_phi_group,input wire[4:0]cfg_phi_state,input wire[9:0]cfg_phi_cost,
    input wire cfg_info_we,input wire[5:0]cfg_info_rank,input wire[6:0]cfg_info_cost,input wire[63:0]cfg_base_states,
    input wire build_start,output wire build_busy,output wire build_done,output wire[31:0]build_cycles,
    input wire decode_start,output wire decode_busy,output wire decode_done,output wire[13:0]best_metric,
    output wire[63:0]best_tep,output wire[31:0]decode_cycles,output wire[31:0]scored_teps,output wire[31:0]bound_rows
);
 cap_full_compiled_decoder #(.SPLIT(48),.COMPACT_ROWS(475),.ROWS(529)) core(.*);
endmodule
