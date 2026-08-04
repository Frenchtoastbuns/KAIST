// BRAM-efficient two-effect compact CAP-PDB engine.
//
// Each group retains the original narrow CAP table organization.  During the
// query phase both ports are reads, so two reachable effects are issued per
// group per cycle without table replication.  During table construction port A
// becomes the write port.  Query batches are pipelined at one batch/cycle.

module cap_pdb_dual_read_bank #(
	parameter integer DEPTH = 10400,
	parameter integer ADDR_W = $clog2(DEPTH),
	parameter integer COST_W = 16
) (
	input wire clk,
	input wire we,
	input wire [ADDR_W-1:0] waddr,
	input wire [COST_W-1:0] wdata,
	input wire re0,
	input wire [ADDR_W-1:0] raddr0,
	output reg [COST_W-1:0] rdata0,
	input wire re1,
	input wire [ADDR_W-1:0] raddr1,
	output reg [COST_W-1:0] rdata1
);
	(* ram_style = "block" *) reg [COST_W-1:0] memory [0:DEPTH-1];
	always @(posedge clk) begin
		if (we)
			memory[waddr] <= wdata;
		else if (re0)
			rdata0 <= memory[raddr0];
		if (re1)
			rdata1 <= memory[raddr1];
	end
endmodule

module cap_pdb_dual_port_compact_allocation_engine #(
	parameter integer K = 64,
	parameter integer ORDER = 4,
	parameter integer GROUPS = 13,
	parameter integer STATE_BITS = 5,
	parameter integer COST_W = 16,
	parameter integer ACC_W = 24,
	parameter integer SPLIT = 48,
	parameter integer STATES = (1 << STATE_BITS),
	parameter integer TABLE_DEPTH = (K + 1) * (ORDER + 1) * STATES,
	parameter integer TABLE_ADDR_W = $clog2(TABLE_DEPTH),
	parameter integer GROUP_W = $clog2(GROUPS),
	parameter [COST_W-1:0] INF = {COST_W{1'b1}}
) (
	input wire clk,
	input wire rst,
	input wire table_we,
	input wire [GROUP_W-1:0] table_wgroup,
	input wire [TABLE_ADDR_W-1:0] table_waddr,
	input wire [COST_W-1:0] table_wdata,
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
	localparam [1:0] IDLE = 2'd0, RUN = 2'd1, DRAIN = 2'd2;
	reg [1:0] phase;
	reg pipeline_valid;
	reg [STATES-1:0] remaining [0:GROUPS-1];
	reg [COST_W-1:0] group_minimum [0:GROUPS-1];
	reg [GROUPS-1:0] issued0;
	reg [GROUPS-1:0] issued1;
	wire [GROUPS*COST_W-1:0] read0_bus;
	wire [GROUPS*COST_W-1:0] read1_bus;
	wire [GROUPS*COST_W-1:0] previous_batch_minimum_bus;

	function automatic [STATE_BITS-1:0] first_effect;
		input [STATES-1:0] mask;
		integer bit_index;
		reg found;
		begin
			first_effect = {STATE_BITS{1'b0}};
			found = 1'b0;
			for (bit_index = 0; bit_index < STATES;
				bit_index = bit_index + 1)
				if (mask[bit_index] && !found) begin
					first_effect = bit_index[STATE_BITS-1:0];
					found = 1'b1;
				end
		end
	endfunction

	function automatic [COST_W-1:0] batch_minimum;
		input [COST_W-1:0] value0;
		input [COST_W-1:0] value1;
		input valid0;
		input valid1;
		begin
			if (valid0 && valid1)
				batch_minimum = value0 < value1 ? value0 : value1;
			else if (valid0)
				batch_minimum = value0;
			else if (valid1)
				batch_minimum = value1;
			else
				batch_minimum = INF;
		end
	endfunction

	wire [TABLE_ADDR_W-1:0] read_base =
		(SPLIT * (ORDER + 1) + right_weight) * STATES;
	wire [GROUPS-1:0] current_valid0;
	wire [GROUPS-1:0] current_valid1;
	wire [GROUPS*STATES-1:0] after_first_bus;
	wire [GROUPS*STATES-1:0] after_second_bus;

	genvar bank;
	generate
		for (bank = 0; bank < GROUPS; bank = bank + 1) begin: g_bank
			wire [STATES-1:0] after_first =
				remaining[bank] & (remaining[bank] - 1'b1);
			wire [STATES-1:0] after_second =
				after_first & (after_first - 1'b1);
			wire [STATE_BITS-1:0] effect0 =
				first_effect(remaining[bank]);
			wire [STATE_BITS-1:0] effect1 =
				first_effect(after_first);
			wire [STATE_BITS-1:0] target0 =
				group_state[bank*STATE_BITS +: STATE_BITS] ^ effect0;
			wire [STATE_BITS-1:0] target1 =
				group_state[bank*STATE_BITS +: STATE_BITS] ^ effect1;
			assign current_valid0[bank] = |remaining[bank];
			assign current_valid1[bank] = |after_first;
			assign after_first_bus[bank*STATES +: STATES] = after_first;
			assign after_second_bus[bank*STATES +: STATES] = after_second;
			assign previous_batch_minimum_bus[bank*COST_W +: COST_W] =
				batch_minimum(
					read0_bus[bank*COST_W +: COST_W],
					read1_bus[bank*COST_W +: COST_W],
					issued0[bank], issued1[bank]
				);

			cap_pdb_dual_read_bank #(
				.DEPTH(TABLE_DEPTH),
				.ADDR_W(TABLE_ADDR_W),
				.COST_W(COST_W)
			) table_bank (
				.clk(clk),
				.we(table_we && table_wgroup == bank),
				.waddr(table_waddr), .wdata(table_wdata),
				.re0(phase == RUN && current_valid0[bank]),
				.raddr0(read_base + target0),
				.rdata0(read0_bus[bank*COST_W +: COST_W]),
				.re1(phase == RUN && current_valid1[bank]),
				.raddr1(read_base + target1),
				.rdata1(read1_bus[bank*COST_W +: COST_W])
			);
		end
	endgenerate

	integer index;
	reg all_after_empty;
	reg [ACC_W-1:0] final_sum;
	reg [COST_W-1:0] selected_minimum;
	always @* begin
		all_after_empty = 1'b1;
		final_sum = {ACC_W{1'b0}};
		selected_minimum = INF;
		for (index = 0; index < GROUPS; index = index + 1) begin
			if (|after_second_bus[index*STATES +: STATES])
				all_after_empty = 1'b0;
			selected_minimum = group_minimum[index];
			if (
				previous_batch_minimum_bus[index*COST_W +: COST_W] <
				selected_minimum
			)
				selected_minimum =
					previous_batch_minimum_bus[index*COST_W +: COST_W];
			final_sum = final_sum + selected_minimum;
		end
	end

	always @(posedge clk) begin
		if (rst) begin
			phase <= IDLE;
			pipeline_valid <= 1'b0;
			busy <= 1'b0;
			done <= 1'b0;
			valid <= 1'b0;
			group_cost_sum <= {ACC_W{1'b0}};
			cycles <= 16'd0;
			issued0 <= {GROUPS{1'b0}};
			issued1 <= {GROUPS{1'b0}};
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
				pipeline_valid <= 1'b0;
				for (index = 0; index < GROUPS; index = index + 1) begin
					remaining[index] <=
						reachable_effects[index*STATES +: STATES];
					group_minimum[index] <= INF;
					if (!(|reachable_effects[index*STATES +: STATES]))
						valid <= 1'b0;
				end
				phase <= RUN;
			end
			RUN: begin
				for (index = 0; index < GROUPS; index = index + 1) begin
					if (pipeline_valid) begin
						if (
							previous_batch_minimum_bus[
								index*COST_W +: COST_W
							] < group_minimum[index]
						)
							group_minimum[index] <=
								previous_batch_minimum_bus[
									index*COST_W +: COST_W
								];
					end
					remaining[index] <=
						after_second_bus[index*STATES +: STATES];
				end
				issued0 <= current_valid0;
				issued1 <= current_valid1;
				pipeline_valid <= 1'b1;
				if (all_after_empty)
					phase <= DRAIN;
			end
			DRAIN: begin
				group_cost_sum <= final_sum;
				busy <= 1'b0;
				done <= 1'b1;
				phase <= IDLE;
			end
			default: phase <= IDLE;
			endcase
		end
	end
endmodule
