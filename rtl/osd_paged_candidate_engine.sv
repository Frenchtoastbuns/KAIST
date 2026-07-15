module osd_paged_candidate_engine #(
	parameter integer K = 64,
	parameter integer R = 63,
	parameter integer P = 8,
	parameter integer LLR_W = 8,
	parameter integer SCORE_W = 24
) (
	input  wire [K-1:0] boundary_mask,
	input  wire [R-1:0] boundary_parity,
	input  wire [K*R-1:0] parity_rows_flat,
	input  wire [P*K-1:0] tep_masks_flat,
	input  wire [$clog2(P+1)-1:0] valid_lanes,
	input  wire [R*LLR_W-1:0] parity_llrs_flat,
	input  wire signed [SCORE_W-1:0] systematic_base_score,
	input  wire [K*SCORE_W-1:0] systematic_deltas_flat,
	output wire [P*R-1:0] candidates_flat,
	output wire [P*SCORE_W-1:0] scores_flat,
	output wire [K-1:0] next_boundary_mask,
	output wire [R-1:0] next_boundary_parity
);

	osd_prefix_delta_page #(
		.K(K),
		.R(R),
		.P(P)
	) expand (
		.boundary_mask(boundary_mask),
		.boundary_parity(boundary_parity),
		.parity_rows_flat(parity_rows_flat),
		.tep_masks_flat(tep_masks_flat),
		.valid_lanes(valid_lanes),
		.candidates_flat(candidates_flat),
		.next_boundary_mask(next_boundary_mask),
		.next_boundary_parity(next_boundary_parity)
	);

	osd_page_score #(
		.K(K),
		.R(R),
		.P(P),
		.LLR_W(LLR_W),
		.SCORE_W(SCORE_W)
	) score (
		.tep_masks_flat(tep_masks_flat),
		.candidates_flat(candidates_flat),
		.valid_lanes(valid_lanes),
		.parity_llrs_flat(parity_llrs_flat),
		.systematic_base_score(systematic_base_score),
		.systematic_deltas_flat(systematic_deltas_flat),
		.scores_flat(scores_flat)
	);

endmodule
