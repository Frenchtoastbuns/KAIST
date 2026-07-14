module tb_osd_paged_candidate_engine;

	localparam integer K = 5;
	localparam integer R = 10;
	localparam integer P = 4;
	localparam integer LLR_W = 8;
	localparam integer SCORE_W = 24;

	reg [K-1:0] boundary_mask;
	reg [R-1:0] boundary_parity;
	reg [K*R-1:0] parity_rows_flat;
	reg [P*K-1:0] tep_masks_flat;
	reg [R*LLR_W-1:0] parity_llrs_flat;
	reg signed [SCORE_W-1:0] systematic_base_score;
	reg [K*SCORE_W-1:0] systematic_deltas_flat;

	wire [P*R-1:0] candidates_flat;
	wire [P*SCORE_W-1:0] scores_flat;
	wire [K-1:0] next_boundary_mask;
	wire [R-1:0] next_boundary_parity;

	reg [P*R-1:0] expected_candidates_flat;
	reg [P*SCORE_W-1:0] expected_scores_flat;
	reg [K-1:0] expected_next_mask;
	reg [R-1:0] expected_next_parity;

	integer vectors;
	integer fields;
	integer cases;
	reg [1023:0] vector_path;

	osd_paged_candidate_engine #(
		.K(K),
		.R(R),
		.P(P),
		.LLR_W(LLR_W),
		.SCORE_W(SCORE_W)
	) dut (
		.boundary_mask(boundary_mask),
		.boundary_parity(boundary_parity),
		.parity_rows_flat(parity_rows_flat),
		.tep_masks_flat(tep_masks_flat),
		.parity_llrs_flat(parity_llrs_flat),
		.systematic_base_score(systematic_base_score),
		.systematic_deltas_flat(systematic_deltas_flat),
		.candidates_flat(candidates_flat),
		.scores_flat(scores_flat),
		.next_boundary_mask(next_boundary_mask),
		.next_boundary_parity(next_boundary_parity)
	);

	initial begin
		if (!$value$plusargs("VECTORS=%s", vector_path))
			$fatal(1, "missing +VECTORS=path");
		vectors = $fopen(vector_path, "r");
		if (!vectors)
			$fatal(1, "could not open vector file");

		cases = 0;
		while (!$feof(vectors)) begin
			fields = $fscanf(
				vectors,
				"%h %h %h %h %h %h %h %h %h %h %h\n",
				boundary_mask,
				boundary_parity,
				parity_rows_flat,
				tep_masks_flat,
				expected_candidates_flat,
				expected_next_mask,
				expected_next_parity,
				parity_llrs_flat,
				systematic_base_score,
				systematic_deltas_flat,
				expected_scores_flat
			);
			if (fields == 11) begin
				#1;
				if (candidates_flat !== expected_candidates_flat)
					$fatal(1, "candidate mismatch at case %0d", cases);
				if (next_boundary_mask !== expected_next_mask)
					$fatal(1, "next mask mismatch at case %0d", cases);
				if (next_boundary_parity !== expected_next_parity)
					$fatal(1, "next parity mismatch at case %0d", cases);
				if (scores_flat !== expected_scores_flat)
					$fatal(1, "score mismatch at case %0d", cases);
				cases = cases + 1;
			end else if (fields != -1) begin
				$fatal(1, "malformed vector line after case %0d", cases);
			end
		end

		$fclose(vectors);
		if (cases != 128)
			$fatal(1, "expected 128 cases, observed %0d", cases);
		$display("RTL_PAGE_PASS cases=%0d K=%0d R=%0d P=%0d", cases, K, R, P);
		$finish;
	end

endmodule
