// Canonical trace-derived cycle audit for the parallel compact CAP-PDB engine.
//
// The decoder executes the unchanged exact compact algorithm.  Instrumentation
// inside compact_lower_bound() evaluates the exact reachable masks encountered
// chronologically and accumulates state-wide engine cycles for P={1,2,4,8,16}.
#define OSD_NOVELTY_COMPARISON
#define main parity_window_e2e_unused_main
#include "parity_window_end_to_end_benchmark.cc"
#undef main

#include <iomanip>

namespace {

#ifndef COMPACT_PARALLEL_TRACE_FRAMES
#define COMPACT_PARALLEL_TRACE_FRAMES 1000
#endif

#ifndef COMPACT_PARALLEL_TRACE_SEED
#define COMPACT_PARALLEL_TRACE_SEED 0x5245434f4e5f3131ULL
#endif

constexpr int N = 128;
constexpr int K = 64;
constexpr int O = 4;
constexpr double EBN0_DB = 0.0;

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

} // namespace

int main()
{
	const auto generator = ebch128_64();
	CODE::LinearEncoder<N, K> encoder;
	CODE::OrderedStatisticsDecoder<N, K, O> classical_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> normal_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> compact_decoder;
	std::mt19937_64 random(COMPACT_PARALLEL_TRACE_SEED);
	std::uniform_int_distribution<int> bit(0, 1);
	const double ebn0 = std::pow(10.0, EBN0_DB / 10.0);
	const double rate = static_cast<double>(K) / N;
	std::normal_distribution<double> noise(
		0.0, std::sqrt(1.0 / (2.0 * rate * ebn0))
	);
	uint64_t stream_checksum = 1469598103934665603ULL;
	uint64_t mismatches = 0;

	std::cout
		<< "frame,frame_checksum,normal_unique_teps,normal_scoring_calls,"
		<< "normal_bound_checks,normal_pdb_lookups,normal_bound_cycles,"
		<< "compact_unique_teps,compact_scoring_calls,"
		<< "compact_bound_checks,compact_table_lookups,"
		<< "compact_allocations,cycles_p1,cycles_p2,cycles_p4,cycles_p8,"
		<< "cycles_p16,word_mismatch,metric_mismatch,tie_mismatch\n";

	for (int frame = 0; frame < COMPACT_PARALLEL_TRACE_FRAMES; ++frame) {
		std::array<uint8_t, (K + 7) / 8> message{};
		for (int position = 0; position < K; ++position)
			CODE::set_be_bit(message.data(), position, bit(random));
		std::array<uint8_t, (N + 7) / 8> encoded{};
		encoder(encoded.data(), message.data(), generator.data());
		std::array<int8_t, N> soft{};
		uint64_t frame_checksum = 1469598103934665603ULL;
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
			const auto sample = static_cast<uint8_t>(
				soft[static_cast<std::size_t>(position)]
			);
			frame_checksum ^= sample;
			frame_checksum *= 1099511628211ULL;
			stream_checksum ^= sample;
			stream_checksum *= 1099511628211ULL;
		}

		const auto reference = experiment::decode_once<N, K, O>(
			classical_decoder, soft, generator, experiment::Mode::baseline
		);
		const auto normal = experiment::decode_once<N, K, O>(
			normal_decoder, soft, generator,
			experiment::Mode::reconstructed_normal_cap_pdb_exact
		);
		const auto compact = experiment::decode_once<N, K, O>(
			compact_decoder, soft, generator,
			experiment::Mode::reconstructed_compact_two_block_pdb_exact
		);

		const bool word_mismatch = differs(compact.decoded, reference.decoded);
		const bool metric_mismatch =
			compact.best_metric != reference.best_metric;
		const bool tie_mismatch = compact.unique != reference.unique;
		mismatches += word_mismatch + metric_mismatch + tie_mismatch;
		assert(normal.decoded == reference.decoded);
		assert(normal.best_metric == reference.best_metric);
		assert(normal.unique == reference.unique);

		std::cout << frame << ',' << frame_checksum << ','
			<< normal.stats.unique_teps_evaluated << ','
			<< normal.stats.scoring_calls << ','
			<< normal.stats.bound_checks << ','
			<< normal.stats.pdb_table_lookups << ','
			<< 2 * normal.stats.pdb_table_lookups / 13 << ','
			<< compact.stats.unique_teps_evaluated << ','
			<< compact.stats.scoring_calls << ','
			<< compact.stats.bound_checks << ','
			<< compact.stats.compact_table_lookups << ','
			<< compact.stats.compact_parallel_allocations;
		for (const auto cycles : compact.stats.compact_parallel_cycles)
			std::cout << ',' << cycles;
		std::cout << ',' << word_mismatch << ',' << metric_mismatch << ','
			<< tie_mismatch << '\n';
	}

	std::cerr
		<< "configuration=eBCH(128,64),OSD-4,BPSK-AWGN,0dB"
		<< ",frames=" << COMPACT_PARALLEL_TRACE_FRAMES
		<< ",seed=" << COMPACT_PARALLEL_TRACE_SEED
		<< ",stream_checksum=" << stream_checksum
		<< ",mismatches=" << mismatches << '\n';
	return mismatches ? 1 : 0;
}
