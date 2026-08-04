                        compact_a_en[cg]=1; compact_a_we[cg]=1;
                        compact_a_addr[cg]=compact_addr(b_suffix,b_pair,b_value_state);
                        compact_a_wdata[cg]=(pair_left(b_pair)==0 || compact_a_q[cg]<=compact_b_q[cg])?
                            compact_a_q[cg]:compact_b_q[cg];
