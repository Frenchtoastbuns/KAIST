`timescale 1ns/1ps

module cap_pdb_dual_port_compact_tb;
	localparam integer K = 8;
	localparam integer ORDER = 3;
	localparam integer GROUPS = 3;
	localparam integer STATE_BITS = 5;
	localparam integer STATES = 32;
	localparam integer COST_W = 8;
	localparam integer ACC_W = 16;
	localparam integer SPLIT = 6;
	localparam integer TABLE_DEPTH = (K+1)*(ORDER+1)*STATES;
	localparam integer ADDR_W = $clog2(TABLE_DEPTH);
	localparam integer GROUP_W = $clog2(GROUPS);

	reg clk = 1'b0;
	reg rst = 1'b1;
	always #5 clk = ~clk;
	initial #17 rst = 1'b0;

	reg [2:0] phase;
	localparam [2:0] LOAD = 3'd0, START = 3'd1, WAIT_DONE = 3'd2,
		FINISHED = 3'd3;
	integer load_group;
	integer load_address;
	integer age;
	reg start;
	reg passed;
	reg [GROUPS*STATE_BITS-1:0] group_state;
	reg [GROUPS*STATES-1:0] reachable_effects;

	wire table_we = phase == LOAD;
	wire [GROUP_W-1:0] table_wgroup = load_group[GROUP_W-1:0];
	wire [ADDR_W-1:0] table_waddr = load_address[ADDR_W-1:0];
	wire [COST_W-1:0] table_wdata =
		3*load_group +
		2*((load_address / STATES) % (ORDER+1)) +
		(load_address % STATES) + 1;
	wire busy;
	wire done;
	wire valid;
	wire [ACC_W-1:0] group_cost_sum;
	wire [15:0] cycles;

	cap_pdb_dual_port_compact_allocation_engine #(
		.K(K), .ORDER(ORDER), .GROUPS(GROUPS),
		.STATE_BITS(STATE_BITS), .COST_W(COST_W), .ACC_W(ACC_W),
		.SPLIT(SPLIT)
	) dut (
		.clk(clk), .rst(rst),
		.table_we(table_we), .table_wgroup(table_wgroup),
		.table_waddr(table_waddr), .table_wdata(table_wdata),
		.start(start), .right_weight(2'd2),
		.group_state(group_state), .reachable_effects(reachable_effects),
		.busy(busy), .done(done), .valid(valid),
		.group_cost_sum(group_cost_sum), .cycles(cycles)
	);

	always @(posedge clk) begin
		if (rst) begin
			phase <= LOAD;
			load_group <= 0;
			load_address <= 0;
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
				if (load_address == TABLE_DEPTH-1) begin
					load_address <= 0;
					if (load_group == GROUPS-1)
						phase <= START;
					else
						load_group <= load_group + 1;
				end else begin
					load_address <= load_address + 1;
				end
			end
			START: begin
				start <= 1'b1;
				phase <= WAIT_DONE;
			end
			WAIT_DONE: if (done) begin
				if (!valid)
					$fatal(1, "dual-port result unexpectedly invalid");
				if (group_cost_sum != 16'd24)
					$fatal(
						1, "wrong dual-port sum: got %0d expected 24",
						group_cost_sum
					);
				if (cycles != 16'd3)
					$fatal(
						1, "wrong dual-port cycles: got %0d expected 3",
						cycles
					);
				passed <= 1'b1;
				phase <= FINISHED;
			end
			default: phase <= FINISHED;
			endcase
			if (age == 3500) begin
				if (!passed)
					$fatal(1, "dual-port test did not complete");
				$display(
					"PASS dual_port lanes=2 sum=%0d cycles=%0d",
					group_cost_sum, cycles
				);
				$finish;
			end
		end
	end
endmodule
