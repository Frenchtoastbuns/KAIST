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
#include <numeric>
#include <pthread.h>
#include <random>
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
constexpr int WORKERS = 8;
constexpr uint64_t EXPECTED_CANDIDATES = 679121;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

enum class SearchMode {
	baseline,
	spawn_full,
	persistent_full,
	spawn_parity,
	persistent_parity,
	exact_spawn_full,
	exact_persistent_full,
	exact_spawn_parity,
	exact_persistent_parity
};

struct ModeSpec {
	const char *name;
	SearchMode mode;
	bool persistent;
	bool parity;
	bool exact;
};

constexpr std::array<ModeSpec, 9> MODES{{
	{"baseline", SearchMode::baseline, false, false, false},
	{"spawn_full_8", SearchMode::spawn_full, false, false, false},
	{"persistent_full_8", SearchMode::persistent_full, true, false, false},
	{"spawn_parity_8", SearchMode::spawn_parity, false, true, false},
	{"persistent_parity_8", SearchMode::persistent_parity, true, true, false},
	{"exact_spawn_full_8", SearchMode::exact_spawn_full, false, false, true},
	{"exact_persistent_full_8", SearchMode::exact_persistent_full, true, false, true},
	{"exact_spawn_parity_8", SearchMode::exact_spawn_parity, false, true, true},
	{"exact_persistent_parity_8", SearchMode::exact_persistent_parity, true, true, true}
}};

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
	uint64_t state_row_applications = 0;
	int best = 0;
	int next = -1;
	bool exact_stop = false;
};

SearchStats search_stats;
SearchMode search_mode = SearchMode::baseline;

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
				const uint64_t remaining =
					static_cast<uint64_t>(K - b - 1);
				const uint64_t count =
					1 + remaining + remaining * (remaining - 1) / 2;
				result.push_back({
					(uint64_t{1} << a) | (uint64_t{1} << b),
					candidate_index,
					count,
					static_cast<uint16_t>(a),
					static_cast<uint16_t>(b),
					true
				});
				candidate_index += count;
			}
		}
		assert(candidate_index == EXPECTED_CANDIDATES);
		return result;
	}();
	return tasks;
}

struct alignas(64) SearchAccumulator {
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

class DfsJob {
	const int8_t *matrix;
	const int8_t *base;
	int8_t *candidate;
	const int8_t *soft;
	const int width;
	const bool parity_only;
	const std::vector<SubtreeTask> &tasks;
	std::atomic<std::size_t> next_task{0};

	int initial_systematic_metric() const
	{
		int metric = 0;
		for (int index = 0; index < K; ++index)
			metric += (1 - 2 * base[index]) * soft[index];
		return metric;
	}

	void parity_flip(
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

	void parity_evaluate(
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

	void run_parity_task(
		const SubtreeTask &task,
		SearchAccumulator &result
	) const
	{
		std::array<int8_t, PW> parity{};
		std::copy(base + K, base + K + PW, parity.begin());
		uint64_t mask = 0;
		int systematic_metric = initial_systematic_metric();
		parity_flip(task.a, mask, systematic_metric, parity, result);
		if (task.pair)
			parity_flip(task.b, mask, systematic_metric, parity, result);

		uint64_t candidate_index = task.first_index;
		parity_evaluate(
			candidate_index++, mask, systematic_metric, parity, result
		);
		if (!task.pair) {
			assert(candidate_index == task.first_index + task.candidate_count);
			return;
		}

		for (int c = task.b + 1; c < K; ++c) {
			parity_flip(c, mask, systematic_metric, parity, result);
			parity_evaluate(
				candidate_index++, mask, systematic_metric, parity, result
			);
			for (int d = c + 1; d < K; ++d) {
				parity_flip(d, mask, systematic_metric, parity, result);
				parity_evaluate(
					candidate_index++, mask, systematic_metric, parity, result
				);
				parity_flip(d, mask, systematic_metric, parity, result);
			}
			parity_flip(c, mask, systematic_metric, parity, result);
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
	DfsJob(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *output_candidate,
		const int8_t *input_soft,
		int input_width,
		bool input_parity_only
	):
		matrix(input_matrix),
		base(input_base),
		candidate(output_candidate),
		soft(input_soft),
		width(input_width),
		parity_only(input_parity_only),
		tasks(subtree_tasks())
	{
		assert(width == FW);
	}

	void run_worker(SearchAccumulator &result)
	{
		result = {};
		for (;;) {
			const std::size_t index = next_task.fetch_add(
				1,
				std::memory_order_relaxed
			);
			if (index >= tasks.size())
				break;
			if (parity_only)
				run_parity_task(tasks[index], result);
			else
				run_full_task(tasks[index], result);
		}
	}

	void finish(const std::vector<SearchAccumulator> &results)
	{
		SearchAccumulator combined;
		std::array<int8_t, PW> base_parity{};
		std::copy(base + K, base + K + PW, base_parity.begin());
		int base_metric = initial_systematic_metric();
		for (int offset = 0; offset < PW; ++offset)
			base_metric +=
				(1 - 2 * base_parity[static_cast<std::size_t>(offset)]) *
				soft[K + offset];
		combined.consider(base_metric, 0, 0, base_parity.data());
		for (const auto &result: results)
			combined.merge(result);

		assert(combined.candidates == EXPECTED_CANDIDATES);
		copy_candidate(combined);
		search_stats.candidates = combined.candidates;
		search_stats.state_row_applications =
			combined.state_row_applications;
		search_stats.best = combined.best;
		search_stats.next = combined.best_count > 1
			? combined.best
			: std::max(-1, combined.second);
	}
};

struct SpawnArgument {
	DfsJob *job = nullptr;
	SearchAccumulator *result = nullptr;
};

void *spawn_worker_entry(void *opaque)
{
	auto &argument = *static_cast<SpawnArgument *>(opaque);
	argument.job->run_worker(*argument.result);
	return nullptr;
}

void run_spawn(DfsJob &job, int workers)
{
	assert(workers >= 2);
	std::vector<SearchAccumulator> results(
		static_cast<std::size_t>(workers)
	);
	std::vector<pthread_t> threads(static_cast<std::size_t>(workers - 1));
	std::vector<SpawnArgument> arguments(
		static_cast<std::size_t>(workers - 1)
	);
	for (int index = 1; index < workers; ++index) {
		auto &argument = arguments[static_cast<std::size_t>(index - 1)];
		argument.job = &job;
		argument.result = &results[static_cast<std::size_t>(index)];
		const int status = pthread_create(
			&threads[static_cast<std::size_t>(index - 1)],
			nullptr,
			spawn_worker_entry,
			&argument
		);
		assert(status == 0);
	}
	job.run_worker(results[0]);
	for (pthread_t &thread: threads) {
		const int status = pthread_join(thread, nullptr);
		assert(status == 0);
	}
	job.finish(results);
}

class PersistentPool {
	struct WorkerArgument {
		PersistentPool *pool = nullptr;
		std::size_t result_index = 0;
	};

	const int worker_count;
	std::vector<pthread_t> threads;
	std::vector<WorkerArgument> arguments;
	std::vector<SearchAccumulator> results;
	pthread_mutex_t mutex{};
	pthread_cond_t start_condition{};
	pthread_cond_t done_condition{};
	DfsJob *job = nullptr;
	uint64_t generation = 0;
	int remaining = 0;
	bool shutdown = false;

	static void *worker_entry(void *opaque)
	{
		auto &argument = *static_cast<WorkerArgument *>(opaque);
		argument.pool->worker_loop(argument.result_index);
		return nullptr;
	}

	void worker_loop(std::size_t result_index)
	{
		uint64_t observed_generation = 0;
		const int lock_status = pthread_mutex_lock(&mutex);
		assert(lock_status == 0);
		for (;;) {
			while (!shutdown && generation == observed_generation) {
				const int status = pthread_cond_wait(
					&start_condition,
					&mutex
				);
				assert(status == 0);
			}
			if (shutdown)
				break;
			DfsJob *current_job = job;
			observed_generation = generation;
			int status = pthread_mutex_unlock(&mutex);
			assert(status == 0);
			current_job->run_worker(results[result_index]);
			status = pthread_mutex_lock(&mutex);
			assert(status == 0);
			--remaining;
			if (remaining == 0) {
				status = pthread_cond_signal(&done_condition);
				assert(status == 0);
			}
		}
		const int unlock_status = pthread_mutex_unlock(&mutex);
		assert(unlock_status == 0);
	}

public:
	explicit PersistentPool(int workers):
		worker_count(workers),
		threads(static_cast<std::size_t>(workers - 1)),
		arguments(static_cast<std::size_t>(workers - 1)),
		results(static_cast<std::size_t>(workers))
	{
		assert(worker_count >= 2);
		assert(pthread_mutex_init(&mutex, nullptr) == 0);
		assert(pthread_cond_init(&start_condition, nullptr) == 0);
		assert(pthread_cond_init(&done_condition, nullptr) == 0);
		for (int index = 1; index < worker_count; ++index) {
			auto &argument = arguments[static_cast<std::size_t>(index - 1)];
			argument.pool = this;
			argument.result_index = static_cast<std::size_t>(index);
			const int status = pthread_create(
				&threads[static_cast<std::size_t>(index - 1)],
				nullptr,
				worker_entry,
				&argument
			);
			assert(status == 0);
		}
	}

	~PersistentPool()
	{
		assert(pthread_mutex_lock(&mutex) == 0);
		shutdown = true;
		++generation;
		assert(pthread_cond_broadcast(&start_condition) == 0);
		assert(pthread_mutex_unlock(&mutex) == 0);
		for (pthread_t &thread: threads)
			assert(pthread_join(thread, nullptr) == 0);
		assert(pthread_cond_destroy(&done_condition) == 0);
		assert(pthread_cond_destroy(&start_condition) == 0);
		assert(pthread_mutex_destroy(&mutex) == 0);
	}

	void run(DfsJob &input_job)
	{
		assert(pthread_mutex_lock(&mutex) == 0);
		job = &input_job;
		remaining = worker_count - 1;
		++generation;
		assert(pthread_cond_broadcast(&start_condition) == 0);
		assert(pthread_mutex_unlock(&mutex) == 0);

		input_job.run_worker(results[0]);

		assert(pthread_mutex_lock(&mutex) == 0);
		while (remaining != 0)
			assert(pthread_cond_wait(&done_condition, &mutex) == 0);
		job = nullptr;
		assert(pthread_mutex_unlock(&mutex) == 0);
		input_job.finish(results);
	}
};

PersistentPool *persistent_pool = nullptr;

bool try_exact_order_zero(
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int length,
	int width,
	int &best,
	int &next
)
{
	int metric = 0;
	int upper_bound = 0;
	for (int index = 0; index < length; ++index) {
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
	search_stats.candidates = 1;
	search_stats.best = best;
	search_stats.next = next;
	search_stats.exact_stop = true;
	return true;
}

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
	if (search_mode == SearchMode::baseline)
		return false;
	assert(length == N);
	assert(dimension == K);
	assert(width == FW);
	assert(order == O);
	search_stats = {};

	const bool exact =
		search_mode == SearchMode::exact_spawn_full ||
		search_mode == SearchMode::exact_persistent_full ||
		search_mode == SearchMode::exact_spawn_parity ||
		search_mode == SearchMode::exact_persistent_parity;
	if (exact && try_exact_order_zero(
		base, candidate, soft, length, width, best, next
	))
		return true;

	const bool parity =
		search_mode == SearchMode::spawn_parity ||
		search_mode == SearchMode::persistent_parity ||
		search_mode == SearchMode::exact_spawn_parity ||
		search_mode == SearchMode::exact_persistent_parity;
	DfsJob job(matrix, base, candidate, soft, width, parity);
	const bool persistent =
		search_mode == SearchMode::persistent_full ||
		search_mode == SearchMode::persistent_parity ||
		search_mode == SearchMode::exact_persistent_full ||
		search_mode == SearchMode::exact_persistent_parity;
	if (persistent) {
		assert(persistent_pool);
		persistent_pool->run(job);
	} else {
		run_spawn(job, WORKERS);
	}
	best = search_stats.best;
	next = search_stats.next;
	return true;
}

struct DecodeResult {
	bool unique = false;
	bool exact_stop = false;
	uint64_t candidates = 0;
	uint64_t total_ns = 0;
	uint64_t candidate_ns = 0;
	std::array<uint8_t, (N + 7) / 8> decoded{};
	Completion completion_state;
};

DecodeResult decode_once(
	CODE::OrderedStatisticsDecoder<N, K, O> &decoder,
	const std::array<int8_t, N> &soft,
	const int8_t *genmat,
	SearchMode mode
)
{
	search_mode = mode;
	search_stats = {};
	profile.reset();
	DecodeResult result;
	const auto start = Clock::now();
	result.unique = decoder(result.decoded.data(), soft.data(), genmat);
	const auto end = Clock::now();
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	result.candidate_ns = profile.elapsed[7];
	result.exact_stop = search_stats.exact_stop;
	result.candidates = mode == SearchMode::baseline
		? EXPECTED_CANDIDATES
		: search_stats.candidates;
	result.completion_state = completion;
	return result;
}

void assert_equal(const DecodeResult &reference, const DecodeResult &tested)
{
	assert(reference.unique == tested.unique);
	assert(reference.decoded == tested.decoded);
	assert(reference.completion_state.best == tested.completion_state.best);
	if (!tested.exact_stop)
		assert(reference.completion_state.next == tested.completion_state.next);
	else
		assert(reference.unique);
	assert(
		reference.completion_state.candidate ==
		tested.completion_state.candidate
	);
}

uint64_t percentile(std::vector<uint64_t> values, double fraction)
{
	std::sort(values.begin(), values.end());
	const std::size_t index = static_cast<std::size_t>(
		std::ceil(values.size() * fraction)
	) - 1;
	return values[index];
}

double mean(const std::vector<uint64_t> &values)
{
	const long double sum = std::accumulate(
		values.begin(),
		values.end(),
		static_cast<long double>(0)
	);
	return static_cast<double>(sum / values.size());
}

uint64_t checksum(const DecodeResult &result)
{
	uint64_t value = 1469598103934665603ULL;
	for (const uint8_t byte: result.decoded)
		value = value * FNV_PRIME ^ byte;
	value = value * FNV_PRIME ^ static_cast<uint64_t>(result.unique);
	value = value * FNV_PRIME ^
		static_cast<uint64_t>(result.completion_state.best);
	return value;
}

struct Samples {
	std::vector<uint64_t> total;
	std::vector<uint64_t> candidate;
	uint64_t exact_stops = 0;
	uint64_t candidates = 0;
};

struct Summary {
	double snr_db = 0;
	std::size_t mode_index = 0;
	uint64_t samples = 0;
	uint64_t exact_stops = 0;
	double mean_candidates = 0;
	uint64_t p50_total_ns = 0;
	uint64_t p95_total_ns = 0;
	uint64_t p99_total_ns = 0;
	double mean_total_ns = 0;
	uint64_t p50_candidate_ns = 0;
	uint64_t p95_candidate_ns = 0;
	uint64_t p99_candidate_ns = 0;
};

}

int main(int argc, char **argv)
{
	using namespace experiment;
#ifndef PERSISTENT_POOL_FRAMES
#define PERSISTENT_POOL_FRAMES 12
#endif
#ifndef PERSISTENT_POOL_REPEATS
#define PERSISTENT_POOL_REPEATS 2
#endif
#ifndef PERSISTENT_POOL_WARMUPS
#define PERSISTENT_POOL_WARMUPS 1
#endif
	constexpr int FRAMES = PERSISTENT_POOL_FRAMES;
	constexpr int REPEATS = PERSISTENT_POOL_REPEATS;
	constexpr int WARMUPS = PERSISTENT_POOL_WARMUPS;
	constexpr std::array<double, 4> SNR_VALUES{4.0, 6.0, 8.0, 10.0};

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

	const auto pool_start = Clock::now();
	PersistentPool pool(WORKERS);
	const auto pool_end = Clock::now();
	const uint64_t pool_startup_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			pool_end - pool_start
		).count()
	);
	persistent_pool = &pool;

	std::ofstream raw;
	if (argc > 2) {
		raw.open(argv[2]);
		raw << "ebn0_db,repeat,frame,mode,workers,total_ns,candidate_ns,"
			"candidates,exact_stop,checksum,result\n";
	}

	std::mt19937_64 random(0x5045525349535455ULL);
	std::uniform_int_distribution<int> bit(0, 1);
	std::vector<Summary> summaries;
	uint64_t output_checksum = 0;

	for (const double snr_db: SNR_VALUES) {
		const double snr = std::pow(10.0, snr_db / 10.0);
		const double rate = static_cast<double>(K) / N;
		const double sigma = std::sqrt(1.0 / (2.0 * rate * snr));
		std::normal_distribution<double> noise(0.0, sigma);
		std::vector<std::array<int8_t, N>> frames(
			static_cast<std::size_t>(FRAMES)
		);

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
				soft[static_cast<std::size_t>(index)] = received < 0
					? static_cast<int8_t>(-magnitude)
					: static_cast<int8_t>(magnitude);
			}
		}

		std::vector<DecodeResult> references;
		references.reserve(frames.size());
		for (const auto &frame: frames)
			references.push_back(decode_once(
				decoder, frame, genmat, SearchMode::baseline
			));

		for (int warmup = 0; warmup < WARMUPS; ++warmup)
			for (const auto &mode: MODES)
				for (int frame = 0; frame < std::min(FRAMES, 2); ++frame)
					decode_once(
						decoder,
						frames[static_cast<std::size_t>(frame)],
						genmat,
						mode.mode
					);

		std::array<Samples, MODES.size()> samples;
		for (int repeat = 0; repeat < REPEATS; ++repeat) {
			for (int frame = 0; frame < FRAMES; ++frame) {
				std::array<std::size_t, MODES.size()> order{};
				std::iota(order.begin(), order.end(), 0);
				const std::size_t shift = static_cast<std::size_t>(
					(repeat + frame) % static_cast<int>(MODES.size())
				);
				std::rotate(order.begin(), order.begin() + shift, order.end());
				if ((repeat + frame) & 1)
					std::reverse(order.begin(), order.end());

				for (const std::size_t mode_index: order) {
					const auto &mode = MODES[mode_index];
					const DecodeResult result = decode_once(
						decoder,
						frames[static_cast<std::size_t>(frame)],
						genmat,
						mode.mode
					);
					assert_equal(
						references[static_cast<std::size_t>(frame)],
						result
					);
					auto &mode_samples = samples[mode_index];
					mode_samples.total.push_back(result.total_ns);
					mode_samples.candidate.push_back(result.candidate_ns);
					mode_samples.exact_stops += result.exact_stop;
					mode_samples.candidates += result.candidates;
					const uint64_t digest = checksum(result);
					output_checksum ^= digest;
					if (raw)
						raw << std::fixed << std::setprecision(3) << snr_db
							<< ',' << repeat + 1 << ',' << frame + 1 << ','
							<< mode.name << ','
							<< (mode.mode == SearchMode::baseline ? 1 : WORKERS)
							<< ',' << result.total_ns << ','
							<< result.candidate_ns << ',' << result.candidates
							<< ',' << result.exact_stop << ',' << std::hex
							<< digest << std::dec << ",pass\n";
				}
			}
		}

		for (std::size_t mode_index = 0;
			mode_index < MODES.size();
			++mode_index) {
			const auto &value = samples[mode_index];
			Summary summary;
			summary.snr_db = snr_db;
			summary.mode_index = mode_index;
			summary.samples = value.total.size();
			summary.exact_stops = value.exact_stops;
			summary.mean_candidates =
				static_cast<double>(value.candidates) / value.total.size();
			summary.p50_total_ns = percentile(value.total, 0.50);
			summary.p95_total_ns = percentile(value.total, 0.95);
			summary.p99_total_ns = percentile(value.total, 0.99);
			summary.mean_total_ns = mean(value.total);
			summary.p50_candidate_ns = percentile(value.candidate, 0.50);
			summary.p95_candidate_ns = percentile(value.candidate, 0.95);
			summary.p99_candidate_ns = percentile(value.candidate, 0.99);
			summaries.push_back(summary);
		}
	}

	if (argc > 1) {
		std::ofstream csv(argv[1]);
		csv << "ebn0_db,mode,workers,frames,repeats,samples,pool_startup_ns,"
			"exact_stops,stop_rate,mean_candidates,p50_total_ns,p95_total_ns,"
			"p99_total_ns,mean_total_ns,p50_candidate_ns,p95_candidate_ns,"
			"p99_candidate_ns,blocks_per_second,p50_speedup,p95_speedup,"
			"p99_speedup,result\n";
		for (const auto &summary: summaries) {
			const std::size_t snr_offset =
				static_cast<std::size_t>(&summary - summaries.data()) /
				MODES.size() * MODES.size();
			const auto &baseline = summaries[snr_offset];
			const auto &mode = MODES[summary.mode_index];
			csv << std::fixed << std::setprecision(6) << summary.snr_db
				<< ',' << mode.name << ','
				<< (mode.mode == SearchMode::baseline ? 1 : WORKERS)
				<< ',' << FRAMES << ',' << REPEATS << ',' << summary.samples
				<< ',' << pool_startup_ns << ',' << summary.exact_stops << ','
				<< static_cast<double>(summary.exact_stops) / summary.samples
				<< ',' << summary.mean_candidates << ',' << summary.p50_total_ns
				<< ',' << summary.p95_total_ns << ',' << summary.p99_total_ns
				<< ',' << summary.mean_total_ns << ','
				<< summary.p50_candidate_ns << ',' << summary.p95_candidate_ns
				<< ',' << summary.p99_candidate_ns << ','
				<< 1e9 / summary.mean_total_ns << ','
				<< static_cast<double>(baseline.p50_total_ns) /
					summary.p50_total_ns << ','
				<< static_cast<double>(baseline.p95_total_ns) /
					summary.p95_total_ns << ','
				<< static_cast<double>(baseline.p99_total_ns) /
					summary.p99_total_ns << ",pass\n";
		}
	}

	std::cout << "PERSISTENT_POOL_PASS frames_per_snr=" << FRAMES
		<< " repeats=" << REPEATS << " workers=" << WORKERS
		<< " pool_startup_ns=" << pool_startup_ns << '\n';
	for (const auto &summary: summaries) {
		const std::size_t snr_offset =
			static_cast<std::size_t>(&summary - summaries.data()) /
			MODES.size() * MODES.size();
		const auto &baseline = summaries[snr_offset];
		const auto &mode = MODES[summary.mode_index];
		std::cout << "PERSISTENT_POOL_RESULT ebn0_db=" << summary.snr_db
			<< " mode=" << mode.name
			<< " stops=" << summary.exact_stops << '/' << summary.samples
			<< " mean_candidates=" << static_cast<uint64_t>(
				summary.mean_candidates
			)
			<< " p50_ns=" << summary.p50_total_ns
			<< " p95_ns=" << summary.p95_total_ns
			<< " p99_ns=" << summary.p99_total_ns
			<< " p50_speedup=" << std::fixed << std::setprecision(3)
			<< static_cast<double>(baseline.p50_total_ns) /
				summary.p50_total_ns
			<< " p95_speedup="
			<< static_cast<double>(baseline.p95_total_ns) /
				summary.p95_total_ns
			<< " p99_speedup="
			<< static_cast<double>(baseline.p99_total_ns) /
				summary.p99_total_ns << '\n';
	}
	std::cout << "PERSISTENT_POOL_CHECKSUM " << std::hex
		<< output_checksum << std::dec << '\n';
	persistent_pool = nullptr;
	return 0;
}
