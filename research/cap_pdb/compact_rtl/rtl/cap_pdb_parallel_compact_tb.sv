`timescale 1ns/1ps

module cap_pdb_parallel_compact_tb #(
	parameter integer LANES = 1
) ();
	reg clk = 1'b0;
	reg rst = 1'b1;
	always #5 clk = ~clk;
	initial begin
		#17 rst = 1'b0;
	end
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
	localparam integer EXPECTED_CYCLES = 1 + (3 + LANES - 1) / LANES;

	reg [2:0] phase;
	localparam [2:0] LOAD = 3'd0, START = 3'd1, WAIT_DONE = 3'd2,
		FINISHED = 3'd3;
	integer load_group;
	integer load_row;
	integer state;
	integer age;
	reg start;
	reg passed;
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

	always @* begin
		table_wdata = {WORD_W{1'b0}};
		for (state = 0; state < STATES; state = state + 1)
			// cost = 3*group + 2*weight + state + 1
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
		.start(start), .right_weight(2'd2),
		.group_state(group_state), .reachable_effects(reachable_effects),
		.busy(busy), .done(done), .valid(valid),
		.group_cost_sum(group_cost_sum), .cycles(cycles)
	);

	always @(posedge clk) begin
		if (rst) begin
			phase <= LOAD;
			load_group <= 0;
			load_row <= 0;
			age <= 0;
			start <= 1'b0;
			passed <= 1'b0;
			group_state <= {GROUPS*STATE_BITS{1'b0}};
			group_state[0*STATE_BITS +: STATE_BITS] <= 5'd1;
			group_state[1*STATE_BITS +: STATE_BITS] <= 5'd2;
			group_state[2*STATE_BITS +: STATE_BITS] <= 5'd3;
			reachable_effects <= {GROUPS*STATES{1'b0}};
			reachable_effects[0*STATES +: STATES] <=
				(32'd1 << 0) | (32'd1 << 1) | (32'd1 << 3);
			reachable_effects[1*STATES +: STATES] <=
				(32'd1 << 1) | (32'd1 << 2);
			reachable_effects[2*STATES +: STATES] <=
				(32'd1 << 2) | (32'd1 << 3);
		end else begin
			age <= age + 1;
			start <= 1'b0;
			case (phase)
			LOAD: begin
				if (load_row == TABLE_ROWS-1) begin
					load_row <= 0;
					if (load_group == GROUPS-1)
						phase <= START;
					else
						load_group <= load_group + 1;
				end else begin
					load_row <= load_row + 1;
				end
			end
			START: begin
				start <= 1'b1;
				phase <= WAIT_DONE;
			end
			WAIT_DONE: if (done) begin
				if (!valid)
					$fatal(1, "compact result unexpectedly invalid");
				if (group_cost_sum != 16'd24)
					$fatal(
						1, "wrong compact sum: got %0d expected 24",
						group_cost_sum
					);
				if (cycles != EXPECTED_CYCLES)
					$fatal(
						1, "wrong cycle count: got %0d expected %0d",
						cycles, EXPECTED_CYCLES
					);
				passed <= 1'b1;
				phase <= FINISHED;
			end
			default: phase <= FINISHED;
			endcase
			if (age == 130) begin
				if (!passed)
					$fatal(1, "test did not complete");
				$display(
					"PASS lanes=%0d sum=%0d cycles=%0d",
					LANES, group_cost_sum, cycles
				);
				$finish;
			end
		end
	end
endmodule
