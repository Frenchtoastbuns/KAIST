// Controlled operation-count audit for the reconstructed 2026-07-29
// normal, compact two-block, and four-update dual CAP-PDB variants.
#define OSD_NOVELTY_COMPARISON
#define main parity_window_e2e_unused_main
#include "parity_window_end_to_end_benchmark.cc"
#undef main

#include <iomanip>

namespace {

#ifndef RECONSTRUCTED_AUDIT_FRAMES
#define RECONSTRUCTED_AUDIT_FRAMES 1000
#endif

#ifndef RECONSTRUCTED_AUDIT_SEED
#define RECONSTRUCTED_AUDIT_SEED 0x5245434f4e5f3131ULL
#endif
#ifndef RECONSTRUCTED_AUDIT_SKIP_DYNAMIC_DUAL
#define RECONSTRUCTED_AUDIT_SKIP_DYNAMIC_DUAL 0
#endif
#ifndef RECONSTRUCTED_AUDIT_SKIP_COMPILED_DUAL
#define RECONSTRUCTED_AUDIT_SKIP_COMPILED_DUAL 0
#endif

constexpr int N = 128;
constexpr int K = 64;
constexpr int O = 4;
constexpr double EBN0_DB = 0.0;
constexpr uint64_t CLASSICAL_TEPS = 679121;

template <int DIMENSION>
std::array<int8_t, 128 * DIMENSION> extend_bch127(
	const std::array<int8_t, 127 * DIMENSION> &input
)
{
	std::array<int8_t, 128 * DIMENSION> output{};
	for (int row = 0; row < DIMENSION; ++row) {
		uint8_t parity = 0;
		for (int column = 0; column < 127; ++column) {
			const uint8_t value = static_cast<uint8_t>(
				input[static_cast<std::size_t>(row * 127 + column)] & 1
			);
			output[static_cast<std::size_t>(row * 128 + column)] =
				static_cast<int8_t>(value);
			parity ^= value;
		}
		output[static_cast<std::size_t>(row * 128 + 127)] =
			static_cast<int8_t>(parity);
	}
	return output;
}

std::array<int8_t, N * K> ebch128_64()
{
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
)
{
	for (int position = 0; position < N; ++position)
		if (
			CODE::get_be_bit(left.data(), position) !=
			CODE::get_be_bit(right.data(), position)
		)
			return true;
	return false;
}

struct Aggregate {
	uint64_t frames = 0;
	uint64_t word_mismatches = 0;
	uint64_t metric_mismatches = 0;
	uint64_t tie_mismatches = 0;
	long double unique_teps = 0;
	long double scoring_calls = 0;
	long double bound_checks = 0;
	long double pdb_lookups = 0;
	long double compact_lookups = 0;
	long double dual_queries = 0;
	long double dual_transitions = 0;
	long double compiled_lookups = 0;
	long double compiled_build_transitions = 0;
	long double incumbent_syncs = 0;
	long double incumbent_batch_items = 0;
	long double bytes = 0;
};

template <class Result>
void accumulate(
	Aggregate &aggregate,
	const Result &result,
	const Result &reference
)
{
	++aggregate.frames;
	aggregate.word_mismatches += differs(result.decoded, reference.decoded);
	aggregate.metric_mismatches +=
		result.best_metric != reference.best_metric;
	aggregate.tie_mismatches += result.unique != reference.unique;
	aggregate.unique_teps += result.stats.unique_teps_evaluated;
	aggregate.scoring_calls += result.stats.scoring_calls;
	aggregate.bound_checks += result.stats.bound_checks;
	aggregate.pdb_lookups += result.stats.pdb_table_lookups;
	aggregate.compact_lookups += result.stats.compact_table_lookups;
	aggregate.dual_queries += result.stats.dual_queries;
	aggregate.dual_transitions += result.stats.dual_dp_transitions;
	aggregate.compiled_lookups += result.stats.compiled_dual_lookups;
	aggregate.compiled_build_transitions +=
		result.stats.compiled_dual_build_transitions;
	aggregate.incumbent_syncs += result.stats.incumbent_syncs;
	aggregate.incumbent_batch_items += result.stats.incumbent_batch_items;
	aggregate.bytes += result.stats.dp_bytes;
	assert(result.stats.unique_teps_evaluated <= CLASSICAL_TEPS);
}

void print(const char *method, const Aggregate &value)
{
	const long double frames = value.frames;
	const long double unique = value.unique_teps / frames;
	std::cout << method << ',' << value.frames << ','
		<< std::fixed << std::setprecision(3)
		<< static_cast<double>(unique) << ','
		<< static_cast<double>(value.scoring_calls / frames) << ','
		<< static_cast<double>(value.bound_checks / frames) << ','
		<< static_cast<double>(value.pdb_lookups / frames) << ','
		<< static_cast<double>(value.compact_lookups / frames) << ','
		<< static_cast<double>(value.dual_queries / frames) << ','
		<< static_cast<double>(value.dual_transitions / frames) << ','
		<< static_cast<double>(value.compiled_lookups / frames) << ','
		<< static_cast<double>(
			value.compiled_build_transitions / frames
		) << ','
		<< static_cast<double>(value.incumbent_syncs / frames) << ','
		<< static_cast<double>(value.incumbent_batch_items / frames) << ','
		<< static_cast<double>(value.bytes / frames) << ','
		<< static_cast<double>(
			100.0L * (1.0L - unique / CLASSICAL_TEPS)
		) << ','
		<< value.word_mismatches << ','
		<< value.metric_mismatches << ','
		<< value.tie_mismatches << '\n';
}

} // namespace

int main()
{
	const auto generator = ebch128_64();
	CODE::LinearEncoder<N, K> encoder;
	CODE::OrderedStatisticsDecoder<N, K, O> classical_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> normal_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> compact_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> dual_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> compiled_decoder;
	std::mt19937_64 random(RECONSTRUCTED_AUDIT_SEED);
	std::uniform_int_distribution<int> bit(0, 1);
	const double ebn0 = std::pow(10.0, EBN0_DB / 10.0);
	const double rate = static_cast<double>(K) / N;
	std::normal_distribution<double> noise(
		0.0, std::sqrt(1.0 / (2.0 * rate * ebn0))
	);
	uint64_t frame_checksum = 1469598103934665603ULL;
	Aggregate normal{};
	Aggregate compact{};
	Aggregate dual{};
	Aggregate compiled{};

	for (int frame = 0; frame < RECONSTRUCTED_AUDIT_FRAMES; ++frame) {
		std::array<uint8_t, (K + 7) / 8> message{};
		for (int position = 0; position < K; ++position)
			CODE::set_be_bit(message.data(), position, bit(random));
		std::array<uint8_t, (N + 7) / 8> encoded{};
		encoder(encoded.data(), message.data(), generator.data());
		std::array<int8_t, N> soft{};
		for (int position = 0; position < N; ++position) {
			const double symbol = CODE::get_be_bit(
				encoded.data(), position
			) ? -1.0 : 1.0;
			const double received = symbol + noise(random);
			const int magnitude = std::max(
				1, std::min(127, static_cast<int>(
					std::lround(std::abs(received) * 32.0)
				))
			);
			soft[static_cast<std::size_t>(position)] =
				static_cast<int8_t>(received < 0 ? -magnitude : magnitude);
			frame_checksum ^= static_cast<uint8_t>(
				soft[static_cast<std::size_t>(position)]
			);
			frame_checksum *= 1099511628211ULL;
		}

		const auto reference = experiment::decode_once<N, K, O>(
			classical_decoder, soft, generator, experiment::Mode::baseline
		);
		const auto normal_result = experiment::decode_once<N, K, O>(
			normal_decoder, soft, generator,
			experiment::Mode::reconstructed_normal_cap_pdb_exact
		);
		const auto compact_result = experiment::decode_once<N, K, O>(
			compact_decoder, soft, generator,
			experiment::Mode::reconstructed_compact_two_block_pdb_exact
		);
		if constexpr (!RECONSTRUCTED_AUDIT_SKIP_DYNAMIC_DUAL) {
			const auto dual_result = experiment::decode_once<N, K, O>(
				dual_decoder, soft, generator,
				experiment::Mode::reconstructed_dual_two_block_pdb_exact
			);
			accumulate(dual, dual_result, reference);
		}
		if constexpr (!RECONSTRUCTED_AUDIT_SKIP_COMPILED_DUAL) {
			const auto compiled_result = experiment::decode_once<N, K, O>(
				compiled_decoder, soft, generator,
				experiment::Mode::reconstructed_compiled_dual_bank_pdb_exact
			);
			accumulate(compiled, compiled_result, reference);
		}
		accumulate(normal, normal_result, reference);
		accumulate(compact, compact_result, reference);
	}

	std::cout << "# configuration=eBCH(128,64),OSD-4,BPSK-AWGN,0dB"
		<< ",frames=" << RECONSTRUCTED_AUDIT_FRAMES
		<< ",seed=" << RECONSTRUCTED_AUDIT_SEED
		<< ",frame_checksum=" << frame_checksum << '\n';
	std::cout << "method,frames,unique_teps_per_frame,scoring_calls_per_frame,"
		"bound_checks_per_frame,pdb_lookups_per_frame,"
		"compact_lookups_per_frame,dual_queries_per_frame,"
		"dual_dp_transitions_per_frame,compiled_dual_lookups_per_frame,"
		"compiled_dual_build_transitions_per_frame,"
		"incumbent_syncs_per_frame,incumbent_batch_items_per_frame,"
		"auxiliary_bytes_per_frame,"
		"unique_tep_reduction_vs_classical_percent,word_mismatches,"
		"metric_mismatches,tie_mismatches\n";
	std::cout << "classical_osd," << RECONSTRUCTED_AUDIT_FRAMES << ','
		<< CLASSICAL_TEPS << ',' << CLASSICAL_TEPS
		<< ",0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
	print("normal_cap_pdb", normal);
	print("compact_two_block_cap_pdb", compact);
	if constexpr (!RECONSTRUCTED_AUDIT_SKIP_DYNAMIC_DUAL)
		print("dual_two_block_cap_pdb", dual);
	if constexpr (!RECONSTRUCTED_AUDIT_SKIP_COMPILED_DUAL)
		print("compiled_dual_bank_cap_pdb", compiled);
	return (
		normal.word_mismatches || normal.metric_mismatches ||
		normal.tie_mismatches || compact.word_mismatches ||
		compact.metric_mismatches || compact.tie_mismatches ||
		dual.word_mismatches || dual.metric_mismatches ||
		dual.tie_mismatches || compiled.word_mismatches ||
		compiled.metric_mismatches || compiled.tie_mismatches
	) ? 1 : 0;
}
