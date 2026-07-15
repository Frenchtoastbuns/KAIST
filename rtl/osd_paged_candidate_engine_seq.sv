module osd_paged_candidate_engine_seq #(
	parameter integer K = 64,
	parameter integer R = 63,
	parameter integer P = 8,
	parameter integer LLR_W = 8,
	parameter integer SCORE_W = 24
) (
	input  wire clk,
	input  wire reset,
	input  wire start,
	output reg  busy,
	output reg  done,
	input  wire [K-1:0] boundary_mask,
	input  wire [R-1:0] boundary_parity,
	input  wire [K*R-1:0] parity_rows_flat,
	input  wire [P*K-1:0] tep_masks_flat,
	input  wire [$clog2(P+1)-1:0] valid_lanes,
	input  wire [R*LLR_W-1:0] parity_llrs_flat,
	input  wire signed [SCORE_W-1:0] systematic_base_score,
	input  wire [K*SCORE_W-1:0] systematic_deltas_flat,
	output reg  [P*R-1:0] candidates_flat,
	output reg  [P*SCORE_W-1:0] scores_flat,
	output reg  [K-1:0] next_boundary_mask,
	output reg  [R-1:0] next_boundary_parity
);

	localparam integer ROW_W = K <= 1 ? 1 : $clog2(K);
	localparam integer PARITY_W = R <= 1 ? 1 : $clog2(R);
	localparam integer VALID_W = $clog2(P+1);
	localparam [1:0] IDLE = 2'd0;
	localparam [1:0] BUILD = 2'd1;
	localparam [1:0] EXPAND = 2'd2;
	localparam [1:0] SCORE = 2'd3;

	reg [1:0] state;
	reg [ROW_W-1:0] row_index;
	reg [PARITY_W-1:0] parity_index;
	reg [VALID_W-1:0] valid_lanes_reg;
	reg [P*R-1:0] edge_deltas_flat;
	reg [P*SCORE_W-1:0] score_accumulators_flat;

	integer lane;
	reg [K-1:0] lane_mask;
	reg [K-1:0] previous_lane_mask;
	reg [R-1:0] selected_parity_row;
	reg signed [SCORE_W-1:0] selected_systematic_delta;
	reg signed [LLR_W-1:0] selected_llr;
	reg signed [SCORE_W-1:0] score_contribution;
	reg [R-1:0] prefix_temp;
	reg [R-1:0] candidate_temp;

	always @(posedge clk) begin
		if (reset) begin
			state <= IDLE;
			busy <= 1'b0;
			done <= 1'b0;
			row_index <= {ROW_W{1'b0}};
			parity_index <= {PARITY_W{1'b0}};
			valid_lanes_reg <= {VALID_W{1'b0}};
			edge_deltas_flat <= {(P*R){1'b0}};
			score_accumulators_flat <= {(P*SCORE_W){1'b0}};
			candidates_flat <= {(P*R){1'b0}};
			scores_flat <= {(P*SCORE_W){1'b0}};
			next_boundary_mask <= {K{1'b0}};
			next_boundary_parity <= {R{1'b0}};
		end else begin
			done <= 1'b0;
			case (state)
				IDLE: begin
					if (start) begin
						busy <= 1'b1;
						valid_lanes_reg <= valid_lanes;
						row_index <= {ROW_W{1'b0}};
						edge_deltas_flat <= {(P*R){1'b0}};
						candidates_flat <= {(P*R){1'b0}};
						scores_flat <= {(P*SCORE_W){1'b0}};
						next_boundary_mask <= boundary_mask;
						next_boundary_parity <= boundary_parity;
						for (lane = 0; lane < P; lane = lane + 1)
							score_accumulators_flat[
								lane*SCORE_W +: SCORE_W
							] <= systematic_base_score;
						state <= BUILD;
					end
				end

				BUILD: begin
					selected_parity_row =
						parity_rows_flat[row_index*R +: R];
					selected_systematic_delta = $signed(
						systematic_deltas_flat[
							row_index*SCORE_W +: SCORE_W
						]
					);
					for (lane = 0; lane < P; lane = lane + 1) begin
						if (lane < valid_lanes_reg) begin
							lane_mask = tep_masks_flat[lane*K +: K];
							if (lane == 0)
								previous_lane_mask = boundary_mask;
							else
								previous_lane_mask =
									tep_masks_flat[(lane-1)*K +: K];

							if (
								lane_mask[row_index] ^
								previous_lane_mask[row_index]
							)
								edge_deltas_flat[
									lane*R +: R
								] <= edge_deltas_flat[
									lane*R +: R
								] ^ selected_parity_row;

							if (lane_mask[row_index])
								score_accumulators_flat[
									lane*SCORE_W +: SCORE_W
								] <= $signed(
									score_accumulators_flat[
										lane*SCORE_W +: SCORE_W
									]
								) + selected_systematic_delta;
						end
					end

					if (row_index == K-1) begin
						state <= EXPAND;
					end else begin
						row_index <= row_index + 1'b1;
					end
				end

				EXPAND: begin
					prefix_temp = {R{1'b0}};
					candidate_temp = boundary_parity;
					for (lane = 0; lane < P; lane = lane + 1) begin
						if (lane < valid_lanes_reg) begin
							prefix_temp = prefix_temp ^
								edge_deltas_flat[lane*R +: R];
							candidate_temp = boundary_parity ^ prefix_temp;
							candidates_flat[lane*R +: R] <=
								candidate_temp;
							next_boundary_mask <=
								tep_masks_flat[lane*K +: K];
							next_boundary_parity <= candidate_temp;
						end
					end
					parity_index <= {PARITY_W{1'b0}};
					state <= SCORE;
				end

				SCORE: begin
					selected_llr = $signed(
						parity_llrs_flat[
							parity_index*LLR_W +: LLR_W
						]
					);
					for (lane = 0; lane < P; lane = lane + 1) begin
						if (lane < valid_lanes_reg) begin
							if (candidates_flat[lane*R + parity_index])
								score_contribution = -selected_llr;
							else
								score_contribution = selected_llr;

							if (parity_index == R-1)
								scores_flat[
									lane*SCORE_W +: SCORE_W
								] <= $signed(
									score_accumulators_flat[
										lane*SCORE_W +: SCORE_W
									]
								) + score_contribution;
							else
								score_accumulators_flat[
									lane*SCORE_W +: SCORE_W
								] <= $signed(
									score_accumulators_flat[
										lane*SCORE_W +: SCORE_W
									]
								) + score_contribution;
						end
					end

					if (parity_index == R-1) begin
						busy <= 1'b0;
						done <= 1'b1;
						state <= IDLE;
					end else begin
						parity_index <= parity_index + 1'b1;
					end
				end
			endcase
		end
	end

endmodule
