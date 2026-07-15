#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <string>
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
void profile_begin(int stage);
void profile_end(int stage);

}

#define CODE_OSD_SEARCH_OVERRIDE(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next) \
	experiment::search_override(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next)
#define CODE_OSD_SEARCH_COMPLETE(best, next, candidate, permutation, length, width) \
	experiment::search_complete(best, next, candidate, permutation, length, width)
#define CODE_OSD_PROFILE_BEGIN(stage) experiment::profile_begin(stage)
#define CODE_OSD_PROFILE_END(stage) experiment::profile_end(stage)

#include "bitman.hh"
#include "osd.hh"

namespace experiment {

using Clock = std::chrono::steady_clock;
constexpr int N = 127;
constexpr int K = 64;
constexpr int O = 4;
constexpr int R = N - K;
constexpr int PW = 64;
constexpr int FW = 128;
constexpr uint64_t EXPECTED_CANDIDATES = 679121;
constexpr uint64_t EXPECTED_FLIPS = 1358240;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

int search_mode = 0;

struct Profile {
	std::array<Clock::time_point, 9> started{};
	std::array<uint64_t, 9> elapsed{};

	void reset()
	{
		elapsed.fill(0);
	}
};

Profile profile;

struct Completion {
	int best = 0;
	int next = -1;
	std::array<int8_t, N> candidate{};
};

Completion completion;

struct SearchStats {
	uint64_t candidates = 0;
	uint64_t flips = 0;
	uint64_t parity_metric_terms = 0;
	uint64_t parity_flip_terms = 0;
	uint64_t best_mask = 0;
	int best = 0;
	int next = -1;
};

SearchStats search_stats;

void profile_begin(int stage)
{
	profile.started[static_cast<std::size_t>(stage)] = Clock::now();
}

void profile_end(int stage)
{
	const auto end = Clock::now();
	profile.elapsed[static_cast<std::size_t>(stage)] +=
		static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				end - profile.started[static_cast<std::size_t>(stage)]
			).count()
		);
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

class NativeParitySearch {
	const int8_t *matrix;
	const int8_t *base;
	int8_t *candidate;
	const int8_t *soft;
	const int width;
	alignas(64) std::array<int8_t, PW> parity{};
	uint64_t mask = 0;
	int systematic_metric = 0;
	int initial_systematic_metric = 0;
	int best = 0;
	int next = -1;
	uint64_t candidates = 0;
	uint64_t flips = 0;
	uint64_t parity_metric_terms = 0;
	uint64_t parity_flip_terms = 0;
	uint64_t best_mask = 0;

	void copy_candidate()
	{
		for (int index = 0; index < K; ++index)
			candidate[index] = base[index] ^
				static_cast<int8_t>((mask >> index) & 1);
		std::copy(parity.begin(), parity.begin() + R, candidate + K);
		for (int index = N; index < width; ++index)
			candidate[index] = 0;
	}

public:
	NativeParitySearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *output_candidate,
		const int8_t *input_soft,
		int input_width
	):
		matrix(input_matrix),
		base(input_base),
		candidate(output_candidate),
		soft(input_soft),
		width(input_width)
	{
		std::copy(base + K, base + K + PW, parity.begin());
		for (int index = 0; index < K; ++index)
			initial_systematic_metric +=
				(1 - 2 * base[index]) * soft[index];
		systematic_metric = initial_systematic_metric;
	}

	void flip(int index)
	{
		const int old_bit = base[index] ^
			static_cast<int>((mask >> index) & 1);
		systematic_metric -=
			2 * (1 - 2 * old_bit) * soft[index];
		mask ^= uint64_t{1} << index;
		for (int offset = 0; offset < PW; ++offset)
			parity[static_cast<std::size_t>(offset)] ^=
				matrix[index * width + K + offset];
		++flips;
		parity_flip_terms += PW;
	}

	void evaluate()
	{
		int metric = systematic_metric;
		for (int offset = 0; offset < PW; ++offset)
			metric +=
				(1 - 2 * parity[static_cast<std::size_t>(offset)]) *
				soft[K + offset];
		parity_metric_terms += PW;

		const bool first = candidates == 0;
		if (first || metric > best) {
			if (!first)
				next = best;
			best = metric;
			best_mask = mask;
			copy_candidate();
		} else if (metric > next) {
			next = metric;
		}
		++candidates;
	}

	void run()
	{
		evaluate();
		for (int a = 0; a < K; ++a) {
			flip(a);
			evaluate();
			for (int b = a + 1; b < K; ++b) {
				flip(b);
				evaluate();
				for (int c = b + 1; c < K; ++c) {
					flip(c);
					evaluate();
					for (int d = c + 1; d < K; ++d) {
						flip(d);
						evaluate();
						flip(d);
					}
					flip(c);
				}
				flip(b);
			}
			flip(a);
		}
		assert(mask == 0);
		assert(systematic_metric == initial_systematic_metric);
		for (int offset = 0; offset < PW; ++offset)
			assert(parity[static_cast<std::size_t>(offset)] == base[K + offset]);
		assert(candidates == EXPECTED_CANDIDATES);
		assert(flips == EXPECTED_FLIPS);
		search_stats.candidates = candidates;
		search_stats.flips = flips;
		search_stats.parity_metric_terms = parity_metric_terms;
		search_stats.parity_flip_terms = parity_flip_terms;
		search_stats.best_mask = best_mask;
		search_stats.best = best;
		search_stats.next = next;
	}

	int best_metric() const { return best; }
	int next_metric() const { return next; }
};

struct SubtreeTask {
	uint64_t mask = 0;
	uint64_t first_index = 0;
	uint64_t candidate_count = 0;
	uint16_t a = 0;
	uint16_t b = 0;
	bool pair = false;
};

const std::vector<SubtreeTask> &subtree_tasks()
{
	static const std::vector<SubtreeTask> tasks = []() {
		std::vector<SubtreeTask> result;
		result.reserve(K + K * (K - 1) / 2);
		uint64_t candidate_index = 1;
		for (int a = 0; a < K; ++a) {
			result.push_back({
				uint64_t{1} << a,
				candidate_index,
				1,
				static_cast<uint16_t>(a),
				0,
				false
			});
			++candidate_index;
			for (int b = a + 1; b < K; ++b) {
				const uint64_t remaining = static_cast<uint64_t>(K - b - 1);
				const uint64_t subtree_candidates =
					1 + remaining + remaining * (remaining - 1) / 2;
				result.push_back({
					(uint64_t{1} << a) | (uint64_t{1} << b),
					candidate_index,
					subtree_candidates,
					static_cast<uint16_t>(a),
					static_cast<uint16_t>(b),
					true
				});
				candidate_index += subtree_candidates;
			}
		}
		assert(candidate_index == EXPECTED_CANDIDATES);
		return result;
	}();
	return tasks;
}

struct SearchAccumulator {
	int best = INT_MIN;
	int second = INT_MIN;
	uint64_t best_count = 0;
	uint64_t best_index = UINT64_MAX;
	uint64_t best_mask = 0;
	std::array<int8_t, PW> best_parity{};
	uint64_t candidates = 0;
	uint64_t state_row_applications = 0;

	void consider(
		int metric,
		uint64_t candidate_index,
		uint64_t mask,
		const int8_t *parity
	)
	{
		++candidates;
		if (metric > best) {
			second = best;
			best = metric;
			best_count = 1;
			best_index = candidate_index;
			best_mask = mask;
			std::copy(parity, parity + PW, best_parity.begin());
		} else if (metric == best) {
			++best_count;
			if (candidate_index < best_index) {
				best_index = candidate_index;
				best_mask = mask;
				std::copy(parity, parity + PW, best_parity.begin());
			}
		} else if (metric > second) {
			second = metric;
		}
	}

	void merge(const SearchAccumulator &other)
	{
		candidates += other.candidates;
		state_row_applications += other.state_row_applications;
		if (other.best > best) {
			second = std::max({second, best, other.second});
			best = other.best;
			best_count = other.best_count;
			best_index = other.best_index;
			best_mask = other.best_mask;
			best_parity = other.best_parity;
		} else if (other.best == best) {
			second = std::max(second, other.second);
			best_count += other.best_count;
			if (other.best_index < best_index) {
				best_index = other.best_index;
				best_mask = other.best_mask;
				best_parity = other.best_parity;
			}
		} else {
			second = std::max({second, other.best, other.second});
		}
	}
};

class PthreadSearch {
	const int8_t *matrix;
	const int8_t *base;
	int8_t *candidate;
	const int8_t *soft;
	const int width;
	const int thread_count;
	const bool parity_only;
	const std::vector<SubtreeTask> &tasks;
	std::atomic<std::size_t> next_task{0};

	struct alignas(64) Worker {
		PthreadSearch *search = nullptr;
		SearchAccumulator result;
	};

	std::vector<Worker> workers;

	static void *worker_entry(void *opaque)
	{
		auto &worker = *static_cast<Worker *>(opaque);
		worker.search->run_worker(worker.result);
		return nullptr;
	}

	int initial_systematic_metric() const
	{
		int metric = 0;
		for (int index = 0; index < K; ++index)
			metric += (1 - 2 * base[index]) * soft[index];
		return metric;
	}

	void flip(
		int index,
		uint64_t &mask,
		int &systematic_metric,
		std::array<int8_t, PW> &parity,
		SearchAccumulator &result
	) const
	{
		const int old_bit = base[index] ^
			static_cast<int>((mask >> index) & 1);
		systematic_metric -=
			2 * (1 - 2 * old_bit) * soft[index];
		mask ^= uint64_t{1} << index;
		for (int offset = 0; offset < PW; ++offset)
			parity[static_cast<std::size_t>(offset)] ^=
				matrix[index * width + K + offset];
		++result.state_row_applications;
	}

	void evaluate(
		uint64_t candidate_index,
		uint64_t mask,
		int systematic_metric,
		const std::array<int8_t, PW> &parity,
		SearchAccumulator &result
	) const
	{
		int metric = systematic_metric;
		for (int offset = 0; offset < PW; ++offset)
			metric +=
				(1 - 2 * parity[static_cast<std::size_t>(offset)]) *
				soft[K + offset];
		result.consider(metric, candidate_index, mask, parity.data());
	}

	void run_task(const SubtreeTask &task, SearchAccumulator &result) const
	{
		std::array<int8_t, PW> parity{};
		std::copy(base + K, base + K + PW, parity.begin());
		uint64_t mask = 0;
		int systematic_metric = initial_systematic_metric();
		flip(task.a, mask, systematic_metric, parity, result);
		if (task.pair)
			flip(task.b, mask, systematic_metric, parity, result);

		uint64_t candidate_index = task.first_index;
		evaluate(candidate_index++, mask, systematic_metric, parity, result);
		if (!task.pair) {
			assert(candidate_index == task.first_index + task.candidate_count);
			return;
		}

		for (int c = task.b + 1; c < K; ++c) {
			flip(c, mask, systematic_metric, parity, result);
			evaluate(candidate_index++, mask, systematic_metric, parity, result);
			for (int d = c + 1; d < K; ++d) {
				flip(d, mask, systematic_metric, parity, result);
				evaluate(candidate_index++, mask, systematic_metric, parity, result);
				flip(d, mask, systematic_metric, parity, result);
			}
			flip(c, mask, systematic_metric, parity, result);
		}
		assert(mask == task.mask);
		assert(candidate_index == task.first_index + task.candidate_count);
	}

	void full_flip(
		int index,
		uint64_t &mask,
		std::array<int8_t, FW> &word,
		SearchAccumulator &result
	) const
	{
		mask ^= uint64_t{1} << index;
		for (int offset = 0; offset < FW; ++offset)
			word[static_cast<std::size_t>(offset)] ^=
				matrix[index * width + offset];
		++result.state_row_applications;
	}

	void full_evaluate(
		uint64_t candidate_index,
		uint64_t mask,
		const std::array<int8_t, FW> &word,
		SearchAccumulator &result
	) const
	{
		int metric = 0;
		for (int offset = 0; offset < FW; ++offset)
			metric +=
				(1 - 2 * word[static_cast<std::size_t>(offset)]) *
				soft[offset];
		result.consider(metric, candidate_index, mask, word.data() + K);
	}

	void run_full_task(
		const SubtreeTask &task,
		SearchAccumulator &result
	) const
	{
		std::array<int8_t, FW> word{};
		std::copy(base, base + FW, word.begin());
		uint64_t mask = 0;
		full_flip(task.a, mask, word, result);
		if (task.pair)
			full_flip(task.b, mask, word, result);

		uint64_t candidate_index = task.first_index;
		full_evaluate(candidate_index++, mask, word, result);
		if (!task.pair) {
			assert(candidate_index == task.first_index + task.candidate_count);
			return;
		}

		for (int c = task.b + 1; c < K; ++c) {
			full_flip(c, mask, word, result);
			full_evaluate(candidate_index++, mask, word, result);
			for (int d = c + 1; d < K; ++d) {
				full_flip(d, mask, word, result);
				full_evaluate(candidate_index++, mask, word, result);
				full_flip(d, mask, word, result);
			}
			full_flip(c, mask, word, result);
		}
		assert(mask == task.mask);
		assert(candidate_index == task.first_index + task.candidate_count);
	}

	void run_worker(SearchAccumulator &result)
	{
		for (;;) {
			const std::size_t index = next_task.fetch_add(
				1,
				std::memory_order_relaxed
			);
			if (index >= tasks.size())
				break;
			if (parity_only)
				run_task(tasks[index], result);
			else
				run_full_task(tasks[index], result);
		}
	}

	void copy_candidate(const SearchAccumulator &result)
	{
		for (int index = 0; index < K; ++index)
			candidate[index] = base[index] ^
				static_cast<int8_t>((result.best_mask >> index) & 1);
		std::copy(
			result.best_parity.begin(),
			result.best_parity.begin() + R,
			candidate + K
		);
		for (int index = N; index < width; ++index)
			candidate[index] = 0;
	}

public:
	PthreadSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *output_candidate,
		const int8_t *input_soft,
		int input_width,
		int input_thread_count,
		bool input_parity_only
	):
		matrix(input_matrix),
		base(input_base),
		candidate(output_candidate),
		soft(input_soft),
		width(input_width),
		thread_count(input_thread_count),
		parity_only(input_parity_only),
		tasks(subtree_tasks()),
		workers(static_cast<std::size_t>(input_thread_count))
	{
		assert(thread_count >= 2);
		assert(width == FW);
		for (auto &worker: workers)
			worker.search = this;
	}

	void run()
	{
		std::vector<pthread_t> threads(
			static_cast<std::size_t>(thread_count - 1)
		);
		for (int index = 1; index < thread_count; ++index) {
			const int status = pthread_create(
				&threads[static_cast<std::size_t>(index - 1)],
				nullptr,
				worker_entry,
				&workers[static_cast<std::size_t>(index)]
			);
			assert(status == 0);
		}
		run_worker(workers[0].result);
		for (pthread_t &thread: threads) {
			const int status = pthread_join(thread, nullptr);
			assert(status == 0);
		}

		SearchAccumulator combined;
		std::array<int8_t, PW> base_parity{};
		std::copy(base + K, base + K + PW, base_parity.begin());
		int base_metric = initial_systematic_metric();
		for (int offset = 0; offset < PW; ++offset)
			base_metric +=
				(1 - 2 * base_parity[static_cast<std::size_t>(offset)]) *
				soft[K + offset];
		combined.consider(base_metric, 0, 0, base_parity.data());
		for (const auto &worker: workers)
			combined.merge(worker.result);

		assert(combined.candidates == EXPECTED_CANDIDATES);
		copy_candidate(combined);
		search_stats.candidates = combined.candidates;
		search_stats.flips = combined.state_row_applications;
		const uint64_t terms_per_state = parity_only ? PW : FW;
		search_stats.parity_metric_terms =
			combined.candidates * terms_per_state;
		search_stats.parity_flip_terms =
			combined.state_row_applications * terms_per_state;
		search_stats.best_mask = combined.best_mask;
		search_stats.best = combined.best;
		search_stats.next = combined.best_count > 1
			? combined.best
			: std::max(-1, combined.second);
	}

	int best_metric() const { return search_stats.best; }
	int next_metric() const { return search_stats.next; }
};

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
	if (search_mode == 0)
		return false;
	assert(length == N);
	assert(dimension == K);
	assert(order == O);
	search_stats = {};
	if (search_mode == 1) {
		NativeParitySearch search(matrix, base, candidate, soft, width);
		search.run();
		best = search.best_metric();
		next = search.next_metric();
	} else {
		PthreadSearch search(
			matrix,
			base,
			candidate,
			soft,
			width,
			std::abs(search_mode),
			search_mode > 0
		);
		search.run();
		best = search.best_metric();
		next = search.next_metric();
	}
	return true;
}

uint64_t next_random(uint64_t &state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

struct DecodeResult {
	bool unique = false;
	std::array<uint8_t, (N + 7) / 8> decoded{};
	Completion completion_state;
	uint64_t total_ns = 0;
	uint64_t candidate_ns = 0;
};

DecodeResult decode_once(
	CODE::OrderedStatisticsDecoder<N, K, O> &decoder,
	const std::array<int8_t, N> &soft,
	const int8_t *genmat,
	int mode
)
{
	search_mode = mode;
	profile.reset();
	DecodeResult result;
	const auto start = Clock::now();
	result.unique = decoder(result.decoded.data(), soft.data(), genmat);
	const auto end = Clock::now();
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	result.candidate_ns = profile.elapsed[7];
	result.completion_state = completion;
	return result;
}

double median(std::vector<uint64_t> values)
{
	std::sort(values.begin(), values.end());
	return static_cast<double>(values[values.size() / 2]);
}

double percentile(std::vector<uint64_t> values, double fraction)
{
	std::sort(values.begin(), values.end());
	const std::size_t index = static_cast<std::size_t>(
		std::ceil(values.size() * fraction)
	) - 1;
	return static_cast<double>(values[index]);
}

struct ModeSummary {
	std::string name;
	int mode = 0;
	int threads = 1;
	double median_total_ns = 0;
	double p95_total_ns = 0;
	double median_candidate_ns = 0;
	double blocks_per_second = 0;
};

}

int main(int argc, char **argv)
{
	using namespace experiment;
#ifndef OSD_BENCH_FRAME_COUNT
#define OSD_BENCH_FRAME_COUNT 9
#endif
#ifndef OSD_BENCH_WARMUP_ROUNDS
#define OSD_BENCH_WARMUP_ROUNDS 2
#endif
#ifndef OSD_BENCH_REPEATS
#define OSD_BENCH_REPEATS 9
#endif
	constexpr int FRAME_COUNT = OSD_BENCH_FRAME_COUNT;
	constexpr int WARMUP_ROUNDS = OSD_BENCH_WARMUP_ROUNDS;
	constexpr int REPEATS = OSD_BENCH_REPEATS;
	const std::array<int, 10> mode_values{
		0, 1, -2, -4, -8, -16, 2, 4, 8, 16
	};
	const std::array<std::string, 10> mode_names{
		"baseline",
		"native_parity",
		"pthread_full_2",
		"pthread_full_4",
		"pthread_full_8",
		"pthread_full_16",
		"pthread_parity_2",
		"pthread_parity_4",
		"pthread_parity_8",
		"pthread_parity_16"
	};

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

	std::array<std::array<int8_t, N>, FRAME_COUNT> frames{};
	uint64_t random_state = 0x4e41544956455f50ULL;
	for (auto &frame: frames) {
		for (int index = 0; index < N; ++index) {
			const uint64_t value = next_random(random_state);
			const int magnitude = 1 + static_cast<int>(value % 127);
			frame[static_cast<std::size_t>(index)] =
				value & (uint64_t{1} << 63)
				? static_cast<int8_t>(-magnitude)
				: static_cast<int8_t>(magnitude);
		}
	}

	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	for (const auto &frame: frames) {
		const DecodeResult baseline = decode_once(decoder, frame, genmat, 0);
		for (std::size_t mode_index = 1;
			mode_index < mode_values.size();
			++mode_index) {
			const DecodeResult tested = decode_once(
				decoder,
				frame,
				genmat,
				mode_values[mode_index]
			);
			assert(baseline.unique == tested.unique);
			assert(baseline.decoded == tested.decoded);
			assert(
				baseline.completion_state.best ==
				tested.completion_state.best
			);
			assert(
				baseline.completion_state.next ==
				tested.completion_state.next
			);
			assert(
				baseline.completion_state.candidate ==
				tested.completion_state.candidate
			);
			assert(search_stats.best == tested.completion_state.best);
			assert(search_stats.next == tested.completion_state.next);
		}
	}

	volatile uint64_t output_checksum = 0;
	for (int warmup = 0; warmup < WARMUP_ROUNDS; ++warmup)
		for (const int mode: mode_values)
			for (const auto &frame: frames) {
				const DecodeResult result =
					decode_once(decoder, frame, genmat, mode);
				output_checksum ^= result.decoded[0];
			}

	std::array<std::vector<uint64_t>, 10> total_samples;
	std::array<std::vector<uint64_t>, 10> candidate_samples;
	std::ofstream raw;
	if (argc > 2) {
		raw.open(argv[2]);
		raw << "repeat,mode,threads,frames,total_ns,ns_per_block,"
			"candidate_ns_per_block,checksum\n";
	}

	for (int repeat = 0; repeat < REPEATS; ++repeat) {
		std::array<std::size_t, 10> order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		if (repeat & 1)
			std::reverse(order.begin(), order.end());
		for (const std::size_t mode_index: order) {
			const int mode = mode_values[mode_index];
			uint64_t total_ns = 0;
			uint64_t candidate_ns = 0;
			uint64_t checksum = 0;
			for (const auto &frame: frames) {
				const DecodeResult result =
					decode_once(decoder, frame, genmat, mode);
				total_ns += result.total_ns;
				candidate_ns += result.candidate_ns;
				for (const uint8_t byte: result.decoded)
					checksum = checksum * FNV_PRIME ^ byte;
				checksum ^= static_cast<uint64_t>(result.unique);
			}
			output_checksum ^= checksum;
			const uint64_t total_per_block = total_ns / FRAME_COUNT;
			const uint64_t candidate_per_block = candidate_ns / FRAME_COUNT;
			total_samples[mode_index].push_back(
				total_per_block
			);
			candidate_samples[mode_index].push_back(
				candidate_per_block
			);
			if (raw)
				raw << repeat + 1 << ','
					<< mode_names[mode_index] << ','
					<< (std::abs(mode) >= 2 ? std::abs(mode) : 1) << ','
					<< FRAME_COUNT << ',' << total_ns << ','
					<< total_per_block << ',' << candidate_per_block << ','
					<< std::hex << checksum << std::dec << '\n';
		}
	}

	std::array<ModeSummary, 10> summaries;
	for (std::size_t mode_index = 0;
		mode_index < summaries.size();
		++mode_index) {
		auto &summary = summaries[mode_index];
		summary.name = mode_names[mode_index];
		summary.mode = mode_values[mode_index];
		summary.threads = std::abs(summary.mode) >= 2
			? std::abs(summary.mode)
			: 1;
		summary.median_total_ns = median(total_samples[mode_index]);
		summary.p95_total_ns = percentile(total_samples[mode_index], 0.95);
		summary.median_candidate_ns = median(candidate_samples[mode_index]);
		summary.blocks_per_second = 1e9 / summary.median_total_ns;
	}

	if (argc > 1) {
		std::ofstream summary(argv[1]);
		summary << "mode,threads,frames_per_repeat,repeats,median_total_ns,"
			"p95_total_ns,median_candidate_ns,blocks_per_second,"
			"speedup_vs_baseline,candidates,state_row_applications,"
			"metric_terms,state_update_terms,result\n";
		for (const auto &value: summaries) {
			const double speedup =
				summaries[0].median_total_ns / value.median_total_ns;
			const uint64_t flips = value.mode == 0
				? 0
				: (value.mode == 1 ? EXPECTED_FLIPS : 1358176);
			const uint64_t terms_per_state = value.mode == 0
				? 0
				: (value.mode < 0 ? FW : PW);
			summary << value.name << ',' << value.threads << ','
				<< FRAME_COUNT << ',' << REPEATS
				<< ',' << static_cast<uint64_t>(value.median_total_ns)
				<< ',' << static_cast<uint64_t>(value.p95_total_ns)
				<< ',' << static_cast<uint64_t>(value.median_candidate_ns)
				<< ',' << std::fixed << std::setprecision(6)
				<< value.blocks_per_second << ','
				<< speedup << ','
				<< EXPECTED_CANDIDATES << ','
				<< flips << ','
				<< EXPECTED_CANDIDATES * terms_per_state << ','
				<< flips * terms_per_state
				<< ",pass\n";
		}
	}

	std::cout << "PTHREAD_DFS_PASS "
		<< "frames=" << FRAME_COUNT << ' '
		<< "candidates=" << search_stats.candidates << ' '
		<< "flips=" << search_stats.flips << ' '
		<< "metric_terms=" << search_stats.parity_metric_terms << ' '
		<< "state_update_terms=" << search_stats.parity_flip_terms << '\n';
	for (const auto &summary: summaries)
		std::cout << "PTHREAD_DFS_BENCH " << summary.name << ' '
			<< "threads=" << summary.threads << ' '
			<< "median_total_ns="
			<< static_cast<uint64_t>(summary.median_total_ns) << ' '
			<< "p95_total_ns="
			<< static_cast<uint64_t>(summary.p95_total_ns) << ' '
			<< "median_candidate_ns="
			<< static_cast<uint64_t>(summary.median_candidate_ns) << ' '
			<< "blocks_per_second=" << std::fixed
			<< std::setprecision(3) << summary.blocks_per_second << '\n';
	for (const auto &summary: summaries)
		std::cout << "PTHREAD_DFS_SPEEDUP " << summary.name << ' '
			<< "overall=" << std::fixed << std::setprecision(5)
			<< summaries[0].median_total_ns / summary.median_total_ns << ' '
			<< "candidate="
			<< summaries[0].median_candidate_ns /
				summary.median_candidate_ns << '\n';
	std::cout << "PTHREAD_DFS_CHECKSUM "
		<< std::hex << output_checksum << std::dec << '\n';
	return 0;
}
