#include <verilated.h>
#include "Vcap_nonblocking_pair_top.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {
constexpr int K = 64;
constexpr int GROUPS = 13;
constexpr int STATES = 32;
constexpr uint64_t DEFAULT_SEED = 0x4341505f42415443ULL;

using Rows = std::array<std::array<uint8_t, K>, GROUPS>;
using Phi = std::array<std::array<uint16_t, STATES>, GROUPS>;
using Info = std::array<uint8_t, K>;

struct Oracle {
    uint16_t seed_metric = 0;
    uint16_t best_metric = std::numeric_limits<uint16_t>::max();
    uint64_t best_mask = 0;
    bool tied = false;
};

struct Frame {
    Rows rows{};
    Phi phi{};
    Info info{};
    uint64_t base_states = 0;
    Oracle oracle{};
};

uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int group_state(uint64_t packed_states, int group) {
    return group == 12 ? int((packed_states >> 60) & 0x0fULL)
                       : int((packed_states >> (group * 5)) & 0x1fULL);
}

uint64_t xor_row(uint64_t packed_states, int rank, const Rows& rows) {
    for (int group = 0; group < 12; ++group)
        packed_states ^= uint64_t(rows[group][rank]) << (group * 5);
    packed_states ^= uint64_t(rows[12][rank] & 0x0fU) << 60;
    return packed_states;
}

int popcount64(uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int count = 0;
    while (value) { count += int(value & 1ULL); value >>= 1; }
    return count;
#endif
}

bool canonical_before(uint64_t left, uint64_t right) {
    const int lw = popcount64(left);
    const int rw = popcount64(right);
    if (lw != rw) return lw < rw;
    for (int bit = 0; bit < K; ++bit) {
        const bool lb = ((left >> bit) & 1ULL) != 0;
        const bool rb = ((right >> bit) & 1ULL) != 0;
        if (lb != rb) return lb;
    }
    return false;
}

uint16_t metric(uint64_t states, uint16_t information,
                const Phi& phi) {
    uint32_t value = information;
    for (int group = 0; group < GROUPS; ++group)
        value += phi[group][group_state(states, group)];
    if (value >= (1U << 14))
        throw std::runtime_error("metric exceeds RTL width");
    return static_cast<uint16_t>(value);
}

void consider(Oracle& oracle, uint64_t mask, uint16_t information,
              uint64_t states, const Phi& phi) {
    const uint16_t value = metric(states, information, phi);
    if (value < oracle.best_metric) {
        oracle.best_metric = value;
        oracle.best_mask = mask;
        oracle.tied = false;
    } else if (value == oracle.best_metric && mask != oracle.best_mask) {
        oracle.tied = true;
        if (canonical_before(mask, oracle.best_mask)) oracle.best_mask = mask;
    }
}

Frame make_frame(uint64_t master_seed, uint64_t frame_index) {
    Frame frame;
    std::mt19937_64 rng(splitmix64(master_seed ^ splitmix64(frame_index)));

    for (int group = 0; group < GROUPS; ++group) {
        for (int rank = 0; rank < K; ++rank) {
            const uint8_t mask = group == 12 ? 0x0fU : 0x1fU;
            frame.rows[group][rank] = static_cast<uint8_t>(rng()) & mask;
        }
    }
    for (int group = 0; group < GROUPS; ++group) {
        for (int state = 0; state < STATES; ++state) {
            frame.phi[group][state] = static_cast<uint16_t>(rng() % 48U)
                                    + static_cast<uint16_t>(state == 0 ? 0 : 1);
        }
    }

    uint16_t reliability = 1;
    for (int rank = 0; rank < K; ++rank) {
        reliability = static_cast<uint16_t>(reliability + (rng() & 1ULL));
        if (reliability > 120) reliability = 120;
        frame.info[rank] = static_cast<uint8_t>(reliability);
    }
    frame.base_states = rng();

    Oracle oracle;
    oracle.seed_metric = metric(frame.base_states, 0, frame.phi);
    oracle.best_metric = oracle.seed_metric;
    oracle.best_mask = 0;
    oracle.tied = false;

    for (int i = 0; i < K; ++i) {
        const uint64_t s1 = xor_row(frame.base_states, i, frame.rows);
        const uint16_t c1 = frame.info[i];
        const uint64_t m1 = 1ULL << i;
        consider(oracle, m1, c1, s1, frame.phi);
        for (int j = i + 1; j < K; ++j) {
            const uint64_t s2 = xor_row(s1, j, frame.rows);
            const uint16_t c2 = static_cast<uint16_t>(c1 + frame.info[j]);
            const uint64_t m2 = m1 | (1ULL << j);
            consider(oracle, m2, c2, s2, frame.phi);
            for (int k = j + 1; k < K; ++k) {
                const uint64_t s3 = xor_row(s2, k, frame.rows);
                const uint16_t c3 = static_cast<uint16_t>(c2 + frame.info[k]);
                const uint64_t m3 = m2 | (1ULL << k);
                consider(oracle, m3, c3, s3, frame.phi);
                for (int l = k + 1; l < K; ++l) {
                    const uint64_t s4 = xor_row(s3, l, frame.rows);
                    const uint16_t c4 = static_cast<uint16_t>(c3 + frame.info[l]);
                    const uint64_t m4 = m3 | (1ULL << l);
                    consider(oracle, m4, c4, s4, frame.phi);
                }
            }
        }
    }
    frame.oracle = oracle;
    return frame;
}

unsigned popcount4(uint8_t value) {
    return unsigned(value & 1U) + unsigned((value >> 1) & 1U)
         + unsigned((value >> 2) & 1U) + unsigned((value >> 3) & 1U);
}

class Simulator {
public:
    Simulator() : top_(new Vcap_nonblocking_pair_top) {
        top_->clk = 0;
        top_->rst = 1;
        top_->cfg_row_we = 0;
        top_->cfg_phi_we = 0;
        top_->cfg_info_we = 0;
        top_->build_start = 0;
        top_->decode_start = 0;
        for (int i = 0; i < 4; ++i) tick(false);
        top_->rst = 0;
        tick(false);
    }

    ~Simulator() {
        top_->final();
        delete top_;
    }

    struct Result {
        uint64_t normal_issues = 0;
        uint64_t s45_issues = 0;
        uint32_t normal_build_cycles = 0;
        uint32_t s45_build_cycles = 0;
        uint32_t normal_decode_cycles = 0;
        uint32_t s45_decode_cycles = 0;
        uint32_t normal_bound_rows = 0;
        uint32_t s45_bound_rows = 0;
        uint32_t normal_wait = 0;
        uint32_t s45_wait = 0;
        uint16_t normal_metric = 0;
        uint16_t s45_metric = 0;
        uint64_t normal_mask = 0;
        uint64_t s45_mask = 0;
        bool normal_tie = false;
        bool s45_tie = false;
    };

    Result run(const Frame& frame) {
        configure(frame);
        top_->cfg_base_states = frame.base_states;
        top_->cfg_seed_best_metric = frame.oracle.seed_metric;
        top_->cfg_seed_mask = 0;
        top_->cfg_seed_tie = 0;

        bool normal_build_done = false;
        bool s45_build_done = false;
        top_->build_start = 1;
        tick(false);
        top_->build_start = 0;
        for (uint64_t guard = 0; !(normal_build_done && s45_build_done); ++guard) {
            if (guard > 200000ULL) throw std::runtime_error("builder timeout");
            tick(false);
            normal_build_done = normal_build_done || top_->normal_build_done;
            s45_build_done = s45_build_done || top_->s45_build_done;
        }

        Result result;
        result.normal_build_cycles = top_->normal_build_cycles;
        result.s45_build_cycles = top_->s45_build_cycles;

        bool normal_decode_done = false;
        bool s45_decode_done = false;
        top_->decode_start = 1;
        tick(true, &result);
        top_->decode_start = 0;
        for (uint64_t guard = 0; !(normal_decode_done && s45_decode_done); ++guard) {
            if (guard > 5000000ULL) throw std::runtime_error("decode timeout");
            tick(true, &result);
            normal_decode_done = normal_decode_done || top_->normal_decode_done;
            s45_decode_done = s45_decode_done || top_->s45_decode_done;
        }

        result.normal_decode_cycles = top_->normal_decode_cycles;
        result.s45_decode_cycles = top_->s45_decode_cycles;
        result.normal_bound_rows = top_->normal_bound_rows;
        result.s45_bound_rows = top_->s45_bound_rows;
        result.normal_wait = top_->normal_context_wait;
        result.s45_wait = top_->s45_context_wait;
        result.normal_metric = top_->normal_best_metric;
        result.s45_metric = top_->s45_best_metric;
        result.normal_mask = top_->normal_best_tep;
        result.s45_mask = top_->s45_best_tep;
        result.normal_tie = top_->normal_best_tied;
        result.s45_tie = top_->s45_best_tied;
        return result;
    }

private:
    Vcap_nonblocking_pair_top* top_;

    void tick(bool count_issues, Result* result = nullptr) {
        top_->clk = 0;
        top_->eval();
        top_->clk = 1;
        top_->eval();
        if (count_issues && result != nullptr) {
            result->normal_issues += popcount4(top_->normal_issue_mask);
            result->s45_issues += popcount4(top_->s45_issue_mask);
        }
    }

    void configure(const Frame& frame) {
        top_->cfg_row_we = 1;
        for (int group = 0; group < GROUPS; ++group) {
            for (int rank = 0; rank < K; ++rank) {
                top_->cfg_row_group = group;
                top_->cfg_row_rank = rank;
                top_->cfg_row_effect = frame.rows[group][rank];
                tick(false);
            }
        }
        top_->cfg_row_we = 0;

        top_->cfg_phi_we = 1;
        for (int group = 0; group < GROUPS; ++group) {
            for (int state = 0; state < STATES; ++state) {
                top_->cfg_phi_group = group;
                top_->cfg_phi_state = state;
                top_->cfg_phi_cost = frame.phi[group][state];
                tick(false);
            }
        }
        top_->cfg_phi_we = 0;

        top_->cfg_info_we = 1;
        for (int rank = 0; rank < K; ++rank) {
            top_->cfg_info_rank = rank;
            top_->cfg_info_cost = frame.info[rank];
            tick(false);
        }
        top_->cfg_info_we = 0;
        tick(false);
    }
};

uint64_t parse_u64(const char* text) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0') throw std::runtime_error("invalid integer argument");
    return static_cast<uint64_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    uint64_t start = 0;
    uint64_t count = 1;
    uint64_t seed = DEFAULT_SEED;
    std::string csv_path = "batch_results.csv";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--start" && i + 1 < argc) start = parse_u64(argv[++i]);
        else if (arg == "--count" && i + 1 < argc) count = parse_u64(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = parse_u64(argv[++i]);
        else if (arg == "--csv" && i + 1 < argc) csv_path = argv[++i];
        else throw std::runtime_error("unknown or incomplete argument: " + arg);
    }

    std::ofstream csv(csv_path);
    if (!csv) throw std::runtime_error("cannot open CSV output");
    csv << "frame,seed_metric,expected_metric,expected_mask,expected_tie,"
           "normal_metric,normal_mask,normal_tie,normal_build_cycles,normal_decode_cycles,normal_issues,normal_bound_rows,normal_wait,"
           "s45_metric,s45_mask,s45_tie,s45_build_cycles,s45_decode_cycles,s45_issues,s45_bound_rows,s45_wait,errors\n";

    Simulator simulator;
    uint64_t errors = 0;
    uint64_t normal_cycles_sum = 0, s45_cycles_sum = 0;
    uint64_t normal_issues_sum = 0, s45_issues_sum = 0;
    uint64_t tie_frames = 0;

    for (uint64_t offset = 0; offset < count; ++offset) {
        const uint64_t frame_index = start + offset;
        const Frame frame = make_frame(seed, frame_index);
        if (frame.oracle.tied) ++tie_frames;
        const Simulator::Result result = simulator.run(frame);

        unsigned frame_errors = 0;
        if (result.normal_metric != frame.oracle.best_metric ||
            result.normal_mask != frame.oracle.best_mask ||
            result.normal_tie != frame.oracle.tied) ++frame_errors;
        if (result.s45_metric != frame.oracle.best_metric ||
            result.s45_mask != frame.oracle.best_mask ||
            result.s45_tie != frame.oracle.tied) ++frame_errors;
        errors += frame_errors;
        normal_cycles_sum += result.normal_decode_cycles;
        s45_cycles_sum += result.s45_decode_cycles;
        normal_issues_sum += result.normal_issues;
        s45_issues_sum += result.s45_issues;

        csv << frame_index << ',' << frame.oracle.seed_metric << ','
            << frame.oracle.best_metric << ",0x" << std::hex << std::setw(16)
            << std::setfill('0') << frame.oracle.best_mask << std::dec << std::setfill(' ')
            << ',' << int(frame.oracle.tied) << ','
            << result.normal_metric << ",0x" << std::hex << std::setw(16)
            << std::setfill('0') << result.normal_mask << std::dec << std::setfill(' ')
            << ',' << int(result.normal_tie) << ',' << result.normal_build_cycles
            << ',' << result.normal_decode_cycles << ',' << result.normal_issues
            << ',' << result.normal_bound_rows << ',' << result.normal_wait << ','
            << result.s45_metric << ",0x" << std::hex << std::setw(16)
            << std::setfill('0') << result.s45_mask << std::dec << std::setfill(' ')
            << ',' << int(result.s45_tie) << ',' << result.s45_build_cycles
            << ',' << result.s45_decode_cycles << ',' << result.s45_issues
            << ',' << result.s45_bound_rows << ',' << result.s45_wait << ','
            << frame_errors << '\n';

        std::cout << "FRAME " << frame_index << " errors=" << frame_errors
                  << " normal_cycles=" << result.normal_decode_cycles
                  << " normal_issues=" << result.normal_issues
                  << " s45_cycles=" << result.s45_decode_cycles
                  << " s45_issues=" << result.s45_issues << '\n';
    }

    const double normal_util = normal_cycles_sum == 0 ? 0.0
        : double(normal_issues_sum) / (4.0 * double(normal_cycles_sum));
    const double s45_util = s45_cycles_sum == 0 ? 0.0
        : double(s45_issues_sum) / (4.0 * double(s45_cycles_sum));
    std::cout << std::fixed << std::setprecision(6)
              << "BATCH_SUMMARY start=" << start << " count=" << count
              << " errors=" << errors << " tie_frames=" << tie_frames
              << " normal_cycles=" << normal_cycles_sum
              << " normal_issues=" << normal_issues_sum
              << " normal_util=" << normal_util
              << " s45_cycles=" << s45_cycles_sum
              << " s45_issues=" << s45_issues_sum
              << " s45_util=" << s45_util << '\n';
    return errors == 0 ? 0 : 1;
}
