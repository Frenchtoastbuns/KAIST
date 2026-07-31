`timescale 1ns/1ps
module cap_corrected_differential_tb;
  localparam K=64, GROUPS=13, STATES=32, ORDER=4, SPLIT=45;
  localparam NORMAL_DEPTH=10400, RIGHT_DEPTH=3200, COMPACT_DEPTH=22080;
  reg clk=0,rst=1; always #5 clk=~clk;
  reg cfg_row_we=0; reg [3:0] cfg_row_group; reg [5:0] cfg_row_rank; reg [4:0] cfg_row_effect;
  reg cfg_phi_we=0; reg [3:0] cfg_phi_group; reg [4:0] cfg_phi_state; reg [9:0] cfg_phi_cost;
  reg cfg_info_we=0; reg [5:0] cfg_info_rank; reg [6:0] cfg_info_cost;
  reg [63:0] cfg_base_states; reg [13:0] cfg_seed_best_metric; reg [63:0] cfg_seed_mask; reg cfg_seed_tie;
  reg build_start=0,decode_start=0;
  wire build_busy_n,build_done_n; wire [31:0] build_cycles_n; wire decode_busy_n,decode_done_n; wire [13:0] best_metric_n; wire [63:0] best_tep_n; wire best_tied_n;
  wire [31:0] decode_cycles_n,score_issues_n,bound_rows_n,wait_cycles_n; wire [11:0] max_occ_n; wire [2:0] max_wait_n;
  wire build_busy_c,build_done_c; wire [31:0] build_cycles_c; wire decode_busy_c,decode_done_c; wire [13:0] best_metric_c; wire [63:0] best_tep_c; wire best_tied_c;
  wire [31:0] decode_cycles_c,score_issues_c,bound_rows_c,wait_cycles_c; wire [11:0] max_occ_c; wire [2:0] max_wait_c;

  reg dbg_table_re_n=0,dbg_table_sel_n=0; reg [3:0] dbg_table_group_n; reg [14:0] dbg_table_addr_n; wire dbg_table_valid_n; wire [9:0] dbg_table_data_n;
  reg dbg_table_re_c=0,dbg_table_sel_c=0; reg [3:0] dbg_table_group_c; reg [14:0] dbg_table_addr_c; wire dbg_table_valid_c; wire [9:0] dbg_table_data_c;
  reg dbg_query_start_n=0,dbg_query_force_full_n=1; reg [5:0] dbg_query_suffix_n; reg [2:0] dbg_query_budget_n; reg [13:0] dbg_query_info_n; reg [63:0] dbg_query_states_n; reg [13:0] dbg_query_threshold_n=14'h3fff;
  wire dbg_query_done_n,dbg_query_pass_n; wire [13:0] dbg_query_min_n;
  reg dbg_query_start_c=0,dbg_query_force_full_c=1; reg [5:0] dbg_query_suffix_c; reg [2:0] dbg_query_budget_c; reg [13:0] dbg_query_info_c; reg [63:0] dbg_query_states_c; reg [13:0] dbg_query_threshold_c=14'h3fff;
  wire dbg_query_done_c,dbg_query_pass_c; wire [13:0] dbg_query_min_c;
  reg dbg_score_start_n=0; reg [63:0] dbg_score_mask_n; reg [13:0] dbg_score_info_n; reg [63:0] dbg_score_states_n; wire dbg_score_done_n; wire [13:0] dbg_score_metric_n;
  reg dbg_score_start_c=0; reg [63:0] dbg_score_mask_c; reg [13:0] dbg_score_info_c; reg [63:0] dbg_score_states_c; wire dbg_score_done_c; wire [13:0] dbg_score_metric_c;

  cap_nonblocking_normal_top normal(
    .clk,.rst,.cfg_row_we,.cfg_row_group,.cfg_row_rank,.cfg_row_effect,.cfg_phi_we,.cfg_phi_group,.cfg_phi_state,.cfg_phi_cost,
    .cfg_info_we,.cfg_info_rank,.cfg_info_cost,.cfg_base_states,.cfg_seed_best_metric,.cfg_seed_mask,.cfg_seed_tie,
    .build_start,.build_busy(build_busy_n),.build_done(build_done_n),.build_cycles(build_cycles_n),
    .decode_start,.decode_busy(decode_busy_n),.decode_done(decode_done_n),.best_metric(best_metric_n),.best_tep(best_tep_n),.best_tied(best_tied_n),
    .decode_cycles(decode_cycles_n),.score_issues(score_issues_n),.bound_row_cycles(bound_rows_n),.context_wait_cycles(wait_cycles_n),.max_task_occupancy(max_occ_n),.max_bound_waiters(max_wait_n),
    .dbg_table_re(dbg_table_re_n),.dbg_table_sel(dbg_table_sel_n),.dbg_table_group(dbg_table_group_n),.dbg_table_addr(dbg_table_addr_n),.dbg_table_valid(dbg_table_valid_n),.dbg_table_data(dbg_table_data_n),
    .dbg_query_start(dbg_query_start_n),.dbg_query_force_full(dbg_query_force_full_n),.dbg_query_suffix(dbg_query_suffix_n),.dbg_query_budget(dbg_query_budget_n),.dbg_query_information_cost(dbg_query_info_n),.dbg_query_states(dbg_query_states_n),.dbg_query_threshold(dbg_query_threshold_n),.dbg_query_done(dbg_query_done_n),.dbg_query_pass(dbg_query_pass_n),.dbg_query_minimum(dbg_query_min_n),
    .dbg_score_start(dbg_score_start_n),.dbg_score_mask(dbg_score_mask_n),.dbg_score_information_cost(dbg_score_info_n),.dbg_score_states(dbg_score_states_n),.dbg_score_done(dbg_score_done_n),.dbg_score_metric(dbg_score_metric_n));
  cap_nonblocking_s45_top compact(
    .clk,.rst,.cfg_row_we,.cfg_row_group,.cfg_row_rank,.cfg_row_effect,.cfg_phi_we,.cfg_phi_group,.cfg_phi_state,.cfg_phi_cost,
    .cfg_info_we,.cfg_info_rank,.cfg_info_cost,.cfg_base_states,.cfg_seed_best_metric,.cfg_seed_mask,.cfg_seed_tie,
    .build_start,.build_busy(build_busy_c),.build_done(build_done_c),.build_cycles(build_cycles_c),
    .decode_start,.decode_busy(decode_busy_c),.decode_done(decode_done_c),.best_metric(best_metric_c),.best_tep(best_tep_c),.best_tied(best_tied_c),
    .decode_cycles(decode_cycles_c),.score_issues(score_issues_c),.bound_row_cycles(bound_rows_c),.context_wait_cycles(wait_cycles_c),.max_task_occupancy(max_occ_c),.max_bound_waiters(max_wait_c),
    .dbg_table_re(dbg_table_re_c),.dbg_table_sel(dbg_table_sel_c),.dbg_table_group(dbg_table_group_c),.dbg_table_addr(dbg_table_addr_c),.dbg_table_valid(dbg_table_valid_c),.dbg_table_data(dbg_table_data_c),
    .dbg_query_start(dbg_query_start_c),.dbg_query_force_full(dbg_query_force_full_c),.dbg_query_suffix(dbg_query_suffix_c),.dbg_query_budget(dbg_query_budget_c),.dbg_query_information_cost(dbg_query_info_c),.dbg_query_states(dbg_query_states_c),.dbg_query_threshold(dbg_query_threshold_c),.dbg_query_done(dbg_query_done_c),.dbg_query_pass(dbg_query_pass_c),.dbg_query_minimum(dbg_query_min_c),
    .dbg_score_start(dbg_score_start_c),.dbg_score_mask(dbg_score_mask_c),.dbg_score_information_cost(dbg_score_info_c),.dbg_score_states(dbg_score_states_c),.dbg_score_done(dbg_score_done_c),.dbg_score_metric(dbg_score_metric_c));

  reg [7:0] rows[0:GROUPS*K-1]; reg [9:0] phi[0:GROUPS*STATES-1]; reg [6:0] info[0:K-1];
  reg [9:0] normal_exp[0:GROUPS*NORMAL_DEPTH-1]; reg [9:0] right_exp[0:GROUPS*RIGHT_DEPTH-1]; reg [9:0] compact_exp[0:GROUPS*COMPACT_DEPTH-1];
  integer errors=0,g,r,s,a,fd,count,rc,timeout; reg [63:0] meta_base,meta_mask; reg [13:0] meta_metric; integer meta_tie;
  integer q_suffix,q_budget,q_info,q_n,q_c; reg [63:0] q_states;
  reg [63:0] score_mask,score_states; integer score_info,score_expected;

  task automatic read_normal(input integer group,input integer addr,output reg [9:0] value);
    integer guard; begin
      @(negedge clk); dbg_table_group_n=group; dbg_table_addr_n=addr; dbg_table_sel_n=0; dbg_table_re_n=1;
      @(negedge clk); dbg_table_re_n=0; guard=0; while(!dbg_table_valid_n && guard<8) begin @(negedge clk);guard=guard+1;end
      if(!dbg_table_valid_n) begin $display("NORMAL_READ_TIMEOUT g=%0d a=%0d",group,addr);errors=errors+1;value='x;end else value=dbg_table_data_n;
    end endtask
  task automatic read_compact(input integer sel,input integer group,input integer addr,output reg [9:0] value);
    integer guard; begin
      @(negedge clk); dbg_table_group_c=group; dbg_table_addr_c=addr; dbg_table_sel_c=sel; dbg_table_re_c=1;
      @(negedge clk); dbg_table_re_c=0; guard=0; while(!dbg_table_valid_c && guard<8) begin @(negedge clk);guard=guard+1;end
      if(!dbg_table_valid_c) begin $display("S45_READ_TIMEOUT sel=%0d g=%0d a=%0d",sel,group,addr);errors=errors+1;value='x;end else value=dbg_table_data_c;
    end endtask
  reg [9:0] got;

  initial begin
    $readmemh("experiments/results/corrected_vectors/rows.hex",rows);
    $readmemh("experiments/results/corrected_vectors/phi.hex",phi);
    $readmemh("experiments/results/corrected_vectors/info.hex",info);
    $readmemh("experiments/results/corrected_vectors/normal.hex",normal_exp);
    $readmemh("experiments/results/corrected_vectors/right.hex",right_exp);
    $readmemh("experiments/results/corrected_vectors/compact.hex",compact_exp);
    fd=$fopen("experiments/results/corrected_vectors/meta.txt","r"); rc=$fscanf(fd,"%h %h %h %d",meta_base,meta_metric,meta_mask,meta_tie);$fclose(fd);
    if(rc!=4)$fatal(1,"meta parse failed");cfg_base_states=meta_base;cfg_seed_best_metric=meta_metric;cfg_seed_mask=meta_mask;cfg_seed_tie=meta_tie;
    repeat(4)@(negedge clk);rst=0;
    cfg_row_we=1;for(g=0;g<GROUPS;g=g+1)for(r=0;r<K;r=r+1)begin @(negedge clk);cfg_row_group=g;cfg_row_rank=r;cfg_row_effect=rows[g*K+r][4:0];end cfg_row_we=0;
    cfg_phi_we=1;for(g=0;g<GROUPS;g=g+1)for(s=0;s<STATES;s=s+1)begin @(negedge clk);cfg_phi_group=g;cfg_phi_state=s;cfg_phi_cost=phi[g*STATES+s];end cfg_phi_we=0;
    cfg_info_we=1;for(r=0;r<K;r=r+1)begin @(negedge clk);cfg_info_rank=r;cfg_info_cost=info[r];end cfg_info_we=0;
    @(negedge clk);build_start=1;@(negedge clk);build_start=0;timeout=0;
    while((!build_done_n||!build_done_c)&&timeout<200000)begin @(negedge clk);timeout=timeout+1;end
    if(timeout>=200000)$fatal(1,"builder timeout");
    $display("BUILD_DONE normal=%0d s45=%0d",build_cycles_n,build_cycles_c);

    for(g=0;g<GROUPS;g=g+1)for(a=0;a<NORMAL_DEPTH;a=a+1)begin read_normal(g,a,got);if(got!==normal_exp[g*NORMAL_DEPTH+a])begin if(errors<20)$display("NORMAL_MISMATCH g=%0d a=%0d got=%h exp=%h",g,a,got,normal_exp[g*NORMAL_DEPTH+a]);errors=errors+1;end end
    for(g=0;g<GROUPS;g=g+1)for(a=0;a<RIGHT_DEPTH;a=a+1)begin read_compact(0,g,a,got);if(got!==right_exp[g*RIGHT_DEPTH+a])begin if(errors<20)$display("RIGHT_MISMATCH g=%0d a=%0d got=%h exp=%h",g,a,got,right_exp[g*RIGHT_DEPTH+a]);errors=errors+1;end end
    for(g=0;g<GROUPS;g=g+1)for(a=0;a<COMPACT_DEPTH;a=a+1)begin read_compact(1,g,a,got);if(got!==compact_exp[g*COMPACT_DEPTH+a])begin if(errors<20)$display("COMPACT_MISMATCH g=%0d a=%0d got=%h exp=%h",g,a,got,compact_exp[g*COMPACT_DEPTH+a]);errors=errors+1;end end
    $display("TABLE_CHECK_DONE errors=%0d",errors);

    fd=$fopen("experiments/results/corrected_vectors/queries.txt","r");rc=$fscanf(fd,"%d",count);for(r=0;r<count;r=r+1)begin
      rc=$fscanf(fd,"%h %h %h %h %h %h",q_suffix,q_budget,q_info,q_states,q_n,q_c);if(rc!=6)$fatal(1,"query parse");
      @(negedge clk);dbg_query_suffix_n=q_suffix;dbg_query_budget_n=q_budget;dbg_query_info_n=q_info;dbg_query_states_n=q_states;dbg_query_start_n=1;
      dbg_query_suffix_c=q_suffix;dbg_query_budget_c=q_budget;dbg_query_info_c=q_info;dbg_query_states_c=q_states;dbg_query_start_c=1;
      @(negedge clk);dbg_query_start_n=0;dbg_query_start_c=0;timeout=0;while((!dbg_query_done_n||!dbg_query_done_c)&&timeout<200)begin @(negedge clk);timeout=timeout+1;end
      if(timeout>=200)$fatal(1,"query timeout");if(dbg_query_min_n!==q_n[13:0])begin if(errors<20)$display("NORMAL_QUERY_MISMATCH %0d got=%h exp=%h",r,dbg_query_min_n,q_n);errors=errors+1;end
      if(dbg_query_min_c!==q_c[13:0])begin if(errors<20)$display("S45_QUERY_MISMATCH %0d got=%h exp=%h",r,dbg_query_min_c,q_c);errors=errors+1;end
    end $fclose(fd);$display("QUERY_CHECK_DONE errors=%0d",errors);

    fd=$fopen("experiments/results/corrected_vectors/scores.txt","r");rc=$fscanf(fd,"%d",count);for(r=0;r<count;r=r+1)begin
      rc=$fscanf(fd,"%h %h %h %h",score_mask,score_info,score_states,score_expected);if(rc!=4)$fatal(1,"score parse");
      @(negedge clk);dbg_score_mask_n=score_mask;dbg_score_info_n=score_info;dbg_score_states_n=score_states;dbg_score_start_n=1;
      dbg_score_mask_c=score_mask;dbg_score_info_c=score_info;dbg_score_states_c=score_states;dbg_score_start_c=1;
      @(negedge clk);dbg_score_start_n=0;dbg_score_start_c=0;timeout=0;while((!dbg_score_done_n||!dbg_score_done_c)&&timeout<20)begin @(negedge clk);timeout=timeout+1;end
      if(timeout>=20)$fatal(1,"score timeout");if(dbg_score_metric_n!==score_expected[13:0]||dbg_score_metric_c!==score_expected[13:0])begin if(errors<20)$display("SCORE_MISMATCH %0d n=%h c=%h exp=%h",r,dbg_score_metric_n,dbg_score_metric_c,score_expected);errors=errors+1;end
    end $fclose(fd);$display("SCORE_CHECK_DONE errors=%0d",errors);

    @(negedge clk);decode_start=1;@(negedge clk);decode_start=0;timeout=0;while((!decode_done_n||!decode_done_c)&&timeout<5000000)begin @(negedge clk);timeout=timeout+1;end
    if(timeout>=5000000)$fatal(1,"decode timeout");
    if(best_metric_n!==meta_metric||best_tep_n!==meta_mask||best_tied_n!==meta_tie)begin $display("NORMAL_FINAL_MISMATCH metric=%h/%h mask=%h/%h tie=%b/%0d",best_metric_n,meta_metric,best_tep_n,meta_mask,best_tied_n,meta_tie);errors=errors+1;end
    if(best_metric_c!==meta_metric||best_tep_c!==meta_mask||best_tied_c!==meta_tie)begin $display("S45_FINAL_MISMATCH metric=%h/%h mask=%h/%h tie=%b/%0d",best_metric_c,meta_metric,best_tep_c,meta_mask,best_tied_c,meta_tie);errors=errors+1;end
    $display("DECODE normal_cycles=%0d normal_issues=%0d normal_util_ppm=%0d s45_cycles=%0d s45_issues=%0d s45_util_ppm=%0d",decode_cycles_n,score_issues_n,(score_issues_n*250000)/decode_cycles_n,decode_cycles_c,score_issues_c,(score_issues_c*250000)/decode_cycles_c);
    $display("COUNTERS normal_bound_rows=%0d normal_wait=%0d s45_bound_rows=%0d s45_wait=%0d max_occ=%0d/%0d",bound_rows_n,wait_cycles_n,bound_rows_c,wait_cycles_c,max_occ_n,max_occ_c);
    if(errors)$fatal(1,"DIFFERENTIAL_FAIL errors=%0d",errors);else $display("DIFFERENTIAL_PASS");
    $finish;
  end
endmodule
