`timescale 1ns/1ps

module cap_pdb_parallel_compact_random_tb #(
	parameter integer LANES = 1,
	parameter integer QUERIES = 64
) ();
	localparam integer K = 8;
	localparam integer ORDER = 3;
	localparam integer GROUPS = 3;
	localparam integer STATE_BITS = 5;
	localparam integer STATES = 32;
	localparam integer COST_W = 8;
	localparam integer ACC_W = 16;
	localparam integer SPLIT = 6;
	localparam integer TABLE_ROWS = (K+1)*(ORDER+1);
	localparam integer ROW_W = $clog2(TABLE_ROWS);
	localparam integer GROUP_W = $clog2(GROUPS);
	localparam integer WORD_W = STATES*COST_W;

	reg clk = 1'b0;
	reg rst = 1'b1;
	always #5 clk = ~clk;
	initial #17 rst = 1'b0;

	localparam [2:0] LOAD = 3'd0, PREPARE = 3'd1, START = 3'd2,
		WAIT_DONE = 3'd3, FINISHED = 3'd4;
	reg [2:0] phase;
	integer load_group;
	integer load_row;
	integer state;
	integer query;
	integer group;
	integer effect;
	integer population;
	integer maximum_population;
	integer expected_group;
	integer expected_sum;
	reg [31:0] lfsr;
	reg start;
	reg [$clog2(ORDER+1)-1:0] right_weight;
	reg [GROUPS*STATE_BITS-1:0] group_state;
	reg [GROUPS*STATES-1:0] reachable_effects;
	reg [WORD_W-1:0] table_wdata;

	wire table_we = phase == LOAD;
	wire [GROUP_W-1:0] table_wgroup = load_group[GROUP_W-1:0];
	wire [ROW_W-1:0] table_wrow = load_row[ROW_W-1:0];
	wire busy;
	wire done;
	wire valid;
	wire [ACC_W-1:0] group_cost_sum;
	wire [15:0] cycles;

	function automatic [31:0] lfsr_next;
		input [31:0] value;
		begin
			lfsr_next = {value[30:0],
				value[31] ^ value[21] ^ value[1] ^ value[0]};
		end
	endfunction

	always @* begin
		table_wdata = {WORD_W{1'b0}};
		for (state = 0; state < STATES; state = state + 1)
			table_wdata[state*COST_W +: COST_W] =
				3*load_group +
				2*(load_row % (ORDER+1)) +
				state + 1;
	end

	cap_pdb_parallel_compact_allocation_engine #(
		.K(K), .ORDER(ORDER), .GROUPS(GROUPS),
		.STATE_BITS(STATE_BITS), .COST_W(COST_W), .ACC_W(ACC_W),
		.SPLIT(SPLIT), .LANES(LANES)
	) dut (
		.clk(clk), .rst(rst),
		.table_we(table_we), .table_wgroup(table_wgroup),
		.table_wrow(table_wrow), .table_wdata(table_wdata),
		.start(start), .right_weight(right_weight),
		.group_state(group_state), .reachable_effects(reachable_effects),
		.busy(busy), .done(done), .valid(valid),
		.group_cost_sum(group_cost_sum), .cycles(cycles)
	);

	always @(posedge clk) begin
		if (rst) begin
			phase <= LOAD;
			load_group <= 0;
			load_row <= 0;
			query <= 0;
			lfsr <= 32'h6d5a56e9;
			start <= 1'b0;
			right_weight <= 0;
			group_state <= 0;
			reachable_effects <= 0;
		end else begin
			start <= 1'b0;
			case (phase)
			LOAD: begin
				if (load_row == TABLE_ROWS-1) begin
					load_row <= 0;
					if (load_group == GROUPS-1)
						phase <= PREPARE;
					else
						load_group <= load_group + 1;
				end else begin
					load_row <= load_row + 1;
				end
			end
			PREPARE: begin
				right_weight <= lfsr[1:0];
				group_state[0*STATE_BITS +: STATE_BITS] <= lfsr[6:2];
				group_state[1*STATE_BITS +: STATE_BITS] <= lfsr[11:7];
				group_state[2*STATE_BITS +: STATE_BITS] <= lfsr[16:12];
				reachable_effects[0*STATES +: STATES] <= lfsr | 32'h1;
				reachable_effects[1*STATES +: STATES] <=
					{lfsr[15:0], lfsr[31:16]} | 32'h2;
				reachable_effects[2*STATES +: STATES] <=
					{lfsr[7:0], lfsr[31:8]} | 32'h4;
				lfsr <= lfsr_next(lfsr);
				phase <= START;
			end
			START: begin
				start <= 1'b1;
				phase <= WAIT_DONE;
			end
			WAIT_DONE: if (done) begin
				if (!valid)
					$fatal(1, "random compact result unexpectedly invalid");
				expected_sum = 0;
				maximum_population = 0;
				for (group = 0; group < GROUPS; group = group + 1) begin
					expected_group = 255;
					population = 0;
					for (effect = 0; effect < STATES;
						effect = effect + 1) begin
						if (reachable_effects[group*STATES + effect]) begin
							population = population + 1;
							if (
								3*group + 2*right_weight +
								(group_state[
									group*STATE_BITS +: STATE_BITS
								] ^ effect) + 1 < expected_group
							)
								expected_group =
									3*group + 2*right_weight +
									(group_state[
										group*STATE_BITS +: STATE_BITS
									] ^ effect) + 1;
						end
					end
					if (population > maximum_population)
						maximum_population = population;
					expected_sum = expected_sum + expected_group;
				end
				if (group_cost_sum != expected_sum)
					$fatal(
						1,
						"query %0d lanes %0d: sum %0d expected %0d",
						query, LANES, group_cost_sum, expected_sum
					);
				if (
					cycles !=
					1 + (maximum_population + LANES - 1) / LANES
				)
					$fatal(
						1,
						"query %0d lanes %0d: cycles %0d expected %0d",
						query, LANES, cycles,
						1 + (maximum_population + LANES - 1) / LANES
					);
				if (query == QUERIES-1)
					phase <= FINISHED;
				else begin
					query <= query + 1;
					phase <= PREPARE;
				end
			end
			FINISHED: begin
				$display(
					"PASS random lanes=%0d queries=%0d",
					LANES, QUERIES
				);
				$finish;
			end
			default: phase <= FINISHED;
			endcase
		end
	end
endmodule
