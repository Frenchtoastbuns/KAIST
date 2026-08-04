#define OSD_NOVELTY_COMPARISON
#define OSD_R3_DECISIVE_AUDIT
#define main parity_window_e2e_unused_main
#include "parity_window_end_to_end_benchmark.cc"
#undef main

#include <iomanip>
#include <string>

namespace {
constexpr int N = 128;
constexpr int K = 64;
constexpr int O = 4;
constexpr uint64_t CLASSICAL_TEPS = 679121;
constexpr uint64_t SEED = 0x5245434f4e5f3131ULL;

template<int DIMENSION>
std::array<int8_t, N * DIMENSION> extend_bch127(
    const std::array<int8_t, 127 * DIMENSION> &input
) {
    std::array<int8_t, N * DIMENSION> output{};
    for (int row = 0; row < DIMENSION; ++row) {
        uint8_t parity = 0;
        for (int column = 0; column < 127; ++column) {
            const uint8_t value = static_cast<uint8_t>(
                input[static_cast<std::size_t>(row * 127 + column)] & 1
            );
            output[static_cast<std::size_t>(row * N + column)] =
                static_cast<int8_t>(value);
            parity ^= value;
        }
        output[static_cast<std::size_t>(row * N + 127)] =
            static_cast<int8_t>(parity);
    }
    return output;
}

std::array<int8_t, N * K> generator() {
    std::array<int8_t, 127 * K> base{};
    CODE::BoseChaudhuriHocquenghemGenerator<127, K>::matrix(
        base.data(), true,
        {
            0b10001001, 0b10001111, 0b10011101,
            0b11110111, 0b10111111, 0b11010101,
            0b10000011, 0b11101111, 0b11001011
        }
    );
    return extend_bch127<K>(base);
}

bool differs(
    const std::array<uint8_t, (N + 7) / 8> &left,
    const std::array<uint8_t, (N + 7) / 8> &right
) {
    for (int position = 0; position < N; ++position)
        if (CODE::get_be_bit(left.data(), position) !=
            CODE::get_be_bit(right.data(), position))
            return true;
    return false;
}

experiment::Mode parse_mode(const std::string &name) {
    if (name == "specialized")
        return experiment::Mode::r3_specialized_dual_exact;
    if (name == "fused_specialized")
        return experiment::Mode::r3_fused_specialized_dual_exact;
    if (name == "fused_w1")
        return experiment::Mode::r3_fused_w1_oracle_exact;
    if (name == "fused_w1w2")
        return experiment::Mode::r3_fused_w1w2_oracle_exact;
    throw std::runtime_error("unknown mode: " + name);
}

uint64_t fnv_update(uint64_t value, uint8_t byte) {
    value ^= byte;
    return value * 1099511628211ULL;
}

template<class Container>
uint64_t fnv_container(const Container &container) {
    uint64_t value = 1469598103934665603ULL;
    for (const auto item : container)
        value = fnv_update(value, static_cast<uint8_t>(item));
    return value;
}
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: r3_decisive_audit MODE FRAMES [EBN0_DB] [START_FRAME]\n";
        return 2;
    }
    const std::string mode_name = argv[1];
    const int frames = std::stoi(argv[2]);
    const double ebn0_db = argc > 3 ? std::stod(argv[3]) : 0.0;
    const int start_frame = argc > 4 ? std::stoi(argv[4]) : 0;
    const experiment::Mode mode = parse_mode(mode_name);

    const auto gen = generator();
    const uint64_t generator_hash = fnv_container(gen);
    CODE::LinearEncoder<N, K> encoder;
    CODE::OrderedStatisticsDecoder<N, K, O> reference_decoder;
    CODE::OrderedStatisticsDecoder<N, K, O> test_decoder;
    std::mt19937_64 random(SEED);
    std::uniform_int_distribution<int> bit(0, 1);
    const double ebn0 = std::pow(10.0, ebn0_db / 10.0);
    const double rate = static_cast<double>(K) / N;
    std::normal_distribution<double> noise(
        0.0, std::sqrt(1.0 / (2.0 * rate * ebn0))
    );

    uint64_t stream_checksum = 1469598103934665603ULL;
    uint64_t word_mismatches = 0;
    uint64_t metric_mismatches = 0;
    uint64_t tie_mismatches = 0;
    uint64_t structural_checks = 0;

    std::cout
        << "frame,frame_hash,stream_checksum,teps,scoring_calls,bound_checks,"
        << "compact_lookups,dual_solver_ops,dual_updates,dual_group_solves,"
        << "oracle_candidates,oracle_w1_candidates,oracle_w2_candidates,"
        << "oracle_primitive_ops,dual_witness_candidates,dual_witness_primitive_ops,"
        << "score_primitive_ops,total_primitive_ops,ops_w1,ops_w2,ops_w3,"
        << "structural_checks,word_mismatch,metric_mismatch,tie_mismatch\n";

    for (int frame = 0; frame < start_frame + frames; ++frame) {
        std::array<uint8_t, (K + 7) / 8> message{};
        for (int position = 0; position < K; ++position)
            CODE::set_be_bit(message.data(), position, bit(random));
        std::array<uint8_t, (N + 7) / 8> encoded{};
        encoder(encoded.data(), message.data(), gen.data());
        std::array<int8_t, N> soft{};
        uint64_t frame_hash = 1469598103934665603ULL;
        for (int position = 0; position < N; ++position) {
            const double symbol = CODE::get_be_bit(encoded.data(), position)
                ? -1.0 : 1.0;
            const double received = symbol + noise(random);
            const int magnitude = std::max(
                1, std::min(127, static_cast<int>(
                    std::lround(std::abs(received) * 32.0)
                ))
            );
            soft[static_cast<std::size_t>(position)] =
                static_cast<int8_t>(received < 0 ? -magnitude : magnitude);
            const uint8_t byte = static_cast<uint8_t>(
                soft[static_cast<std::size_t>(position)]
            );
            frame_hash = fnv_update(frame_hash, byte);
            stream_checksum = fnv_update(stream_checksum, byte);
        }
        if (frame < start_frame)
            continue;

        const auto reference = experiment::decode_once<N, K, O>(
            reference_decoder, soft, gen, experiment::Mode::baseline
        );
        const auto result = experiment::decode_once<N, K, O>(
            test_decoder, soft, gen, mode
        );
        const bool word_mismatch = differs(result.decoded, reference.decoded);
        const bool metric_mismatch = result.best_metric != reference.best_metric;
        const bool tie_mismatch = result.unique != reference.unique;
        word_mismatches += word_mismatch;
        metric_mismatches += metric_mismatch;
        tie_mismatches += tie_mismatch;
        structural_checks += result.stats.r3_structural_checks;

        const uint64_t score_primitives =
            result.stats.scoring_calls * static_cast<uint64_t>(N - K);
        const uint64_t total_primitives =
            result.stats.dual_dp_transitions +
            result.stats.compact_table_lookups +
            result.stats.exact_oracle_primitive_ops +
            result.stats.dual_witness_primitive_ops +
            score_primitives + result.stats.bound_checks;

        std::cout
            << frame << ',' << frame_hash << ',' << stream_checksum << ','
            << result.stats.unique_teps_evaluated << ','
            << result.stats.scoring_calls << ','
            << result.stats.bound_checks << ','
            << result.stats.compact_table_lookups << ','
            << result.stats.dual_dp_transitions << ','
            << result.stats.dual_updates_executed << ','
            << result.stats.dual_group_solves << ','
            << result.stats.exact_oracle_candidates << ','
            << result.stats.exact_oracle_candidates_by_weight[1] << ','
            << result.stats.exact_oracle_candidates_by_weight[2] << ','
            << result.stats.exact_oracle_primitive_ops << ','
            << result.stats.dual_witness_candidates << ','
            << result.stats.dual_witness_primitive_ops << ','
            << score_primitives << ',' << total_primitives << ','
            << result.stats.dual_operations_by_weight[1] << ','
            << result.stats.dual_operations_by_weight[2] << ','
            << result.stats.dual_operations_by_weight[3] << ','
            << result.stats.r3_structural_checks << ','
            << word_mismatch << ',' << metric_mismatch << ',' << tie_mismatch
            << '\n';

        const int completed = frame - start_frame + 1;
        if (completed % 25 == 0 || completed == frames)
            std::cerr << mode_name << " frame " << completed << '/'
                << frames << " absolute=" << frame << " teps=" << result.stats.unique_teps_evaluated
                << " dual_ops=" << result.stats.dual_dp_transitions
                << " total_ops=" << total_primitives << '\n';
    }

    std::cerr << "FINAL mode=" << mode_name
        << " frames=" << frames
        << " start_frame=" << start_frame
        << " ebn0_db=" << std::fixed << std::setprecision(3) << ebn0_db
        << " seed=" << SEED
        << " generator_hash=" << generator_hash
        << " stream_checksum=" << stream_checksum
        << " structural_checks=" << structural_checks
        << " mismatches=" << word_mismatches << '/'
        << metric_mismatches << '/' << tie_mismatches << '\n';
    return (word_mismatches || metric_mismatches || tie_mismatches) ? 1 : 0;
}
