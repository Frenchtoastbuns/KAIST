module osd_prefix_delta_page #(
	parameter integer K = 64,
	parameter integer R = 63,
	parameter integer P = 8
) (
	input  wire [K-1:0] boundary_mask,
	input  wire [R-1:0] boundary_parity,
	input  wire [K*R-1:0] parity_rows_flat,
	input  wire [P*K-1:0] tep_masks_flat,
	output reg  [P*R-1:0] candidates_flat,
	output reg  [K-1:0] next_boundary_mask,
	output reg  [R-1:0] next_boundary_parity
);

	integer lane;
	integer row;
	reg [K-1:0] previous_mask;
	reg [K-1:0] edge_mask;
	reg [R-1:0] edge_delta;
	reg [R-1:0] prefix_delta;

	always @* begin
		candidates_flat = {(P*R){1'b0}};
		previous_mask = boundary_mask;
		prefix_delta = {R{1'b0}};
		edge_mask = {K{1'b0}};
		edge_delta = {R{1'b0}};

		for (lane = 0; lane < P; lane = lane + 1) begin
			edge_mask =
				tep_masks_flat[lane*K +: K] ^ previous_mask;
			edge_delta = {R{1'b0}};
			for (row = 0; row < K; row = row + 1)
				if (edge_mask[row])
					edge_delta = edge_delta ^
						parity_rows_flat[row*R +: R];
			prefix_delta = prefix_delta ^ edge_delta;
			candidates_flat[lane*R +: R] =
				boundary_parity ^ prefix_delta;
			previous_mask = tep_masks_flat[lane*K +: K];
		end

		next_boundary_mask = previous_mask;
		next_boundary_parity = boundary_parity ^ prefix_delta;
	end

endmodule
