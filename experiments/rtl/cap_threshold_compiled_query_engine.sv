// Threshold-compiled compact CAP-PDB query engine.
//
// The expensive reachable-effect convolution is compiled into a frame-local
// table F_g(row, state). One tagged allocation request is accepted per cycle.
// Twelve 5-bit groups use 10-bit entries; the final 4-bit tail group uses
// 9-bit entries. The same memory ports support frame-local table loading.

module cap_compiled_bank #(
    parameter integer DEPTH = 16352,
    parameter integer ADDR_W = $clog2(DEPTH),
    parameter integer DATA_W = 10
) (
    input  wire                  clk,
    input  wire                  we,
    input  wire [ADDR_W-1:0]     waddr,
    input  wire [DATA_W-1:0]     wdata,
    input  wire                  re,
    input  wire [ADDR_W-1:0]     raddr,
    output reg  [DATA_W-1:0]     rdata
);
    (* ram_style = "block" *) reg [DATA_W-1:0] memory [0:DEPTH-1];

    always @(posedge clk) begin
        if (we)
            memory[waddr] <= wdata;
        if (re)
            rdata <= memory[raddr];
    end
endmodule

module cap_threshold_compiled_query_engine #(
    parameter integer ROWS = 511,
    parameter integer ROW_W = $clog2(ROWS),
    parameter integer GROUPS5 = 12,
    parameter integer DEPTH5 = ROWS * 32,
    parameter integer DEPTH4 = ROWS * 16,
    parameter integer ADDR5_W = $clog2(DEPTH5),
    parameter integer ADDR4_W = $clog2(DEPTH4)
) (
    input  wire                  clk,
    input  wire                  rst,

    input  wire                  table_we,
    input  wire [3:0]            table_wgroup,
    input  wire [ROW_W-1:0]      table_wrow,
    input  wire [4:0]            table_wstate,
    input  wire [9:0]            table_wcost,

    input  wire                  req_valid,
    input  wire [ROW_W-1:0]      req_row,
    input  wire [59:0]           req_state5,
    input  wire [3:0]            req_state4,
    input  wire [13:0]           req_base_cost,
    input  wire [13:0]           req_incumbent,
    input  wire [15:0]           req_tag,

    output reg                   rsp_valid,
    output reg  [13:0]           rsp_bound,
    output reg                   rsp_cannot_prune,
    output reg  [15:0]           rsp_tag
);
    localparam [9:0] INF5 = 10'h3ff;
    localparam [8:0] INF4 = 9'h1ff;
    localparam [13:0] INF_ACC = 14'h3fff;

    wire [GROUPS5*10-1:0] cost5_bus;
    wire [8:0] cost4;

    wire [ADDR5_W-1:0] read_base5 = {req_row, 5'b00000};
    wire [ADDR4_W-1:0] read_base4 = {req_row, 4'b0000};
    wire [ADDR5_W-1:0] write_addr5 =
        {table_wrow, 5'b00000} | table_wstate;
    wire [ADDR4_W-1:0] write_addr4 =
        {table_wrow, 4'b0000} | table_wstate[3:0];

    genvar group_index;
    generate
        for (group_index = 0; group_index < GROUPS5;
             group_index = group_index + 1) begin: g_group5
            wire [4:0] query_state =
                req_state5[group_index*5 +: 5];
            cap_compiled_bank #(
                .DEPTH(DEPTH5),
                .ADDR_W(ADDR5_W),
                .DATA_W(10)
            ) group_bank (
                .clk(clk),
                .we(table_we && table_wgroup == group_index),
                .waddr(write_addr5),
                .wdata(table_wcost),
                .re(req_valid),
                .raddr(read_base5 | query_state),
                .rdata(cost5_bus[group_index*10 +: 10])
            );
        end
    endgenerate

    cap_compiled_bank #(
        .DEPTH(DEPTH4),
        .ADDR_W(ADDR4_W),
        .DATA_W(9)
    ) tail_bank (
        .clk(clk),
        .we(table_we && table_wgroup == 4'd12),
        .waddr(write_addr4),
        .wdata(table_wcost[8:0]),
        .re(req_valid),
        .raddr(read_base4 | req_state4),
        .rdata(cost4)
    );

    reg valid_s0;
    reg [13:0] base_s0;
    reg [13:0] incumbent_s0;
    reg [15:0] tag_s0;

    reg valid_s1;
    reg [10:0] pair_s1 [0:5];
    reg [8:0] tail_s1;
    reg invalid_s1;
    reg [13:0] base_s1;
    reg [13:0] incumbent_s1;
    reg [15:0] tag_s1;

    reg valid_s2;
    reg [11:0] quad_s2 [0:2];
    reg [8:0] tail_s2;
    reg invalid_s2;
    reg [13:0] base_s2;
    reg [13:0] incumbent_s2;
    reg [15:0] tag_s2;

    reg valid_s3;
    reg [12:0] oct_s3;
    reg [11:0] four_s3;
    reg [8:0] tail_s3;
    reg invalid_s3;
    reg [13:0] base_s3;
    reg [13:0] incumbent_s3;
    reg [15:0] tag_s3;

    integer i;
    reg invalid_comb;
    always @* begin
        invalid_comb = (cost4 == INF4);
        for (i = 0; i < GROUPS5; i = i + 1)
            if (cost5_bus[i*10 +: 10] == INF5)
                invalid_comb = 1'b1;
    end

    wire [13:0] parity_sum_s3 =
        {1'b0, oct_s3} + {2'b00, four_s3} +
        {5'b00000, tail_s3};
    wire [14:0] total_sum_s3 =
        {1'b0, base_s3} + {1'b0, parity_sum_s3};
    wire [13:0] final_bound_s3 =
        invalid_s3 || total_sum_s3[14]
        ? INF_ACC : total_sum_s3[13:0];

    always @(posedge clk) begin
        if (rst) begin
            valid_s0 <= 1'b0;
            valid_s1 <= 1'b0;
            valid_s2 <= 1'b0;
            valid_s3 <= 1'b0;
            rsp_valid <= 1'b0;
            rsp_bound <= INF_ACC;
            rsp_cannot_prune <= 1'b0;
            rsp_tag <= 16'd0;
            base_s0 <= 14'd0;
            incumbent_s0 <= 14'd0;
            tag_s0 <= 16'd0;
            tail_s1 <= 9'd0;
            invalid_s1 <= 1'b0;
            base_s1 <= 14'd0;
            incumbent_s1 <= 14'd0;
            tag_s1 <= 16'd0;
            tail_s2 <= 9'd0;
            invalid_s2 <= 1'b0;
            base_s2 <= 14'd0;
            incumbent_s2 <= 14'd0;
            tag_s2 <= 16'd0;
            oct_s3 <= 13'd0;
            four_s3 <= 12'd0;
            tail_s3 <= 9'd0;
            invalid_s3 <= 1'b0;
            base_s3 <= 14'd0;
            incumbent_s3 <= 14'd0;
            tag_s3 <= 16'd0;
            for (i = 0; i < 6; i = i + 1)
                pair_s1[i] <= 11'd0;
            for (i = 0; i < 3; i = i + 1)
                quad_s2[i] <= 12'd0;
        end else begin
            valid_s0 <= req_valid;
            base_s0 <= req_base_cost;
            incumbent_s0 <= req_incumbent;
            tag_s0 <= req_tag;

            valid_s1 <= valid_s0;
            for (i = 0; i < 6; i = i + 1)
                pair_s1[i] <=
                    {1'b0, cost5_bus[(2*i)*10 +: 10]} +
                    {1'b0, cost5_bus[(2*i+1)*10 +: 10]};
            tail_s1 <= cost4;
            invalid_s1 <= invalid_comb;
            base_s1 <= base_s0;
            incumbent_s1 <= incumbent_s0;
            tag_s1 <= tag_s0;

            valid_s2 <= valid_s1;
            quad_s2[0] <=
                {1'b0, pair_s1[0]} + {1'b0, pair_s1[1]};
            quad_s2[1] <=
                {1'b0, pair_s1[2]} + {1'b0, pair_s1[3]};
            quad_s2[2] <=
                {1'b0, pair_s1[4]} + {1'b0, pair_s1[5]};
            tail_s2 <= tail_s1;
            invalid_s2 <= invalid_s1;
            base_s2 <= base_s1;
            incumbent_s2 <= incumbent_s1;
            tag_s2 <= tag_s1;

            valid_s3 <= valid_s2;
            oct_s3 <=
                {1'b0, quad_s2[0]} + {1'b0, quad_s2[1]};
            four_s3 <= quad_s2[2];
            tail_s3 <= tail_s2;
            invalid_s3 <= invalid_s2;
            base_s3 <= base_s2;
            incumbent_s3 <= incumbent_s2;
            tag_s3 <= tag_s2;

            rsp_valid <= valid_s3;
            rsp_bound <= final_bound_s3;
            rsp_cannot_prune <= final_bound_s3 <= incumbent_s3;
            rsp_tag <= tag_s3;
        end
    end
endmodule

module cap_streamed_normal_top (
    input wire clk, input wire rst,
    input wire table_we, input wire [3:0] table_wgroup,
    input wire [8:0] table_wrow, input wire [4:0] table_wstate,
    input wire [9:0] table_wcost,
    input wire req_valid, input wire [8:0] req_row,
    input wire [59:0] req_state5, input wire [3:0] req_state4,
    input wire [13:0] req_base_cost,
    input wire [13:0] req_incumbent, input wire [15:0] req_tag,
    output wire rsp_valid, output wire [13:0] rsp_bound,
    output wire rsp_cannot_prune, output wire [15:0] rsp_tag
);
    cap_threshold_compiled_query_engine #(
        .ROWS(325), .ROW_W(9)
    ) core (.*);
endmodule

module cap_compiled_s45_top (
    input wire clk, input wire rst,
    input wire table_we, input wire [3:0] table_wgroup,
    input wire [8:0] table_wrow, input wire [4:0] table_wstate,
    input wire [9:0] table_wcost,
    input wire req_valid, input wire [8:0] req_row,
    input wire [59:0] req_state5, input wire [3:0] req_state4,
    input wire [13:0] req_base_cost,
    input wire [13:0] req_incumbent, input wire [15:0] req_tag,
    output wire rsp_valid, output wire [13:0] rsp_bound,
    output wire rsp_cannot_prune, output wire [15:0] rsp_tag
);
    cap_threshold_compiled_query_engine #(
        .ROWS(511), .ROW_W(9)
    ) core (.*);
endmodule

module cap_compiled_s48_top (
    input wire clk, input wire rst,
    input wire table_we, input wire [3:0] table_wgroup,
    input wire [9:0] table_wrow, input wire [4:0] table_wstate,
    input wire [9:0] table_wcost,
    input wire req_valid, input wire [9:0] req_row,
    input wire [59:0] req_state5, input wire [3:0] req_state4,
    input wire [13:0] req_base_cost,
    input wire [13:0] req_incumbent, input wire [15:0] req_tag,
    output wire rsp_valid, output wire [13:0] rsp_bound,
    output wire rsp_cannot_prune, output wire [15:0] rsp_tag
);
    cap_threshold_compiled_query_engine #(
        .ROWS(529), .ROW_W(10)
    ) core (.*);
endmodule
