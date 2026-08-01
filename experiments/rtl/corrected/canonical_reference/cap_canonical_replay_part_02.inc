        uint32_t s45_b_n=0,s45_b_r=0,s45_b_exp=0,s45_b_tri=0,s45_b_prefix=0;
        uint16_t normal_metric=0,s45_metric=0;uint64_t normal_mask=0,s45_mask=0;bool normal_tie=false,s45_tie=false;
        uint16_t normal_max_fifo=0,s45_max_fifo=0;uint8_t normal_max_waiters=0,s45_max_waiters=0;
    };
    Simulator():top_(new Vcap_canonical_replay_pair_top){top_->clk=0;top_->rst=1;top_->cfg_row_we=0;top_->cfg_phi_we=0;top_->cfg_info_we=0;top_->build_start=0;top_->decode_start=0;for(int i=0;i<4;++i)tick(false);top_->rst=0;tick(false);}
    ~Simulator(){top_->final();delete top_;}
    Result run(const Frame& f){configure(f);top_->cfg_base_states=f.base_states;top_->cfg_seed_best_metric=f.seed_metric;top_->cfg_seed_mask=f.seed_mask;top_->cfg_seed_tie=f.seed_tie;
        bool nd=false,cd=false;top_->build_start=1;tick(false);top_->build_start=0;for(uint64_t guard=0;!(nd&&cd);++guard){if(guard>200000)throw std::runtime_error("builder timeout");tick(false);nd|=top_->normal_build_done;cd|=top_->s45_build_done;}
        Result r;r.normal_build=top_->normal_build_cycles;r.s45_build=top_->s45_build_cycles;
        r.normal_b_n=top_->normal_build_normal_dp_cycles;r.normal_b_r=top_->normal_build_right_dp_cycles;r.normal_b_exp=top_->normal_build_expansion_cycles;r.normal_b_tri=top_->normal_build_triangular_cycles;r.normal_b_prefix=top_->normal_build_prefix_cycles;
        r.s45_b_n=top_->s45_build_normal_dp_cycles;r.s45_b_r=top_->s45_build_right_dp_cycles;r.s45_b_exp=top_->s45_build_expansion_cycles;r.s45_b_tri=top_->s45_build_triangular_cycles;r.s45_b_prefix=top_->s45_build_prefix_cycles;
        nd=false;cd=false;top_->decode_start=1;tick(true,&r);top_->decode_start=0;for(uint64_t guard=0;!(nd&&cd);++guard){if(guard>5000000)throw std::runtime_error("decode timeout");tick(true,&r);nd|=top_->normal_decode_done;cd|=top_->s45_decode_done;}
        r.normal_decode=top_->normal_decode_cycles;r.s45_decode=top_->s45_decode_cycles;r.normal_bound=top_->normal_bound_rows;r.s45_bound=top_->s45_bound_rows;r.normal_wait=top_->normal_context_wait;r.s45_wait=top_->s45_context_wait;
        r.normal_fifo_full=top_->normal_fifo_full_cycles;r.s45_fifo_full=top_->s45_fifo_full_cycles;r.normal_fifo_empty=top_->normal_fifo_empty_cycles;r.s45_fifo_empty=top_->s45_fifo_empty_cycles;
        r.normal_max_fifo=top_->normal_max_task_occupancy;r.s45_max_fifo=top_->s45_max_task_occupancy;r.normal_max_waiters=top_->normal_max_bound_waiters;r.s45_max_waiters=top_->s45_max_bound_waiters;
        r.normal_metric=top_->normal_best_metric;r.s45_metric=top_->s45_best_metric;r.normal_mask=top_->normal_best_tep;r.s45_mask=top_->s45_best_tep;r.normal_tie=top_->normal_best_tied;r.s45_tie=top_->s45_best_tied;
        if(r.normal_issues!=top_->normal_score_issues_reported||r.s45_issues!=top_->s45_score_issues_reported)throw std::runtime_error("score issue telemetry mismatch");return r;}
private:
    Vcap_canonical_replay_pair_top* top_;
    void tick(bool count,Result* r=nullptr){top_->clk=0;top_->eval();top_->clk=1;top_->eval();if(count&&r){r->normal_issues+=popcount4(top_->normal_issue_mask);r->s45_issues+=popcount4(top_->s45_issue_mask);}}
    void configure(const Frame& f){top_->cfg_row_we=1;for(int g=0;g<GROUPS;++g)for(int r=0;r<K;++r){top_->cfg_row_group=g;top_->cfg_row_rank=r;top_->cfg_row_effect=f.rows[g][r];tick(false);}top_->cfg_row_we=0;
        top_->cfg_phi_we=1;for(int g=0;g<GROUPS;++g)for(int s=0;s<STATES;++s){top_->cfg_phi_group=g;top_->cfg_phi_state=s;top_->cfg_phi_cost=f.phi[g][s];tick(false);}top_->cfg_phi_we=0;
        top_->cfg_info_we=1;for(int r=0;r<K;++r){top_->cfg_info_rank=r;top_->cfg_info_cost=f.info[r];tick(false);}top_->cfg_info_we=0;tick(false);}
};
}

int main(int argc,char** argv){Verilated::commandArgs(argc,argv);std::string trace_path,csv_path="canonical_replay.csv";uint64_t start=0,count=0;
    for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--trace"&&i+1<argc)trace_path=argv[++i];else if(a=="--csv"&&i+1<argc)csv_path=argv[++i];else if(a=="--start"&&i+1<argc)start=parse_u64(argv[++i]);else if(a=="--count"&&i+1<argc)count=parse_u64(argv[++i]);else throw std::runtime_error("unknown or incomplete argument: "+a);}if(trace_path.empty())throw std::runtime_error("--trace is required");
    std::ifstream trace(trace_path,std::ios::binary);if(!trace)throw std::runtime_error("cannot open canonical trace");Header h=read_header(trace);if(start>=h.frames)throw std::runtime_error("start outside trace");if(count==0)count=h.frames-start;if(start+count>h.frames)throw std::runtime_error("range outside trace");
    std::ofstream csv(csv_path);if(!csv)throw std::runtime_error("cannot open replay CSV");
    csv<<"frame,soft_checksum,payload_checksum,seed_metric,seed_mask,seed_tie,expected_metric,expected_mask,expected_tie,"
          "normal_metric,normal_mask,normal_tie,normal_build_cycles,normal_decode_cycles,normal_total_cycles,normal_issues,normal_prune_rate,normal_bound_rows,normal_utilisation,normal_context_wait,normal_fifo_max,normal_fifo_full_cycles,normal_fifo_empty_cycles,normal_max_bound_waiters,normal_build_normal_dp,normal_build_right_dp,normal_build_expansion,normal_build_triangular,normal_build_prefix,normal_cycles_x_luts,"
          "s45_metric,s45_mask,s45_tie,s45_build_cycles,s45_decode_cycles,s45_total_cycles,s45_issues,s45_prune_rate,s45_bound_rows,s45_utilisation,s45_context_wait,s45_fifo_max,s45_fifo_full_cycles,s45_fifo_empty_cycles,s45_max_bound_waiters,s45_build_normal_dp,s45_build_right_dp,s45_build_expansion,s45_build_triangular,s45_build_prefix,s45_cycles_x_luts,errors\n";
    Simulator sim;uint64_t errors=0,normal_total_sum=0,s45_total_sum=0;
    for(uint64_t offset=0;offset<count;++offset){Frame f=read_frame(trace,h,start+offset);auto r=sim.run(f);unsigned e=0;if(r.normal_metric!=f.reference_metric||r.normal_mask!=f.reference_mask||r.normal_tie!=bool(f.reference_tie))++e;if(r.s45_metric!=f.reference_metric||r.s45_mask!=f.reference_mask||r.s45_tie!=bool(f.reference_tie))++e;errors+=e;
        const uint64_t nt=uint64_t(r.normal_build)+r.normal_decode,st=uint64_t(r.s45_build)+r.s45_decode;normal_total_sum+=nt;s45_total_sum+=st;
        const double npr=1.0-double(r.normal_issues+1)/double(CLASSICAL_CANDIDATES),spr=1.0-double(r.s45_issues+1)/double(CLASSICAL_CANDIDATES);const double nu=r.normal_decode?double(r.normal_issues)/(4.0*r.normal_decode):0.0,su=r.s45_decode?double(r.s45_issues)/(4.0*r.s45_decode):0.0;
        csv<<f.index<<','<<f.soft_checksum<<','<<f.payload_checksum<<','<<f.seed_metric<<",0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<f.seed_mask<<std::dec<<std::setfill(' ')<<','<<int(f.seed_tie)<<','<<f.reference_metric<<",0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<f.reference_mask<<std::dec<<std::setfill(' ')<<','<<int(f.reference_tie)<<',';
        csv<<r.normal_metric<<",0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<r.normal_mask<<std::dec<<std::setfill(' ')<<','<<int(r.normal_tie)<<','<<r.normal_build<<','<<r.normal_decode<<','<<nt<<','<<r.normal_issues<<','<<std::setprecision(12)<<npr<<','<<r.normal_bound<<','<<nu<<','<<r.normal_wait<<','<<r.normal_max_fifo<<','<<r.normal_fifo_full<<','<<r.normal_fifo_empty<<','<<unsigned(r.normal_max_waiters)<<','<<r.normal_b_n<<','<<r.normal_b_r<<','<<r.normal_b_exp<<','<<r.normal_b_tri<<','<<r.normal_b_prefix<<','<<nt*NORMAL_LUTS<<',';
        csv<<r.s45_metric<<",0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<r.s45_mask<<std::dec<<std::setfill(' ')<<','<<int(r.s45_tie)<<','<<r.s45_build<<','<<r.s45_decode<<','<<st<<','<<r.s45_issues<<','<<std::setprecision(12)<<spr<<','<<r.s45_bound<<','<<su<<','<<r.s45_wait<<','<<r.s45_max_fifo<<','<<r.s45_fifo_full<<','<<r.s45_fifo_empty<<','<<unsigned(r.s45_max_waiters)<<','<<r.s45_b_n<<','<<r.s45_b_r<<','<<r.s45_b_exp<<','<<r.s45_b_tri<<','<<r.s45_b_prefix<<','<<st*S45_LUTS<<','<<e<<'\n';
        std::cout<<"CANONICAL_FRAME "<<f.index<<" errors="<<e<<" normal_total="<<nt<<" s45_total="<<st<<" normal_issues="<<r.normal_issues<<" s45_issues="<<r.s45_issues<<'\n';if(e){csv.flush();throw std::runtime_error("canonical replay mismatch");}}
    std::cout<<std::fixed<<std::setprecision(6)<<"CANONICAL_REPLAY_PASS start="<<start<<" count="<<count<<" errors="<<errors<<" normal_total_cycles="<<normal_total_sum<<" s45_total_cycles="<<s45_total_sum<<" ratio="<<(double(s45_total_sum)/normal_total_sum)<<'\n';return errors?1:0;}
