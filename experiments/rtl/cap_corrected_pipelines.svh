    // ------------------------------------------------------------------
    // Shared table-read arbitration and table-debug response.
    // ------------------------------------------------------------------
    reg q_issue_valid;
    reg [ROW_W-1:0] q_issue_row;
    reg [59:0] q_issue_state5;
    reg [3:0] q_issue_state4;
    reg [13:0] q_issue_base;
    reg [13:0] q_issue_inc;
    reg [1:0] q_issue_ctx;
    reg q_issue_debug;

    wire dbg_table_issue = dbg_table_req && !build_busy &&
        !decode_busy && !q_issue_valid;

    always @* begin
        table_re = 0;
        table_rrow = 0;
        table_rstate5 = 0;
        table_rstate4 = 0;
        if (!build_busy && q_issue_valid) begin
            table_re = 1;
            table_rrow = q_issue_row;
            table_rstate5 = q_issue_state5;
            table_rstate4 = q_issue_state4;
        end else if (dbg_table_issue) begin
            table_re = 1;
            table_rrow = dbg_table_row;
            for (si = 0; si < 12; si = si + 1)
                table_rstate5[si*5 +: 5] = dbg_table_state;
            table_rstate4 = dbg_table_state[3:0];
        end
    end

    always @(posedge clk) begin
        if (rst) dbg_table_valid <= 0;
        else dbg_table_valid <= dbg_table_issue;
    end

    // ------------------------------------------------------------------
    // Correctly aligned threshold-bound pipeline.
    // ------------------------------------------------------------------
    reg qmem_valid, qmem_debug;
    reg [13:0] qmem_base, qmem_inc;
    reg [1:0] qmem_ctx;

    reg qv1, qd1, qinv1;
    reg [13:0] qb1, qi1;
    reg [1:0] qc1;
    reg [10:0] qpair1 [0:5];
    reg [8:0] qtail1;

    reg qv2, qd2, qinv2;
    reg [13:0] qb2, qi2;
    reg [1:0] qc2;
    reg [11:0] qquad2 [0:2];
    reg [8:0] qtail2;

    reg qv3, qd3, qinv3;
    reg [13:0] qb3, qi3;
    reg [1:0] qc3;
    reg [12:0] qoct3;
    reg [11:0] qfour3;
    reg [8:0] qtail3;

    reg q_rsp_valid;
    reg q_rsp_debug;
    reg [1:0] q_rsp_ctx;
    reg [13:0] q_rsp_bound;
    reg q_rsp_may_improve;

    reg qinvalid_now;
    reg [14:0] qsum_now;
    integer qi;

    always @* begin
        qinvalid_now = (table_q4 == INF9);
        for (qi = 0; qi < 12; qi = qi + 1)
            if (table_q5[qi*10 +: 10] == INF10)
                qinvalid_now = 1;
        qsum_now = {1'b0, qb3} + {2'b0, qoct3} +
            {3'b0, qfour3} + {6'b0, qtail3};
    end

    always @(posedge clk) begin
        if (rst) begin
            qmem_valid <= 0;
            qv1 <= 0;
            qv2 <= 0;
            qv3 <= 0;
            q_rsp_valid <= 0;
            dbg_bound_valid <= 0;
            dbg_bound_value <= 0;
            dbg_bound_may_improve <= 0;
        end else begin
            qmem_valid <= q_issue_valid;
            qmem_debug <= q_issue_debug;
            qmem_base <= q_issue_base;
            qmem_inc <= q_issue_inc;
            qmem_ctx <= q_issue_ctx;

            qv1 <= qmem_valid;
            qd1 <= qmem_debug;
            qb1 <= qmem_base;
            qi1 <= qmem_inc;
            qc1 <= qmem_ctx;
            for (qi = 0; qi < 6; qi = qi + 1)
                qpair1[qi] <=
                    {1'b0, table_q5[(2*qi)*10 +: 10]} +
                    {1'b0, table_q5[(2*qi+1)*10 +: 10]};
            qtail1 <= table_q4;
            qinv1 <= qinvalid_now;

            qv2 <= qv1;
            qd2 <= qd1;
            qb2 <= qb1;
            qi2 <= qi1;
            qc2 <= qc1;
            qquad2[0] <= {1'b0, qpair1[0]} + {1'b0, qpair1[1]};
            qquad2[1] <= {1'b0, qpair1[2]} + {1'b0, qpair1[3]};
            qquad2[2] <= {1'b0, qpair1[4]} + {1'b0, qpair1[5]};
            qtail2 <= qtail1;
            qinv2 <= qinv1;

            qv3 <= qv2;
            qd3 <= qd2;
            qb3 <= qb2;
            qi3 <= qi2;
            qc3 <= qc2;
            qoct3 <= {1'b0, qquad2[0]} + {1'b0, qquad2[1]};
            qfour3 <= qquad2[2];
            qtail3 <= qtail2;
            qinv3 <= qinv2;

            q_rsp_valid <= qv3;
            q_rsp_debug <= qd3;
            q_rsp_ctx <= qc3;
            q_rsp_bound <= (qsum_now[14] || qinv3) ? INF14 :
                qsum_now[13:0];
            q_rsp_may_improve <= !qinv3 && !qsum_now[14] &&
                (qsum_now[13:0] <= qi3);

            dbg_bound_valid <= q_rsp_valid && q_rsp_debug;
            if (q_rsp_valid && q_rsp_debug) begin
                dbg_bound_value <= q_rsp_bound;
                dbg_bound_may_improve <= q_rsp_may_improve;
            end
        end
    end

    // ------------------------------------------------------------------
    // Four independent, one-request-per-cycle scoring pipelines.
    // ------------------------------------------------------------------
    reg [3:0] s_req_valid;
    reg [3:0] s_req_debug;
    reg [63:0] s_req_states [0:3];
    reg [13:0] s_req_info [0:3];
    reg [63:0] s_req_mask [0:3];

    reg [3:0] sv1, sd1;
    reg [10:0] spair1 [0:3][0:5];
    reg [8:0] stail1 [0:3];
    reg [13:0] sinfo1 [0:3];
    reg [63:0] smask1 [0:3];

    reg [3:0] sv2, sd2s;
    reg [11:0] squad2 [0:3][0:2];
    reg [8:0] stail2 [0:3];
    reg [13:0] sinfo2 [0:3];
    reg [63:0] smask2 [0:3];

    reg [3:0] sv3, sd3s;
    reg [12:0] soct3 [0:3];
    reg [11:0] sfour3 [0:3];
    reg [8:0] stail3 [0:3];
    reg [13:0] sinfo3 [0:3];
    reg [63:0] smask3 [0:3];

    reg [3:0] s_rsp_valid;
    reg [3:0] s_rsp_debug;
    reg [13:0] s_rsp_metric [0:3];
    reg [63:0] s_rsp_mask [0:3];

    reg [9:0] scost [0:3][0:12];
    integer sl, sg;

    always @* begin
        for (sl = 0; sl < 4; sl = sl + 1) begin
            for (sg = 0; sg < 12; sg = sg + 1)
                scost[sl][sg] =
                    phi_cost[sg][s_req_states[sl][sg*5 +: 5]];
            scost[sl][12] = {1'b0,
                phi_cost[12][{1'b0, s_req_states[sl][60 +: 4]}][8:0]};
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            sv1 <= 0;
            sv2 <= 0;
            sv3 <= 0;
            s_rsp_valid <= 0;
            s_rsp_debug <= 0;
        end else begin
            sv1 <= s_req_valid;
            sd1 <= s_req_debug;
            for (sl = 0; sl < 4; sl = sl + 1) begin
                sinfo1[sl] <= s_req_info[sl];
                smask1[sl] <= s_req_mask[sl];
                for (sg = 0; sg < 6; sg = sg + 1)
                    spair1[sl][sg] <=
                        {1'b0, scost[sl][2*sg]} +
                        {1'b0, scost[sl][2*sg+1]};
                stail1[sl] <= scost[sl][12][8:0];
            end

            sv2 <= sv1;
            sd2s <= sd1;
            for (sl = 0; sl < 4; sl = sl + 1) begin
                sinfo2[sl] <= sinfo1[sl];
                smask2[sl] <= smask1[sl];
                squad2[sl][0] <=
                    {1'b0, spair1[sl][0]} + {1'b0, spair1[sl][1]};
                squad2[sl][1] <=
                    {1'b0, spair1[sl][2]} + {1'b0, spair1[sl][3]};
                squad2[sl][2] <=
                    {1'b0, spair1[sl][4]} + {1'b0, spair1[sl][5]};
                stail2[sl] <= stail1[sl];
            end

            sv3 <= sv2;
            sd3s <= sd2s;
            for (sl = 0; sl < 4; sl = sl + 1) begin
                sinfo3[sl] <= sinfo2[sl];
                smask3[sl] <= smask2[sl];
                soct3[sl] <=
                    {1'b0, squad2[sl][0]} + {1'b0, squad2[sl][1]};
                sfour3[sl] <= squad2[sl][2];
                stail3[sl] <= stail2[sl];
            end

            s_rsp_valid <= sv3;
            s_rsp_debug <= sd3s;
            for (sl = 0; sl < 4; sl = sl + 1) begin
                s_rsp_metric[sl] <= sinfo3[sl] + soct3[sl] +
                    sfour3[sl] + stail3[sl];
                s_rsp_mask[sl] <= smask3[sl];
            end
        end
    end

    assign dbg_score_valid = s_rsp_valid[0] && s_rsp_debug[0];
    assign dbg_score_metric = s_rsp_metric[0];
    assign dbg_score_mask_out = s_rsp_mask[0];
