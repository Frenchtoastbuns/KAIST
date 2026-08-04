    // Correct cumulative information-cost prefixes for bound construction.
    // The earlier build-start nonblocking loop read the previous-cycle values
    // of every prefix element, so info_prefix[k] collapsed to a single rank
    // cost rather than sum(info_cost[0..k-1]).  Use a continuous prefix chain
    // for the query/scheduler path; the registered array is retained only for
    // source compatibility with the existing builder controller.
    wire [METRIC_W-1:0] info_prefix_live [0:K];
    assign info_prefix_live[0] = {METRIC_W{1'b0}};
    genvar ipg;
    generate
        for (ipg=0; ipg<K; ipg=ipg+1) begin: g_info_prefix_live
            assign info_prefix_live[ipg+1] = info_prefix_live[ipg] + info_cost[ipg];
        end
    endgenerate

`define info_prefix info_prefix_live
