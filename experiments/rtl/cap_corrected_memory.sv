// Correct, nonblocking normal/threshold-compiled CAP accelerator for
// eBCH(128,64), OSD-4.
//
// Corrections relative to the feasibility RTL:
//  * the compact builder uses the complete 15-state triangular (left,right)
//    allocation encoding, including (0,0);
//  * the right five-weight DP is explicitly expanded into that allocation
//    encoding before any left-block transition;
//  * scorer requests are fire-and-forget;
//  * depth-4 leaves use a one-leaf-per-context-per-cycle fast path;
//  * a shared prefix-task FIFO dynamically feeds idle DFS contexts;
//  * incumbent updates implement deterministic metric/mask tie tracking;
//  * completion waits for all bound and score pipelines to drain.
//
// Reliability sorting, Gaussian elimination and final codeword materialisation
// remain outside this core. Inputs are already ordered/systematicised.

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
    input wire a_we,
    input wire [ADDR_W-1:0] a_addr,
    input wire [DATA_W-1:0] a_wdata,
    output reg [DATA_W-1:0] a_rdata,
    input wire b_we,
    input wire [ADDR_W-1:0] b_addr,
    input wire [DATA_W-1:0] b_wdata,
    output reg [DATA_W-1:0] b_rdata
);
    (* ram_style = "block" *) reg [DATA_W-1:0] mem [0:DEPTH-1];
    always @(posedge clk) begin
        if (a_we) mem[a_addr] <= a_wdata;
        else      a_rdata <= mem[a_addr];
        if (b_we) mem[b_addr] <= b_wdata;
        else      b_rdata <= mem[b_addr];
    end
endmodule

// old_sel=0: bank0 is old/read, bank1 is new/write.
// old_sel=1: bank1 is old/read, bank0 is new/write.
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
        .a_we(b0_a_we), .a_addr(b0_a_addr), .a_wdata(b0_a_data),
        .a_rdata(b0_a_r),
        .b_we(1'b0), .b_addr(rd_addr_b), .b_wdata({DATA_W{1'b0}}),
        .b_rdata(b0_b_r)
    );
    cap_tdp_ram #(.DEPTH(DEPTH), .ADDR_W(ADDR_W), .DATA_W(DATA_W)) bank1 (
        .clk(clk),
        .a_we(b1_a_we), .a_addr(b1_a_addr), .a_wdata(b1_a_data),
        .a_rdata(b1_a_r),
        .b_we(1'b0), .b_addr(rd_addr_b), .b_wdata({DATA_W{1'b0}}),
        .b_rdata(b1_b_r)
    );

    assign rd_data_a = old_sel ? b1_a_r : b0_a_r;
    assign rd_data_b = old_sel ? b1_b_r : b0_b_r;
endmodule
