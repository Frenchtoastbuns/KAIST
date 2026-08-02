`timescale 1ns/1ps

`ifndef DUT_MODULE
`define DUT_MODULE cap_r2_normal_top
`endif

module cap_r2_post_route_tb;
    localparam integer HEADER_BYTES = 96;
    localparam integer RECORD_BYTES = 1778;
    localparam integer FRAMES = 1000;
    localparam integer TRACE_BYTES = HEADER_BYTES + FRAMES * RECORD_BYTES;
    localparam integer MAX_GUARD_CYCLES = 2000000;

    reg [7:0] trace_mem [0:TRACE_BYTES-1];
    reg clk = 0;
    reg rst = 1;
    always #1 clk = ~clk;

    reg cfg_row_we;
    reg [3:0] cfg_row_group;
    reg [5:0] cfg_row_rank;
    reg [4:0] cfg_row_effect;
    reg cfg_phi_we;
    reg [3:0] cfg_phi_group;
    reg [4:0] cfg_phi_state;
    reg [9:0] cfg_phi_cost;
    reg cfg_info_we;
    reg [5:0] cfg_info_rank;
    reg [6:0] cfg_info_cost;
    reg [63:0] cfg_base_states;
    reg [13:0] cfg_seed_best_metric;
    reg [63:0] cfg_seed_mask;
    reg cfg_seed_tie;
    reg build_start;
    wire build_busy;
    wire build_done;
    wire [31:0] build_cycles;
    reg decode_start;
    wire decode_busy;
    wire decode_done;
    wire [13:0] best_metric;
    wire [63:0] best_tep;
    wire best_tied;
    wire [31:0] decode_cycles;
    wire [31:0] score_issues;
    wire [31:0] bound_row_cycles;
    wire [31:0] context_wait_cycles;
    wire [11:0] max_task_occupancy;
    wire [2:0] max_bound_waiters;

    `DUT_MODULE dut (
        .clk(clk), .rst(rst),
        .cfg_row_we(cfg_row_we), .cfg_row_group(cfg_row_group),
        .cfg_row_rank(cfg_row_rank), .cfg_row_effect(cfg_row_effect),
        .cfg_phi_we(cfg_phi_we), .cfg_phi_group(cfg_phi_group),
        .cfg_phi_state(cfg_phi_state), .cfg_phi_cost(cfg_phi_cost),
        .cfg_info_we(cfg_info_we), .cfg_info_rank(cfg_info_rank),
        .cfg_info_cost(cfg_info_cost), .cfg_base_states(cfg_base_states),
        .cfg_seed_best_metric(cfg_seed_best_metric), .cfg_seed_mask(cfg_seed_mask),
        .cfg_seed_tie(cfg_seed_tie),
        .build_start(build_start), .build_busy(build_busy), .build_done(build_done),
        .build_cycles(build_cycles), .decode_start(decode_start),
        .decode_busy(decode_busy), .decode_done(decode_done),
        .best_metric(best_metric), .best_tep(best_tep), .best_tied(best_tied),
        .decode_cycles(decode_cycles), .score_issues(score_issues),
        .bound_row_cycles(bound_row_cycles), .context_wait_cycles(context_wait_cycles),
        .max_task_occupancy(max_task_occupancy), .max_bound_waiters(max_bound_waiters)
    );

    function automatic [15:0] read_u16(input integer address);
        begin
            read_u16 = {trace_mem[address+1], trace_mem[address]};
        end
    endfunction

    function automatic [31:0] read_u32(input integer address);
        begin
            read_u32 = {trace_mem[address+3], trace_mem[address+2],
                        trace_mem[address+1], trace_mem[address]};
        end
    endfunction

    function automatic [63:0] read_u64(input integer address);
        begin
            read_u64 = {trace_mem[address+7], trace_mem[address+6],
                        trace_mem[address+5], trace_mem[address+4],
                        trace_mem[address+3], trace_mem[address+2],
                        trace_mem[address+1], trace_mem[address]};
        end
    endfunction

    task automatic drive_idle;
        begin
            cfg_row_we = 0;
            cfg_phi_we = 0;
            cfg_info_we = 0;
            build_start = 0;
            decode_start = 0;
            cfg_row_group = 0;
            cfg_row_rank = 0;
            cfg_row_effect = 0;
            cfg_phi_group = 0;
            cfg_phi_state = 0;
            cfg_phi_cost = 0;
            cfg_info_rank = 0;
            cfg_info_cost = 0;
            cfg_base_states = 0;
            cfg_seed_best_metric = 0;
            cfg_seed_mask = 0;
            cfg_seed_tie = 0;
        end
    endtask

    integer start_frame;
    integer count_frames;
    integer frame;
    integer record_base;
    integer address;
    integer group;
    integer rank;
    integer state;
    integer guard;
    integer errors;
    integer csv;
    string trace_path;
    string csv_path;
    reg [13:0] expected_metric;
    reg [63:0] expected_mask;
    reg expected_tie;

    initial begin
        start_frame = 0;
        count_frames = FRAMES;
        trace_path = "canonical_cap_trace.hex";
        csv_path = "post_route.csv";
        void'($value$plusargs("START=%d", start_frame));
        void'($value$plusargs("COUNT=%d", count_frames));
        void'($value$plusargs("TRACE=%s", trace_path));
        void'($value$plusargs("CSV=%s", csv_path));
        if (start_frame < 0 || count_frames <= 0 || start_frame + count_frames > FRAMES)
            $fatal(1, "invalid replay window");

        $readmemh(trace_path, trace_mem);
        if ({trace_mem[6],trace_mem[5],trace_mem[4],trace_mem[3],trace_mem[2],trace_mem[1],trace_mem[0]} != "CAPCAN1")
            $fatal(1, "frozen trace magic mismatch");
        if (read_u32(20) != FRAMES || read_u32(12) != HEADER_BYTES || read_u32(16) != RECORD_BYTES)
            $fatal(1, "frozen trace geometry mismatch");
        if (read_u64(56) != 64'd5928218492399464753 ||
            read_u64(64) != 64'd11508365490867720138 ||
            read_u64(72) != 64'd15049493467287215415 ||
            read_u64(80) != 64'd332897943003122562)
            $fatal(1, "frozen trace checksum metadata mismatch");

        drive_idle();
        repeat (20) @(posedge clk);
        rst = 0;
        repeat (5) @(posedge clk);

        csv = $fopen(csv_path, "w");
        if (csv == 0) $fatal(1, "cannot open CSV output");
        $fwrite(csv, "frame,metric,mask,tie,build_cycles,decode_cycles,total_cycles,score_issues,bound_rows,context_wait,max_task_occupancy,max_bound_waiters,errors\n");
        errors = 0;

        for (frame = start_frame; frame < start_frame + count_frames; frame = frame + 1) begin
            record_base = HEADER_BYTES + frame * RECORD_BYTES;
            if (read_u32(record_base) != frame) $fatal(1, "trace frame index mismatch");

            address = record_base + 12;
            for (group = 0; group < 13; group = group + 1) begin
                for (rank = 0; rank < 64; rank = rank + 1) begin
                    @(negedge clk);
                    cfg_row_we = 1;
                    cfg_row_group = group[3:0];
                    cfg_row_rank = rank[5:0];
                    cfg_row_effect = trace_mem[address][4:0];
                    address = address + 1;
                end
            end
            @(negedge clk); cfg_row_we = 0;

            for (group = 0; group < 13; group = group + 1) begin
                for (state = 0; state < 32; state = state + 1) begin
                    @(negedge clk);
                    cfg_phi_we = 1;
                    cfg_phi_group = group[3:0];
                    cfg_phi_state = state[4:0];
                    cfg_phi_cost = read_u16(address);
                    address = address + 2;
                end
            end
            @(negedge clk); cfg_phi_we = 0;

            for (rank = 0; rank < 64; rank = rank + 1) begin
                @(negedge clk);
                cfg_info_we = 1;
                cfg_info_rank = rank[5:0];
                cfg_info_cost = trace_mem[address][6:0];
                address = address + 1;
            end
            @(negedge clk); cfg_info_we = 0;

            cfg_base_states = read_u64(address); address = address + 8;
            cfg_seed_best_metric = read_u16(address); address = address + 2;
            cfg_seed_mask = read_u64(address); address = address + 8;
            cfg_seed_tie = trace_mem[address][0]; address = address + 1;
            expected_metric = read_u16(address); address = address + 2;
            expected_mask = read_u64(address); address = address + 8;
            expected_tie = trace_mem[address][0];

            @(negedge clk); build_start = 1;
            @(negedge clk); build_start = 0;
            guard = 0;
            while (!build_done) begin
                @(posedge clk);
                guard = guard + 1;
                if (guard > MAX_GUARD_CYCLES) $fatal(1, "build timeout at frame %0d", frame);
            end

            @(negedge clk); decode_start = 1;
            @(negedge clk); decode_start = 0;
            guard = 0;
            while (!decode_done) begin
                @(posedge clk);
                guard = guard + 1;
                if (guard > MAX_GUARD_CYCLES) $fatal(1, "decode timeout at frame %0d", frame);
            end

            if (best_metric !== expected_metric || best_tep !== expected_mask || best_tied !== expected_tie) begin
                errors = errors + 1;
                $display("POST_ROUTE_MISMATCH frame=%0d metric=%0d/%0d mask=%016h/%016h tie=%0d/%0d",
                         frame,best_metric,expected_metric,best_tep,expected_mask,best_tied,expected_tie);
            end
            $fwrite(csv, "%0d,%0d,0x%016h,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",
                    frame,best_metric,best_tep,best_tied,build_cycles,decode_cycles,
                    build_cycles+decode_cycles,score_issues,bound_row_cycles,
                    context_wait_cycles,max_task_occupancy,max_bound_waiters,
                    (best_metric !== expected_metric || best_tep !== expected_mask || best_tied !== expected_tie));
        end

        $fclose(csv);
        if (errors != 0) $fatal(1, "post-route replay had %0d mismatch frames", errors);
        $display("POST_ROUTE_REPLAY_PASS start=%0d count=%0d errors=0", start_frame, count_frames);
        $finish;
    end
endmodule
