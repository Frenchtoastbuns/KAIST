// State-wide, effect-parallel compact CAP-PDB allocation engine.
//
// Unlike the phase-1 narrow scanner, each parity group stores all 32 state
// costs for one (suffix, cardinality) row in a wide synchronous word.  A
// single memory read therefore feeds LANES independent effect selectors.
// The payload is unchanged; only its physical organization differs.

module cap_pdb_state_wide_bank #(
	parameter integer ROWS = 325,
	parameter integer ROW_ADDR_W = $clog2(ROWS),
	parameter integer WORD_W = 512
) (
	input wire clk,
	input wire we,
	input wire [ROW_ADDR_W-1:0] waddr,
	input wire [WORD_W-1:0] wdata,
	input wire re,
	input wire [ROW_ADDR_W-1:0] raddr,
	output reg [WORD_W-1:0] rdata
);
	(* ram_style = "block" *) reg [WORD_W-1:0] memory [0:ROWS-1];
	always @(posedge clk) begin
		if (we)
			memory[waddr] <= wdata;
		if (re)
			rdata <= memory[raddr];
	end
endmodule

// Balanced 32-way masked minimum.  Fixed state positions avoid one 32:1 cost
// selector per lane; the selected-effect mask is XOR-permuted instead.
module cap_pdb_masked_min32 #(
	parameter integer COST_W = 16,
	parameter [COST_W-1:0] INF = {COST_W{1'b1}}
) (
	input wire [32*COST_W-1:0] costs,
	input wire [31:0] selected,
	output wire [COST_W-1:0] minimum
);
	wire [32*COST_W-1:0] level0;
	wire [16*COST_W-1:0] level1;
	wire [8*COST_W-1:0] level2;
	wire [4*COST_W-1:0] level3;
	wire [2*COST_W-1:0] level4;

	genvar item;
	generate
		for (item = 0; item < 32; item = item + 1) begin: g_leaf
			assign level0[item*COST_W +: COST_W] = selected[item]
				? costs[item*COST_W +: COST_W] : INF;
		end
		for (item = 0; item < 16; item = item + 1) begin: g_l1
			assign level1[item*COST_W +: COST_W] =
				level0[(2*item)*COST_W +: COST_W] <
				level0[(2*item+1)*COST_W +: COST_W]
				? level0[(2*item)*COST_W +: COST_W]
				: level0[(2*item+1)*COST_W +: COST_W];
		end
		for (item = 0; item < 8; item = item + 1) begin: g_l2
			assign level2[item*COST_W +: COST_W] =
				level1[(2*item)*COST_W +: COST_W] <
				level1[(2*item+1)*COST_W +: COST_W]
				? level1[(2*item)*COST_W +: COST_W]
				: level1[(2*item+1)*COST_W +: COST_W];
		end
		for (item = 0; item < 4; item = item + 1) begin: g_l3
			assign level3[item*COST_W +: COST_W] =
				level2[(2*item)*COST_W +: COST_W] <
				level2[(2*item+1)*COST_W +: COST_W]
				? level2[(2*item)*COST_W +: COST_W]
				: level2[(2*item+1)*COST_W +: COST_W];
		end
		for (item = 0; item < 2; item = item + 1) begin: g_l4
			assign level4[item*COST_W +: COST_W] =
				level3[(2*item)*COST_W +: COST_W] <
				level3[(2*item+1)*COST_W +: COST_W]
				? level3[(2*item)*COST_W +: COST_W]
				: level3[(2*item+1)*COST_W +: COST_W];
		end
	endgenerate

	assign minimum =
		level4[0 +: COST_W] < level4[COST_W +: COST_W]
			? level4[0 +: COST_W] : level4[COST_W +: COST_W];
endmodule

module cap_pdb_parallel_compact_allocation_engine #(
	parameter integer K = 64,
	parameter integer ORDER = 4,
	parameter integer GROUPS = 13,
	parameter integer STATE_BITS = 5,
	parameter integer COST_W = 16,
	parameter integer ACC_W = 24,
	parameter integer SPLIT = 48,
	parameter integer LANES = 8,
	parameter integer STATES = (1 << STATE_BITS),
	parameter integer TABLE_ROWS = (K + 1) * (ORDER + 1),
	parameter integer TABLE_ROW_ADDR_W = $clog2(TABLE_ROWS),
	parameter integer GROUP_W = $clog2(GROUPS),
	parameter integer WORD_W = STATES * COST_W,
	parameter [COST_W-1:0] INF = {COST_W{1'b1}}
) (
	input wire clk,
	input wire rst,

	// The frame-local table builder writes one complete 32-state vector.
	// A vector-wide builder is the natural companion to the XOR-permutation
	// recurrence and avoids multiported replication of the 2.16-Mbit payload.
	input wire table_we,
	input wire [GROUP_W-1:0] table_wgroup,
	input wire [TABLE_ROW_ADDR_W-1:0] table_wrow,
	input wire [WORD_W-1:0] table_wdata,

	input wire start,
	input wire [$clog2(ORDER+1)-1:0] right_weight,
	input wire [GROUPS*STATE_BITS-1:0] group_state,
	input wire [GROUPS*STATES-1:0] reachable_effects,

	output reg busy,
	output reg done,
	output reg valid,
	output reg [ACC_W-1:0] group_cost_sum,
	output reg [15:0] cycles
);
	localparam [1:0] IDLE = 2'd0, READ_TABLE = 2'd1, SCAN = 2'd2;

	reg [1:0] phase;
	reg [STATES-1:0] remaining [0:GROUPS-1];
	reg [COST_W-1:0] group_minimum [0:GROUPS-1];
	wire [GROUPS*WORD_W-1:0] table_word_bus;
	wire [GROUPS*COST_W-1:0] batch_minimum_bus;
	reg [GROUPS*STATES-1:0] selected_effect_bus;
	reg [GROUPS*STATES-1:0] remaining_after_bus;

	function automatic [31:0] xor_permute_mask;
		input [31:0] mask;
		input [4:0] xor_state;
		reg [31:0] stage0;
		reg [31:0] stage1;
		reg [31:0] stage2;
		reg [31:0] stage3;
		begin
			stage0 = xor_state[0]
				? ((mask & 32'haaaaaaaa) >> 1) |
					((mask & 32'h55555555) << 1)
				: mask;
			stage1 = xor_state[1]
				? ((stage0 & 32'hcccccccc) >> 2) |
					((stage0 & 32'h33333333) << 2)
				: stage0;
			stage2 = xor_state[2]
				? ((stage1 & 32'hf0f0f0f0) >> 4) |
					((stage1 & 32'h0f0f0f0f) << 4)
				: stage1;
			stage3 = xor_state[3]
				? ((stage2 & 32'hff00ff00) >> 8) |
					((stage2 & 32'h00ff00ff) << 8)
				: stage2;
			xor_permute_mask = xor_state[4]
				? (stage3 >> 16) | (stage3 << 16)
				: stage3;
		end
	endfunction

	wire [TABLE_ROW_ADDR_W-1:0] read_row =
		SPLIT * (ORDER + 1) + right_weight;

	genvar bank;
	generate
		for (bank = 0; bank < GROUPS; bank = bank + 1) begin: g_bank
			cap_pdb_state_wide_bank #(
				.ROWS(TABLE_ROWS),
				.ROW_ADDR_W(TABLE_ROW_ADDR_W),
				.WORD_W(WORD_W)
			) table_bank (
				.clk(clk),
				.we(table_we && table_wgroup == bank),
				.waddr(table_wrow),
				.wdata(table_wdata),
				.re(phase == READ_TABLE),
				.raddr(read_row),
				.rdata(table_word_bus[bank*WORD_W +: WORD_W])
			);

			wire [31:0] target_state_mask = xor_permute_mask(
				selected_effect_bus[bank*STATES +: STATES],
				group_state[bank*STATE_BITS +: STATE_BITS]
			);
			cap_pdb_masked_min32 #(
				.COST_W(COST_W), .INF(INF)
			) batch_minimum_tree (
				.costs(table_word_bus[bank*WORD_W +: WORD_W]),
				.selected(target_state_mask),
				.minimum(batch_minimum_bus[bank*COST_W +: COST_W])
			);
		end
	endgenerate

	integer index;
	integer select_group;
	integer select_lane;
	reg [STATES-1:0] select_pending;
	reg [STATES-1:0] select_least;
	reg all_after_empty;
	reg [ACC_W-1:0] final_sum;
	reg [COST_W-1:0] final_group_minimum;
	always @* begin
		selected_effect_bus = {(GROUPS*STATES){1'b0}};
		remaining_after_bus = {(GROUPS*STATES){1'b0}};
		select_pending = {STATES{1'b0}};
		select_least = {STATES{1'b0}};
		for (select_group = 0; select_group < GROUPS;
			select_group = select_group + 1) begin
			select_pending = remaining[select_group];
			for (select_lane = 0; select_lane < LANES;
				select_lane = select_lane + 1) begin
				select_least = select_pending &
					(~select_pending + {{(STATES-1){1'b0}}, 1'b1});
				selected_effect_bus[
					select_group*STATES +: STATES
				] = selected_effect_bus[
					select_group*STATES +: STATES
				] | select_least;
				select_pending = select_pending & ~select_least;
			end
			remaining_after_bus[
				select_group*STATES +: STATES
			] = select_pending;
		end
	end

	always @* begin
		all_after_empty = 1'b1;
		final_sum = {ACC_W{1'b0}};
		for (index = 0; index < GROUPS; index = index + 1) begin
			if (|remaining_after_bus[index*STATES +: STATES])
				all_after_empty = 1'b0;
			final_group_minimum = group_minimum[index];
			if (
				batch_minimum_bus[index*COST_W +: COST_W] <
				final_group_minimum
			)
				final_group_minimum =
					batch_minimum_bus[index*COST_W +: COST_W];
			final_sum = final_sum + final_group_minimum;
		end
	end

	always @(posedge clk) begin
		if (rst) begin
			phase <= IDLE;
			busy <= 1'b0;
			done <= 1'b0;
			valid <= 1'b0;
			group_cost_sum <= {ACC_W{1'b0}};
			cycles <= 16'd0;
			for (index = 0; index < GROUPS; index = index + 1) begin
				remaining[index] <= {STATES{1'b0}};
				group_minimum[index] <= INF;
			end
		end else begin
			done <= 1'b0;
			if (busy)
				cycles <= cycles + 1'b1;
			case (phase)
			IDLE: if (start) begin
				busy <= 1'b1;
				valid <= 1'b1;
				cycles <= 16'd0;
				for (index = 0; index < GROUPS; index = index + 1) begin
					remaining[index] <=
						reachable_effects[index*STATES +: STATES];
					group_minimum[index] <= INF;
					if (!(|reachable_effects[index*STATES +: STATES]))
						valid <= 1'b0;
				end
				phase <= READ_TABLE;
			end
			READ_TABLE: phase <= SCAN;
			SCAN: begin
				for (index = 0; index < GROUPS; index = index + 1) begin
					if (
						batch_minimum_bus[index*COST_W +: COST_W] <
						group_minimum[index]
					)
						group_minimum[index] <=
							batch_minimum_bus[index*COST_W +: COST_W];
					remaining[index] <= remaining[index] &
						~selected_effect_bus[index*STATES +: STATES];
				end
				if (all_after_empty) begin
					group_cost_sum <= final_sum;
					busy <= 1'b0;
					done <= 1'b1;
					phase <= IDLE;
				end
			end
			default: phase <= IDLE;
			endcase
		end
	end
endmodule
