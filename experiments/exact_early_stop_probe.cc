#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace experiment {

bool search_override(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	const int16_t *permutation,
	int length,
	int dimension,
	int width,
	int order,
	int &best,
	int &next
);
void search_complete(
	int best,
	int next,
	const int8_t *candidate,
	const int16_t *permutation,
	int length,
	int width
);

}

#define CODE_OSD_SEARCH_OVERRIDE(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next) \
	experiment::search_override(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next)
#define CODE_OSD_SEARCH_COMPLETE(best, next, candidate, permutation, length, width) \
	experiment::search_complete(best, next, candidate, permutation, length, width)

#include "bitman.hh"
#include "osd.hh"

namespace experiment {

using Clock = std::chrono::steady_clock;
constexpr int N = 127;
constexpr int K = 64;
constexpr int O = 4;
constexpr int W = 128;
#ifndef EXACT_STOP_FRAMES
#define EXACT_STOP_FRAMES 100
#endif
constexpr int FRAMES = EXACT_STOP_FRAMES;
constexpr uint64_t EXPECTED_CANDIDATES = 679121;

bool early_stop_enabled = false;
bool stop_taken = false;
bool capture_enabled = false;
bool capture_only_enabled = false;

struct Completion {
	int best = 0;
	int next = -1;
	std::array<int8_t, N> candidate{};
};

Completion completion;

struct SearchCapture {
	std::array<int8_t, K * W> matrix{};
	std::array<int8_t, W> base{};
	std::array<int8_t, W> soft{};
};

SearchCapture capture;

bool search_override(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	const int16_t *,
	int length,
	int dimension,
	int width,
	int order,
	int &best,
	int &next
)
{
	if (capture_enabled) {
		std::copy(matrix, matrix + K * W, capture.matrix.begin());
		std::copy(base, base + W, capture.base.begin());
		std::copy(soft, soft + W, capture.soft.begin());
	}
	if (capture_only_enabled) {
		int metric = 0;
		for (int index = 0; index < length; ++index)
			metric += (1 - 2 * base[index]) * soft[index];
		std::copy(base, base + length, candidate);
		for (int index = length; index < width; ++index)
			candidate[index] = 0;
		best = metric;
		next = -1;
		return true;
	}
	if (!early_stop_enabled)
		return false;
	assert(length == N);
	assert(dimension == K);
	assert(width == W);
	assert(order == O);

	int metric = 0;
	int upper_bound = 0;
	for (int index = 0; index < length; ++index) {
		assert(soft[index] != 0);
		metric += (1 - 2 * base[index]) * soft[index];
		upper_bound += std::abs(static_cast<int>(soft[index]));
	}
	if (metric != upper_bound)
		return false;

	std::copy(base, base + length, candidate);
	for (int index = length; index < width; ++index)
		candidate[index] = 0;
	best = metric;
	next = -1;
	stop_taken = true;
	return true;
}

void search_complete(
	int best,
	int next,
	const int8_t *candidate,
	const int16_t *,
	int length,
	int
)
{
	assert(length == N);
	completion.best = best;
	completion.next = next;
	std::copy(candidate, candidate + length, completion.candidate.begin());
}

struct DecodeResult {
	bool unique = false;
	bool stopped = false;
	uint64_t elapsed_ns = 0;
	std::array<uint8_t, (N + 7) / 8> decoded{};
	Completion completion_state;
};

DecodeResult decode_once(
	CODE::OrderedStatisticsDecoder<N, K, O> &decoder,
	const std::array<int8_t, N> &soft,
	const int8_t *genmat,
	bool enable_stop
)
{
	early_stop_enabled = enable_stop;
	stop_taken = false;
	DecodeResult result;
	const auto start = Clock::now();
	result.unique = decoder(result.decoded.data(), soft.data(), genmat);
	const auto end = Clock::now();
	result.elapsed_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	result.stopped = stop_taken;
	result.completion_state = completion;
	return result;
}

void capture_once(
	CODE::OrderedStatisticsDecoder<N, K, O> &decoder,
	const std::array<int8_t, N> &soft,
	const int8_t *genmat
)
{
	capture_enabled = true;
	capture_only_enabled = true;
	decode_once(decoder, soft, genmat, false);
	capture_only_enabled = false;
	capture_enabled = false;
}

uint64_t percentile(std::vector<uint64_t> values, double fraction)
{
	std::sort(values.begin(), values.end());
	const std::size_t index = static_cast<std::size_t>(
		std::ceil(values.size() * fraction)
	) - 1;
	return values[index];
}

struct SnrResult {
	double snr_db = 0;
	uint64_t exact_stops = 0;
	uint64_t baseline_total_ns = 0;
	uint64_t stopped_total_ns = 0;
	uint64_t baseline_p95_ns = 0;
	uint64_t stopped_p95_ns = 0;
	uint64_t bound_candidates_total = 0;
	uint64_t combined_candidates_total = 0;
	uint64_t bound_p95_candidates = 0;
	uint64_t combined_p95_candidates = 0;
	uint64_t pruned_tasks_total = 0;
};

struct BoundResult {
	uint64_t candidates = 0;
	uint64_t pruned_tasks = 0;
};

BoundResult analyze_subtree_bound(int best, int next)
{
	constexpr uint64_t SEED_CANDIDATES =
		1 + K + static_cast<uint64_t>(K) * (K - 1) / 2;
	BoundResult result;
	result.candidates = EXPECTED_CANDIDATES;
	std::array<std::array<uint8_t, N - K>, K + 1> parity_reachable{};
	for (int row = K - 1; row >= 0; --row) {
		parity_reachable[static_cast<std::size_t>(row)] =
			parity_reachable[static_cast<std::size_t>(row + 1)];
		for (int parity = 0; parity < N - K; ++parity)
			if (capture.matrix[row * W + K + parity])
				parity_reachable[static_cast<std::size_t>(row)]
					[static_cast<std::size_t>(parity)] = 1;
	}

	for (int a = 0; a < K; ++a) {
		for (int b = a + 1; b < K; ++b) {
			std::array<int8_t, W> word = capture.base;
			for (const int row: {a, b})
				for (int offset = 0; offset < W; ++offset)
					word[static_cast<std::size_t>(offset)] ^=
						capture.matrix[row * W + offset];

			int systematic_metric = 0;
			for (int index = 0; index < K; ++index)
				systematic_metric +=
					(1 - 2 * word[static_cast<std::size_t>(index)]) *
					capture.soft[index];
			int first_gain = 0;
			int second_gain = 0;
			for (int index = b + 1; index < K; ++index) {
				const int gain = -2 *
					(1 - 2 * word[static_cast<std::size_t>(index)]) *
					capture.soft[index];
				if (gain > first_gain) {
					second_gain = first_gain;
					first_gain = gain;
				} else if (gain > second_gain) {
					second_gain = gain;
				}
			}
			int parity_upper_bound = 0;
			for (int index = K; index < N; ++index) {
				if (parity_reachable[static_cast<std::size_t>(b + 1)]
					[static_cast<std::size_t>(index - K)]) {
					parity_upper_bound += std::abs(
						static_cast<int>(capture.soft[index])
					);
				} else {
					parity_upper_bound +=
						(1 - 2 * word[static_cast<std::size_t>(index)]) *
						capture.soft[index];
				}
			}
			const int upper_bound = systematic_metric +
				first_gain + second_gain + parity_upper_bound;
			if (upper_bound <= next && upper_bound < best) {
				const uint64_t remaining =
					static_cast<uint64_t>(K - b - 1);
				const uint64_t descendants =
					remaining + remaining * (remaining - 1) / 2;
				if (descendants) {
					result.candidates -= descendants;
					++result.pruned_tasks;
				}
			}
		}
	}
	assert(result.candidates >= SEED_CANDIDATES);
	return result;
}

}

int main(int argc, char **argv)
{
	using namespace experiment;
	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		{
			0b10001001, 0b10001111, 0b10011101,
			0b11110111, 0b10111111, 0b11010101,
			0b10000011, 0b11101111, 0b11001011
		}
	);

	CODE::LinearEncoder<N, K> encoder;
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	std::mt19937_64 random(0x45584143545f5354ULL);
	std::uniform_int_distribution<int> bit(0, 1);
	const std::array<double, 4> snr_values{4.0, 6.0, 8.0, 10.0};
	std::vector<SnrResult> results;

	for (const double snr_db: snr_values) {
		const double snr = std::pow(10.0, snr_db / 10.0);
		const double rate = static_cast<double>(K) / N;
		const double sigma = std::sqrt(1.0 / (2.0 * rate * snr));
		std::normal_distribution<double> noise(0.0, sigma);
		std::array<std::array<int8_t, N>, FRAMES> frames{};

		for (auto &soft: frames) {
			std::array<uint8_t, (K + 7) / 8> message{};
			for (int index = 0; index < K; ++index)
				CODE::set_be_bit(message.data(), index, bit(random));
			std::array<uint8_t, (N + 7) / 8> encoded{};
			encoder(encoded.data(), message.data(), genmat);
			for (int index = 0; index < N; ++index) {
				const double symbol = CODE::get_be_bit(encoded.data(), index)
					? -1.0
					: 1.0;
				const double received = symbol + noise(random);
				const int magnitude = std::max(
					1,
					std::min(127, static_cast<int>(std::lround(
						std::abs(received) * 32.0
					)))
				);
				soft[static_cast<std::size_t>(index)] =
					received < 0
					? static_cast<int8_t>(-magnitude)
					: static_cast<int8_t>(magnitude);
			}
		}

		for (int warmup = 0; warmup < 2; ++warmup) {
			decode_once(decoder, frames[warmup], genmat, false);
			decode_once(decoder, frames[warmup], genmat, true);
		}

		SnrResult summary;
		summary.snr_db = snr_db;
		std::vector<uint64_t> baseline_samples;
		std::vector<uint64_t> stopped_samples;
		std::vector<uint64_t> bound_candidate_samples;
		std::vector<uint64_t> combined_candidate_samples;
		for (int frame = 0; frame < FRAMES; ++frame) {
			DecodeResult baseline;
			DecodeResult stopped;
			if (frame & 1) {
				stopped = decode_once(decoder, frames[frame], genmat, true);
				baseline = decode_once(decoder, frames[frame], genmat, false);
			} else {
				baseline = decode_once(decoder, frames[frame], genmat, false);
				stopped = decode_once(decoder, frames[frame], genmat, true);
			}

			assert(baseline.decoded == stopped.decoded);
			assert(baseline.unique == stopped.unique);
			assert(
				baseline.completion_state.best ==
				stopped.completion_state.best
			);
			assert(
				baseline.completion_state.candidate ==
				stopped.completion_state.candidate
			);
			if (!stopped.stopped)
				assert(
					baseline.completion_state.next ==
					stopped.completion_state.next
				);
			else
				assert(baseline.unique);

			capture_once(decoder, frames[frame], genmat);
			const BoundResult bound = analyze_subtree_bound(
				baseline.completion_state.best,
				baseline.completion_state.next
			);
			const uint64_t combined_candidates = stopped.stopped
				? 1
				: bound.candidates;
			summary.exact_stops += stopped.stopped;
			summary.baseline_total_ns += baseline.elapsed_ns;
			summary.stopped_total_ns += stopped.elapsed_ns;
			summary.bound_candidates_total += bound.candidates;
			summary.combined_candidates_total += combined_candidates;
			summary.pruned_tasks_total += bound.pruned_tasks;
			baseline_samples.push_back(baseline.elapsed_ns);
			stopped_samples.push_back(stopped.elapsed_ns);
			bound_candidate_samples.push_back(bound.candidates);
			combined_candidate_samples.push_back(combined_candidates);
		}
		summary.baseline_p95_ns = percentile(baseline_samples, 0.95);
		summary.stopped_p95_ns = percentile(stopped_samples, 0.95);
		summary.bound_p95_candidates = percentile(
			bound_candidate_samples,
			0.95
		);
		summary.combined_p95_candidates = percentile(
			combined_candidate_samples,
			0.95
		);
		results.push_back(summary);
	}

	std::ofstream csv;
	if (argc > 1) {
		csv.open(argv[1]);
		csv << "ebn0_db,frames,exact_stops,stop_rate,mean_candidates,"
			"candidate_reduction,baseline_mean_ns,early_mean_ns,"
			"speedup,baseline_p95_ns,early_p95_ns,"
			"oracle_bound_mean_candidates,oracle_bound_reduction,"
			"oracle_bound_p95_candidates,combined_mean_candidates,"
			"combined_reduction,combined_p95_candidates,"
			"mean_pruned_tasks,result\n";
	}

	for (const auto &result: results) {
		const double stop_rate =
			static_cast<double>(result.exact_stops) / FRAMES;
		const double mean_candidates =
			result.exact_stops +
			(FRAMES - result.exact_stops) * EXPECTED_CANDIDATES;
		const double candidates_per_frame = mean_candidates / FRAMES;
		const double candidate_reduction =
			1.0 - candidates_per_frame / EXPECTED_CANDIDATES;
		const double baseline_mean =
			static_cast<double>(result.baseline_total_ns) / FRAMES;
		const double early_mean =
			static_cast<double>(result.stopped_total_ns) / FRAMES;
		const double speedup = baseline_mean / early_mean;
		const double bound_mean_candidates =
			static_cast<double>(result.bound_candidates_total) / FRAMES;
		const double bound_reduction =
			1.0 - bound_mean_candidates / EXPECTED_CANDIDATES;
		const double combined_mean_candidates =
			static_cast<double>(result.combined_candidates_total) / FRAMES;
		const double combined_reduction =
			1.0 - combined_mean_candidates / EXPECTED_CANDIDATES;
		const double mean_pruned_tasks =
			static_cast<double>(result.pruned_tasks_total) / FRAMES;

		std::cout << "EXACT_STOP_RESULT ebn0_db=" << result.snr_db
			<< " stops=" << result.exact_stops << '/' << FRAMES
			<< " stop_rate=" << std::fixed << std::setprecision(3)
			<< stop_rate
			<< " mean_candidates=" << static_cast<uint64_t>(
				candidates_per_frame
			)
			<< " speedup=" << speedup
			<< " oracle_bound_mean_candidates="
			<< static_cast<uint64_t>(bound_mean_candidates)
			<< " oracle_bound_p95_candidates="
			<< result.bound_p95_candidates
			<< " combined_mean_candidates="
			<< static_cast<uint64_t>(combined_mean_candidates)
			<< " combined_p95_candidates="
			<< result.combined_p95_candidates
			<< " mean_pruned_tasks=" << mean_pruned_tasks << '\n';
		if (csv)
			csv << std::fixed << std::setprecision(6) << result.snr_db
				<< ',' << FRAMES << ',' << result.exact_stops << ','
				<< stop_rate << ',' << candidates_per_frame << ','
				<< candidate_reduction << ','
				<< static_cast<uint64_t>(baseline_mean) << ','
					<< static_cast<uint64_t>(early_mean) << ','
					<< speedup << ',' << result.baseline_p95_ns << ','
					<< result.stopped_p95_ns << ','
					<< bound_mean_candidates << ',' << bound_reduction << ','
					<< result.bound_p95_candidates << ','
					<< combined_mean_candidates << ','
					<< combined_reduction << ','
					<< result.combined_p95_candidates << ','
					<< mean_pruned_tasks << ",pass\n";
	}
	return 0;
}
