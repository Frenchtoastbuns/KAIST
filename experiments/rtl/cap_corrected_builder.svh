    // ------------------------------------------------------------------
    // Builder scratch banks and corrected right-to-allocation expansion.
    // ------------------------------------------------------------------
    reg scratch_old_sel;
    reg scratch_init_we;
    reg [SCRATCH_AW-1:0] scratch_init_addr;
    reg [SCRATCH_AW-1:0] scratch_rd_a [0:12];
    reg [SCRATCH_AW-1:0] scratch_rd_b [0:12];
    wire [9:0] scratch_q_a [0:12];
    wire [9:0] scratch_q_b [0:12];
    reg scratch_new_we;
    reg [SCRATCH_AW-1:0] scratch_new_addr;
    reg [9:0] scratch_new_data [0:12];

    generate
        for (gg = 0; gg < 13; gg = gg + 1) begin: scratch
            cap_scratch_pair #(
                .DEPTH(SCRATCH_DEPTH),
                .ADDR_W(SCRATCH_AW),
                .DATA_W(10)
            ) pair (
                .clk(clk), .old_sel(scratch_old_sel),
                .init_we(scratch_init_we),
                .init_addr(scratch_init_addr),
                .init_data(
                    (gg == 12 && scratch_init_addr[4:0] >= 16) ? INF10 :
                    ((scratch_init_addr < 32) ?
                        phi_cost[gg][scratch_init_addr[4:0]] : INF10)
                ),
                .rd_addr_a(scratch_rd_a[gg]),
                .rd_addr_b(scratch_rd_b[gg]),
                .rd_data_a(scratch_q_a[gg]),
                .rd_data_b(scratch_q_b[gg]),
                .new_we(scratch_new_we),
                .new_addr(scratch_new_addr),
                .new_data(scratch_new_data[gg])
            );
        end
    endgenerate

    localparam [3:0]
        BP_IDLE       = 0,
        BP_INIT       = 1,
        BP_RIGHT      = 2,
        BP_COPY_RIGHT = 3,
        BP_COPY_BASE  = 4,
        BP_EXPAND     = 5,
        BP_LEFT       = 6,
        BP_WAIT       = 7,
        BP_DONE       = 8;

    reg [3:0] bphase;
    reg [5:0] brank;
    reg [8:0] bcell;
    reg [2:0] bcopy_weight;
    reg [4:0] bcopy_state;

    reg pipe_valid;
    reg pipe_last;
    reg [3:0] pipe_phase;
    reg [SCRATCH_AW-1:0] pipe_dest;
    reg [4:0] pipe_state;
    reg [3:0] pipe_alloc;
    reg pipe_skip_valid;
    reg pipe_take_valid;
    reg pipe_store_main;
    reg [ROW_W-1:0] pipe_main_row;

    wire [2:0] bweight = bcell[7:5];
    wire [4:0] bstate = bcell[4:0];
    wire [3:0] balloc = bcell[8:5];
    wire [2:0] bleft = alloc_left(balloc);
    wire [2:0] btotal = alloc_weight(balloc);
    wire [2:0] bright = btotal - bleft;
    wire [2:0] max_copy_weight =
        ((K - brank) < O) ? (K - brank) : O;
    integer bg;

    always @* begin
        scratch_init_we = 1'b0;
        scratch_init_addr = bcell;
        scratch_new_we = 1'b0;
        scratch_new_addr = pipe_dest;
        table_we = 1'b0;
        table_wrow = pipe_main_row;
        table_wstate = pipe_state;
        build_value5 = {GROUPS5{INF10}};
        build_value4 = INF9;

        for (bg = 0; bg < 13; bg = bg + 1) begin
            scratch_rd_a[bg] = 0;
            scratch_rd_b[bg] = 0;
            scratch_new_data[bg] = INF10;
        end

        if (bphase == BP_INIT)
            scratch_init_we = 1'b1;

        if (bphase == BP_RIGHT) begin
            for (bg = 0; bg < 13; bg = bg + 1) begin
                scratch_rd_a[bg] = bcell;
                scratch_rd_b[bg] = (bweight == 0) ? bcell :
                    ((bweight - 1) * 32 +
                     (bstate ^ row_effect[bg][brank]));
            end
        end else if (bphase == BP_COPY_RIGHT ||
                     bphase == BP_COPY_BASE) begin
            for (bg = 0; bg < 13; bg = bg + 1)
                scratch_rd_a[bg] = bcopy_weight * 32 + bcopy_state;
        end else if (bphase == BP_EXPAND) begin
            for (bg = 0; bg < 13; bg = bg + 1)
                scratch_rd_a[bg] = bright * 32 + bstate;
        end else if (bphase == BP_LEFT) begin
            for (bg = 0; bg < 13; bg = bg + 1) begin
                scratch_rd_a[bg] = bcell;
                scratch_rd_b[bg] = (bleft == 0) ? bcell :
                    (alloc_index(bleft - 1, bright) * 32 +
                     (bstate ^ row_effect[bg][brank]));
            end
        end

        if (pipe_valid &&
            (pipe_phase == BP_RIGHT || pipe_phase == BP_LEFT)) begin
            scratch_new_we = 1'b1;
            for (bg = 0; bg < 13; bg = bg + 1) begin
                if (!pipe_skip_valid && !pipe_take_valid)
                    scratch_new_data[bg] = INF10;
                else if (!pipe_skip_valid)
                    scratch_new_data[bg] = scratch_q_b[bg];
                else if (!pipe_take_valid)
                    scratch_new_data[bg] = scratch_q_a[bg];
                else
                    scratch_new_data[bg] =
                        (scratch_q_a[bg] < scratch_q_b[bg]) ?
                        scratch_q_a[bg] : scratch_q_b[bg];
            end
        end else if (pipe_valid && pipe_phase == BP_EXPAND) begin
            scratch_new_we = 1'b1;
            for (bg = 0; bg < 13; bg = bg + 1)
                scratch_new_data[bg] =
                    (alloc_left(pipe_alloc) == 0) ?
                    scratch_q_a[bg] : INF10;
        end

        if (pipe_valid && pipe_store_main) begin
            table_we = 1'b1;
            for (bg = 0; bg < 12; bg = bg + 1) begin
                if (pipe_phase == BP_LEFT)
                    build_value5[bg*10 +: 10] = scratch_new_data[bg];
                else
                    build_value5[bg*10 +: 10] = scratch_q_a[bg];
            end
            if (pipe_phase == BP_LEFT)
                build_value4 = scratch_new_data[12][8:0];
            else
                build_value4 = scratch_q_a[12][8:0];
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            build_busy <= 0;
            build_done <= 0;
            tables_ready <= 0;
            build_cycles <= 0;
            bphase <= BP_IDLE;
            brank <= K - 1;
            bcell <= 0;
            bcopy_weight <= 0;
            bcopy_state <= 0;
            scratch_old_sel <= 0;
            pipe_valid <= 0;
            pipe_last <= 0;
            pipe_phase <= BP_IDLE;
            pipe_dest <= 0;
            pipe_state <= 0;
            pipe_alloc <= 0;
            pipe_skip_valid <= 0;
            pipe_take_valid <= 0;
            pipe_store_main <= 0;
            pipe_main_row <= 0;
        end else begin
            build_done <= 0;
            if (build_start && !build_busy && !decode_busy) begin
                build_busy <= 1;
                tables_ready <= 0;
                build_cycles <= 0;
                bphase <= BP_INIT;
                brank <= K - 1;
                bcell <= 0;
                scratch_old_sel <= 0;
                pipe_valid <= 0;
            end else if (build_busy) begin
                build_cycles <= build_cycles + 1;
                pipe_valid <= 0;

                case (bphase)
                    BP_INIT: begin
                        if (bcell == 159) begin
                            bcell <= 0;
                            bphase <= BP_RIGHT;
                        end else bcell <= bcell + 1;
                    end

                    BP_RIGHT: begin
                        pipe_valid <= 1;
                        pipe_phase <= BP_RIGHT;
                        pipe_dest <= bcell;
                        pipe_state <= bstate;
                        pipe_skip_valid <= 1;
                        pipe_take_valid <= (bweight != 0);
                        pipe_store_main <= 0;
                        pipe_last <= (bcell == 159);
                        if (bcell == 159) bphase <= BP_WAIT;
                        else bcell <= bcell + 1;
                    end

                    BP_COPY_RIGHT, BP_COPY_BASE: begin
                        pipe_valid <= 1;
                        pipe_phase <= bphase;
                        pipe_state <= bcopy_state;
                        pipe_store_main <= 1;
                        pipe_last <=
                            (bcopy_state == 31 &&
                             bcopy_weight ==
                                ((bphase == BP_COPY_BASE) ? O :
                                 max_copy_weight));
                        pipe_main_row <=
                            (bphase == BP_COPY_BASE) ? bcopy_weight :
                            normal_row_id(brank, bcopy_weight);
                        if (bcopy_state == 31) begin
                            bcopy_state <= 0;
                            if (bcopy_weight ==
                                ((bphase == BP_COPY_BASE) ? O :
                                 max_copy_weight)) begin
                                bphase <= BP_WAIT;
                            end else bcopy_weight <= bcopy_weight + 1;
                        end else bcopy_state <= bcopy_state + 1;
                    end

                    BP_EXPAND: begin
                        pipe_valid <= 1;
                        pipe_phase <= BP_EXPAND;
                        pipe_dest <= bcell;
                        pipe_state <= bstate;
                        pipe_alloc <= balloc;
                        pipe_store_main <= 0;
                        pipe_last <= (bcell == 479);
                        if (bcell == 479) bphase <= BP_WAIT;
                        else bcell <= bcell + 1;
                    end

                    BP_LEFT: begin
                        pipe_valid <= 1;
                        pipe_phase <= BP_LEFT;
                        pipe_dest <= bcell;
                        pipe_state <= bstate;
                        pipe_alloc <= balloc;
                        pipe_skip_valid <=
                            (bleft <= SPLIT - brank - 1);
                        pipe_take_valid <=
                            (bleft > 0 && bleft <= SPLIT - brank);
                        pipe_store_main <=
                            (bleft > 0 && bleft <= SPLIT - brank);
                        pipe_main_row <= compact_row_id(brank, bleft, bright);
                        pipe_last <= (bcell == 479);
                        if (bcell == 479) bphase <= BP_WAIT;
                        else bcell <= bcell + 1;
                    end

                    BP_DONE: begin
                        build_busy <= 0;
                        build_done <= 1;
                        tables_ready <= 1;
                        bphase <= BP_IDLE;
                    end

                    default: begin end
                endcase

                if (pipe_valid && pipe_last) begin
                    case (pipe_phase)
                        BP_RIGHT: begin
                            scratch_old_sel <= ~scratch_old_sel;
                            if (brank > SPLIT) begin
                                bcopy_weight <= 1;
                                bcopy_state <= 0;
                                bphase <= BP_COPY_RIGHT;
                            end else begin
                                bcopy_weight <= 0;
                                bcopy_state <= 0;
                                bphase <= BP_COPY_BASE;
                            end
                        end

                        BP_COPY_RIGHT: begin
                            brank <= brank - 1;
                            bcell <= 0;
                            bphase <= BP_RIGHT;
                        end

                        BP_COPY_BASE: begin
                            if (SPLIT == 0) bphase <= BP_DONE;
                            else begin
                                bcell <= 0;
                                bphase <= BP_EXPAND;
                            end
                        end

                        BP_EXPAND: begin
                            scratch_old_sel <= ~scratch_old_sel;
                            brank <= SPLIT - 1;
                            bcell <= 0;
                            bphase <= BP_LEFT;
                        end

                        BP_LEFT: begin
                            scratch_old_sel <= ~scratch_old_sel;
                            if (brank == 0) bphase <= BP_DONE;
                            else begin
                                brank <= brank - 1;
                                bcell <= 0;
                                bphase <= BP_LEFT;
                            end
                        end

                        default: begin end
                    endcase
                end
            end
        end
    end
