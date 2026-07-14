module osd_page_score #(
	parameter integer K = 64,
	parameter integer R = 63,
	parameter integer P = 8,
	parameter integer LLR_W = 8,
	parameter integer SCORE_W = 24
) (
	input  wire [P*K-1:0] tep_masks_flat,
	input  wire [P*R-1:0] candidates_flat,
	input  wire [$clog2(P+1)-1:0] valid_lanes,
	input  wire [R*LLR_W-1:0] parity_llrs_flat,
	input  wire signed [SCORE_W-1:0] systematic_base_score,
	input  wire [K*SCORE_W-1:0] systematic_deltas_flat,
	output reg  [P*SCORE_W-1:0] scores_flat
);

	integer lane;
	integer index;
	reg signed [SCORE_W-1:0] accumulator;
	reg signed [LLR_W-1:0] llr_value;

	always @* begin
		scores_flat = {(P*SCORE_W){1'b0}};
		accumulator = {SCORE_W{1'b0}};
		llr_value = {LLR_W{1'b0}};

		for (lane = 0; lane < P; lane = lane + 1) begin
			if (lane < valid_lanes) begin
				accumulator = systematic_base_score;
				for (index = 0; index < K; index = index + 1)
					if (tep_masks_flat[lane*K + index])
						accumulator = accumulator +
							$signed(
								systematic_deltas_flat[
									index*SCORE_W +: SCORE_W
								]
							);

				for (index = 0; index < R; index = index + 1) begin
					llr_value = $signed(
						parity_llrs_flat[index*LLR_W +: LLR_W]
					);
					if (candidates_flat[lane*R + index])
						accumulator = accumulator - llr_value;
					else
						accumulator = accumulator + llr_value;
				end
				scores_flat[lane*SCORE_W +: SCORE_W] = accumulator;
			end
		end
	end

endmodule
