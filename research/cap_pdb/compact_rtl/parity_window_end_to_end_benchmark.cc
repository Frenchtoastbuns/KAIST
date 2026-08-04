#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace experiment {

template <int N, int K, int O>
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
void profile_begin(int stage);
void profile_end(int stage);
void capture_complete(int best, int next);
#ifdef OSD_ADAPTIVE_DATASET
void capture_adaptive_ready(
	const int8_t *matrix,
	const int8_t *hard,
	const int8_t *soft,
	const int16_t *permutation,
	int length,
	int dimension,
	int width
);
#endif

}

#define CODE_OSD_SEARCH_OVERRIDE(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next) \
	experiment::search_override<N, K, O>(matrix, base, candidate, soft, permutation, length, dimension, width, order, best, next)
#define CODE_OSD_PROFILE_BEGIN(stage) experiment::profile_begin(stage)
#define CODE_OSD_PROFILE_END(stage) experiment::profile_end(stage)
#define CODE_OSD_SEARCH_COMPLETE(best, next, candidate, permutation, length, width) \
	experiment::capture_complete(best, next)
#ifdef OSD_ADAPTIVE_DATASET
#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) \
	experiment::capture_adaptive_ready( \
		matrix, hard, soft, permutation, length, dimension, width \
	)
#endif

#include "bitman.hh"
#include "canonical_osd.hh"
#include "basic_orbgrand_stub.hh"

namespace experiment {

using Clock = std::chrono::steady_clock;
constexpr int STAGE_COUNT = 9;
#ifndef PARITY_WINDOW_E2E_WINDOW_BITS
#define PARITY_WINDOW_E2E_WINDOW_BITS 4
#endif
constexpr int WINDOW_BITS = PARITY_WINDOW_E2E_WINDOW_BITS;
static_assert(WINDOW_BITS > 0 && WINDOW_BITS <= 16);
constexpr int NEGATIVE_INFINITY = -1000000000;
#ifndef PARITY_WINDOW_E2E_FRAMES
#define PARITY_WINDOW_E2E_FRAMES 200
#endif
constexpr int FRAMES_PER_SNR = PARITY_WINDOW_E2E_FRAMES;
#ifndef PARITY_WINDOW_E2E_REPEATS
#define PARITY_WINDOW_E2E_REPEATS 3
#endif
constexpr int REPEATS = PARITY_WINDOW_E2E_REPEATS;
#ifndef PARITY_WINDOW_E2E_CANDIDATE_BUDGET
#define PARITY_WINDOW_E2E_CANDIDATE_BUDGET 1024
#endif
constexpr uint64_t DEFAULT_GUARDED_CANDIDATE_BUDGET =
	PARITY_WINDOW_E2E_CANDIDATE_BUDGET;
#ifndef OSD_PROJECTED_WARM_CANDIDATES
#define OSD_PROJECTED_WARM_CANDIDATES 8
#endif
constexpr uint64_t PROJECTED_WARM_CANDIDATES =
	OSD_PROJECTED_WARM_CANDIDATES;
static_assert(PROJECTED_WARM_CANDIDATES > 0);
#ifndef OSD_PROJECTED_PATTERN_MULTIPLIER
#define OSD_PROJECTED_PATTERN_MULTIPLIER 16
#endif
constexpr uint64_t PROJECTED_PATTERN_MULTIPLIER =
	OSD_PROJECTED_PATTERN_MULTIPLIER;
static_assert(PROJECTED_PATTERN_MULTIPLIER > 0);
#ifndef OSD_PROJECTED_SCREEN_BITS
#define OSD_PROJECTED_SCREEN_BITS PARITY_WINDOW_E2E_WINDOW_BITS
#endif
constexpr int PROJECTED_SCREEN_BITS = OSD_PROJECTED_SCREEN_BITS;
static_assert(PROJECTED_SCREEN_BITS > 0 && PROJECTED_SCREEN_BITS <= 16);
#ifndef OSD_ADDITIVE_PDB_BITS
// Frozen before the precision, code-family, channel, and comparator holdouts.
#define OSD_ADDITIVE_PDB_BITS 5
#endif
constexpr int ADDITIVE_PDB_BITS = OSD_ADDITIVE_PDB_BITS;
static_assert(ADDITIVE_PDB_BITS > 0 && ADDITIVE_PDB_BITS <= 8);
#ifndef OSD_PDB_GROUPING
// 0: reliability-contiguous groups; 1: reliability-striped groups;
// 2: both partitions, combined by a pointwise maximum bound;
// 3: reliability-contiguous groups plus a fused top-two-group abstraction.
#define OSD_PDB_GROUPING 0
#endif
constexpr int PDB_GROUPING = OSD_PDB_GROUPING;
static_assert(PDB_GROUPING >= 0 && PDB_GROUPING <= 3);
#ifndef OSD_SELECTIVE_COUPLING_GROUP_LIMIT
// Oracle-only breadth of the parity-pair pool.  The first groups contain the
// largest parity reliabilities because the contiguous PDB partition is sorted
// in descending reliability order.  A value of 999 includes every group.
#define OSD_SELECTIVE_COUPLING_GROUP_LIMIT 2
#endif
constexpr int SELECTIVE_COUPLING_GROUP_LIMIT =
	OSD_SELECTIVE_COUPLING_GROUP_LIMIT;
static_assert(SELECTIVE_COUPLING_GROUP_LIMIT >= 2);
#ifndef OSD_FINGERPRINT_PDB_BITS
#define OSD_FINGERPRINT_PDB_BITS 2
#endif
constexpr int FINGERPRINT_PDB_BITS = OSD_FINGERPRINT_PDB_BITS;
static_assert(FINGERPRINT_PDB_BITS > 0 && FINGERPRINT_PDB_BITS <= 4);
#ifndef OSD_FINGERPRINT_PDB_GROUPS
#define OSD_FINGERPRINT_PDB_GROUPS 999
#endif
constexpr int FINGERPRINT_PDB_GROUPS = OSD_FINGERPRINT_PDB_GROUPS;
static_assert(FINGERPRINT_PDB_GROUPS > 0);
#ifndef OSD_QUOTIENT_PDB_BITS
#define OSD_QUOTIENT_PDB_BITS 8
#endif
constexpr int QUOTIENT_PDB_BITS = OSD_QUOTIENT_PDB_BITS;
static_assert(QUOTIENT_PDB_BITS > 0 && QUOTIENT_PDB_BITS <= 15);
#ifndef OSD_QUOTIENT_PDB_DIRECT_BITS
#define OSD_QUOTIENT_PDB_DIRECT_BITS 4
#endif
constexpr int QUOTIENT_PDB_DIRECT_BITS = OSD_QUOTIENT_PDB_DIRECT_BITS;
static_assert(
	QUOTIENT_PDB_DIRECT_BITS >= 0 &&
	QUOTIENT_PDB_DIRECT_BITS <= QUOTIENT_PDB_BITS
);
#ifndef OSD_PACKED_PDB_BOUND_DEPTH_MASK
#define OSD_PACKED_PDB_BOUND_DEPTH_MASK 7
#endif
constexpr int PACKED_PDB_BOUND_DEPTH_MASK = OSD_PACKED_PDB_BOUND_DEPTH_MASK;
static_assert(PACKED_PDB_BOUND_DEPTH_MASK >= 0 && PACKED_PDB_BOUND_DEPTH_MASK < 8);
#ifndef OSD_PACKED_PDB_MIN_DESCENDANTS
// A strong PDB lookup can cost more than directly visiting a tiny residual
// subtree.  This exact routing gate skips the lookup (not the subtree) below
// the frozen cardinality threshold.  A wrong routing decision can therefore
// increase work but cannot remove an OSD candidate.
#define OSD_PACKED_PDB_MIN_DESCENDANTS 0
#endif
constexpr uint64_t PACKED_PDB_MIN_DESCENDANTS =
	OSD_PACKED_PDB_MIN_DESCENDANTS;
#ifndef OSD_PACKED_PDB_WITNESS_PROPOSALS
// Reuse each additive abstraction as a proposal policy before exact
// certification.  The proposals only initialize the incumbent; the complete
// fixed-order OSD list is still certified afterward.
#define OSD_PACKED_PDB_WITNESS_PROPOSALS 0
#endif
constexpr bool PACKED_PDB_WITNESS_PROPOSALS =
	OSD_PACKED_PDB_WITNESS_PROPOSALS != 0;
#ifndef OSD_PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI
// Invoke witness proposals only when the discovery incumbent has normalized
// weighted distance d_seed/sum|r| above this threshold (in thousandths).
#define OSD_PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI 0
#endif
constexpr int PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI =
	OSD_PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI;
static_assert(
	PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI >= 0 &&
	PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI <= 1000
);
#ifndef OSD_PACKED_PDB_TRIANGLE_PREFILTER
#define OSD_PACKED_PDB_TRIANGLE_PREFILTER 1
#endif
constexpr bool PACKED_PDB_TRIANGLE_PREFILTER =
	OSD_PACKED_PDB_TRIANGLE_PREFILTER != 0;
#ifndef OSD_PACKED_PDB_CONFLICT_LEARNING
// Frame-local exact no-good learning for the packed certification tree.
// A learned core C is retained only when an admissible relaxation proves
// that every order-O TEP containing C has cost strictly above the incumbent.
// Later tree nodes containing C can then be discarded without a PDB query.
#define OSD_PACKED_PDB_CONFLICT_LEARNING 0
#endif
constexpr bool PACKED_PDB_CONFLICT_LEARNING =
	OSD_PACKED_PDB_CONFLICT_LEARNING != 0;
#ifndef OSD_PACKED_PDB_CONFLICT_CAPACITY
#define OSD_PACKED_PDB_CONFLICT_CAPACITY 64
#endif
constexpr int PACKED_PDB_CONFLICT_CAPACITY =
	OSD_PACKED_PDB_CONFLICT_CAPACITY;
static_assert(
	PACKED_PDB_CONFLICT_CAPACITY > 0 &&
	PACKED_PDB_CONFLICT_CAPACITY <= 1024
);
#ifndef OSD_PACKED_PDB_CONFLICT_DERIVATION_BUDGET
#define OSD_PACKED_PDB_CONFLICT_DERIVATION_BUDGET 64
#endif
constexpr uint64_t PACKED_PDB_CONFLICT_DERIVATION_BUDGET =
	OSD_PACKED_PDB_CONFLICT_DERIVATION_BUDGET;
static_assert(PACKED_PDB_CONFLICT_DERIVATION_BUDGET > 0);
#ifndef OSD_PACKED_PDB_LANDMARKS
#define OSD_PACKED_PDB_LANDMARKS 1
#endif
constexpr int PACKED_PDB_LANDMARKS = OSD_PACKED_PDB_LANDMARKS;
static_assert(PACKED_PDB_LANDMARKS >= 1 && PACKED_PDB_LANDMARKS <= 4);
#ifndef OSD_PACKED_PDB_DISPATCH_MILLI
#define OSD_PACKED_PDB_DISPATCH_MILLI 80
#endif
constexpr int PACKED_PDB_DISPATCH_MILLI = OSD_PACKED_PDB_DISPATCH_MILLI;
static_assert(
	PACKED_PDB_DISPATCH_MILLI >= 0 && PACKED_PDB_DISPATCH_MILLI <= 1000
);
#ifndef OSD_ORB_SEED_QUERIES
#define OSD_ORB_SEED_QUERIES 64
#endif
constexpr uint64_t ORB_SEED_QUERIES = OSD_ORB_SEED_QUERIES;
static_assert(ORB_SEED_QUERIES > 0);
#ifndef OSD_SOFT_MAGNITUDE_BITS
#define OSD_SOFT_MAGNITUDE_BITS 7
#endif
constexpr int SOFT_MAGNITUDE_BITS = OSD_SOFT_MAGNITUDE_BITS;
static_assert(SOFT_MAGNITUDE_BITS >= 2 && SOFT_MAGNITUDE_BITS <= 7);
constexpr int SOFT_MAGNITUDE_MAX = (1 << SOFT_MAGNITUDE_BITS) - 1;
// Preserve the original approximately +/-4 observation dynamic range at
// every tested fixed-point width.  Seven magnitude bits therefore retain the
// historical scale of 32 quantization steps per received-symbol unit.
constexpr double SOFT_QUANTIZATION_SCALE =
	static_cast<double>(SOFT_MAGNITUDE_MAX + 1) / 4.0;
#ifndef OSD_ADDITIVE_PDB_DISPATCH_MILLI
#define OSD_ADDITIVE_PDB_DISPATCH_MILLI 80
#endif
constexpr int ADDITIVE_PDB_DISPATCH_MILLI =
	OSD_ADDITIVE_PDB_DISPATCH_MILLI;
static_assert(
	ADDITIVE_PDB_DISPATCH_MILLI >= 0 &&
	ADDITIVE_PDB_DISPATCH_MILLI <= 1000
);
#ifndef OSD_ADDITIVE_PDB_DISCOVERY_BUDGET
#define OSD_ADDITIVE_PDB_DISCOVERY_BUDGET 16
#endif
constexpr uint64_t ADDITIVE_PDB_DISCOVERY_BUDGET =
	OSD_ADDITIVE_PDB_DISCOVERY_BUDGET;
static_assert(ADDITIVE_PDB_DISCOVERY_BUDGET > 0);
#ifndef OSD_EXPERIMENT_SEED
#define OSD_EXPERIMENT_SEED 0x4e4f56454c545931ULL
#endif
#ifndef OSD_PROJECTED_LEVEL1_CANDIDATES
#define OSD_PROJECTED_LEVEL1_CANDIDATES 512
#endif
#ifndef OSD_PROJECTED_LEVEL2_CANDIDATES
#define OSD_PROJECTED_LEVEL2_CANDIDATES 768
#endif
constexpr uint64_t PROJECTED_LEVEL1_CANDIDATES =
	OSD_PROJECTED_LEVEL1_CANDIDATES;
constexpr uint64_t PROJECTED_LEVEL2_CANDIDATES =
	OSD_PROJECTED_LEVEL2_CANDIDATES;
static_assert(PROJECTED_LEVEL1_CANDIDATES < PROJECTED_LEVEL2_CANDIDATES);
#ifndef OSD_PORTFOLIO_LOW_RATE_BUDGET
#define OSD_PORTFOLIO_LOW_RATE_BUDGET 64
#endif
constexpr uint64_t PORTFOLIO_LOW_RATE_BUDGET =
	OSD_PORTFOLIO_LOW_RATE_BUDGET;
static_assert(PORTFOLIO_LOW_RATE_BUDGET > 0);
#ifndef OSD_PACKED_DISCOVERY_BUDGET
#define OSD_PACKED_DISCOVERY_BUDGET 64
#endif
constexpr uint64_t PACKED_DISCOVERY_BUDGET = OSD_PACKED_DISCOVERY_BUDGET;

#ifndef OSD_PACKED_MIN_CANDIDATES
#define OSD_PACKED_MIN_CANDIDATES 16384
#endif
constexpr uint64_t PACKED_MIN_CANDIDATES = OSD_PACKED_MIN_CANDIDATES;
static_assert(PACKED_DISCOVERY_BUDGET > 0);
#ifndef OSD_SYNDROME_SKETCH_POSITIONS
#define OSD_SYNDROME_SKETCH_POSITIONS 15
#endif
constexpr int SYNDROME_SKETCH_POSITIONS = OSD_SYNDROME_SKETCH_POSITIONS;
static_assert(SYNDROME_SKETCH_POSITIONS > 0);
uint64_t active_guard_candidate_budget = DEFAULT_GUARDED_CANDIDATE_BUDGET;
constexpr uint64_t MIN_BOUND_WORKLOAD = 4096;

#ifdef OSD_ADAPTIVE_DATASET
// All dispatcher features are available at the decoder's ordinary
// post-systematicization search boundary.  None depends on a candidate-search
// result.  The histogram implementation exploits the int8 reliability alphabet
// and keeps feature extraction bounded by O(N + 127).
struct AdaptiveFeatures {
	uint64_t extraction_ns = 0;
	double total_abs = 0.0;
	double order0_distance = 0.0;
	double order0_distance_norm = 0.0;
	double order0_mismatch_fraction = 0.0;
	double information_mean_norm = 0.0;
	double information_std_norm = 0.0;
	double information_min_norm = 0.0;
	double information_q10_norm = 0.0;
	double information_q25_norm = 0.0;
	double information_median_norm = 0.0;
	double parity_mean_norm = 0.0;
	double parity_std_norm = 0.0;
	double parity_q10_norm = 0.0;
	double parity_q25_norm = 0.0;
	double parity_median_norm = 0.0;
	double parity_max_norm = 0.0;
	double parity_mismatch_fraction = 0.0;
	double parity_mismatch_cost_norm = 0.0;
	double projected_mismatch_fraction = 0.0;
	double projected_mismatch_cost_norm = 0.0;
	double information_min4_cost_norm = 0.0;
	double saturation_fraction = 0.0;
	double original_adjacent_variation = 0.0;
	double original_abs_lag1_correlation = 0.0;
	double original_sign_transition_fraction = 0.0;
	double original_low_reliability_run_fraction = 0.0;
};

AdaptiveFeatures captured_adaptive_features;
bool capture_adaptive_enabled = false;

int histogram_quantile(
	const std::array<int, 128> &histogram,
	int count,
	int numerator,
	int denominator
)
{
	assert(count > 0);
	const int rank = ((count - 1) * numerator) / denominator;
	int cumulative = 0;
	for (int value = 0; value < 128; ++value) {
		cumulative += histogram[static_cast<std::size_t>(value)];
		if (cumulative > rank)
			return value;
	}
	assert(false);
	return 127;
}

void capture_adaptive_ready(
	const int8_t *,
	const int8_t *hard,
	const int8_t *soft,
	const int16_t *permutation,
	int length,
	int dimension,
	int width
)
{
	if (!capture_adaptive_enabled)
		return;
	const auto start = Clock::now();
	assert(length > dimension && dimension > 0 && width >= length);
	AdaptiveFeatures result;
	std::array<int, 128> all_histogram{};
	std::array<int, 128> information_histogram{};
	std::array<int, 128> parity_histogram{};
	std::vector<int> original_abs(static_cast<std::size_t>(length));
	std::vector<int8_t> original_sign(static_cast<std::size_t>(length));
	std::array<std::pair<int, int>, 4> strongest_parity{};
	for (auto &entry: strongest_parity)
		entry = {-1, std::numeric_limits<int>::max()};
	std::array<int, 4> weakest_information{};
	weakest_information.fill(128);

	double information_sum = 0.0;
	double information_square_sum = 0.0;
	double parity_sum = 0.0;
	double parity_square_sum = 0.0;
	int order0_mismatches = 0;
	int parity_mismatches = 0;
	int saturated = 0;
	for (int index = 0; index < length; ++index) {
		const int reliability = std::abs(static_cast<int>(soft[index]));
		const bool mismatch = hard[index] != (soft[index] < 0);
		result.total_abs += reliability;
		result.order0_distance += mismatch ? reliability : 0;
		order0_mismatches += mismatch;
		saturated += reliability == 127;
		++all_histogram[static_cast<std::size_t>(reliability)];
		const int original = permutation[index];
		assert(original >= 0 && original < length);
		original_abs[static_cast<std::size_t>(original)] = reliability;
		original_sign[static_cast<std::size_t>(original)] = soft[index] < 0;
		if (index < dimension) {
			information_sum += reliability;
			information_square_sum += reliability * reliability;
			++information_histogram[static_cast<std::size_t>(reliability)];
			for (int slot = 0; slot < 4; ++slot) {
				if (reliability < weakest_information[static_cast<std::size_t>(slot)]) {
					for (int move = 3; move > slot; --move)
						weakest_information[static_cast<std::size_t>(move)] =
							weakest_information[static_cast<std::size_t>(move - 1)];
					weakest_information[static_cast<std::size_t>(slot)] = reliability;
					break;
				}
			}
		} else {
			parity_sum += reliability;
			parity_square_sum += reliability * reliability;
			++parity_histogram[static_cast<std::size_t>(reliability)];
			parity_mismatches += mismatch;
			result.parity_mismatch_cost_norm += mismatch ? reliability : 0;
			const std::pair<int, int> proposed{reliability, index};
			for (int slot = 0; slot < 4; ++slot) {
				const auto current = strongest_parity[static_cast<std::size_t>(slot)];
				if (proposed.first > current.first ||
					(proposed.first == current.first && proposed.second < current.second)) {
					for (int move = 3; move > slot; --move)
						strongest_parity[static_cast<std::size_t>(move)] =
							strongest_parity[static_cast<std::size_t>(move - 1)];
					strongest_parity[static_cast<std::size_t>(slot)] = proposed;
					break;
				}
			}
		}
	}

	const int parity_count = length - dimension;
	const double global_mean = result.total_abs / length;
	const double scale = std::max(global_mean, 1.0);
	const double information_mean = information_sum / dimension;
	const double parity_mean = parity_sum / parity_count;
	const double information_variance = std::max(
		0.0, information_square_sum / dimension -
			information_mean * information_mean
	);
	const double parity_variance = std::max(
		0.0, parity_square_sum / parity_count - parity_mean * parity_mean
	);
	result.order0_distance_norm = result.order0_distance /
		std::max(result.total_abs, 1.0);
	result.order0_mismatch_fraction =
		static_cast<double>(order0_mismatches) / length;
	result.information_mean_norm = information_mean / scale;
	result.information_std_norm = std::sqrt(information_variance) / scale;
	result.information_min_norm =
		histogram_quantile(information_histogram, dimension, 0, 1) / scale;
	result.information_q10_norm =
		histogram_quantile(information_histogram, dimension, 1, 10) / scale;
	result.information_q25_norm =
		histogram_quantile(information_histogram, dimension, 1, 4) / scale;
	result.information_median_norm =
		histogram_quantile(information_histogram, dimension, 1, 2) / scale;
	result.parity_mean_norm = parity_mean / scale;
	result.parity_std_norm = std::sqrt(parity_variance) / scale;
	result.parity_q10_norm =
		histogram_quantile(parity_histogram, parity_count, 1, 10) / scale;
	result.parity_q25_norm =
		histogram_quantile(parity_histogram, parity_count, 1, 4) / scale;
	result.parity_median_norm =
		histogram_quantile(parity_histogram, parity_count, 1, 2) / scale;
	result.parity_max_norm =
		histogram_quantile(parity_histogram, parity_count, 1, 1) / scale;
	result.parity_mismatch_fraction =
		static_cast<double>(parity_mismatches) / parity_count;
	result.parity_mismatch_cost_norm /= std::max(result.total_abs, 1.0);
	const int projected_bits = std::min(4, parity_count);
	int projected_mismatches = 0;
	double projected_mismatch_cost = 0.0;
	for (int slot = 0; slot < projected_bits; ++slot) {
		const int index = strongest_parity[static_cast<std::size_t>(slot)].second;
		assert(index >= dimension && index < length);
		const bool mismatch = hard[index] != (soft[index] < 0);
		projected_mismatches += mismatch;
		projected_mismatch_cost += mismatch
			? strongest_parity[static_cast<std::size_t>(slot)].first : 0;
	}
	result.projected_mismatch_fraction =
		static_cast<double>(projected_mismatches) / projected_bits;
	result.projected_mismatch_cost_norm = projected_mismatch_cost /
		std::max(result.total_abs, 1.0);
	result.information_min4_cost_norm = static_cast<double>(
		std::accumulate(
			weakest_information.begin(), weakest_information.end(), 0
		)
	) / std::max(result.total_abs, 1.0);
	result.saturation_fraction = static_cast<double>(saturated) / length;

	double adjacent_variation = 0.0;
	double lag_numerator = 0.0;
	double lag_left_square = 0.0;
	double lag_right_square = 0.0;
	int sign_transitions = 0;
	const int low_threshold = histogram_quantile(all_histogram, length, 1, 4);
	int current_low_run = 0;
	int maximum_low_run = 0;
	for (int index = 0; index < length; ++index) {
		const int reliability = original_abs[static_cast<std::size_t>(index)];
		if (reliability <= low_threshold) {
			++current_low_run;
			maximum_low_run = std::max(maximum_low_run, current_low_run);
		} else {
			current_low_run = 0;
		}
		if (index + 1 == length)
			continue;
		const int right = original_abs[static_cast<std::size_t>(index + 1)];
		adjacent_variation += std::abs(reliability - right);
		const double centered_left = reliability - global_mean;
		const double centered_right = right - global_mean;
		lag_numerator += centered_left * centered_right;
		lag_left_square += centered_left * centered_left;
		lag_right_square += centered_right * centered_right;
		sign_transitions +=
			original_sign[static_cast<std::size_t>(index)] !=
			original_sign[static_cast<std::size_t>(index + 1)];
	}
	result.original_adjacent_variation = adjacent_variation /
		(std::max(1, length - 1) * scale);
	const double lag_denominator = std::sqrt(lag_left_square * lag_right_square);
	result.original_abs_lag1_correlation = lag_denominator > 0.0
		? lag_numerator / lag_denominator : 0.0;
	result.original_sign_transition_fraction = static_cast<double>(
		sign_transitions
	) / std::max(1, length - 1);
	result.original_low_reliability_run_fraction =
		static_cast<double>(maximum_low_run) / length;
	const auto end = Clock::now();
	result.extraction_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	captured_adaptive_features = result;
}

void write_adaptive_features(
	std::ostream &output,
	const AdaptiveFeatures &value
)
{
	output << ',' << value.extraction_ns
		<< ',' << value.total_abs
		<< ',' << value.order0_distance
		<< ',' << value.order0_distance_norm
		<< ',' << value.order0_mismatch_fraction
		<< ',' << value.information_mean_norm
		<< ',' << value.information_std_norm
		<< ',' << value.information_min_norm
		<< ',' << value.information_q10_norm
		<< ',' << value.information_q25_norm
		<< ',' << value.information_median_norm
		<< ',' << value.parity_mean_norm
		<< ',' << value.parity_std_norm
		<< ',' << value.parity_q10_norm
		<< ',' << value.parity_q25_norm
		<< ',' << value.parity_median_norm
		<< ',' << value.parity_max_norm
		<< ',' << value.parity_mismatch_fraction
		<< ',' << value.parity_mismatch_cost_norm
		<< ',' << value.projected_mismatch_fraction
		<< ',' << value.projected_mismatch_cost_norm
		<< ',' << value.information_min4_cost_norm
		<< ',' << value.saturation_fraction
		<< ',' << value.original_adjacent_variation
		<< ',' << value.original_abs_lag1_correlation
		<< ',' << value.original_sign_transition_fraction
		<< ',' << value.original_low_reliability_run_fraction;
}
#endif

enum class Mode {
	baseline,
#ifdef OSD_NOVELTY_COMPARISON
	exact_stop_only,
	soft_weight_exact,
	guarded_soft_weight_exact,
	guarded_projected_astar_exact,
	guarded_adaptive_projected_astar_exact,
	guarded_projected_screen_exact,
	guarded_hierarchical_projected_screen_exact,
	guarded_portfolio_projected_screen_exact,
	guarded_syndrome_sketch_screen_exact,
	guarded_discover_certify_exact,
	guarded_packed_parity_certify_exact,
	adaptive_packed_dispatch_exact,
	guarded_additive_pdb_exact,
	guarded_cost_partition_pdb_exact,
	guarded_fingerprint_pdb_exact,
	guarded_quotient_pdb_exact,
	guarded_query_local_pdb_exact,
	guarded_packed_state_pdb_exact,
	guarded_witness_packed_state_pdb_exact,
	reconstructed_normal_cap_pdb_exact,
	reconstructed_compact_two_block_pdb_exact,
	reconstructed_dual_two_block_pdb_exact,
	reconstructed_compiled_dual_bank_pdb_exact,
	oracle_selective_coupling_pdb_exact,
	oracle_perfect_subtree_pdb_exact,
	oracle_perfect_seed_subtree_pdb_exact,
	guarded_orb_seeded_packed_pdb_exact,
	bitplane_frontier_exact,
	transposed_frontier_exact,
	adaptive_packed_state_pdb_exact,
	adaptive_additive_pdb_exact,
	adaptive_cost_partition_pdb_exact,
	published_trivial,
	published_dai,
	published_extra_parity_delta4,
	published_joint_dai_delta4,
	orbgrand_queries_1k,
	orbgrand_queries_10k,
	orbgrand_queries_100k,
	orbgrand_queries_1m,
	orbgrand_1line_auto_queries_1m,
	orbgrand_1line_then_clm_queries_16,
	orbgrand_1line_then_clm_queries_64,
	orbgrand_1line_then_clm_queries_256,
	orbgrand_1line_then_clm_queries_1k,
#endif
	parity_window,
	guarded_parity_window
};

const char *mode_name(Mode mode)
{
	switch (mode) {
	case Mode::baseline: return "baseline";
#ifdef OSD_NOVELTY_COMPARISON
	case Mode::exact_stop_only: return "exact_stop_only";
	case Mode::soft_weight_exact: return "soft_weight_exact";
	case Mode::guarded_soft_weight_exact:
		return "guarded_soft_weight_exact";
	case Mode::guarded_projected_astar_exact:
		return "guarded_projected_astar_exact";
	case Mode::guarded_adaptive_projected_astar_exact:
		return "guarded_adaptive_projected_astar_exact";
	case Mode::guarded_projected_screen_exact:
		return "guarded_projected_screen_exact";
	case Mode::guarded_hierarchical_projected_screen_exact:
		return "guarded_hierarchical_projected_screen_exact";
	case Mode::guarded_portfolio_projected_screen_exact:
		return "guarded_portfolio_projected_screen_exact";
	case Mode::guarded_syndrome_sketch_screen_exact:
		return "guarded_syndrome_sketch_screen_exact";
	case Mode::guarded_discover_certify_exact:
		return "guarded_discover_certify_exact";
	case Mode::guarded_packed_parity_certify_exact:
		return "guarded_packed_parity_certify_exact";
	case Mode::adaptive_packed_dispatch_exact:
		return "adaptive_packed_dispatch_exact";
	case Mode::guarded_additive_pdb_exact:
		return "guarded_additive_pdb_exact";
	case Mode::guarded_cost_partition_pdb_exact:
		return "guarded_cost_partition_pdb_exact";
	case Mode::guarded_fingerprint_pdb_exact:
		return "guarded_fingerprint_pdb_exact";
	case Mode::guarded_quotient_pdb_exact:
		return "guarded_quotient_pdb_exact";
	case Mode::guarded_query_local_pdb_exact:
		return "guarded_query_local_pdb_exact";
	case Mode::guarded_packed_state_pdb_exact:
		return "guarded_packed_state_pdb_exact";
	case Mode::guarded_witness_packed_state_pdb_exact:
		return "guarded_witness_packed_state_pdb_exact";
	case Mode::reconstructed_normal_cap_pdb_exact:
		return "reconstructed_normal_cap_pdb_exact";
	case Mode::reconstructed_compact_two_block_pdb_exact:
		return "reconstructed_compact_two_block_pdb_exact";
	case Mode::reconstructed_dual_two_block_pdb_exact:
		return "reconstructed_dual_two_block_pdb_exact";
	case Mode::reconstructed_compiled_dual_bank_pdb_exact:
		return "reconstructed_compiled_dual_bank_pdb_exact";
	case Mode::oracle_selective_coupling_pdb_exact:
		return "oracle_selective_coupling_pdb_exact";
	case Mode::oracle_perfect_subtree_pdb_exact:
		return "oracle_perfect_subtree_pdb_exact";
	case Mode::oracle_perfect_seed_subtree_pdb_exact:
		return "oracle_perfect_seed_subtree_pdb_exact";
	case Mode::guarded_orb_seeded_packed_pdb_exact:
		return "guarded_orb_seeded_packed_pdb_exact";
	case Mode::bitplane_frontier_exact:
		return "bitplane_frontier_exact";
	case Mode::transposed_frontier_exact:
		return "transposed_frontier_exact";
	case Mode::adaptive_packed_state_pdb_exact:
		return "adaptive_packed_state_pdb_exact";
	case Mode::adaptive_additive_pdb_exact:
		return "adaptive_additive_pdb_exact";
	case Mode::adaptive_cost_partition_pdb_exact:
		return "adaptive_cost_partition_pdb_exact";
	case Mode::published_trivial: return "published_trivial";
	case Mode::published_dai: return "published_dai";
	case Mode::published_extra_parity_delta4:
		return "published_extra_parity_delta4";
	case Mode::published_joint_dai_delta4:
		return "published_joint_dai_delta4";
	case Mode::orbgrand_queries_1k: return "orbgrand_queries_1k";
	case Mode::orbgrand_queries_10k: return "orbgrand_queries_10k";
	case Mode::orbgrand_queries_100k: return "orbgrand_queries_100k";
	case Mode::orbgrand_queries_1m: return "orbgrand_queries_1m";
	case Mode::orbgrand_1line_auto_queries_1m:
		return "orbgrand_1line_auto_queries_1m";
	case Mode::orbgrand_1line_then_clm_queries_16:
		return "orbgrand_1line_then_clm_queries_16";
	case Mode::orbgrand_1line_then_clm_queries_64:
		return "orbgrand_1line_then_clm_queries_64";
	case Mode::orbgrand_1line_then_clm_queries_256:
		return "orbgrand_1line_then_clm_queries_256";
	case Mode::orbgrand_1line_then_clm_queries_1k:
		return "orbgrand_1line_then_clm_queries_1k";
#endif
	case Mode::parity_window: return "parity_window";
	case Mode::guarded_parity_window: return "guarded_parity_window";
	}
	assert(false);
	return "invalid";
}

Mode active_mode = Mode::baseline;
double active_llr_scale = 1.0;
int active_selective_oracle_slot = -1;
bool preparing_selective_oracle = false;
int active_perfect_oracle_slot = -1;
bool preparing_perfect_oracle = false;

#ifdef OSD_CHANNEL_VALIDATION
enum class ValidationChannel {
	awgn_matched,
	rayleigh_matched,
	markov_burst_mismatched,
	rayleigh_csi_mismatched
};

constexpr std::array<ValidationChannel, 4> VALIDATION_CHANNELS{
	ValidationChannel::awgn_matched,
	ValidationChannel::rayleigh_matched,
	ValidationChannel::markov_burst_mismatched,
	ValidationChannel::rayleigh_csi_mismatched
};

const char *validation_channel_name(ValidationChannel channel)
{
	switch (channel) {
	case ValidationChannel::awgn_matched:
		return "awgn_matched";
	case ValidationChannel::rayleigh_matched:
		return "rayleigh_matched";
	case ValidationChannel::markov_burst_mismatched:
		return "markov_burst_mismatched";
	case ValidationChannel::rayleigh_csi_mismatched:
		return "rayleigh_csi_mismatched";
	}
	assert(false);
	return "unknown";
}

struct ValidationChannelState {
	bool burst_bad = false;
};

double validation_soft_observation(
	ValidationChannel channel,
	double symbol,
	double sigma,
	std::mt19937_64 &random,
	ValidationChannelState &state
)
{
	std::normal_distribution<double> standard_normal(0.0, 1.0);
	std::uniform_real_distribution<double> uniform(0.0, 1.0);
	if (channel == ValidationChannel::awgn_matched)
		return symbol + sigma * standard_normal(random);
	if (channel == ValidationChannel::rayleigh_matched ||
		channel == ValidationChannel::rayleigh_csi_mismatched) {
		// Rayleigh amplitude normalized so E[h^2] = 1.  The coherent
		// matched statistic is h*y; the mismatched decoder ignores h.
		const double sample = std::max(
			uniform(random), std::numeric_limits<double>::min()
		);
		const double fading = std::sqrt(-std::log(sample));
		const double received = fading * symbol +
			sigma * standard_normal(random);
		return channel == ValidationChannel::rayleigh_matched
			? fading * received
			: received;
	}

	// A two-state Gaussian channel creates correlated high-variance noise
	// bursts.  The decoder deliberately uses the ordinary AWGN statistic,
	// so it does not know which samples came from the bad state.
	const double transition = uniform(random);
	if (state.burst_bad)
		state.burst_bad = transition >= 0.15;
	else
		state.burst_bad = transition < 0.02;
	const double local_sigma = state.burst_bad ? 4.0 * sigma : sigma;
	return symbol + local_sigma * standard_normal(random);
}
#endif
std::array<Clock::time_point, STAGE_COUNT> stage_started{};
std::array<uint64_t, STAGE_COUNT> stage_elapsed{};
int completed_best = 0;
int completed_next = -1;

struct OverrideStats {
	uint64_t candidates = 0;
	uint64_t materializations = 0;
	uint64_t patterns_considered = 0;
	uint64_t pruned_candidates = 0;
	uint64_t pruned_subtrees = 0;
	uint64_t bound_checks = 0;
	uint64_t exact_check_ns = 0;
	uint64_t dp_build_ns = 0;
	uint64_t bounded_search_ns = 0;
	std::size_t dp_bytes = 0;
	uint64_t learned_conflicts = 0;
	uint64_t learned_conflict_hits = 0;
	uint64_t learned_conflict_derivations = 0;
	uint64_t oracle_coupled_prunes = 0;
	uint64_t oracle_coupled_candidates = 0;
	uint64_t unique_teps_evaluated = 0;
	uint64_t scoring_calls = 0;
	uint64_t pdb_table_lookups = 0;
	uint64_t compact_table_lookups = 0;
	// Cycle counts for the state-wide compact FPGA query engine with
	// 1, 2, 4, 8, and 16 effect lanes.  These are accumulated from the
	// exact reachable masks encountered by the decoder, not inferred from
	// aggregate table-read counts.
	std::array<uint64_t, 5> compact_parallel_cycles{};
	uint64_t compact_parallel_allocations = 0;
	uint64_t dual_queries = 0;
	uint64_t dual_dp_transitions = 0;
	uint64_t compiled_dual_lookups = 0;
	uint64_t compiled_dual_build_transitions = 0;
	uint64_t oracle_schedule_lookups = 0;
	uint64_t perfect_subtree_prunes = 0;
	uint64_t perfect_subtree_candidates = 0;
	uint64_t perfect_subtree_lookups = 0;
	bool exact_stop = false;
	bool fallback = false;
	bool dispatcher_baseline = false;
};

OverrideStats override_stats;

void profile_begin(int stage)
{
	assert(stage >= 0 && stage < STAGE_COUNT);
	stage_started[static_cast<std::size_t>(stage)] = Clock::now();
}

void profile_end(int stage)
{
	assert(stage >= 0 && stage < STAGE_COUNT);
	const auto end = Clock::now();
	stage_elapsed[static_cast<std::size_t>(stage)] +=
		static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				end - stage_started[static_cast<std::size_t>(stage)]
			).count()
		);
}

void capture_complete(int best, int next)
{
	completed_best = best;
	completed_next = next;
}

uint64_t combinations(int count, int weight)
{
	if (weight < 0 || weight > count)
		return 0;
	if (!weight || weight == count)
		return 1;
	if (weight > count - weight)
		weight = count - weight;
	uint64_t value = 1;
	for (int index = 1; index <= weight; ++index)
		value = value * static_cast<uint64_t>(count - weight + index) /
			static_cast<uint64_t>(index);
	return value;
}

uint64_t candidate_count(int dimension, int order)
{
	uint64_t value = 0;
	for (int weight = 0; weight <= order; ++weight)
		value += combinations(dimension, weight);
	return value;
}

uint64_t descendant_count(int dimension, int start, int budget)
{
	const int available = dimension - start;
	uint64_t value = 0;
	for (int weight = 1; weight <= std::min(available, budget); ++weight)
		value += combinations(available, weight);
	return value;
}

template <int N, int K, int O>
class ParityWindowDp {
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_WIDTH = W - K;
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS =
		PARITY_LENGTH < WINDOW_BITS ? PARITY_LENGTH : WINDOW_BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	std::vector<int> at_most_;
	std::vector<int> nonempty_;
	std::array<State, K> row_masks_{};
	std::array<int, K> systematic_delta_{};
	std::array<int, BITS> offsets_{};
	int outside_absolute_ = 0;

	static std::size_t index(int suffix, int budget, std::size_t state)
	{
		return (
			static_cast<std::size_t>(suffix * (O + 1) + budget) * STATES
		) + state;
	}

public:
	ParityWindowDp(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		at_most_(
			static_cast<std::size_t>((K + 1) * (O + 1)) * STATES
		),
		nonempty_(
			static_cast<std::size_t>((K + 1) * (O + 1)) * STATES,
			NEGATIVE_INFINITY
		)
	{
		static_assert(N > K);
		static_assert(O > 0 && O <= K);
		static_assert(BITS > 0 && BITS <= WINDOW_BITS);

		std::array<int, PARITY_LENGTH> ordered_offsets{};
		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[soft](int left, int right) {
				return std::abs(static_cast<int>(soft[K + left])) >
					std::abs(static_cast<int>(soft[K + right]));
			}
		);
		std::copy_n(ordered_offsets.begin(), BITS, offsets_.begin());

		for (int offset = 0; offset < PARITY_WIDTH; ++offset)
			outside_absolute_ += std::abs(static_cast<int>(soft[K + offset]));
		for (const int offset: offsets_)
			outside_absolute_ -= std::abs(static_cast<int>(soft[K + offset]));

		for (int row = 0; row < K; ++row) {
			const int term = (1 - 2 * base[row]) * soft[row];
			systematic_delta_[static_cast<std::size_t>(row)] = -2 * term;
			State mask = 0;
			for (int bit = 0; bit < BITS; ++bit)
				if (matrix[
					row * W + K + offsets_[static_cast<std::size_t>(bit)]
				])
					mask |= static_cast<State>(State{1} << bit);
			row_masks_[static_cast<std::size_t>(row)] = mask;
		}

		std::array<int, STATES> terminal{};
		int all_zero_metric = 0;
		for (const int offset: offsets_)
			all_zero_metric += soft[K + offset];
		terminal[0] = all_zero_metric;
		for (std::size_t state = 1; state < STATES; ++state) {
			const int bit = __builtin_ctzll(
				static_cast<unsigned long long>(state)
			);
			terminal[state] = terminal[state & (state - 1)] -
				2 * soft[K + offsets_[static_cast<std::size_t>(bit)]];
		}

		for (int budget = 0; budget <= O; ++budget)
			std::copy(
				terminal.begin(), terminal.end(),
				at_most_.begin() + static_cast<std::ptrdiff_t>(
					index(K, budget, 0)
				)
			);

		for (int suffix = K - 1; suffix >= 0; --suffix) {
			const State row_mask =
				row_masks_[static_cast<std::size_t>(suffix)];
			const int delta =
				systematic_delta_[static_cast<std::size_t>(suffix)];
			for (std::size_t state = 0; state < STATES; ++state) {
				at_most_[index(suffix, 0, state)] =
					at_most_[index(suffix + 1, 0, state)];
				nonempty_[index(suffix, 0, state)] = NEGATIVE_INFINITY;
			}
			for (int budget = 1; budget <= O; ++budget) {
				for (std::size_t state = 0; state < STATES; ++state) {
					const std::size_t toggled = state ^ row_mask;
					const int take = delta + at_most_[
						index(suffix + 1, budget - 1, toggled)
					];
					at_most_[index(suffix, budget, state)] = std::max(
						at_most_[index(suffix + 1, budget, state)], take
					);
					nonempty_[index(suffix, budget, state)] = std::max(
						nonempty_[index(suffix + 1, budget, state)], take
					);
				}
			}
		}
	}

	int outside_absolute() const { return outside_absolute_; }
	State row_mask(int row) const
	{
		return row_masks_[static_cast<std::size_t>(row)];
	}
	int nonempty(int suffix, int budget, State state) const
	{
		return nonempty_[index(suffix, budget, state)];
	}
	State initial_state(const int8_t *base) const
	{
		State state = 0;
		for (int bit = 0; bit < BITS; ++bit)
			if (base[K + offsets_[static_cast<std::size_t>(bit)]])
				state |= static_cast<State>(State{1} << bit);
		return state;
	}
	std::size_t bytes() const
	{
		return
			(at_most_.size() + nonempty_.size()) * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			systematic_delta_.size() * sizeof(int) +
			offsets_.size() * sizeof(int);
	}
};

template <int N, int K, int O>
class BoundedSearch {
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_WIDTH = W - K;
	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const ParityWindowDp<N, K, O> &dp;
	const uint64_t candidate_budget;
	std::array<int8_t, PARITY_WIDTH> parity{};
	std::array<uint8_t, K> active{};
	State window_state = 0;
	int systematic_metric = 0;
	int initial_systematic_metric = 0;
	int best = std::numeric_limits<int>::min();
	int next = std::numeric_limits<int>::min();
	uint64_t candidates = 0;
	uint64_t pruned_candidates = 0;
	uint64_t pruned_subtrees = 0;
	uint64_t bound_checks = 0;
	bool aborted = false;

	void copy_candidate()
	{
		for (int row = 0; row < K; ++row)
			output[row] = base[row] ^
				static_cast<int8_t>(active[static_cast<std::size_t>(row)]);
		std::copy(parity.begin(), parity.end(), output + K);
	}

	inline void flip(int row)
	{
		const int old_bit = base[row] ^
			static_cast<int>(active[static_cast<std::size_t>(row)]);
		systematic_metric -= 2 * (1 - 2 * old_bit) * soft[row];
		active[static_cast<std::size_t>(row)] ^= uint8_t{1};
		const int8_t *matrix_row = matrix + row * W + K;
		for (int offset = 0; offset < PARITY_WIDTH; ++offset)
			parity[static_cast<std::size_t>(offset)] ^= matrix_row[offset];
		window_state ^= dp.row_mask(row);
	}

	inline void evaluate()
	{
		if (candidates >= candidate_budget) {
			aborted = true;
			return;
		}
		int metric = systematic_metric;
		for (int offset = 0; offset < PARITY_WIDTH; ++offset)
			metric +=
				(1 - 2 * parity[static_cast<std::size_t>(offset)]) *
				soft[K + offset];
		if (!candidates || metric > best) {
			if (candidates)
				next = best;
			best = metric;
			copy_candidate();
		} else if (metric > next) {
			next = metric;
		}
		++candidates;
	}

	bool prune(int suffix, int budget)
	{
		++bound_checks;
		const int future = dp.nonempty(suffix, budget, window_state);
		if (future == NEGATIVE_INFINITY)
			return false;
		const int upper_bound =
			systematic_metric + future + dp.outside_absolute();
		return upper_bound < best;
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		for (int row = start; row < K; ++row) {
			flip(row);
			evaluate();
			if (!aborted) {
				if constexpr (DEPTH + 1 < O) {
					const int suffix = row + 1;
					constexpr int budget = O - (DEPTH + 1);
					if (suffix < K && prune(suffix, budget)) {
						const uint64_t skipped = descendant_count(
							K, suffix, budget
						);
						pruned_candidates += skipped;
						pruned_subtrees += skipped != 0;
					} else {
						traverse<DEPTH + 1>(suffix);
					}
				}
			}
			flip(row);
			if (aborted)
				return;
		}
	}

public:
	BoundedSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		const ParityWindowDp<N, K, O> &input_dp,
		uint64_t input_candidate_budget
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft),
		dp(input_dp),
		candidate_budget(input_candidate_budget),
		window_state(input_dp.initial_state(input_base))
	{
		assert(candidate_budget > 0);
		std::copy(base + K, base + W, parity.begin());
		for (int row = 0; row < K; ++row)
			initial_systematic_metric +=
				(1 - 2 * base[row]) * soft[row];
		systematic_metric = initial_systematic_metric;
	}

	bool run()
	{
		evaluate();
		traverse();
		assert(systematic_metric == initial_systematic_metric);
		assert(std::all_of(
			active.begin(), active.end(), [](uint8_t value) { return value == 0; }
		));
		assert(std::equal(parity.begin(), parity.end(), base + K));
		if (!aborted) {
			assert(candidates + pruned_candidates == candidate_count(K, O));
			for (int index = N; index < W; ++index)
				assert(output[index] == 0);
		}
		return !aborted;
	}

	int best_metric() const { return best; }
	int next_metric() const { return next; }
	uint64_t evaluated() const { return candidates; }
	uint64_t pruned() const { return pruned_candidates; }
	uint64_t subtrees() const { return pruned_subtrees; }
	uint64_t checks() const { return bound_checks; }
};

#ifdef OSD_NOVELTY_COMPARISON

// Exact Dorsch-style comparator: enumerate the same fixed-order TEP list in
// nondecreasing MRB soft weight.  Since a candidate's full weighted Hamming
// distance is at least its MRB soft weight, the remaining queue can be stopped
// exactly once its minimum key is strictly greater than the incumbent.
// This is an independent soft-weight implementation, not author code.
template <int K, int O>
class SoftWeightPatternQueue {
public:
	struct Node {
		int cost = 0;
		uint16_t weight = 0;
		std::array<uint16_t, O> ranks{};
		uint64_t serial = 0;
	};

private:
	struct Greater {
		bool operator()(const Node &left, const Node &right) const
		{
			if (left.cost != right.cost)
				return left.cost > right.cost;
			return left.serial > right.serial;
		}
	};

	std::array<int, K> coordinates_{};
	std::array<int, K> costs_{};
	std::priority_queue<Node, std::vector<Node>, Greater> queue_;
	uint64_t next_serial_ = 0;
	std::size_t peak_nodes_ = 0;

	void push(Node node)
	{
		node.serial = next_serial_++;
		queue_.push(node);
		peak_nodes_ = std::max(peak_nodes_, queue_.size());
	}

	void expand(const Node &node)
	{
		assert(node.weight > 0 && node.weight <= O);
		const std::size_t final = static_cast<std::size_t>(node.weight - 1);
		const int last = node.ranks[final];
		const int next = last + 1;
		if (next >= K)
			return;

		if (node.weight < O) {
			Node added = node;
			added.ranks[static_cast<std::size_t>(added.weight)] =
				static_cast<uint16_t>(next);
			++added.weight;
			added.cost += costs_[static_cast<std::size_t>(next)];
			push(added);
		}

		Node replaced = node;
		replaced.ranks[final] = static_cast<uint16_t>(next);
		replaced.cost += costs_[static_cast<std::size_t>(next)] -
			costs_[static_cast<std::size_t>(last)];
		push(replaced);
	}

public:
	explicit SoftWeightPatternQueue(const int8_t *soft)
	{
		static_assert(K > 0 && O > 0 && O <= K);
		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(soft[left])
				);
				const int right_cost = std::abs(
					static_cast<int>(soft[right])
				);
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank)
			costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[coordinates_[static_cast<std::size_t>(rank)]])
			);

		Node root;
		root.weight = 1;
		root.ranks[0] = 0;
		root.cost = costs_[0];
		push(root);
	}

	bool empty() const { return queue_.empty(); }
	int minimum_cost() const
	{
		assert(!queue_.empty());
		return queue_.top().cost;
	}
	Node pop()
	{
		assert(!queue_.empty());
		const Node node = queue_.top();
		queue_.pop();
		expand(node);
		return node;
	}
	int coordinate(uint16_t rank) const
	{
		assert(rank < K);
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	std::size_t peak_bytes() const
	{
		return peak_nodes_ * sizeof(Node) +
			coordinates_.size() * sizeof(int) + costs_.size() * sizeof(int);
	}
};

void validate_soft_weight_pattern_queue()
{
	constexpr int K = 5;
	constexpr int O = 3;
	const std::array<int8_t, K> soft{5, 1, 4, 1, 3};
	SoftWeightPatternQueue<K, O> patterns(soft.data());
	std::array<uint8_t, std::size_t{1} << K> seen{};
	int previous_cost = -1;
	uint64_t count = 1;
	seen[0] = 1;
	while (!patterns.empty()) {
		const auto pattern = patterns.pop();
		assert(pattern.cost >= previous_cost);
		previous_cost = pattern.cost;
		uint32_t mask = 0;
		for (uint16_t index = 0; index < pattern.weight; ++index)
			mask |= uint32_t{1} << patterns.coordinate(
				pattern.ranks[static_cast<std::size_t>(index)]
			);
		assert(!seen[mask]);
		seen[mask] = 1;
		++count;
	}
	assert(count == candidate_count(K, O));
	for (uint32_t mask = 0; mask < seen.size(); ++mask)
		assert(seen[mask] == (__builtin_popcount(mask) <= O));
}

template <int N, int K, int O>
class SoftWeightExactSearch {
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<int8_t, W> current{};
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t bound_checks = 0;
	std::size_t peak_bytes = 0;
	const uint64_t candidate_budget;
	bool aborted = false;

	int weighted_distance() const
	{
		int metric = 0;
		for (int index = 0; index < W; ++index)
			metric +=
				(1 - 2 * current[static_cast<std::size_t>(index)]) *
				soft[index];
		return (absolute_metric - metric) / 2;
	}

	void evaluate_current()
	{
		const int distance = weighted_distance();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			std::copy(current.begin(), current.end(), output);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

public:
	SoftWeightExactSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		uint64_t input_candidate_budget
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft),
		candidate_budget(input_candidate_budget)
	{
		assert(candidate_budget > 0);
		std::copy(base, base + W, current.begin());
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
	}

	bool run()
	{
		evaluate_current();
		SoftWeightPatternQueue<K, O> patterns(soft);
		while (!patterns.empty()) {
			++bound_checks;
			if (patterns.minimum_cost() > best_distance)
				break;
			if (evaluated_candidates >= candidate_budget) {
				aborted = true;
				break;
			}
			const auto pattern = patterns.pop();
			std::copy(base, base + W, current.begin());
			for (uint16_t index = 0; index < pattern.weight; ++index) {
				const int row = patterns.coordinate(
					pattern.ranks[static_cast<std::size_t>(index)]
				);
				const int8_t *matrix_row = matrix + row * W;
				for (int column = 0; column < W; ++column)
					current[static_cast<std::size_t>(column)] ^=
						matrix_row[column];
			}
			evaluate_current();
		}
		peak_bytes = patterns.peak_bytes();
		assert(evaluated_candidates <= candidate_count(K, O));
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
		return !aborted;
	}

	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t checks() const { return bound_checks; }
	std::size_t bytes() const { return peak_bytes; }
};

template <int N, int K, int O>
bool execute_soft_weight_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto search_start = Clock::now();
	SoftWeightExactSearch<N, K, O> search(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool completed = search.run();
	const auto search_end = Clock::now();
	const uint64_t total = candidate_count(K, O);
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - search_start
		).count()
	);
	override_stats.candidates = search.candidates();
	override_stats.patterns_considered = search.candidates();
	override_stats.pruned_candidates = total - search.candidates();
	override_stats.bound_checks = search.checks();
	override_stats.dp_bytes = search.bytes();
	best = search.best_metric();
	next = search.next_metric();
	return completed;
}

// Cheaper exact parity pivot: retain Dorsch-style information-cost ordering,
// but reject a popped TEP before full re-encoding when its information cost
// plus mismatches on selected parity coordinates already exceeds the
// incumbent.  A separate popped-pattern budget bounds queue growth.
template <
	int N, int K, int O,
	bool HIERARCHICAL = false,
	bool SYNDROME_SKETCH = false,
	bool PACKED_METRIC = false
>
class ProjectedSoftWeightScreenSearch {
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS =
		PARITY_LENGTH < PROJECTED_SCREEN_BITS
			? PARITY_LENGTH
				: PROJECTED_SCREEN_BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	static constexpr int PARITY_WORDS = (PARITY_LENGTH + 63) / 64;
	static constexpr int PARITY_BYTES = (PARITY_LENGTH + 7) / 8;
	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const uint64_t candidate_budget;
	const uint64_t pattern_budget;
	std::array<int8_t, W> current{};
	std::array<State, K> row_masks{};
	std::array<int, BITS> projected_costs{};
	std::array<int, STATES> state_lower{};
	std::array<int, PARITY_LENGTH> ordered_offsets{};
	std::array<uint64_t, K * PARITY_WORDS> parity_row_masks{};
	std::array<uint64_t, PARITY_WORDS> initial_parity_state{};
	std::array<int, PARITY_BYTES * 256> parity_byte_costs{};
	int active_bits = 0;
	State initial_state = 0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t materialized_candidates = 0;
	uint64_t considered_patterns = 0;
	uint64_t filtered_patterns = 0;
	uint64_t bound_checks = 0;
	std::size_t peak_bytes = 0;
	bool aborted = false;
	std::vector<uint64_t> evaluated_masks_;

	template <class Pattern>
	uint64_t pattern_mask(const Pattern &pattern) const
	{
		static_assert(K <= 64);
		uint64_t mask = 0;
		for (uint16_t index = 0; index < pattern.weight; ++index)
			mask |= uint64_t{1} << pattern.ranks[static_cast<std::size_t>(index)];
		return mask;
	}

	int weighted_distance() const
	{
		int metric = 0;
		for (int index = 0; index < W; ++index)
			metric +=
				(1 - 2 * current[static_cast<std::size_t>(index)]) *
				soft[index];
		return (absolute_metric - metric) / 2;
	}

	void evaluate_current()
	{
		const int distance = weighted_distance();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			std::copy(current.begin(), current.end(), output);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
		if (evaluated_candidates == 1)
			evaluated_masks_.push_back(0);
	}

	void build_packed_metric()
	{
		for (int row = 0; row < K; ++row)
			for (int offset = 0; offset < PARITY_LENGTH; ++offset)
				if (matrix[row * W + K + offset])
					parity_row_masks[
						static_cast<std::size_t>(row) * PARITY_WORDS +
						static_cast<std::size_t>(offset / 64)
					] |= uint64_t{1} << (offset % 64);
		for (int offset = 0; offset < PARITY_LENGTH; ++offset)
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_parity_state[static_cast<std::size_t>(offset / 64)] |=
					uint64_t{1} << (offset % 64);
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			for (int value = 1; value < 256; ++value) {
				const int bit = __builtin_ctz(
					static_cast<unsigned int>(value)
				);
				const int offset = byte * 8 + bit;
				parity_byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value)
				] = parity_byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value & (value - 1))
				] + (offset < PARITY_LENGTH
					? std::abs(static_cast<int>(soft[K + offset]))
					: 0);
			}
		}
	}

	int packed_parity_cost(
		const std::array<uint64_t, PARITY_WORDS> &state
	) const
	{
		int cost = 0;
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const auto value = static_cast<uint8_t>(
				state[static_cast<std::size_t>(byte / 8)] >>
				(8 * (byte % 8))
			);
			cost += parity_byte_costs[
				static_cast<std::size_t>(byte) * 256 + value
			];
		}
		return cost;
	}

	template <class Pattern, class Queue>
	void materialize_pattern(const Pattern &pattern, const Queue &patterns)
	{
		std::copy(base, base + W, current.begin());
		for (uint16_t index = 0; index < pattern.weight; ++index) {
			const int row = patterns.coordinate(
				pattern.ranks[static_cast<std::size_t>(index)]
			);
			const int8_t *matrix_row = matrix + row * W;
			for (int column = 0; column < W; ++column)
				current[static_cast<std::size_t>(column)] ^=
					matrix_row[column];
		}
		std::copy(current.begin(), current.end(), output);
		++materialized_candidates;
	}

	void evaluate_packed_order_zero()
	{
		const int distance = packed_parity_cost(initial_parity_state);
		best_distance = distance;
		std::copy(base, base + W, output);
		++evaluated_candidates;
		++materialized_candidates;
		evaluated_masks_.push_back(0);
	}

	template <class Pattern, class Queue>
	void evaluate_packed(const Pattern &pattern, const Queue &patterns)
	{
		auto state = initial_parity_state;
		for (uint16_t index = 0; index < pattern.weight; ++index) {
			const int row = patterns.coordinate(
				pattern.ranks[static_cast<std::size_t>(index)]
			);
			for (int word = 0; word < PARITY_WORDS; ++word)
				state[static_cast<std::size_t>(word)] ^=
					parity_row_masks[
						static_cast<std::size_t>(row) * PARITY_WORDS +
						static_cast<std::size_t>(word)
					];
		}
		const int distance = pattern.cost + packed_parity_cost(state);
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			materialize_pattern(pattern, patterns);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
		evaluated_masks_.push_back(pattern_mask(pattern));
	}

	int projected_cost(State state) const
	{
		return state_lower[static_cast<std::size_t>(state)];
	}

	void rebuild_coordinate_table()
	{
		const std::size_t active_states = std::size_t{1} << active_bits;
		state_lower[0] = 0;
		for (std::size_t state = 1; state < active_states; ++state) {
			const int bit = __builtin_ctzll(
				static_cast<unsigned long long>(state)
			);
			state_lower[state] = state_lower[state & (state - 1)] +
				projected_costs[static_cast<std::size_t>(bit)];
		}
	}

	void build_syndrome_sketch()
	{
		static_assert(!HIERARCHICAL);
		const int positions = std::min({
			PARITY_LENGTH,
			SYNDROME_SKETCH_POSITIONS,
			static_cast<int>(STATES - 1)
		});
		constexpr int LARGE_COST = std::numeric_limits<int>::max() / 4;
		state_lower.fill(LARGE_COST);
		state_lower[0] = 0;
		for (int position = 0; position < positions; ++position) {
			const int offset =
				ordered_offsets[static_cast<std::size_t>(position)];
			const State signature = static_cast<State>(position + 1);
			const int cost = std::abs(static_cast<int>(soft[K + offset]));
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_state = static_cast<State>(
					initial_state ^ signature
				);
			for (int row = 0; row < K; ++row)
				if (matrix[row * W + K + offset])
					row_masks[static_cast<std::size_t>(row)] =
						static_cast<State>(
							row_masks[static_cast<std::size_t>(row)] ^
							signature
						);
			const auto previous = state_lower;
			for (std::size_t state = 0; state < STATES; ++state) {
				if (previous[state] == LARGE_COST)
					continue;
				const std::size_t toggled = state ^ signature;
				state_lower[toggled] = std::min(
					state_lower[toggled], previous[state] + cost
				);
			}
		}
	}

	void activate_projection(int requested_bits)
	{
		const int next_bits = std::min(BITS, requested_bits);
		for (int bit = active_bits; bit < next_bits; ++bit) {
			const int offset = ordered_offsets[static_cast<std::size_t>(bit)];
			projected_costs[static_cast<std::size_t>(bit)] = std::abs(
				static_cast<int>(soft[K + offset])
			);
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_state |= static_cast<State>(State{1} << bit);
			for (int row = 0; row < K; ++row)
				if (matrix[row * W + K + offset])
					row_masks[static_cast<std::size_t>(row)] |=
						static_cast<State>(State{1} << bit);
		}
		active_bits = next_bits;
		rebuild_coordinate_table();
	}

	void maybe_refine_projection()
	{
		if constexpr (HIERARCHICAL) {
			if (evaluated_candidates >= PROJECTED_LEVEL2_CANDIDATES)
				activate_projection(16);
			else if (evaluated_candidates >= PROJECTED_LEVEL1_CANDIDATES)
				activate_projection(8);
		}
	}

public:
	ProjectedSoftWeightScreenSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		uint64_t input_candidate_budget
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft),
		candidate_budget(input_candidate_budget),
		pattern_budget(input_candidate_budget * PROJECTED_PATTERN_MULTIPLIER)
	{
		static_assert(N > K);
		static_assert(BITS > 0 && BITS <= 16);
		assert(candidate_budget > 0);
		std::copy(base, base + W, current.begin());
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));

		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		if constexpr (SYNDROME_SKETCH)
			build_syndrome_sketch();
		else
			activate_projection(HIERARCHICAL ? 4 : BITS);
		if constexpr (PACKED_METRIC)
			build_packed_metric();
	}

	bool run()
	{
		if constexpr (PACKED_METRIC)
			evaluate_packed_order_zero();
		else
			evaluate_current();
		++considered_patterns;
		SoftWeightPatternQueue<K, O> patterns(soft);
		while (!patterns.empty()) {
			++bound_checks;
			if (patterns.minimum_cost() > best_distance)
				break;
			if (considered_patterns >= pattern_budget) {
				aborted = true;
				break;
			}
			const auto pattern = patterns.pop();
			++considered_patterns;
			maybe_refine_projection();
			State state = initial_state;
			for (uint16_t index = 0; index < pattern.weight; ++index) {
				const int row = patterns.coordinate(
					pattern.ranks[static_cast<std::size_t>(index)]
				);
				state = static_cast<State>(
					state ^ row_masks[static_cast<std::size_t>(row)]
				);
			}
			++bound_checks;
			if (pattern.cost + projected_cost(state) > best_distance) {
				++filtered_patterns;
				continue;
			}
			if (evaluated_candidates >= candidate_budget) {
				aborted = true;
				break;
			}
			if constexpr (PACKED_METRIC) {
				evaluate_packed(pattern, patterns);
			} else {
				std::copy(base, base + W, current.begin());
				for (uint16_t index = 0; index < pattern.weight; ++index) {
					const int row = patterns.coordinate(
						pattern.ranks[static_cast<std::size_t>(index)]
					);
					const int8_t *matrix_row = matrix + row * W;
					for (int column = 0; column < W; ++column)
						current[static_cast<std::size_t>(column)] ^=
							matrix_row[column];
				}
				evaluate_current();
				evaluated_masks_.push_back(pattern_mask(pattern));
			}
		}
		peak_bytes = patterns.peak_bytes() + row_masks.size() * sizeof(State) +
			projected_costs.size() * sizeof(int) +
			state_lower.size() * sizeof(int);
		if constexpr (PACKED_METRIC)
			peak_bytes += parity_row_masks.size() * sizeof(uint64_t) +
				initial_parity_state.size() * sizeof(uint64_t) +
				parity_byte_costs.size() * sizeof(int);
		assert(evaluated_candidates <= candidate_count(K, O));
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
		return !aborted;
	}

	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t materializations() const
	{
		return PACKED_METRIC ? materialized_candidates : evaluated_candidates;
	}
	uint64_t patterns() const { return considered_patterns; }
	uint64_t filtered() const { return filtered_patterns; }
	uint64_t checks() const { return bound_checks; }
	std::size_t bytes() const { return peak_bytes; }
	const std::vector<uint64_t> &evaluated_masks() const
	{
		return evaluated_masks_;
	}
};

template <
	int N, int K, int O,
	bool HIERARCHICAL = false,
	bool SYNDROME_SKETCH = false
>
bool execute_projected_screen_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto search_start = Clock::now();
	ProjectedSoftWeightScreenSearch<
		N, K, O, HIERARCHICAL, SYNDROME_SKETCH
	> search(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool completed = search.run();
	const auto search_end = Clock::now();
	const uint64_t total = candidate_count(K, O);
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - search_start
		).count()
	);
	override_stats.candidates = search.candidates();
	override_stats.patterns_considered = search.patterns();
	override_stats.pruned_candidates = total - search.candidates();
	override_stats.bound_checks = search.checks();
	override_stats.dp_bytes = search.bytes();
	best = search.best_metric();
	next = search.next_metric();
	return completed;
}

// Queue-free exact certification phase.  A bounded best-first screen supplies
// a strong incumbent.  If that screen reaches its work cap, this depth-first
// pass visits the fixed-order TEP tree with only information cost and a small
// projected-parity state.  Candidates whose admissible bound cannot tie the
// incumbent are never re-encoded.  Unlike the original guarded fallback, the
// discovery work is therefore used to make the certification pass cheaper.
template <int N, int K, int O>
class ProjectedCertificationSearch {
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS =
		PARITY_LENGTH < PROJECTED_SCREEN_BITS
			? PARITY_LENGTH
			: PROJECTED_SCREEN_BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<int, K> coordinates{};
	std::array<int, K> information_costs{};
	std::array<State, K> row_masks{};
	std::array<int, BITS> projected_costs{};
	std::array<int, STATES> state_costs{};
	std::array<uint16_t, O> selected{};
	std::array<uint16_t, O> seed_selected{};
	std::array<int8_t, W> current{};
	State initial_state = 0;
	uint16_t seed_weight = 0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t considered_patterns = 1;
	uint64_t bound_checks = 0;

	bool is_seed_pattern(int weight) const
	{
		if (weight != seed_weight)
			return false;
		return std::equal(
			selected.begin(), selected.begin() + weight,
			seed_selected.begin()
		);
	}

	int weighted_distance() const
	{
		int metric = 0;
		for (int index = 0; index < W; ++index)
			metric +=
				(1 - 2 * current[static_cast<std::size_t>(index)]) *
				soft[index];
		return (absolute_metric - metric) / 2;
	}

	void evaluate(int weight)
	{
		if (is_seed_pattern(weight))
			return;
		std::copy(base, base + W, current.begin());
		for (int index = 0; index < weight; ++index) {
			const int row = coordinates[
				selected[static_cast<std::size_t>(index)]
			];
			const int8_t *matrix_row = matrix + row * W;
			for (int column = 0; column < W; ++column)
				current[static_cast<std::size_t>(column)] ^=
					matrix_row[column];
		}
		const int distance = weighted_distance();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			std::copy(current.begin(), current.end(), output);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

	template <int DEPTH = 0>
	void traverse(int start, int information_cost, State state)
	{
		for (int rank = start; rank < K; ++rank) {
			const int next_information_cost = information_cost +
				information_costs[static_cast<std::size_t>(rank)];
			if (next_information_cost > best_distance)
				break;
			const State next_state = static_cast<State>(
				state ^ row_masks[static_cast<std::size_t>(rank)]
			);
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			++considered_patterns;
			++bound_checks;
			if (next_information_cost + state_costs[next_state] <=
				best_distance)
				evaluate(DEPTH + 1);
			if constexpr (DEPTH + 1 < O)
				traverse<DEPTH + 1>(
					rank + 1, next_information_cost, next_state
				);
		}
	}

public:
	ProjectedCertificationSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		int seed_best_metric,
		int seed_next_metric
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft)
	{
		static_assert(BITS > 0 && BITS <= 16);
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		best_distance = (absolute_metric - seed_best_metric) / 2;
		next_distance = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric - seed_next_metric) / 2;

		std::iota(coordinates.begin(), coordinates.end(), 0);
		std::stable_sort(
			coordinates.begin(), coordinates.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[right])
				);
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates[static_cast<std::size_t>(rank)];
			information_costs[static_cast<std::size_t>(rank)] =
				std::abs(static_cast<int>(soft[row]));
			if (base[row] != output[row]) {
				assert(seed_weight < O);
				seed_selected[static_cast<std::size_t>(seed_weight++)] =
					static_cast<uint16_t>(rank);
			}
		}

		std::array<int, PARITY_LENGTH> offsets{};
		std::iota(offsets.begin(), offsets.end(), 0);
		std::stable_sort(
			offsets.begin(), offsets.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		for (int bit = 0; bit < BITS; ++bit) {
			const int offset = offsets[static_cast<std::size_t>(bit)];
			projected_costs[static_cast<std::size_t>(bit)] = std::abs(
				static_cast<int>(soft[K + offset])
			);
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_state |= static_cast<State>(State{1} << bit);
			for (int rank = 0; rank < K; ++rank) {
				const int row = coordinates[static_cast<std::size_t>(rank)];
				if (matrix[row * W + K + offset])
					row_masks[static_cast<std::size_t>(rank)] |=
						static_cast<State>(State{1} << bit);
			}
		}
		for (std::size_t state = 1; state < STATES; ++state) {
			const int bit = __builtin_ctzll(
				static_cast<unsigned long long>(state)
			);
			state_costs[state] = state_costs[state & (state - 1)] +
				projected_costs[static_cast<std::size_t>(bit)];
		}
	}

	void run()
	{
		traverse(0, 0, initial_state);
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}
	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t checks() const { return bound_checks; }
	std::size_t bytes() const
	{
		return coordinates.size() * sizeof(int) +
			information_costs.size() * sizeof(int) +
			row_masks.size() * sizeof(State) +
			projected_costs.size() * sizeof(int) +
			state_costs.size() * sizeof(int) +
			selected.size() * sizeof(uint16_t) +
			seed_selected.size() * sizeof(uint16_t);
	}
};

// Exact packed-parity certification.  The cheap projected bound is retained
// for every TEP, but a surviving TEP is scored without constructing a full
// candidate.  Its complete parity mismatch pattern is assembled from packed
// generator-row signatures and converted to an exact reliability cost through
// frame-local byte tables.  A full codeword is materialized only when that
// exact distance improves the incumbent.
template <int N, int K, int O>
class PackedParityCertificationSearch {
	using ProjectedState = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int PROJECTED_BITS =
		PARITY_LENGTH < PROJECTED_SCREEN_BITS
			? PARITY_LENGTH
			: PROJECTED_SCREEN_BITS;
	static constexpr std::size_t PROJECTED_STATES =
		std::size_t{1} << PROJECTED_BITS;
	static constexpr int PARITY_WORDS = (PARITY_LENGTH + 63) / 64;
	static constexpr int PARITY_BYTES = (PARITY_LENGTH + 7) / 8;

	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<int, K> coordinates{};
	std::array<int, K> information_costs{};
	std::array<ProjectedState, K> projected_row_masks{};
	std::array<int, PROJECTED_BITS> projected_costs{};
	std::array<int, PROJECTED_STATES> projected_state_costs{};
	std::array<uint64_t, K * PARITY_WORDS> parity_row_masks{};
	std::array<uint64_t, PARITY_WORDS> initial_parity_state{};
	std::array<int, PARITY_BYTES * 256> byte_costs{};
	std::array<uint16_t, O> selected{};
	std::array<uint16_t, O> seed_selected{};
	std::array<int8_t, W> materialized{};
	ProjectedState initial_projected_state = 0;
	uint16_t seed_weight = 0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t materialized_candidates = 0;
	uint64_t considered_patterns = 1;
	uint64_t bound_checks = 0;

	bool is_seed_pattern(int weight) const
	{
		if (weight != seed_weight)
			return false;
		return std::equal(
			selected.begin(), selected.begin() + weight,
			seed_selected.begin()
		);
	}

	int exact_parity_cost(int weight) const
	{
		std::array<uint64_t, PARITY_WORDS> state = initial_parity_state;
		for (int index = 0; index < weight; ++index) {
			const std::size_t rank = selected[static_cast<std::size_t>(index)];
			for (int word = 0; word < PARITY_WORDS; ++word)
				state[static_cast<std::size_t>(word)] ^=
					parity_row_masks[
						rank * PARITY_WORDS + static_cast<std::size_t>(word)
					];
		}
		int cost = 0;
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const auto value = static_cast<uint8_t>(
				state[static_cast<std::size_t>(byte / 8)] >>
				(8 * (byte % 8))
			);
			cost += byte_costs[
				static_cast<std::size_t>(byte) * 256 + value
			];
		}
		return cost;
	}

	void materialize(int weight)
	{
		std::copy(base, base + W, materialized.begin());
		for (int index = 0; index < weight; ++index) {
			const int row = coordinates[
				selected[static_cast<std::size_t>(index)]
			];
			const int8_t *matrix_row = matrix + row * W;
			for (int column = 0; column < W; ++column)
				materialized[static_cast<std::size_t>(column)] ^=
					matrix_row[column];
		}
		std::copy(materialized.begin(), materialized.end(), output);
		++materialized_candidates;
	}

	void evaluate(int weight, int information_cost)
	{
		if (is_seed_pattern(weight))
			return;
		const int distance = information_cost + exact_parity_cost(weight);
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			materialize(weight);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

	template <int DEPTH = 0>
	void traverse(int start, int information_cost, ProjectedState state)
	{
		for (int rank = start; rank < K; ++rank) {
			const int next_information_cost = information_cost +
				information_costs[static_cast<std::size_t>(rank)];
			if (next_information_cost > best_distance)
				break;
			const ProjectedState next_state = static_cast<ProjectedState>(
				state ^ projected_row_masks[static_cast<std::size_t>(rank)]
			);
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			++considered_patterns;
			++bound_checks;
			if (next_information_cost +
				projected_state_costs[next_state] <= best_distance)
				evaluate(DEPTH + 1, next_information_cost);
			if constexpr (DEPTH + 1 < O)
				traverse<DEPTH + 1>(
					rank + 1, next_information_cost, next_state
				);
		}
	}

public:
	PackedParityCertificationSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		int seed_best_metric,
		int seed_next_metric
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(PROJECTED_BITS > 0 && PROJECTED_BITS <= 16);
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		best_distance = (absolute_metric - seed_best_metric) / 2;
		next_distance = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric - seed_next_metric) / 2;

		std::iota(coordinates.begin(), coordinates.end(), 0);
		std::stable_sort(
			coordinates.begin(), coordinates.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[right])
				);
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates[static_cast<std::size_t>(rank)];
			information_costs[static_cast<std::size_t>(rank)] =
				std::abs(static_cast<int>(soft[row]));
			if (base[row] != output[row]) {
				assert(seed_weight < O);
				seed_selected[static_cast<std::size_t>(seed_weight++)] =
					static_cast<uint16_t>(rank);
			}
			for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
				if (matrix[row * W + K + offset])
					parity_row_masks[
						static_cast<std::size_t>(rank) * PARITY_WORDS +
						static_cast<std::size_t>(offset / 64)
					] |= uint64_t{1} << (offset % 64);
			}
		}

		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_parity_state[
					static_cast<std::size_t>(offset / 64)
				] |= uint64_t{1} << (offset % 64);
		}
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			for (int value = 1; value < 256; ++value) {
				const int bit = __builtin_ctz(
					static_cast<unsigned int>(value)
				);
				const int offset = byte * 8 + bit;
				byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value)
				] = byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value & (value - 1))
				] + (offset < PARITY_LENGTH
					? std::abs(static_cast<int>(soft[K + offset]))
					: 0);
			}
		}

		std::array<int, PARITY_LENGTH> offsets{};
		std::iota(offsets.begin(), offsets.end(), 0);
		std::stable_sort(
			offsets.begin(), offsets.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		for (int bit = 0; bit < PROJECTED_BITS; ++bit) {
			const int offset = offsets[static_cast<std::size_t>(bit)];
			projected_costs[static_cast<std::size_t>(bit)] = std::abs(
				static_cast<int>(soft[K + offset])
			);
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_projected_state |= static_cast<ProjectedState>(
					ProjectedState{1} << bit
				);
			for (int rank = 0; rank < K; ++rank) {
				const int row = coordinates[static_cast<std::size_t>(rank)];
				if (matrix[row * W + K + offset])
					projected_row_masks[static_cast<std::size_t>(rank)] |=
						static_cast<ProjectedState>(ProjectedState{1} << bit);
			}
		}
		for (std::size_t state = 1; state < PROJECTED_STATES; ++state) {
			const int bit = __builtin_ctzll(
				static_cast<unsigned long long>(state)
			);
			projected_state_costs[state] =
				projected_state_costs[state & (state - 1)] +
				projected_costs[static_cast<std::size_t>(bit)];
		}
	}

	void run()
	{
		traverse(0, 0, initial_projected_state);
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}
	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t materializations() const { return materialized_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t checks() const { return bound_checks; }
	std::size_t bytes() const
	{
		return coordinates.size() * sizeof(int) +
			information_costs.size() * sizeof(int) +
			projected_row_masks.size() * sizeof(ProjectedState) +
			projected_costs.size() * sizeof(int) +
			projected_state_costs.size() * sizeof(int) +
			parity_row_masks.size() * sizeof(uint64_t) +
			initial_parity_state.size() * sizeof(uint64_t) +
			byte_costs.size() * sizeof(int) +
			selected.size() * sizeof(uint16_t) +
			seed_selected.size() * sizeof(uint16_t);
	}
};

// Additive parity pattern database.  Every parity coordinate belongs to one
// small abstraction.  Each abstraction solves the exact remaining-flip
// problem for its own coordinates, while allowing the chosen rows to differ
// between abstractions.  Summing those independent minima is therefore a
// relaxation of the real completion problem and remains an admissible lower
// bound.  Unlike the earlier single-window bound, all parity reliability cost
// participates in the certificate without an exponential state over all bits.
template <
	int N, int K, int O,
	bool QUERY_LOCAL = false,
	bool PACKED_SUPPORT = false,
	bool FUSED_SUPPORT = (PDB_GROUPING == 3),
	int FUSED_GROUP_LIMIT = 2
>
class AdditiveParityPatternDatabase {
public:
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS = ADDITIVE_PDB_BITS;
	static constexpr int BASE_GROUPS = (PARITY_LENGTH + BITS - 1) / BITS;
	static constexpr int PARTITIONS = PDB_GROUPING == 2 ? 2 : 1;
	static constexpr int GROUPS = BASE_GROUPS * PARTITIONS;
	static constexpr int FUSED_GROUPS = FUSED_SUPPORT
		? (FUSED_GROUP_LIMIT < BASE_GROUPS ? FUSED_GROUP_LIMIT : BASE_GROUPS)
		: 0;
	static constexpr int FUSED_PAIRS =
		FUSED_GROUPS * (FUSED_GROUPS - 1) / 2;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	static constexpr int PACKED_WORDS = (GROUPS + 7) / 8;
	static constexpr int EXACT_WORDS = (PARITY_LENGTH + 63) / 64;
	static constexpr int EXACT_BYTES = (PARITY_LENGTH + 7) / 8;
	using PackedStates = std::array<uint64_t, PACKED_WORDS>;
	using ExactStates = std::array<uint64_t, EXACT_WORDS>;
	static constexpr int INFINITY_COST = 1000000000;

private:
	std::array<int, K> coordinates_{};
	std::array<int, K> information_costs_{};
	std::array<int, K + 1> information_prefix_{};
	std::array<int, GROUPS * BITS> offsets_{};
	std::array<State, GROUPS * K> row_masks_{};
	std::array<State, GROUPS> initial_states_{};
	std::array<uint64_t, K * PACKED_WORDS> packed_row_masks_{};
	PackedStates initial_packed_states_{};
	std::array<uint64_t, K * EXACT_WORDS> exact_row_masks_{};
	// Byte-major transpose used to score eight deepest-frontier siblings with
	// one gather per parity byte.  Rank-major words remain the cheap update
	// representation at internal DFS nodes.
	std::array<uint8_t, EXACT_BYTES * K> exact_row_bytes_{};
	ExactStates initial_exact_states_{};
	std::array<uint64_t, SOFT_MAGNITUDE_BITS * EXACT_WORDS>
		exact_weight_planes_{};
	std::array<uint64_t, PACKED_PDB_LANDMARKS * EXACT_WORDS> landmarks_{};
	std::array<int, PACKED_PDB_LANDMARKS * (K + 1) * (O + 1)>
		effect_minimum_{};
	std::array<int, PACKED_PDB_LANDMARKS * (K + 1) * (O + 1)>
		effect_maximum_{};
	std::array<int, GROUPS * STATES> terminal_costs_{};
	std::vector<int> minimum_;
	std::vector<int> fused_minimum_;
	static constexpr int FUSED_STATES = 1 << (2 * BITS);

	static std::size_t index(
		int group, int suffix, int weight, State state
	)
	{
		if constexpr (QUERY_LOCAL) {
			return (
				(
					(static_cast<std::size_t>(suffix) * (O + 1) + weight) *
					GROUPS + group
				) * STATES
			) + state;
		} else {
			return (
				(
					(static_cast<std::size_t>(group) * (K + 1) + suffix) *
					(O + 1) + weight
				) * STATES
			) + state;
		}
	}

	static std::size_t terminal_index(int group, State state)
	{
		return static_cast<std::size_t>(group) * STATES + state;
	}

	static std::size_t effect_index(int landmark, int suffix, int weight)
	{
		return (
			static_cast<std::size_t>(landmark) * (K + 1) + suffix
		) * (O + 1) + weight;
	}

	static std::size_t fused_index(
		int pair, int suffix, int weight, int state
	)
	{
		return (
			(
				static_cast<std::size_t>(pair) * (K + 1) + suffix
			) * (O + 1) + weight
		) * FUSED_STATES + state;
	}

	static int fused_pair_first(int pair)
	{
		int first = 0;
		while (pair >= FUSED_GROUPS - first - 1) {
			pair -= FUSED_GROUPS - first - 1;
			++first;
		}
		return first;
	}

	static int fused_pair_second(int pair)
	{
		const int first = fused_pair_first(pair);
		int offset = pair;
		for (int prior = 0; prior < first; ++prior)
			offset -= FUSED_GROUPS - prior - 1;
		return first + 1 + offset;
	}

public:
	AdditiveParityPatternDatabase(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		minimum_(
			static_cast<std::size_t>(GROUPS) * (K + 1) * (O + 1) *
			STATES,
			INFINITY_COST
		)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(GROUPS > 0);
		offsets_.fill(-1);

		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[left]));
				const int right_cost = std::abs(static_cast<int>(soft[right]));
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			assert(base[row] == (soft[row] < 0));
			information_costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[row])
			);
			information_prefix_[static_cast<std::size_t>(rank + 1)] =
				information_prefix_[static_cast<std::size_t>(rank)] +
				information_costs_[static_cast<std::size_t>(rank)];
		}

		std::array<int, PARITY_LENGTH> ordered_offsets{};
		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		for (int partition = 0; partition < PARTITIONS; ++partition) {
			const bool striped = PDB_GROUPING == 1 || partition == 1;
			for (int ordered = 0; ordered < PARITY_LENGTH; ++ordered) {
				const int local_group = striped
					? ordered % BASE_GROUPS
					: ordered / BITS;
				const int bit = striped
					? ordered / BASE_GROUPS
					: ordered % BITS;
				const int group = partition * BASE_GROUPS + local_group;
				assert(local_group < BASE_GROUPS && bit < BITS);
				offsets_[static_cast<std::size_t>(group * BITS + bit)] =
					ordered_offsets[static_cast<std::size_t>(ordered)];
			}
		}

		for (int group = 0; group < GROUPS; ++group) {
			for (int bit = 0; bit < BITS; ++bit) {
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				if (offset < 0)
					continue;
				if (base[K + offset] != (soft[K + offset] < 0))
					initial_states_[static_cast<std::size_t>(group)] |=
						static_cast<State>(State{1} << bit);
				if constexpr (PACKED_SUPPORT) {
					if (base[K + offset] != (soft[K + offset] < 0))
					initial_exact_states_[
						static_cast<std::size_t>(offset / 64)
					] |= uint64_t{1} << (offset % 64);
				}
				for (int rank = 0; rank < K; ++rank) {
					const int row = coordinates_[static_cast<std::size_t>(rank)];
					if (matrix[row * W + K + offset]) {
						row_masks_[static_cast<std::size_t>(group * K + rank)] |=
							static_cast<State>(State{1} << bit);
						if constexpr (PACKED_SUPPORT)
							exact_row_masks_[
								static_cast<std::size_t>(rank) * EXACT_WORDS +
								static_cast<std::size_t>(offset / 64)
							] |= uint64_t{1} << (offset % 64);
					}
				}
			}

			for (std::size_t state = 1; state < STATES; ++state) {
				const int bit = __builtin_ctzll(
					static_cast<unsigned long long>(state)
				);
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				terminal_costs_[terminal_index(
					group, static_cast<State>(state)
				)] = terminal_costs_[terminal_index(
					group, static_cast<State>(state & (state - 1))
				)] + (offset < 0 ? 0 : std::abs(
					static_cast<int>(soft[K + offset])
				));
			}

			for (std::size_t state = 0; state < STATES; ++state)
				minimum_[index(
					group, K, 0, static_cast<State>(state)
				)] = terminal_costs_[terminal_index(
					group, static_cast<State>(state)
				)];
			for (int suffix = K - 1; suffix >= 0; --suffix) {
				const State mask = row_mask(group, suffix);
				for (std::size_t state = 0; state < STATES; ++state) {
					const State current = static_cast<State>(state);
					minimum_[index(group, suffix, 0, current)] =
						minimum_[index(group, suffix + 1, 0, current)];
				}
				for (int weight = 1; weight <= O; ++weight) {
					#if defined(__AVX2__)
					if constexpr (PACKED_SUPPORT && !QUERY_LOCAL && STATES == 32) {
						const int low_mask = mask & 7;
						const int high_mask = mask >> 3;
						const __m256i permutation = _mm256_setr_epi32(
							0 ^ low_mask, 1 ^ low_mask,
							2 ^ low_mask, 3 ^ low_mask,
							4 ^ low_mask, 5 ^ low_mask,
							6 ^ low_mask, 7 ^ low_mask
						);
						for (int block = 0; block < 4; ++block) {
							const int source_block = block ^ high_mask;
							const int *skip_pointer = minimum_.data() + index(
								group, suffix + 1, weight,
								static_cast<State>(block * 8)
							);
							const int *take_pointer = minimum_.data() + index(
								group, suffix + 1, weight - 1,
								static_cast<State>(source_block * 8)
							);
							int *output_pointer = minimum_.data() + index(
								group, suffix, weight,
								static_cast<State>(block * 8)
							);
							const __m256i skip = _mm256_loadu_si256(
								reinterpret_cast<const __m256i *>(skip_pointer)
							);
							const __m256i take_source = _mm256_loadu_si256(
								reinterpret_cast<const __m256i *>(take_pointer)
							);
							const __m256i take = _mm256_permutevar8x32_epi32(
								take_source, permutation
							);
							const __m256i best = _mm256_min_epi32(skip, take);
							_mm256_storeu_si256(
								reinterpret_cast<__m256i *>(output_pointer), best
							);
						}
						continue;
					}
					#endif
					for (std::size_t state = 0; state < STATES; ++state) {
						const State current = static_cast<State>(state);
						const int skip = minimum_[index(
							group, suffix + 1, weight, current
						)];
						const int take = minimum_[index(
							group, suffix + 1, weight - 1,
							static_cast<State>(current ^ mask)
						)];
						minimum_[index(group, suffix, weight, current)] =
							std::min(skip, take);
					}
				}
			}
		}
		if constexpr (FUSED_SUPPORT) {
			static_assert(BITS <= 5);
			static_assert(FUSED_GROUP_LIMIT >= 2);
			static_assert(FUSED_PAIRS >= 1);
			fused_minimum_.assign(
				static_cast<std::size_t>(
					FUSED_PAIRS * (K + 1) * (O + 1) * FUSED_STATES
				),
				INFINITY_COST
			);
			for (int pair = 0; pair < FUSED_PAIRS; ++pair) {
				const int first = fused_pair_first(pair);
				const int second = fused_pair_second(pair);
				for (int state = 0; state < FUSED_STATES; ++state) {
					const State low = static_cast<State>(state & (STATES - 1));
					const State high = static_cast<State>(state >> BITS);
					fused_minimum_[fused_index(pair, K, 0, state)] =
						terminal_costs_[terminal_index(first, low)] +
						terminal_costs_[terminal_index(second, high)];
				}
				for (int suffix = K - 1; suffix >= 0; --suffix) {
					const int mask = row_mask(first, suffix) |
						(static_cast<int>(row_mask(second, suffix)) << BITS);
					for (int weight = 0; weight <= O; ++weight) {
						for (int state = 0; state < FUSED_STATES; ++state) {
							int value = fused_minimum_[fused_index(
								pair, suffix + 1, weight, state
							)];
							if (weight)
								value = std::min(
									value,
									fused_minimum_[fused_index(
										pair, suffix + 1, weight - 1,
										state ^ mask
									)]
								);
							fused_minimum_[fused_index(
								pair, suffix, weight, state
							)] = value;
						}
					}
				}
			}
		}
		if constexpr (PACKED_SUPPORT) {
			for (int group = 0; group < GROUPS; ++group) {
				const int word = group / 8;
				const int shift = 8 * (group % 8);
				initial_packed_states_[static_cast<std::size_t>(word)] |=
					static_cast<uint64_t>(
						initial_states_[static_cast<std::size_t>(group)]
					) << shift;
				for (int rank = 0; rank < K; ++rank)
					packed_row_masks_[
						static_cast<std::size_t>(rank) * PACKED_WORDS +
						static_cast<std::size_t>(word)
					] |= static_cast<uint64_t>(row_mask(group, rank)) << shift;
			}
			for (int byte = 0; byte < EXACT_BYTES; ++byte) {
				for (int rank = 0; rank < K; ++rank)
					exact_row_bytes_[
						static_cast<std::size_t>(byte) * K + rank
					] = static_cast<uint8_t>(
						exact_row_masks_[
							static_cast<std::size_t>(rank) * EXACT_WORDS +
							static_cast<std::size_t>(byte / 8)
						] >> (8 * (byte % 8))
					);
			}
			for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
				const int cost = std::abs(static_cast<int>(soft[K + offset]));
				for (int bit = 0; bit < SOFT_MAGNITUDE_BITS; ++bit)
					if ((cost >> bit) & 1)
						exact_weight_planes_[
							static_cast<std::size_t>(bit) * EXACT_WORDS +
							static_cast<std::size_t>(offset / 64)
						] |= uint64_t{1} << (offset % 64);
			}
			if constexpr (PACKED_PDB_TRIANGLE_PREFILTER) {
				effect_minimum_.fill(INFINITY_COST);
				effect_maximum_.fill(-INFINITY_COST);
				auto distance_to_landmark = [this](
					const ExactStates &effect, int landmark
				) {
					ExactStates difference = effect;
					for (int word = 0; word < EXACT_WORDS; ++word)
						difference[static_cast<std::size_t>(word)] ^=
							landmarks_[
								static_cast<std::size_t>(landmark) * EXACT_WORDS +
								word
							];
					return exact_parity_cost(difference);
				};
				for (int landmark = 1; landmark < PACKED_PDB_LANDMARKS;
					++landmark) {
					int best_rank = 0;
					int best_separation = -1;
					for (int rank = 0; rank < K; ++rank) {
						ExactStates effect{};
						for (int word = 0; word < EXACT_WORDS; ++word)
							effect[static_cast<std::size_t>(word)] =
								exact_row_mask(rank, word);
						int separation = INFINITY_COST;
						for (int prior = 0; prior < landmark; ++prior)
							separation = std::min(
								separation,
								distance_to_landmark(effect, prior)
							);
						if (separation > best_separation) {
							best_separation = separation;
							best_rank = rank;
						}
					}
					for (int word = 0; word < EXACT_WORDS; ++word)
						landmarks_[
							static_cast<std::size_t>(landmark) * EXACT_WORDS + word
						] = exact_row_mask(best_rank, word);
				}
				for (int landmark = 0; landmark < PACKED_PDB_LANDMARKS;
					++landmark) {
					ExactStates zero{};
					const int zero_cost = distance_to_landmark(zero, landmark);
					for (int suffix = 0; suffix <= K; ++suffix) {
						effect_minimum_[effect_index(landmark, suffix, 0)] =
							zero_cost;
						effect_maximum_[effect_index(landmark, suffix, 0)] =
							zero_cost;
					}
					for (int suffix = K - 1; suffix >= 0; --suffix) {
						ExactStates one{};
						for (int word = 0; word < EXACT_WORDS; ++word)
							one[static_cast<std::size_t>(word)] =
								exact_row_mask(suffix, word);
						const int one_cost = distance_to_landmark(one, landmark);
						effect_minimum_[effect_index(landmark, suffix, 1)] =
							std::min(one_cost, effect_minimum_[effect_index(
								landmark, suffix + 1, 1
							)]);
						effect_maximum_[effect_index(landmark, suffix, 1)] =
							std::max(one_cost, effect_maximum_[effect_index(
								landmark, suffix + 1, 1
							)]);
						if constexpr (O >= 2) {
							int minimum_pair = effect_minimum_[effect_index(
								landmark, suffix + 1, 2
							)];
							int maximum_pair = effect_maximum_[effect_index(
								landmark, suffix + 1, 2
							)];
							for (int second = suffix + 1; second < K; ++second) {
								ExactStates pair = one;
								for (int word = 0; word < EXACT_WORDS; ++word)
									pair[static_cast<std::size_t>(word)] ^=
										exact_row_mask(second, word);
								const int pair_cost = distance_to_landmark(
									pair, landmark
								);
								minimum_pair = std::min(minimum_pair, pair_cost);
								maximum_pair = std::max(maximum_pair, pair_cost);
							}
							effect_minimum_[effect_index(landmark, suffix, 2)] =
								minimum_pair;
							effect_maximum_[effect_index(landmark, suffix, 2)] =
								maximum_pair;
						}
					}
				}
			}
		}
	}

	int coordinate(int rank) const
	{
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	int information_cost(int rank) const
	{
		return information_costs_[static_cast<std::size_t>(rank)];
	}
	int minimum_information_cost(int suffix, int weight) const
	{
		if (weight < 0 || suffix + weight > K)
			return INFINITY_COST;
		return information_prefix_[static_cast<std::size_t>(suffix + weight)] -
			information_prefix_[static_cast<std::size_t>(suffix)];
	}
	State row_mask(int group, int rank) const
	{
		return row_masks_[static_cast<std::size_t>(group * K + rank)];
	}
	const std::array<State, GROUPS> &initial_states() const
	{
		return initial_states_;
	}
	const PackedStates &initial_packed_states() const
	{
		return initial_packed_states_;
	}
	uint64_t packed_row_mask(int rank, int word) const
	{
		return packed_row_masks_[
			static_cast<std::size_t>(rank) * PACKED_WORDS +
			static_cast<std::size_t>(word)
		];
	}
	const ExactStates &initial_exact_states() const
	{
		return initial_exact_states_;
	}
	uint64_t exact_row_mask(int rank, int word) const
	{
		return exact_row_masks_[
			static_cast<std::size_t>(rank) * EXACT_WORDS +
			static_cast<std::size_t>(word)
		];
	}
	const uint8_t *exact_row_byte_block(int byte, int rank) const
	{
		return exact_row_bytes_.data() +
			static_cast<std::size_t>(byte) * K + rank;
	}
	uint64_t exact_weight_plane(int bit, int word) const
	{
		return exact_weight_planes_[
			static_cast<std::size_t>(bit) * EXACT_WORDS + word
		];
	}
	State packed_group_state(const PackedStates &states, int group) const
	{
		return static_cast<State>(
			(states[static_cast<std::size_t>(group / 8)] >>
				(8 * (group % 8))) & 0xffu
		);
	}
	int state_cost(int group, State state) const
	{
		return group < BASE_GROUPS
			? terminal_costs_[terminal_index(group, state)]
			: 0;
	}
	int minimum(int group, int suffix, int weight, State state) const
	{
		return minimum_[index(group, suffix, weight, state)];
	}
	int parity_cost(const std::array<State, GROUPS> &states) const
	{
		int value = 0;
		for (int group = 0; group < BASE_GROUPS; ++group)
			value += state_cost(
				group, states[static_cast<std::size_t>(group)]
			);
		return value;
	}
	int parity_cost(const PackedStates &states) const
	{
		int value = 0;
		for (int group = 0; group < BASE_GROUPS; ++group)
			value += state_cost(group, packed_group_state(states, group));
		return value;
	}
	int exact_parity_cost(const ExactStates &states) const
	{
		int value = 0;
		for (int bit = 0; bit < SOFT_MAGNITUDE_BITS; ++bit)
			for (int word = 0; word < EXACT_WORDS; ++word)
				value += (1 << bit) * __builtin_popcountll(
					states[static_cast<std::size_t>(word)] &
					exact_weight_plane(bit, word)
				);
		return value;
	}
	void materialize_pattern(
		const int8_t *base,
		const int8_t *soft,
		const std::array<uint16_t, O> &ranks,
		int weight,
		int8_t *output
	) const
	{
		assert(weight >= 0 && weight <= O);
		std::copy(base, base + W, output);
		ExactStates exact = initial_exact_states_;
		for (int index = 0; index < weight; ++index) {
			const int rank = ranks[static_cast<std::size_t>(index)];
			output[coordinate(rank)] ^= int8_t{1};
			for (int word = 0; word < EXACT_WORDS; ++word)
				exact[static_cast<std::size_t>(word)] ^=
					exact_row_mask(rank, word);
		}
		for (int offset = 0; offset < PARITY_LENGTH; ++offset)
			output[K + offset] = static_cast<int8_t>(
				(soft[K + offset] < 0) ^
				((exact[static_cast<std::size_t>(offset / 64)] >>
					(offset % 64)) & uint64_t{1})
			);
		for (int index = N; index < W; ++index)
			output[index] = 0;
	}
	uint64_t improve_seed_with_group_witnesses(
		const int8_t *base,
		int8_t *candidate,
		const int8_t *soft,
		int &best_metric,
		int &next_metric,
		std::vector<uint64_t> *evaluated_masks = nullptr
	) const
	{
		static_assert(PACKED_SUPPORT);
		int absolute_metric = 0;
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		uint64_t proposals = 0;
		for (int group = 0; group < GROUPS; ++group) {
			for (int target_weight = 1; target_weight <= O; ++target_weight) {
				State state = initial_states_[static_cast<std::size_t>(group)];
				int remaining = target_weight;
				std::array<uint16_t, O> witness{};
				int witness_size = 0;
				for (int rank = 0; rank < K && remaining; ++rank) {
					const int skip = minimum_[index(
						group, rank + 1, remaining, state
					)];
					const State flipped = static_cast<State>(
						state ^ row_mask(group, rank)
					);
					const int take = minimum_[index(
						group, rank + 1, remaining - 1, flipped
					)];
					// Ties take the earlier, less-reliable information rank.
					if (take <= skip) {
						witness[static_cast<std::size_t>(witness_size++)] =
							static_cast<uint16_t>(rank);
						state = flipped;
						--remaining;
					}
				}
				if (remaining)
					continue;
				++proposals;
				if (evaluated_masks != nullptr) {
					uint64_t mask = 0;
					for (int index = 0; index < witness_size; ++index)
						mask |= uint64_t{1} << witness[
							static_cast<std::size_t>(index)
						];
					evaluated_masks->push_back(mask);
				}
				ExactStates exact = initial_exact_states_;
				int distance = 0;
				bool matches_current = true;
				for (int rank = 0; rank < K; ++rank) {
					const bool selected = std::binary_search(
						witness.begin(), witness.begin() + witness_size,
						static_cast<uint16_t>(rank)
					);
					const int row = coordinate(rank);
					if (selected) {
						distance += information_cost(rank);
						for (int word = 0; word < EXACT_WORDS; ++word)
							exact[static_cast<std::size_t>(word)] ^=
								exact_row_mask(rank, word);
					}
					matches_current = matches_current &&
						((candidate[row] != base[row]) == selected);
				}
				distance += exact_parity_cost(exact);
				const int metric = absolute_metric - 2 * distance;
				if (matches_current)
					continue;
				if (metric > best_metric) {
					next_metric = std::max(next_metric, best_metric);
					best_metric = metric;
					std::copy(base, base + W, candidate);
					for (int index = 0; index < witness_size; ++index)
						candidate[coordinate(witness[
							static_cast<std::size_t>(index)
						])] ^= int8_t{1};
					for (int offset = 0; offset < PARITY_LENGTH; ++offset)
						candidate[K + offset] = static_cast<int8_t>(
							(soft[K + offset] < 0) ^
							((exact[static_cast<std::size_t>(offset / 64)] >>
								(offset % 64)) & uint64_t{1})
						);
					for (int index = N; index < W; ++index)
						candidate[index] = 0;
				} else if (metric > next_metric) {
					next_metric = metric;
				}
			}
		}
		return proposals;
	}
	int descendant_effect_norm_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		int current_parity_cost,
		const ExactStates &states
	) const
	{
		std::array<int, PACKED_PDB_LANDMARKS> current_distances{};
		current_distances[0] = current_parity_cost;
		for (int landmark = 1; landmark < PACKED_PDB_LANDMARKS; ++landmark) {
			ExactStates difference = states;
			for (int word = 0; word < EXACT_WORDS; ++word)
				difference[static_cast<std::size_t>(word)] ^=
					landmarks_[
						static_cast<std::size_t>(landmark) * EXACT_WORDS + word
					];
			current_distances[static_cast<std::size_t>(landmark)] =
				exact_parity_cost(difference);
		}
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int parity_bound = 0;
			if (weight <= 2) {
				for (int landmark = 0; landmark < PACKED_PDB_LANDMARKS;
					++landmark) {
					const int minimum = effect_minimum_[effect_index(
						landmark, suffix, weight
					)];
					const int maximum = effect_maximum_[effect_index(
						landmark, suffix, weight
					)];
					if (minimum >= INFINITY_COST)
						continue;
					const int current = current_distances[
						static_cast<std::size_t>(landmark)
					];
					if (current < minimum)
						parity_bound = std::max(
							parity_bound, minimum - current
						);
					else if (current > maximum)
						parity_bound = std::max(
							parity_bound, current - maximum
						);
				}
			}
			best = std::min(
				best,
				current_information_cost +
					minimum_information_cost(suffix, weight) + parity_bound
			);
		}
		return best;
	}
	int descendant_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const std::array<State, GROUPS> &states,
		bool include_fused = true
	) const
	{
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int combined = 0;
			for (int partition = 0; partition < PARTITIONS; ++partition) {
				int value = current_information_cost +
					minimum_information_cost(suffix, weight);
				for (int local_group = 0; local_group < BASE_GROUPS;
					++local_group) {
					const int group = partition * BASE_GROUPS + local_group;
					const int projected = minimum(
						group, suffix, weight,
						states[static_cast<std::size_t>(group)]
					);
					if (projected >= INFINITY_COST) {
						value = INFINITY_COST;
						break;
					}
					value += projected;
				}
				combined = std::max(combined, value);
			}
			if constexpr (FUSED_SUPPORT) {
				if (include_fused) {
					for (int pair = 0; pair < FUSED_PAIRS; ++pair) {
						const int first = fused_pair_first(pair);
						const int second = fused_pair_second(pair);
						const int fused_state =
							states[static_cast<std::size_t>(first)] |
							(static_cast<int>(states[
								static_cast<std::size_t>(second)
							]) << BITS);
						int fused = current_information_cost +
							minimum_information_cost(suffix, weight) +
							fused_minimum_[fused_index(
								pair, suffix, weight, fused_state
							)];
						for (int group = 0; group < BASE_GROUPS; ++group)
							if (group != first && group != second)
								fused += minimum(
									group, suffix, weight,
									states[static_cast<std::size_t>(group)]
								);
						combined = std::max(combined, fused);
					}
				}
			}
			best = std::min(best, combined);
		}
		return best;
	}
	int descendant_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const PackedStates &states,
		bool include_fused = true
	) const
	{
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int combined = 0;
			for (int partition = 0; partition < PARTITIONS; ++partition) {
				#if defined(__AVX2__)
				if constexpr (!QUERY_LOCAL && STATES == 32 && PARTITIONS == 1) {
					constexpr int GROUP_STRIDE = (K + 1) * (O + 1) * STATES;
					const int slice = (suffix * (O + 1) + weight) * STATES;
					__m256i sum_vector = _mm256_setzero_si256();
					int local_group = 0;
					for (; local_group + 8 <= BASE_GROUPS; local_group += 8) {
						const int group = partition * BASE_GROUPS + local_group;
					const __m128i packed = _mm_cvtsi64_si128(
						static_cast<long long>(
							states[static_cast<std::size_t>(group / 8)]
							>> (8 * (group % 8))
						)
					);
					const __m256i state_indices = _mm256_cvtepu8_epi32(packed);
					const __m256i group_indices = _mm256_setr_epi32(
						(group + 0) * GROUP_STRIDE + slice,
						(group + 1) * GROUP_STRIDE + slice,
						(group + 2) * GROUP_STRIDE + slice,
						(group + 3) * GROUP_STRIDE + slice,
						(group + 4) * GROUP_STRIDE + slice,
						(group + 5) * GROUP_STRIDE + slice,
						(group + 6) * GROUP_STRIDE + slice,
						(group + 7) * GROUP_STRIDE + slice
					);
					const __m256i indices = _mm256_add_epi32(
						group_indices, state_indices
					);
					const __m256i values = _mm256_i32gather_epi32(
						minimum_.data(), indices, 4
					);
					sum_vector = _mm256_add_epi32(sum_vector, values);
					}
					alignas(32) std::array<int, 8> lanes{};
					_mm256_store_si256(
					reinterpret_cast<__m256i *>(lanes.data()), sum_vector
				);
					int value = current_information_cost +
					minimum_information_cost(suffix, weight) +
					std::accumulate(lanes.begin(), lanes.end(), 0);
					for (; local_group < BASE_GROUPS; ++local_group) {
						const int group = partition * BASE_GROUPS + local_group;
					value += minimum(
						group, suffix, weight,
						packed_group_state(states, group)
					);
					}
					combined = std::max(combined, value);
					continue;
				}
				#endif
				int value = current_information_cost +
				minimum_information_cost(suffix, weight);
				for (int local_group = 0; local_group < BASE_GROUPS; ++local_group) {
					const int group = partition * BASE_GROUPS + local_group;
				const int projected = minimum(
					group, suffix, weight,
					packed_group_state(states, group)
				);
				if (projected >= INFINITY_COST) {
					value = INFINITY_COST;
					break;
				}
				value += projected;
			}
				combined = std::max(combined, value);
			}
			if constexpr (FUSED_SUPPORT) {
				if (include_fused) {
					for (int pair = 0; pair < FUSED_PAIRS; ++pair) {
						const int first = fused_pair_first(pair);
						const int second = fused_pair_second(pair);
						const int fused_state =
							packed_group_state(states, first) |
							(static_cast<int>(
								packed_group_state(states, second)
							) << BITS);
						int fused = current_information_cost +
							minimum_information_cost(suffix, weight) +
							fused_minimum_[fused_index(
								pair, suffix, weight, fused_state
							)];
						for (int group = 0; group < BASE_GROUPS; ++group)
							if (group != first && group != second)
								fused += minimum(
									group, suffix, weight,
									packed_group_state(states, group)
								);
						combined = std::max(combined, fused);
					}
				}
			}
			best = std::min(best, combined);
		}
		return best;
	}
	int offset(int group, int bit) const
	{
		return offsets_[static_cast<std::size_t>(group * BITS + bit)];
	}
	std::size_t bytes() const
	{
		return minimum_.size() * sizeof(int) +
			fused_minimum_.size() * sizeof(int) +
			coordinates_.size() * sizeof(int) +
			information_costs_.size() * sizeof(int) +
			information_prefix_.size() * sizeof(int) +
			offsets_.size() * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			initial_states_.size() * sizeof(State) +
			(PACKED_SUPPORT
				? packed_row_masks_.size() * sizeof(uint64_t) +
					initial_packed_states_.size() * sizeof(uint64_t) +
					exact_row_masks_.size() * sizeof(uint64_t) +
					exact_row_bytes_.size() * sizeof(uint8_t) +
					initial_exact_states_.size() * sizeof(uint64_t) +
					exact_weight_planes_.size() * sizeof(uint64_t)
					+ landmarks_.size() * sizeof(uint64_t)
					+ effect_minimum_.size() * sizeof(int)
					+ effect_maximum_.size() * sizeof(int)
				: 0) +
			terminal_costs_.size() * sizeof(int);
	}
};

// Compatibility-aware additive parity abstraction.  The ordinary additive
// PDB lets every parity group choose an unrelated size-q completion.  Here,
// every information row is assigned a small XOR fingerprint and a selected
// prefix of the most reliable parity groups, plus the information-cost
// relaxation, must choose completions with the same fingerprint.  Remaining
// groups retain the ordinary cardinality-only relaxation.  A real completion
// has one common fingerprint, so minimizing over fingerprints remains
// admissible.  Collisions preserve a relaxation while ruling out many
// mutually incompatible group minima.
template <int N, int K, int O>
class FingerprintSynchronizedParityPatternDatabase {
public:
	using State = uint16_t;
	using Fingerprint = uint8_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS = ADDITIVE_PDB_BITS;
	static constexpr int GROUPS = (PARITY_LENGTH + BITS - 1) / BITS;
	static constexpr int SYNCHRONIZED_GROUPS =
		FINGERPRINT_PDB_GROUPS < GROUPS ? FINGERPRINT_PDB_GROUPS : GROUPS;
	static constexpr int ORDINARY_GROUPS = GROUPS - SYNCHRONIZED_GROUPS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	static constexpr std::size_t FINGERPRINTS =
		std::size_t{1} << FINGERPRINT_PDB_BITS;
	static constexpr int INFINITY_COST = 1000000000;

private:
	std::array<int, K> coordinates_{};
	std::array<int, K> information_costs_{};
	std::array<Fingerprint, K> row_fingerprints_{};
	std::array<int, GROUPS * BITS> offsets_{};
	std::array<State, GROUPS * K> row_masks_{};
	std::array<State, GROUPS> initial_states_{};
	std::array<int, GROUPS * STATES> terminal_costs_{};
	std::vector<int> information_minimum_;
	std::vector<int> synchronized_parity_minimum_;
	std::vector<int> ordinary_parity_minimum_;

	static uint64_t mix64(uint64_t value)
	{
		value += 0x9e3779b97f4a7c15ULL;
		value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
		value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
		return value ^ (value >> 31);
	}

	static std::size_t synchronized_parity_index(
		int group,
		int suffix,
		int weight,
		State state,
		Fingerprint fingerprint
	)
	{
		return (
			(
				(
					(static_cast<std::size_t>(group) * (K + 1) + suffix) *
					(O + 1) + weight
				) * STATES + state
			) * FINGERPRINTS
		) + fingerprint;
	}

	static std::size_t ordinary_parity_index(
		int group, int suffix, int weight, State state
	)
	{
		assert(group >= SYNCHRONIZED_GROUPS);
		return (
			(
				(static_cast<std::size_t>(group - SYNCHRONIZED_GROUPS) *
					(K + 1) + suffix) * (O + 1) + weight
			) * STATES
		) + state;
	}

	static std::size_t information_index(
		int suffix, int weight, Fingerprint fingerprint
	)
	{
		return (
			(static_cast<std::size_t>(suffix) * (O + 1) + weight) *
			FINGERPRINTS
		) + fingerprint;
	}

	static std::size_t terminal_index(int group, State state)
	{
		return static_cast<std::size_t>(group) * STATES + state;
	}

public:
	FingerprintSynchronizedParityPatternDatabase(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		information_minimum_(
			static_cast<std::size_t>(K + 1) * (O + 1) * FINGERPRINTS,
			INFINITY_COST
		),
		synchronized_parity_minimum_(
			static_cast<std::size_t>(SYNCHRONIZED_GROUPS) * (K + 1) *
			(O + 1) *
			STATES * FINGERPRINTS,
			INFINITY_COST
		),
		ordinary_parity_minimum_(
			static_cast<std::size_t>(ORDINARY_GROUPS) * (K + 1) * (O + 1) *
			STATES,
			INFINITY_COST
		)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(GROUPS > 0);
		offsets_.fill(-1);

		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[left]));
				const int right_cost = std::abs(static_cast<int>(soft[right]));
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			assert(base[row] == (soft[row] < 0));
			information_costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[row])
			);
			uint64_t fingerprint_seed = mix64(
				static_cast<uint64_t>(row + 1)
			);
			for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
				if (matrix[row * W + K + offset])
					fingerprint_seed ^= mix64(
						static_cast<uint64_t>(offset + 1) *
						0xd6e8feb86659fd93ULL
					);
			}
			row_fingerprints_[static_cast<std::size_t>(rank)] =
				static_cast<Fingerprint>(fingerprint_seed & (FINGERPRINTS - 1));
		}

		information_minimum_[information_index(K, 0, Fingerprint{0})] = 0;
		for (int suffix = K - 1; suffix >= 0; --suffix) {
			const Fingerprint row_fingerprint =
				row_fingerprints_[static_cast<std::size_t>(suffix)];
			const int information = information_costs_[
				static_cast<std::size_t>(suffix)
			];
			for (int weight = 0; weight <= O; ++weight) {
				for (std::size_t fingerprint = 0;
					fingerprint < FINGERPRINTS; ++fingerprint) {
					const Fingerprint current = static_cast<Fingerprint>(fingerprint);
					const int skip = information_minimum_[information_index(
						suffix + 1, weight, current
					)];
					int take = INFINITY_COST;
					if (weight > 0) {
						const int child = information_minimum_[information_index(
							suffix + 1,
							weight - 1,
							static_cast<Fingerprint>(current ^ row_fingerprint)
						)];
						if (child < INFINITY_COST)
							take = information + child;
					}
					information_minimum_[information_index(
						suffix, weight, current
					)] = std::min(skip, take);
				}
			}
		}

		std::array<int, PARITY_LENGTH> ordered_offsets{};
		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[K + left]));
				const int right_cost = std::abs(static_cast<int>(soft[K + right]));
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		for (int ordered = 0; ordered < PARITY_LENGTH; ++ordered) {
			const int group = ordered / BITS;
			const int bit = ordered % BITS;
			offsets_[static_cast<std::size_t>(group * BITS + bit)] =
				ordered_offsets[static_cast<std::size_t>(ordered)];
		}

		for (int group = 0; group < GROUPS; ++group) {
			for (int bit = 0; bit < BITS; ++bit) {
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				if (offset < 0)
					continue;
				if (base[K + offset] != (soft[K + offset] < 0))
					initial_states_[static_cast<std::size_t>(group)] |=
						static_cast<State>(State{1} << bit);
				for (int rank = 0; rank < K; ++rank) {
					const int row = coordinates_[static_cast<std::size_t>(rank)];
					if (matrix[row * W + K + offset])
						row_masks_[static_cast<std::size_t>(group * K + rank)] |=
							static_cast<State>(State{1} << bit);
				}
			}

			for (std::size_t state = 1; state < STATES; ++state) {
				const int bit = __builtin_ctzll(
					static_cast<unsigned long long>(state)
				);
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				terminal_costs_[terminal_index(
					group, static_cast<State>(state)
				)] = terminal_costs_[terminal_index(
					group, static_cast<State>(state & (state - 1))
				)] + (offset < 0 ? 0 : std::abs(
					static_cast<int>(soft[K + offset])
				));
			}

			if (group < SYNCHRONIZED_GROUPS) {
				for (std::size_t state = 0; state < STATES; ++state) {
					synchronized_parity_minimum_[synchronized_parity_index(
						group, K, 0, static_cast<State>(state), Fingerprint{0}
					)] = terminal_costs_[terminal_index(
						group, static_cast<State>(state)
					)];
				}
				for (int suffix = K - 1; suffix >= 0; --suffix) {
					const State mask = row_mask(group, suffix);
					const Fingerprint row_fingerprint =
						row_fingerprints_[static_cast<std::size_t>(suffix)];
					for (int weight = 0; weight <= O; ++weight) {
						for (std::size_t state = 0; state < STATES; ++state) {
							const State current_state = static_cast<State>(state);
							for (std::size_t fingerprint = 0;
								fingerprint < FINGERPRINTS; ++fingerprint) {
								const Fingerprint current_fingerprint =
									static_cast<Fingerprint>(fingerprint);
								const int skip = synchronized_parity_minimum_[
									synchronized_parity_index(
										group,
										suffix + 1,
										weight,
										current_state,
										current_fingerprint
									)
								];
								int take = INFINITY_COST;
								if (weight > 0) {
									take = synchronized_parity_minimum_[
										synchronized_parity_index(
											group,
											suffix + 1,
											weight - 1,
											static_cast<State>(current_state ^ mask),
											static_cast<Fingerprint>(
												current_fingerprint ^ row_fingerprint
											)
										)
									];
								}
								synchronized_parity_minimum_[
									synchronized_parity_index(
										group,
										suffix,
										weight,
										current_state,
										current_fingerprint
									)
								] = std::min(skip, take);
							}
						}
					}
				}
			} else {
				for (std::size_t state = 0; state < STATES; ++state) {
					ordinary_parity_minimum_[ordinary_parity_index(
						group, K, 0, static_cast<State>(state)
					)] = terminal_costs_[terminal_index(
						group, static_cast<State>(state)
					)];
				}
				for (int suffix = K - 1; suffix >= 0; --suffix) {
					const State mask = row_mask(group, suffix);
					for (int weight = 0; weight <= O; ++weight) {
						for (std::size_t state = 0; state < STATES; ++state) {
							const State current_state = static_cast<State>(state);
							const int skip = ordinary_parity_minimum_[
								ordinary_parity_index(
									group, suffix + 1, weight, current_state
								)
							];
							int take = INFINITY_COST;
							if (weight > 0) {
								take = ordinary_parity_minimum_[
									ordinary_parity_index(
										group,
										suffix + 1,
										weight - 1,
										static_cast<State>(current_state ^ mask)
									)
								];
							}
							ordinary_parity_minimum_[ordinary_parity_index(
								group, suffix, weight, current_state
							)] = std::min(skip, take);
						}
					}
				}
			}
		}
	}

	int coordinate(int rank) const
	{
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	int information_cost(int rank) const
	{
		return information_costs_[static_cast<std::size_t>(rank)];
	}
	State row_mask(int group, int rank) const
	{
		return row_masks_[static_cast<std::size_t>(group * K + rank)];
	}
	const std::array<State, GROUPS> &initial_states() const
	{
		return initial_states_;
	}
	int state_cost(int group, State state) const
	{
		return terminal_costs_[terminal_index(group, state)];
	}
	int parity_cost(const std::array<State, GROUPS> &states) const
	{
		int value = 0;
		for (int group = 0; group < GROUPS; ++group)
			value += state_cost(
				group, states[static_cast<std::size_t>(group)]
			);
		return value;
	}
	int descendant_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const std::array<State, GROUPS> &states
	) const
	{
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int ordinary_sum = 0;
			for (int group = SYNCHRONIZED_GROUPS; group < GROUPS; ++group) {
				const int projected = ordinary_parity_minimum_[
					ordinary_parity_index(
						group,
						suffix,
						weight,
						states[static_cast<std::size_t>(group)]
					)
				];
				if (projected >= INFINITY_COST) {
					ordinary_sum = INFINITY_COST;
					break;
				}
				ordinary_sum += projected;
			}
			if (ordinary_sum >= INFINITY_COST)
				continue;
			for (std::size_t fingerprint = 0;
				fingerprint < FINGERPRINTS; ++fingerprint) {
				const Fingerprint current = static_cast<Fingerprint>(fingerprint);
				const int information = information_minimum_[information_index(
					suffix, weight, current
				)];
				if (information >= INFINITY_COST)
					continue;
				int value = current_information_cost + information + ordinary_sum;
				for (int group = 0; group < SYNCHRONIZED_GROUPS; ++group) {
					const int projected = synchronized_parity_minimum_[
						synchronized_parity_index(
						group,
						suffix,
						weight,
						states[static_cast<std::size_t>(group)],
						current
						)
					];
					if (projected >= INFINITY_COST) {
						value = INFINITY_COST;
						break;
					}
					value += projected;
				}
				best = std::min(best, value);
			}
		}
		return best;
	}
	int offset(int group, int bit) const
	{
		return offsets_[static_cast<std::size_t>(group * BITS + bit)];
	}
	std::size_t bytes() const
	{
		return information_minimum_.size() * sizeof(int) +
			synchronized_parity_minimum_.size() * sizeof(int) +
			ordinary_parity_minimum_.size() * sizeof(int) +
			coordinates_.size() * sizeof(int) +
			information_costs_.size() * sizeof(int) +
			row_fingerprints_.size() * sizeof(Fingerprint) +
			offsets_.size() * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			initial_states_.size() * sizeof(State) +
			terminal_costs_.size() * sizeof(int);
	}
};

// Reliability-stratified syndrome quotient.  A linear map compresses the
// complete parity-mismatch vector to a small quotient syndrome.  The most
// reliable parity positions receive private quotient bits; all remaining
// positions are mapped into a disjoint hash subspace.  A frame-local weighted
// coset table gives the cheapest real parity vector for every quotient state.
// The suffix DP then couples that admissible parity cost to both the identity
// and the exact cardinality of future information-row flips.  Thus one table
// query replaces the sum of many independent CAP-PDB group queries.
template <int N, int K, int O>
class SyndromeQuotientPatternDatabase {
public:
	using State = uint16_t;
	using QuotientState = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS = ADDITIVE_PDB_BITS;
	static constexpr int GROUPS = (PARITY_LENGTH + BITS - 1) / BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	static constexpr int QUOTIENT_BITS = QUOTIENT_PDB_BITS;
	static constexpr int DIRECT_BITS = QUOTIENT_PDB_DIRECT_BITS;
	static constexpr int HASH_BITS = QUOTIENT_BITS - DIRECT_BITS;
	static constexpr std::size_t QUOTIENT_STATES =
		std::size_t{1} << QUOTIENT_BITS;
	static constexpr int INFINITY_COST = 1000000000;

private:
	std::array<int, K> coordinates_{};
	std::array<int, K> information_costs_{};
	std::array<int, GROUPS * BITS> offsets_{};
	std::array<State, GROUPS * K> row_masks_{};
	std::array<State, GROUPS> initial_states_{};
	std::array<int, GROUPS * STATES> terminal_costs_{};
	std::array<QuotientState, PARITY_LENGTH> signatures_{};
	std::array<QuotientState, K> quotient_row_masks_{};
	std::array<QuotientState, GROUPS * STATES>
		group_state_signatures_{};
	std::vector<int> minimum_;
	QuotientState initial_quotient_state_ = 0;

	static uint64_t mix64(uint64_t value)
	{
		value += 0x9e3779b97f4a7c15ULL;
		value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
		value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
		return value ^ (value >> 31);
	}

	static std::size_t minimum_index(
		int suffix, int weight, QuotientState state
	)
	{
		return (
			(static_cast<std::size_t>(suffix) * (O + 1) + weight) *
			QUOTIENT_STATES
		) + state;
	}

	static std::size_t terminal_index(int group, State state)
	{
		return static_cast<std::size_t>(group) * STATES + state;
	}

	static std::size_t group_signature_index(int group, State state)
	{
		return static_cast<std::size_t>(group) * STATES + state;
	}

public:
	SyndromeQuotientPatternDatabase(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		minimum_(
			static_cast<std::size_t>(K + 1) * (O + 1) * QUOTIENT_STATES,
			INFINITY_COST
		)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(BITS > 0 && BITS <= 8);
		static_assert(QUOTIENT_BITS > 0 && QUOTIENT_BITS <= 15);
		static_assert(DIRECT_BITS >= 0 && DIRECT_BITS <= QUOTIENT_BITS);
		offsets_.fill(-1);

		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[left]));
				const int right_cost = std::abs(static_cast<int>(soft[right]));
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			assert(base[row] == (soft[row] < 0));
			information_costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[row])
			);
		}

		std::array<int, PARITY_LENGTH> ordered_offsets{};
		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		constexpr QuotientState HASH_MASK = HASH_BITS == 0
			? QuotientState{0}
			: static_cast<QuotientState>(
				(QuotientState{1} << HASH_BITS) - 1
			);
		for (int ordered = 0; ordered < PARITY_LENGTH; ++ordered) {
			const int offset = ordered_offsets[static_cast<std::size_t>(ordered)];
			const int group = ordered / BITS;
			const int bit = ordered % BITS;
			offsets_[static_cast<std::size_t>(group * BITS + bit)] = offset;
			QuotientState signature = 0;
			if (ordered < DIRECT_BITS) {
				signature = static_cast<QuotientState>(
					QuotientState{1} << (HASH_BITS + ordered)
				);
			} else if constexpr (HASH_BITS > 0) {
				signature = static_cast<QuotientState>(
					mix64(
						static_cast<uint64_t>(offset + 1) *
						0xd6e8feb86659fd93ULL
					) & HASH_MASK
				);
				if (signature == 0)
					signature = static_cast<QuotientState>(
						QuotientState{1} << ((ordered - DIRECT_BITS) % HASH_BITS)
					);
			}
			signatures_[static_cast<std::size_t>(offset)] = signature;
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_quotient_state_ = static_cast<QuotientState>(
					initial_quotient_state_ ^ signature
				);
		}

		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			for (int offset = 0; offset < PARITY_LENGTH; ++offset)
				if (matrix[row * W + K + offset])
					quotient_row_masks_[static_cast<std::size_t>(rank)] =
						static_cast<QuotientState>(
							quotient_row_masks_[static_cast<std::size_t>(rank)] ^
							signatures_[static_cast<std::size_t>(offset)]
						);
		}

		for (int group = 0; group < GROUPS; ++group) {
			for (int bit = 0; bit < BITS; ++bit) {
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				if (offset < 0)
					continue;
				if (base[K + offset] != (soft[K + offset] < 0))
					initial_states_[static_cast<std::size_t>(group)] |=
						static_cast<State>(State{1} << bit);
				for (int rank = 0; rank < K; ++rank) {
					const int row = coordinates_[static_cast<std::size_t>(rank)];
					if (matrix[row * W + K + offset])
						row_masks_[static_cast<std::size_t>(group * K + rank)] |=
							static_cast<State>(State{1} << bit);
				}
			}
			for (std::size_t state = 1; state < STATES; ++state) {
				const int bit = __builtin_ctzll(
					static_cast<unsigned long long>(state)
				);
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + bit
				)];
				const State previous = static_cast<State>(state & (state - 1));
				terminal_costs_[terminal_index(
					group, static_cast<State>(state)
				)] = terminal_costs_[terminal_index(group, previous)] +
					(offset < 0 ? 0 : std::abs(
						static_cast<int>(soft[K + offset])
					));
				group_state_signatures_[group_signature_index(
					group, static_cast<State>(state)
				)] = static_cast<QuotientState>(
					group_state_signatures_[group_signature_index(
						group, previous
					)] ^ (offset < 0
						? QuotientState{0}
						: signatures_[static_cast<std::size_t>(offset)])
				);
			}
		}

		std::array<int, QUOTIENT_STATES> coset_cost{};
		coset_cost.fill(INFINITY_COST);
		coset_cost[0] = 0;
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			const auto previous = coset_cost;
			const QuotientState signature =
				signatures_[static_cast<std::size_t>(offset)];
			const int cost = std::abs(static_cast<int>(soft[K + offset]));
			for (std::size_t state = 0; state < QUOTIENT_STATES; ++state) {
				if (previous[state] >= INFINITY_COST)
					continue;
				const std::size_t toggled = state ^ signature;
				coset_cost[toggled] = std::min(
					coset_cost[toggled], previous[state] + cost
				);
			}
		}
		for (std::size_t state = 0; state < QUOTIENT_STATES; ++state)
			minimum_[minimum_index(
				K, 0, static_cast<QuotientState>(state)
			)] = coset_cost[state];
		for (int suffix = K - 1; suffix >= 0; --suffix) {
			const QuotientState mask =
				quotient_row_masks_[static_cast<std::size_t>(suffix)];
			const int information =
				information_costs_[static_cast<std::size_t>(suffix)];
			for (int weight = 0; weight <= O; ++weight) {
				for (std::size_t state = 0; state < QUOTIENT_STATES; ++state) {
					const QuotientState current =
						static_cast<QuotientState>(state);
					const int skip = minimum_[minimum_index(
						suffix + 1, weight, current
					)];
					int take = INFINITY_COST;
					if (weight > 0) {
						const int child = minimum_[minimum_index(
							suffix + 1,
							weight - 1,
							static_cast<QuotientState>(current ^ mask)
						)];
						if (child < INFINITY_COST)
							take = information + child;
					}
					minimum_[minimum_index(suffix, weight, current)] =
						std::min(skip, take);
				}
			}
		}
	}

	int coordinate(int rank) const
	{
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	int information_cost(int rank) const
	{
		return information_costs_[static_cast<std::size_t>(rank)];
	}
	State row_mask(int group, int rank) const
	{
		return row_masks_[static_cast<std::size_t>(group * K + rank)];
	}
	const std::array<State, GROUPS> &initial_states() const
	{
		return initial_states_;
	}
	int state_cost(int group, State state) const
	{
		return terminal_costs_[terminal_index(group, state)];
	}
	int parity_cost(const std::array<State, GROUPS> &states) const
	{
		int value = 0;
		for (int group = 0; group < GROUPS; ++group)
			value += state_cost(group, states[static_cast<std::size_t>(group)]);
		return value;
	}
	int descendant_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const std::array<State, GROUPS> &states
	) const
	{
		QuotientState current = 0;
		for (int group = 0; group < GROUPS; ++group)
			current = static_cast<QuotientState>(
				current ^ group_state_signatures_[group_signature_index(
					group, states[static_cast<std::size_t>(group)]
				)]
			);
		assert(current == initial_quotient_state_ || current < QUOTIENT_STATES);
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			const int completion = minimum_[minimum_index(
				suffix, weight, current
			)];
			if (completion < INFINITY_COST)
				best = std::min(
					best, current_information_cost + completion
				);
		}
		return best;
	}
	int offset(int group, int bit) const
	{
		return offsets_[static_cast<std::size_t>(group * BITS + bit)];
	}
	std::size_t bytes() const
	{
		return minimum_.size() * sizeof(int) +
			coordinates_.size() * sizeof(int) +
			information_costs_.size() * sizeof(int) +
			offsets_.size() * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			initial_states_.size() * sizeof(State) +
			terminal_costs_.size() * sizeof(int) +
			signatures_.size() * sizeof(QuotientState) +
			quotient_row_masks_.size() * sizeof(QuotientState) +
			group_state_signatures_.size() * sizeof(QuotientState);
	}
};

// Row-coupled zero-one cost-partitioned parity abstractions.  Every future
// information-bit cost is assigned in full to exactly one parity group: the
// Uniform cost-partitioned CAP-PDB.  Every group charges one G-th of each
// selected information reliability.  Integer arithmetic multiplies each
// group's parity objective by G, so its DP transition cost is exactly c_i and
// no rounding occurs.  For every real completion S, summing all group
// objectives gives G times the complete information-plus-parity metric.
// Independently minimizing the groups is therefore admissible.  It also
// pointwise dominates cardinality-only CAP: min(G p_g + c) is at least
// G min(p_g) + min(c), and summing the latter recovers the ordinary CAP bound.
template <int N, int K, int O>
class CostPartitionedAdditiveParityPatternDatabase {
public:
	using State = uint16_t;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS = ADDITIVE_PDB_BITS;
	static constexpr int GROUPS = (PARITY_LENGTH + BITS - 1) / BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	static constexpr int INFINITY_COST = 1000000000;

private:
	std::array<int, K> coordinates_{};
	std::array<int, K> information_costs_{};
	std::array<int, K + 1> information_prefix_{};
	std::array<int, GROUPS * BITS> offsets_{};
	std::array<State, GROUPS * K> row_masks_{};
	std::array<State, GROUPS> initial_states_{};
	std::array<int, GROUPS * STATES> terminal_costs_{};
	std::vector<int> partitioned_minimum_;

	static std::size_t index(
		int group, int suffix, int weight, State state
	)
	{
		return (
			(
				(static_cast<std::size_t>(group) * (K + 1) + suffix) *
				(O + 1) + weight
			) * STATES
		) + state;
	}

	static std::size_t terminal_index(int group, State state)
	{
		return static_cast<std::size_t>(group) * STATES + state;
	}

public:
	CostPartitionedAdditiveParityPatternDatabase(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		partitioned_minimum_(
			static_cast<std::size_t>(GROUPS) * (K + 1) * (O + 1) *
			STATES,
			INFINITY_COST
		)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(GROUPS > 0);
		offsets_.fill(-1);

		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[left]));
				const int right_cost = std::abs(static_cast<int>(soft[right]));
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			assert(base[row] == (soft[row] < 0));
			information_costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[row])
			);
			information_prefix_[static_cast<std::size_t>(rank + 1)] =
				information_prefix_[static_cast<std::size_t>(rank)] +
				information_costs_[static_cast<std::size_t>(rank)];
		}

		std::array<int, PARITY_LENGTH> ordered_offsets{};
		std::iota(ordered_offsets.begin(), ordered_offsets.end(), 0);
		std::stable_sort(
			ordered_offsets.begin(), ordered_offsets.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		for (int ordered = 0; ordered < PARITY_LENGTH; ++ordered) {
			const int group = ordered / BITS;
			const int local_bit = ordered % BITS;
			offsets_[static_cast<std::size_t>(group * BITS + local_bit)] =
				ordered_offsets[static_cast<std::size_t>(ordered)];
		}
		for (int group = 0; group < GROUPS; ++group) {
			for (int local_bit = 0; local_bit < BITS; ++local_bit) {
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + local_bit
				)];
				if (offset < 0)
					continue;
				if (base[K + offset] != (soft[K + offset] < 0))
					initial_states_[static_cast<std::size_t>(group)] |=
						static_cast<State>(State{1} << local_bit);
				for (int rank = 0; rank < K; ++rank) {
					const int row = coordinates_[static_cast<std::size_t>(rank)];
					if (matrix[row * W + K + offset])
						row_masks_[static_cast<std::size_t>(group * K + rank)] |=
							static_cast<State>(State{1} << local_bit);
				}
			}

			for (std::size_t state = 1; state < STATES; ++state) {
				const int local_bit = __builtin_ctzll(
					static_cast<unsigned long long>(state)
				);
				const int offset = offsets_[static_cast<std::size_t>(
					group * BITS + local_bit
				)];
				terminal_costs_[terminal_index(
					group, static_cast<State>(state)
				)] = terminal_costs_[terminal_index(
					group, static_cast<State>(state & (state - 1))
				)] + (offset < 0 ? 0 : std::abs(
					static_cast<int>(soft[K + offset])
				));
			}

			for (std::size_t state = 0; state < STATES; ++state) {
				const State current = static_cast<State>(state);
				const int terminal = GROUPS * terminal_costs_[terminal_index(
					group, current
				)];
				partitioned_minimum_[index(group, K, 0, current)] = terminal;
			}
			for (int suffix = K - 1; suffix >= 0; --suffix) {
				const State mask = row_mask(group, suffix);
				const int information = information_cost(suffix);
				for (std::size_t state = 0; state < STATES; ++state) {
					const State current = static_cast<State>(state);
					partitioned_minimum_[index(group, suffix, 0, current)] =
						partitioned_minimum_[index(
							group, suffix + 1, 0, current
						)];
				}
				for (int weight = 1; weight <= O; ++weight) {
					for (std::size_t state = 0; state < STATES; ++state) {
						const State current = static_cast<State>(state);
						const State flipped = static_cast<State>(current ^ mask);
						const int skip = partitioned_minimum_[index(
							group, suffix + 1, weight, current
						)];
						const int child = partitioned_minimum_[index(
							group, suffix + 1, weight - 1, flipped
						)];
						const int take = child >= INFINITY_COST
							? INFINITY_COST
							: information + child;
						partitioned_minimum_[index(
							group, suffix, weight, current
						)] = std::min(skip, take);
					}
				}
			}
		}
	}

	int coordinate(int rank) const
	{
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	int information_cost(int rank) const
	{
		return information_costs_[static_cast<std::size_t>(rank)];
	}
	int minimum_information_cost(int suffix, int weight) const
	{
		if (weight < 0 || suffix + weight > K)
			return INFINITY_COST;
		return information_prefix_[static_cast<std::size_t>(suffix + weight)] -
			information_prefix_[static_cast<std::size_t>(suffix)];
	}
	State row_mask(int group, int rank) const
	{
		return row_masks_[static_cast<std::size_t>(group * K + rank)];
	}
	const std::array<State, GROUPS> &initial_states() const
	{
		return initial_states_;
	}
	int state_cost(int group, State state) const
	{
		return terminal_costs_[terminal_index(group, state)];
	}
	int parity_cost(const std::array<State, GROUPS> &states) const
	{
		int value = 0;
		for (int group = 0; group < GROUPS; ++group)
			value += state_cost(
				group, states[static_cast<std::size_t>(group)]
			);
		return value;
	}
	int descendant_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const std::array<State, GROUPS> &states
	) const
	{
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int partitioned_sum = GROUPS * current_information_cost;
			for (int group = 0; group < GROUPS; ++group) {
				const State state = states[static_cast<std::size_t>(group)];
				const int partitioned = partitioned_minimum_[index(
					group, suffix, weight, state
				)];
				if (partitioned >= INFINITY_COST) {
					partitioned_sum = INFINITY_COST;
					break;
				}
				partitioned_sum += partitioned;
			}
			if (partitioned_sum < INFINITY_COST)
				best = std::min(
					best, (partitioned_sum + GROUPS - 1) / GROUPS
				);
		}
		return best;
	}
	int offset(int group, int local_bit) const
	{
		return offsets_[static_cast<std::size_t>(group * BITS + local_bit)];
	}
	std::size_t bytes() const
	{
		return partitioned_minimum_.size() * sizeof(int) +
			coordinates_.size() * sizeof(int) +
			information_costs_.size() * sizeof(int) +
			information_prefix_.size() * sizeof(int) +
			offsets_.size() * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			initial_states_.size() * sizeof(State) +
			terminal_costs_.size() * sizeof(int);
	}
};

template <
	int N,
	int K,
	int O,
	bool COST_PARTITIONED = false,
	bool FINGERPRINT_SYNCHRONIZED = false,
	bool SYNDROME_QUOTIENT = false,
	bool QUERY_LOCAL = false
>
class AdditivePdbCertificationSearch {
	using Pdb = std::conditional_t<
		QUERY_LOCAL,
		AdditiveParityPatternDatabase<N, K, O, true>,
		std::conditional_t<
			SYNDROME_QUOTIENT,
			SyndromeQuotientPatternDatabase<N, K, O>,
			std::conditional_t<
				FINGERPRINT_SYNCHRONIZED,
				FingerprintSynchronizedParityPatternDatabase<N, K, O>,
				std::conditional_t<
					COST_PARTITIONED,
					CostPartitionedAdditiveParityPatternDatabase<N, K, O>,
					AdditiveParityPatternDatabase<N, K, O>
				>
			>
		>
	>;
	using State = typename Pdb::State;
	static constexpr int W = Pdb::W;
	static constexpr int GROUPS = Pdb::GROUPS;
	static constexpr int BITS = Pdb::BITS;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const Pdb &pdb;
	std::array<State, GROUPS> states{};
	std::array<uint16_t, O> selected{};
	std::array<uint16_t, O> seed_selected{};
	uint16_t seed_weight = 0;
	int absolute_metric = 0;
	int information_cost = 0;
	int parity_cost = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t considered_patterns = 1;
	uint64_t pruned_patterns = 0;
	uint64_t bound_checks = 0;

	bool is_seed_pattern(int weight) const
	{
		if (weight != seed_weight)
			return false;
		return std::equal(
			selected.begin(), selected.begin() + weight,
			seed_selected.begin()
		);
	}

	void copy_candidate(int weight)
	{
		std::copy(base, base + W, output);
		for (int index = 0; index < weight; ++index)
			output[pdb.coordinate(
				selected[static_cast<std::size_t>(index)]
			)] ^= int8_t{1};
		for (int group = 0; group < GROUPS; ++group) {
			const State state = states[static_cast<std::size_t>(group)];
			for (int bit = 0; bit < BITS; ++bit) {
				const int offset = pdb.offset(group, bit);
				if (offset >= 0)
					output[K + offset] = static_cast<int8_t>(
						(soft[K + offset] < 0) ^ ((state >> bit) & 1)
					);
			}
		}
		for (int index = N; index < W; ++index)
			output[index] = 0;
	}

	void flip(int rank)
	{
		information_cost += pdb.information_cost(rank);
		for (int group = 0; group < GROUPS; ++group) {
			State &state = states[static_cast<std::size_t>(group)];
			parity_cost -= pdb.state_cost(group, state);
			state ^= pdb.row_mask(group, rank);
			parity_cost += pdb.state_cost(group, state);
		}
	}

	void unflip(int rank)
	{
		for (int group = 0; group < GROUPS; ++group) {
			State &state = states[static_cast<std::size_t>(group)];
			parity_cost -= pdb.state_cost(group, state);
			state ^= pdb.row_mask(group, rank);
			parity_cost += pdb.state_cost(group, state);
		}
		information_cost -= pdb.information_cost(rank);
	}

	void evaluate(int weight)
	{
		++evaluated_candidates;
		if (is_seed_pattern(weight))
			return;
		const int distance = information_cost + parity_cost;
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			copy_candidate(weight);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		for (int rank = start; rank < K; ++rank) {
			if (information_cost + pdb.information_cost(rank) > best_distance)
				break;
			flip(rank);
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			++considered_patterns;
			evaluate(DEPTH + 1);
			if constexpr (DEPTH + 1 < O) {
				const int suffix = rank + 1;
				constexpr int budget = O - (DEPTH + 1);
				if (suffix < K) {
					++bound_checks;
					const int lower_bound = pdb.descendant_lower_bound(
						suffix, budget, information_cost, states
					);
					if (lower_bound <= best_distance) {
						traverse<DEPTH + 1>(suffix);
					} else {
						pruned_patterns += descendant_count(
							K, suffix, budget
						);
					}
				}
			}
			unflip(rank);
		}
	}

public:
	AdditivePdbCertificationSearch(
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		const Pdb &input_pdb,
		int seed_best_metric,
		int seed_next_metric
	):
		base(input_base),
		output(input_output),
		soft(input_soft),
		pdb(input_pdb),
		states(input_pdb.initial_states())
	{
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		best_distance = (absolute_metric - seed_best_metric) / 2;
		next_distance = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric - seed_next_metric) / 2;
		parity_cost = pdb.parity_cost(states);
		for (int rank = 0; rank < K; ++rank) {
			const int row = pdb.coordinate(rank);
			if (base[row] != output[row]) {
				assert(seed_weight < O);
				seed_selected[static_cast<std::size_t>(seed_weight++)] =
					static_cast<uint16_t>(rank);
			}
		}
	}

	void run()
	{
		traverse();
		assert(information_cost == 0);
		assert(states == pdb.initial_states());
		assert(parity_cost == pdb.parity_cost(states));
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}
	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t pruned() const { return pruned_patterns; }
	uint64_t checks() const { return bound_checks; }
};

// The CAP-PDB abstractions are five-bit states, but storing one uint16_t per
// group makes every DFS edge execute a group loop and two terminal-cost
// updates.  This certificate state packs each group into one byte and updates
// the complete parity abstraction with a few word XORs.  Exact parity cost is
// reconstructed only at visited candidates; subtree bounds read the same
// packed state.  The bound and traversal order are otherwise identical.
template <int K>
struct SelectiveCouplingOracleSchedule {
	static constexpr std::size_t KEYS =
		static_cast<std::size_t>(K) +
		static_cast<std::size_t>(K) * K +
		static_cast<std::size_t>(K) * K * K;
	std::array<uint8_t, KEYS> prune{};
};

template <int K>
std::vector<SelectiveCouplingOracleSchedule<K>> &selective_oracle_schedules()
{
	static std::vector<SelectiveCouplingOracleSchedule<K>> schedules;
	return schedules;
}

template <int K, int O>
struct PerfectSubtreeOracleSchedule {
	static_assert(O > 0 && O <= 4);
	static constexpr std::size_t KEYS =
		static_cast<std::size_t>(K) +
		static_cast<std::size_t>(K) * K +
		static_cast<std::size_t>(K) * K * K;
	static constexpr int INFINITY_COST = 1000000000;
	std::array<int, KEYS> minimum{};
	std::vector<int> leaf_minimum;
	std::array<uint16_t, O> best_ranks{};
	int best_weight = 0;
	int best_distance = INFINITY_COST;

	PerfectSubtreeOracleSchedule():
		leaf_minimum(
			O == 4 ? static_cast<std::size_t>(combinations(K, 4)) : 0,
			INFINITY_COST
		)
	{
		minimum.fill(INFINITY_COST);
	}

	static std::size_t key(
		const std::array<uint16_t, O> &ranks,
		int weight
	)
	{
		assert(weight >= 1 && weight <= std::min(O, 3));
		if (weight == 1)
			return ranks[0];
		if (weight == 2)
			return static_cast<std::size_t>(K) +
				static_cast<std::size_t>(ranks[0]) * K + ranks[1];
		return static_cast<std::size_t>(K) +
			static_cast<std::size_t>(K) * K +
			static_cast<std::size_t>(ranks[0]) * K * K +
			static_cast<std::size_t>(ranks[1]) * K + ranks[2];
	}

	static std::size_t leaf_key(const std::array<uint16_t, O> &ranks)
	{
		static_assert(O == 4);
		return static_cast<std::size_t>(
			combinations(ranks[0], 1) +
			combinations(ranks[1], 2) +
			combinations(ranks[2], 3) +
			combinations(ranks[3], 4)
		);
	}

	void update(
		const std::array<uint16_t, O> &ranks,
		int weight,
		int distance
	)
	{
		if constexpr (O == 4) {
			if (weight == 4) {
				const std::size_t index = leaf_key(ranks);
				leaf_minimum[index] = std::min(
					leaf_minimum[index], distance
				);
				return;
			}
		}
		const std::size_t index = key(ranks, weight);
		minimum[index] = std::min(minimum[index], distance);
	}

	int value(
		const std::array<uint16_t, O> &ranks,
		int weight
	) const
	{
		if constexpr (O == 4)
			if (weight == 4)
				return leaf_minimum[leaf_key(ranks)];
		return minimum[key(ranks, weight)];
	}
};

template <int K, int O>
std::vector<PerfectSubtreeOracleSchedule<K, O>> &perfect_oracle_schedules()
{
	static std::vector<PerfectSubtreeOracleSchedule<K, O>> schedules;
	return schedules;
}

// Offline exhaustive constructor for the impossible perfect-subtree oracle.
// Every prefix stores the exact minimum distance of any fixed-order OSD TEP
// beginning with that prefix, including the prefix candidate itself.  The
// table is constructed before timing and is never claimed as a deployable
// decoder data structure.
template <int N, int K, int O>
class PerfectSubtreeOracleBuilder {
	using Pdb = AdditiveParityPatternDatabase<
		N, K, O, false, true, false, 2
	>;
	using ExactStates = typename Pdb::ExactStates;
	static constexpr int EXACT_WORDS = Pdb::EXACT_WORDS;
	const Pdb &pdb;
	PerfectSubtreeOracleSchedule<K, O> &schedule;
	ExactStates exact_states{};
	std::array<uint16_t, O> selected{};
	int information_cost = 0;

	void record(int weight)
	{
		const int distance = information_cost +
			pdb.exact_parity_cost(exact_states);
		for (int prefix = 1; prefix <= weight; ++prefix) {
			schedule.update(selected, prefix, distance);
		}
		if (distance < schedule.best_distance) {
			schedule.best_distance = distance;
			schedule.best_weight = weight;
			schedule.best_ranks = selected;
		}
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		for (int rank = start; rank < K; ++rank) {
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			information_cost += pdb.information_cost(rank);
			for (int word = 0; word < EXACT_WORDS; ++word)
				exact_states[static_cast<std::size_t>(word)] ^=
					pdb.exact_row_mask(rank, word);
			record(DEPTH + 1);
			if constexpr (DEPTH + 1 < O)
				traverse<DEPTH + 1>(rank + 1);
			for (int word = 0; word < EXACT_WORDS; ++word)
				exact_states[static_cast<std::size_t>(word)] ^=
					pdb.exact_row_mask(rank, word);
			information_cost -= pdb.information_cost(rank);
		}
	}

public:
	PerfectSubtreeOracleBuilder(
		const Pdb &input_pdb,
		PerfectSubtreeOracleSchedule<K, O> &input_schedule
	):
		pdb(input_pdb),
		schedule(input_schedule),
		exact_states(input_pdb.initial_exact_states())
	{}

	void run()
	{
		schedule = {};
		schedule.best_distance = pdb.exact_parity_cost(exact_states);
		schedule.best_weight = 0;
		traverse();
		assert(information_cost == 0);
		assert(exact_states == pdb.initial_exact_states());
		assert((schedule.best_distance <
			PerfectSubtreeOracleSchedule<K, O>::INFINITY_COST));
		if constexpr (O == 4)
			for (const int value: schedule.leaf_minimum)
				assert((value <
					PerfectSubtreeOracleSchedule<K, O>::INFINITY_COST));
	}
};

// Timed replay using the exact offline subtree minima.  The lookup occurs
// before flipping or scoring the prefix candidate, so an impossible branch
// removes the entire subtree at the earliest point any bound could do so.
template <int N, int K, int O>
class PerfectSubtreeOracleCertificationSearch {
	using Pdb = AdditiveParityPatternDatabase<
		N, K, O, false, true, false, 2
	>;
	using ExactStates = typename Pdb::ExactStates;
	static constexpr int W = Pdb::W;
	static constexpr int EXACT_WORDS = Pdb::EXACT_WORDS;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const Pdb &pdb;
	const PerfectSubtreeOracleSchedule<K, O> &schedule;
	ExactStates exact_states{};
	std::array<uint16_t, O> selected{};
	std::array<uint16_t, O> seed_selected{};
	int seed_weight = 0;
	int absolute_metric = 0;
	int information_cost = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t considered_patterns = 1;
	uint64_t pruned_patterns = 0;
	uint64_t pruned_subtrees = 0;
	uint64_t lookups = 0;

	bool is_seed_pattern(int weight) const
	{
		return weight == seed_weight && std::equal(
			selected.begin(), selected.begin() + weight,
			seed_selected.begin()
		);
	}

	void flip(int rank)
	{
		information_cost += pdb.information_cost(rank);
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_states[static_cast<std::size_t>(word)] ^=
				pdb.exact_row_mask(rank, word);
	}

	void unflip(int rank)
	{
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_states[static_cast<std::size_t>(word)] ^=
				pdb.exact_row_mask(rank, word);
		information_cost -= pdb.information_cost(rank);
	}

	void evaluate(int weight)
	{
		if (is_seed_pattern(weight))
			return;
		++evaluated_candidates;
		const int distance = information_cost +
			pdb.exact_parity_cost(exact_states);
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			pdb.materialize_pattern(
				base, soft, selected, weight, output
			);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		for (int rank = start; rank < K; ++rank) {
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			++lookups;
			uint64_t subtree = 1;
			if constexpr (DEPTH + 1 < O)
				if (rank + 1 < K)
					subtree += descendant_count(
						K, rank + 1, O - (DEPTH + 1)
					);
			if (schedule.value(selected, DEPTH + 1) > best_distance) {
				pruned_patterns += subtree;
				++pruned_subtrees;
				continue;
			}
			flip(rank);
			++considered_patterns;
			evaluate(DEPTH + 1);
			if constexpr (DEPTH + 1 < O)
				traverse<DEPTH + 1>(rank + 1);
			unflip(rank);
		}
	}

public:
	PerfectSubtreeOracleCertificationSearch(
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		const Pdb &input_pdb,
		const PerfectSubtreeOracleSchedule<K, O> &input_schedule,
		int seed_best_metric,
		int seed_next_metric
	):
		base(input_base),
		output(input_output),
		soft(input_soft),
		pdb(input_pdb),
		schedule(input_schedule),
		exact_states(input_pdb.initial_exact_states())
	{
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		best_distance = (absolute_metric - seed_best_metric) / 2;
		next_distance = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric - seed_next_metric) / 2;
		for (int rank = 0; rank < K; ++rank) {
			const int row = pdb.coordinate(rank);
			if (base[row] != output[row]) {
				assert(seed_weight < O);
				seed_selected[static_cast<std::size_t>(seed_weight++)] =
					static_cast<uint16_t>(rank);
			}
		}
	}

	void run()
	{
		traverse();
		assert(information_cost == 0);
		assert(exact_states == pdb.initial_exact_states());
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}
	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t pruned() const { return pruned_patterns; }
	uint64_t subtrees() const { return pruned_subtrees; }
	uint64_t checks() const { return lookups; }
};

template <
	int N, int K, int O,
	bool ENABLE_CONFLICT_LEARNING = false,
	bool FUSED_SUPPORT = (PDB_GROUPING == 3),
	int FUSED_GROUP_LIMIT = 2,
	bool DISCOVER_SELECTIVE_ORACLE = false,
	bool REPLAY_SELECTIVE_ORACLE = false
>
class PackedStateAdditivePdbCertificationSearch {
	using Pdb = AdditiveParityPatternDatabase<
		N, K, O, false, true, FUSED_SUPPORT, FUSED_GROUP_LIMIT
	>;
	using State = typename Pdb::State;
	using PackedStates = typename Pdb::PackedStates;
	using ExactStates = typename Pdb::ExactStates;
	static constexpr int W = Pdb::W;
	static constexpr int GROUPS = Pdb::GROUPS;
	static constexpr int BITS = Pdb::BITS;
	static constexpr int PACKED_WORDS = Pdb::PACKED_WORDS;
	static constexpr int EXACT_WORDS = Pdb::EXACT_WORDS;
	static constexpr int EXACT_BYTES = Pdb::EXACT_BYTES;
	static constexpr int CONFLICT_WORDS = (K + 63) / 64;
	using ConflictMask = std::array<uint64_t, CONFLICT_WORDS>;
	struct LearnedConflict {
		ConflictMask mask{};
		uint16_t size = 0;
	};
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const Pdb &pdb;
	PackedStates states{};
	ExactStates exact_states{};
	std::array<uint16_t, O> selected{};
	std::array<uint16_t, O> seed_selected{};
	uint16_t seed_weight = 0;
	int absolute_metric = 0;
	int information_cost = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t considered_patterns = 1;
	uint64_t pruned_patterns = 0;
	uint64_t bound_checks = 0;
	ConflictMask selected_mask{};
	std::array<LearnedConflict, PACKED_PDB_CONFLICT_CAPACITY>
		learned_conflicts{};
	int learned_conflict_count = 0;
	uint64_t learned_conflict_hits = 0;
	uint64_t learned_conflict_derivations = 0;
	SelectiveCouplingOracleSchedule<K> *oracle_schedule = nullptr;
	uint64_t oracle_coupled_prunes = 0;
	uint64_t oracle_coupled_candidates = 0;
	uint64_t oracle_schedule_lookups = 0;

	std::size_t oracle_schedule_key(int weight) const
	{
		static_assert(O <= 4);
		assert(weight >= 1 && weight <= 3);
		if (weight == 1)
			return selected[0];
		if (weight == 2)
			return static_cast<std::size_t>(K) +
				static_cast<std::size_t>(selected[0]) * K + selected[1];
		return static_cast<std::size_t>(K) +
			static_cast<std::size_t>(K) * K +
			static_cast<std::size_t>(selected[0]) * K * K +
			static_cast<std::size_t>(selected[1]) * K + selected[2];
	}

	static bool mask_subset(
		const ConflictMask &subset,
		const ConflictMask &superset
	)
	{
		for (int word = 0; word < CONFLICT_WORDS; ++word)
			if (
				subset[static_cast<std::size_t>(word)] &
				~superset[static_cast<std::size_t>(word)]
			)
				return false;
		return true;
	}

	static void set_mask_rank(ConflictMask &mask, int rank)
	{
		mask[static_cast<std::size_t>(rank / 64)] |=
			uint64_t{1} << (rank % 64);
	}

	static void clear_mask_rank(ConflictMask &mask, int rank)
	{
		mask[static_cast<std::size_t>(rank / 64)] &=
			~(uint64_t{1} << (rank % 64));
	}

	static bool has_mask_rank(const ConflictMask &mask, int rank)
	{
		return (
			mask[static_cast<std::size_t>(rank / 64)] >> (rank % 64)
		) & uint64_t{1};
	}

	bool matches_learned_conflict(int pending_rank = -1)
	{
		if constexpr (!ENABLE_CONFLICT_LEARNING)
			return false;
		for (int index = 0; index < learned_conflict_count; ++index) {
			const LearnedConflict &conflict = learned_conflicts[
				static_cast<std::size_t>(index)
			];
			bool contained = true;
			for (int word = 0; word < CONFLICT_WORDS; ++word) {
				uint64_t current = selected_mask[
					static_cast<std::size_t>(word)
				];
				if (pending_rank >= 0 && pending_rank / 64 == word)
					current |= uint64_t{1} << (pending_rank % 64);
				if (
					conflict.mask[static_cast<std::size_t>(word)] & ~current
				) {
					contained = false;
					break;
				}
			}
			if (contained) {
				++learned_conflict_hits;
				return true;
			}
		}
		return false;
	}

	int core_lower_bound(const ConflictMask &core, int core_size) const
	{
		// Relax all additional flips to the complete rank range [0,K), even
		// allowing a core rank to be selected a second time.  Every valid
		// order-O superset of core is contained in this enlarged set, so the
		// additive PDB minimum cannot exceed any real matching TEP's cost.
		// The deliberately enlarged set can weaken learning but makes every
		// stored no-good an exact, reusable certificate.
		PackedStates core_states = pdb.initial_packed_states();
		int core_information_cost = 0;
		for (int rank = 0; rank < K; ++rank) {
			if (!has_mask_rank(core, rank))
				continue;
			core_information_cost += pdb.information_cost(rank);
			for (int word = 0; word < PACKED_WORDS; ++word)
				core_states[static_cast<std::size_t>(word)] ^=
					pdb.packed_row_mask(rank, word);
		}
		int lower_bound = core_information_cost +
			pdb.parity_cost(core_states);
		const int budget = O - core_size;
		if (budget > 0)
			lower_bound = std::min(
				lower_bound,
				pdb.descendant_lower_bound(
					0, budget, core_information_cost, core_states
				)
			);
		return lower_bound;
	}

	void store_learned_conflict(
		const ConflictMask &core,
		int core_size
	)
	{
		// An existing smaller core already proves everything proved by this one.
		for (int index = 0; index < learned_conflict_count; ++index)
			if (mask_subset(
				learned_conflicts[static_cast<std::size_t>(index)].mask,
				core
			))
				return;

		// Remove supersets made redundant by the new, smaller core.
		int output_index = 0;
		for (int index = 0; index < learned_conflict_count; ++index) {
			const LearnedConflict &existing = learned_conflicts[
				static_cast<std::size_t>(index)
			];
			if (mask_subset(core, existing.mask))
				continue;
			if (output_index != index)
				learned_conflicts[static_cast<std::size_t>(output_index)] =
					existing;
			++output_index;
		}
		learned_conflict_count = output_index;
		if (learned_conflict_count >= PACKED_PDB_CONFLICT_CAPACITY)
			return;
		LearnedConflict &learned = learned_conflicts[
			static_cast<std::size_t>(learned_conflict_count++)
		];
		learned.mask = core;
		learned.size = static_cast<uint16_t>(core_size);
	}

	void learn_conflict(int weight)
	{
		if constexpr (!ENABLE_CONFLICT_LEARNING)
			return;
		if (
			weight <= 1 ||
			learned_conflict_derivations >=
				PACKED_PDB_CONFLICT_DERIVATION_BUDGET
		)
			return;
		++learned_conflict_derivations;
		ConflictMask core = selected_mask;
		int core_size = weight;
		if (core_lower_bound(core, core_size) <= best_distance)
			return;

		// Greedily remove early ranks first.  Retaining later ranks gives the
		// learned core more opportunity to match yet-unvisited DFS branches.
		for (int index = 0; index < weight; ++index) {
			const int rank = selected[static_cast<std::size_t>(index)];
			if (!has_mask_rank(core, rank))
				continue;
			ConflictMask trial = core;
			clear_mask_rank(trial, rank);
			if (core_lower_bound(trial, core_size - 1) > best_distance) {
				core = trial;
				--core_size;
			}
		}

		// The unminimized current prefix cannot recur in a combination tree;
		// only a proper, non-empty core can prune a future branch.
		if (core_size <= 0 || core_size >= weight)
			return;
		store_learned_conflict(core, core_size);
	}

	bool is_seed_pattern(int weight) const
	{
		if (weight != seed_weight)
			return false;
		return std::equal(
			selected.begin(), selected.begin() + weight,
			seed_selected.begin()
		);
	}

	void copy_candidate(int weight, int pending_rank = -1)
	{
		std::copy(base, base + W, output);
		for (int index = 0; index < weight; ++index)
			output[pdb.coordinate(
				selected[static_cast<std::size_t>(index)]
			)] ^= int8_t{1};
		for (int offset = 0; offset < N - K; ++offset)
			output[K + offset] = static_cast<int8_t>(
				(soft[K + offset] < 0) ^
				(((exact_states[static_cast<std::size_t>(offset / 64)] ^
					(pending_rank < 0 ? uint64_t{0} : pdb.exact_row_mask(
						pending_rank, offset / 64
					))) >>
					(offset % 64)) & uint64_t{1})
			);
		for (int index = N; index < W; ++index)
			output[index] = 0;
	}

	void flip(int rank)
	{
		information_cost += pdb.information_cost(rank);
		for (int word = 0; word < PACKED_WORDS; ++word)
			states[static_cast<std::size_t>(word)] ^=
				pdb.packed_row_mask(rank, word);
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_states[static_cast<std::size_t>(word)] ^=
				pdb.exact_row_mask(rank, word);
	}

	void unflip(int rank)
	{
		for (int word = 0; word < PACKED_WORDS; ++word)
			states[static_cast<std::size_t>(word)] ^=
				pdb.packed_row_mask(rank, word);
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_states[static_cast<std::size_t>(word)] ^=
				pdb.exact_row_mask(rank, word);
		information_cost -= pdb.information_cost(rank);
	}

	int evaluate(int weight)
	{
		++evaluated_candidates;
		const int parity = pdb.exact_parity_cost(exact_states);
		if (is_seed_pattern(weight))
			return parity;
		const int distance = information_cost + parity;
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			copy_candidate(weight);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		return parity;
	}

	void process_deepest_candidate(int rank, int distance)
	{
		selected[static_cast<std::size_t>(O - 1)] =
			static_cast<uint16_t>(rank);
		++considered_patterns;
		if (matches_learned_conflict(rank)) {
			++pruned_patterns;
			return;
		}
		++evaluated_candidates;
		if (is_seed_pattern(O))
			return;
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			copy_candidate(O, rank);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
	}

	int deepest_candidate_distance(int rank) const
	{
		ExactStates candidate = exact_states;
		for (int word = 0; word < EXACT_WORDS; ++word)
			candidate[static_cast<std::size_t>(word)] ^=
				pdb.exact_row_mask(rank, word);
		return information_cost + pdb.information_cost(rank) +
			pdb.exact_parity_cost(candidate);
	}

	void traverse_deepest(int start)
	{
		for (int rank = start; rank < K;) {
			if (
				information_cost + pdb.information_cost(rank) > best_distance
			)
				break;
			#if defined(__AVX512VPOPCNTDQ__) && defined(__AVX512DQ__)
			if (
				EXACT_WORDS == 2 &&
				rank + 4 <= K &&
				information_cost + pdb.information_cost(rank + 3) <=
					best_distance
			) {
				alignas(64) std::array<uint64_t, 4> parity_costs{};
				{
					const int first = rank;
					const __m512i candidates = _mm512_setr_epi64(
						static_cast<long long>(exact_states[0] ^
							pdb.exact_row_mask(first + 0, 0)),
						static_cast<long long>(exact_states[1] ^
							pdb.exact_row_mask(first + 0, 1)),
						static_cast<long long>(exact_states[0] ^
							pdb.exact_row_mask(first + 1, 0)),
						static_cast<long long>(exact_states[1] ^
							pdb.exact_row_mask(first + 1, 1)),
						static_cast<long long>(exact_states[0] ^
							pdb.exact_row_mask(first + 2, 0)),
						static_cast<long long>(exact_states[1] ^
							pdb.exact_row_mask(first + 2, 1)),
						static_cast<long long>(exact_states[0] ^
							pdb.exact_row_mask(first + 3, 0)),
						static_cast<long long>(exact_states[1] ^
							pdb.exact_row_mask(first + 3, 1))
					);
					__m512i sums = _mm512_setzero_si512();
					for (int bit = 0; bit < SOFT_MAGNITUDE_BITS; ++bit) {
						const uint64_t plane0 = pdb.exact_weight_plane(bit, 0);
						const uint64_t plane1 = pdb.exact_weight_plane(bit, 1);
						const __m512i planes = _mm512_setr_epi64(
							static_cast<long long>(plane0),
							static_cast<long long>(plane1),
							static_cast<long long>(plane0),
							static_cast<long long>(plane1),
							static_cast<long long>(plane0),
							static_cast<long long>(plane1),
							static_cast<long long>(plane0),
							static_cast<long long>(plane1)
						);
						const __m512i counts = _mm512_popcnt_epi64(
							_mm512_and_si512(candidates, planes)
						);
						sums = _mm512_add_epi64(
							sums,
							_mm512_mullo_epi64(
								counts, _mm512_set1_epi64(1LL << bit)
							)
						);
					}
					alignas(64) std::array<uint64_t, 8> words{};
					_mm512_store_si512(words.data(), sums);
					for (int lane = 0; lane < 4; ++lane)
						parity_costs[static_cast<std::size_t>(lane)] =
							words[static_cast<std::size_t>(2 * lane)] +
							words[static_cast<std::size_t>(2 * lane + 1)];
				}
				for (int lane = 0; lane < 4; ++lane)
					process_deepest_candidate(
						rank + lane,
						information_cost + pdb.information_cost(rank + lane) +
							static_cast<int>(parity_costs[
								static_cast<std::size_t>(lane)
							])
					);
				rank += 4;
				continue;
			}
			#endif
			process_deepest_candidate(
				rank, deepest_candidate_distance(rank)
			);
			++rank;
		}
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		if constexpr (DEPTH + 1 == O) {
			traverse_deepest(start);
			return;
		}
		for (int rank = start; rank < K; ++rank) {
			if (information_cost + pdb.information_cost(rank) > best_distance)
				break;
			flip(rank);
			selected[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			set_mask_rank(selected_mask, rank);
			++considered_patterns;
			if (matches_learned_conflict()) {
				uint64_t skipped = 1;
				if constexpr (DEPTH + 1 < O) {
					const int suffix = rank + 1;
					constexpr int budget = O - (DEPTH + 1);
					if (suffix < K)
						skipped += descendant_count(K, suffix, budget);
				}
				pruned_patterns += skipped;
				clear_mask_rank(selected_mask, rank);
				unflip(rank);
				continue;
			}
			const int current_parity_cost = evaluate(DEPTH + 1);
			if constexpr (DEPTH + 1 < O) {
				const int suffix = rank + 1;
				constexpr int budget = O - (DEPTH + 1);
				if (suffix < K) {
					const uint64_t descendants = descendant_count(
						K, suffix, budget
					);
					if constexpr (
						(PACKED_PDB_BOUND_DEPTH_MASK & (1 << DEPTH)) == 0
					) {
						traverse<DEPTH + 1>(suffix);
					} else if (descendants < PACKED_PDB_MIN_DESCENDANTS) {
						traverse<DEPTH + 1>(suffix);
					} else {
						++bound_checks;
						bool prune = false;
						if constexpr (PACKED_PDB_TRIANGLE_PREFILTER) {
							const int triangle =
								pdb.descendant_effect_norm_lower_bound(
									suffix, budget, information_cost,
									current_parity_cost, exact_states
								);
							prune = triangle > best_distance;
						}
						if (!prune) {
							if constexpr (
								DISCOVER_SELECTIVE_ORACLE ||
								REPLAY_SELECTIVE_ORACLE
							) {
								const int independent = pdb.descendant_lower_bound(
									suffix, budget, information_cost, states, false
								);
								prune = independent > best_distance;
								if (!prune) {
									++oracle_schedule_lookups;
									const std::size_t key = oracle_schedule_key(
										DEPTH + 1
									);
									if constexpr (DISCOVER_SELECTIVE_ORACLE) {
										const int coupled = pdb.descendant_lower_bound(
											suffix, budget, information_cost,
											states, true
										);
										if (coupled > best_distance) {
											oracle_schedule->prune[key] = 1;
											prune = true;
										}
									} else {
										prune = oracle_schedule->prune[key] != 0;
									}
									if (prune) {
										++oracle_coupled_prunes;
										oracle_coupled_candidates += descendants;
									}
								}
							} else {
								const int lower_bound = pdb.descendant_lower_bound(
									suffix, budget, information_cost, states
								);
								prune = lower_bound > best_distance;
							}
						}
						if (!prune) {
							traverse<DEPTH + 1>(suffix);
						} else {
							pruned_patterns += descendants;
							learn_conflict(DEPTH + 1);
						}
					}
				}
			}
			clear_mask_rank(selected_mask, rank);
			unflip(rank);
		}
	}

public:
	PackedStateAdditivePdbCertificationSearch(
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		const Pdb &input_pdb,
		int seed_best_metric,
		int seed_next_metric,
		SelectiveCouplingOracleSchedule<K> *input_oracle_schedule = nullptr
	):
		base(input_base),
		output(input_output),
		soft(input_soft),
		pdb(input_pdb),
		states(input_pdb.initial_packed_states()),
		exact_states(input_pdb.initial_exact_states()),
		oracle_schedule(input_oracle_schedule)
	{
		static_assert(
			!(DISCOVER_SELECTIVE_ORACLE && REPLAY_SELECTIVE_ORACLE)
		);
		static_assert(!DISCOVER_SELECTIVE_ORACLE || FUSED_SUPPORT);
		static_assert(!REPLAY_SELECTIVE_ORACLE || !FUSED_SUPPORT);
		if constexpr (
			DISCOVER_SELECTIVE_ORACLE || REPLAY_SELECTIVE_ORACLE
		)
			assert(oracle_schedule != nullptr);
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		best_distance = (absolute_metric - seed_best_metric) / 2;
		next_distance = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric - seed_next_metric) / 2;
		for (int rank = 0; rank < K; ++rank) {
			const int row = pdb.coordinate(rank);
			if (base[row] != output[row]) {
				assert(seed_weight < O);
				seed_selected[static_cast<std::size_t>(seed_weight++)] =
					static_cast<uint16_t>(rank);
			}
		}
	}

	void run()
	{
		traverse();
		assert(information_cost == 0);
		assert(states == pdb.initial_packed_states());
		assert(exact_states == pdb.initial_exact_states());
		for (uint64_t word : selected_mask)
			assert(word == 0);
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}
	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t pruned() const { return pruned_patterns; }
	uint64_t checks() const
	{
		return bound_checks + learned_conflict_derivations;
	}
	uint64_t conflict_hits() const { return learned_conflict_hits; }
	uint64_t conflict_derivations() const
	{
		return learned_conflict_derivations;
	}
	uint64_t conflicts() const
	{
		return static_cast<uint64_t>(learned_conflict_count);
	}
	uint64_t oracle_prunes() const { return oracle_coupled_prunes; }
	uint64_t oracle_candidates() const { return oracle_coupled_candidates; }
	uint64_t oracle_lookups() const { return oracle_schedule_lookups; }
};

// Reconstructed 2026-07-29 compact two-block and four-update dual bounds.
//
// The compact table stores one reachable-effect bitset per
// (group,suffix,weight).  The information ranks are split at 75% of K.
// At a query, every parity group must use the same allocation of the remaining
// flips between the two rank blocks.  Within each group, reachable first-block
// effects are convolved with the ordinary suffix PDB for the second block.
// This is stronger than synchronising only the total cardinality, while the
// groups may still choose different rows and hence the result is admissible.
//
// The dual query is the row-consensus relaxation from the preserved July-24
// prototype, applied at an arbitrary DFS subtree.  Group objectives are scaled
// by GROUPS.  Row multipliers always sum to GROUPS times the information cost;
// the subgradient update has zero sum across groups.  Therefore every retained
// dual value is an admissible lower bound.  Four updates reproduce the frozen
// July-29 configuration.
#ifndef OSD_RECONSTRUCTED_DUAL_UPDATES
#define OSD_RECONSTRUCTED_DUAL_UPDATES 4
#endif
#ifndef OSD_RECONSTRUCTED_DUAL_SPARSE_KERNEL
#define OSD_RECONSTRUCTED_DUAL_SPARSE_KERNEL 1
#endif
template <int N, int K, int O>
class ReconstructedTwoBlockPatternDatabase:
	public AdditiveParityPatternDatabase<N, K, O, false, true>
{
	using Base = AdditiveParityPatternDatabase<N, K, O, false, true>;

public:
	using State = typename Base::State;
	using PackedStates = typename Base::PackedStates;
	using ExactStates = typename Base::ExactStates;
	static constexpr int W = Base::W;
	static constexpr int GROUPS = Base::BASE_GROUPS;
	static constexpr int STATES = static_cast<int>(Base::STATES);
	static constexpr int PACKED_WORDS = Base::PACKED_WORDS;
	static constexpr int EXACT_WORDS = Base::EXACT_WORDS;
	static constexpr int SPLIT = (3 * K + 3) / 4;
	static constexpr int DUAL_UPDATES = OSD_RECONSTRUCTED_DUAL_UPDATES;
	static_assert(DUAL_UPDATES >= 1);
	static constexpr int DUAL_MAX_STEP = 64;
	static constexpr int INFINITY_COST = Base::INFINITY_COST;

private:
	std::vector<uint64_t> reachable_;
	mutable std::vector<int> dual_dp_;
	mutable std::vector<int> compiled_dual_;
	mutable uint64_t compiled_build_transitions_ = 0;
	mutable std::array<uint64_t, 5> compact_parallel_cycles_{};
	mutable uint64_t compact_parallel_allocations_ = 0;

	static constexpr int COMPILED_SCALES = 2;
	static constexpr int COMPILED_BANKS = COMPILED_SCALES * GROUPS;
	static constexpr std::array<int, COMPILED_SCALES> COMPILED_SCALE{
		16, 64
	};

	static std::size_t reachable_index(int group, int suffix, int weight)
	{
		return (
			(static_cast<std::size_t>(group) * (K + 1) + suffix) *
			(O + 1)
		) + weight;
	}

	static std::size_t dual_index(int suffix, int weight, int state)
	{
		return static_cast<std::size_t>(
			(suffix * (O + 1) + weight) * STATES + state
		);
	}

	static std::size_t compiled_index(
		int bank, int group, int suffix, int weight, int state
	)
	{
		return (
			(
				(
					(static_cast<std::size_t>(bank) * GROUPS + group) *
					(K + 1) + suffix
				) * (O + 1) + weight
			) * STATES
		) + state;
	}

	void ensure_compiled_dual() const
	{
		if (!compiled_dual_.empty())
			return;
		compiled_dual_.assign(
			static_cast<std::size_t>(COMPILED_BANKS) * GROUPS *
				(K + 1) * (O + 1) * STATES,
			INFINITY_COST
		);
		for (int bank = 0; bank < COMPILED_BANKS; ++bank) {
			const int focus = bank % GROUPS;
			const int scale = COMPILED_SCALE[
				static_cast<std::size_t>(bank / GROUPS)
			];
			for (int group = 0; group < GROUPS; ++group) {
				for (int state = 0; state < STATES; ++state)
					compiled_dual_[compiled_index(
						bank, group, K, 0, state
					)] = GROUPS * this->state_cost(
						group, static_cast<State>(state)
					);
				for (int suffix = K - 1; suffix >= 0; --suffix) {
					const State row = this->row_mask(group, suffix);
					const int direction = group == focus
						? GROUPS - 1 : -1;
					const int multiplier =
						this->information_cost(suffix) + scale * direction;
					for (int weight = 0; weight <= O; ++weight) {
						for (int state = 0; state < STATES; ++state) {
							int value = compiled_dual_[compiled_index(
								bank, group, suffix + 1, weight, state
							)];
							if (weight) {
								const int child = compiled_dual_[compiled_index(
									bank, group, suffix + 1, weight - 1,
									state ^ row
								)];
								if (child < INFINITY_COST)
									value = std::min(
										value, multiplier + child
									);
							}
							compiled_dual_[compiled_index(
								bank, group, suffix, weight, state
							)] = value;
							++compiled_build_transitions_;
						}
					}
				}
			}
		}
	}

	int information_range_minimum(int begin, int end, int weight) const
	{
		if (weight < 0 || begin + weight > end)
			return INFINITY_COST;
		int value = 0;
		for (int index = 0; index < weight; ++index)
			value += this->information_cost(begin + index);
		return value;
	}

	static int ceil_divide(int value, int denominator)
	{
		return value >= 0
			? (value + denominator - 1) / denominator
			: value / denominator;
	}

	static uint64_t binomial(int n, int k)
	{
		if (k < 0 || k > n)
			return 0;
		k = std::min(k, n - k);
		uint64_t value = 1;
		for (int index = 1; index <= k; ++index)
			value = value * static_cast<uint64_t>(n - k + index) /
				static_cast<uint64_t>(index);
		return value;
	}

	std::pair<int, uint64_t> solve_dual_group_sparse(
		int group,
		int suffix,
		int target_weight,
		State initial_state,
		const std::vector<int> &lambda,
		uint64_t &operations
	) const
	{
		int best = INFINITY_COST;
		uint64_t best_selected = 0;
		auto enumerate = [&](auto &&self, int next, int remaining,
			State state, int value, uint64_t selected) -> void {
			if (!remaining) {
				value += GROUPS * this->state_cost(group, state);
				++operations;
				if (value < best) {
					best = value;
					best_selected = selected;
				}
				return;
			}
			for (int rank = next; rank + remaining <= K; ++rank) {
				++operations;
				self(
					self, rank + 1, remaining - 1,
					static_cast<State>(state ^ this->row_mask(group, rank)),
					value + lambda[
						static_cast<std::size_t>(group * K + rank)
					],
					selected | (uint64_t{1} << rank)
				);
			}
		};
		enumerate(
			enumerate, suffix, target_weight, initial_state, 0, uint64_t{0}
		);
		assert(best < INFINITY_COST);
		return {best, best_selected};
	}

	std::pair<int, uint64_t> solve_dual_group(
		int group,
		int suffix,
		int target_weight,
		State initial_state,
		const std::vector<int> &lambda,
		uint64_t &dp_transitions
	) const
	{
		if constexpr (OSD_RECONSTRUCTED_DUAL_SPARSE_KERNEL) {
			const int remaining = K - suffix;
			const uint64_t combinations = binomial(
				remaining, target_weight
			);
			const uint64_t dense_cells =
				static_cast<uint64_t>(remaining) *
				static_cast<uint64_t>(target_weight + 1) * STATES;
			if (combinations <= dense_cells)
				return solve_dual_group_sparse(
					group, suffix, target_weight, initial_state,
					lambda, dp_transitions
				);
		}
		std::fill(dual_dp_.begin(), dual_dp_.end(), INFINITY_COST);
		for (int state = 0; state < STATES; ++state) {
			dual_dp_[dual_index(K, 0, state)] =
				GROUPS * this->state_cost(group, static_cast<State>(state));
		}
		for (int rank = K - 1; rank >= suffix; --rank) {
			const State row = this->row_mask(group, rank);
			for (int weight = 0; weight <= target_weight; ++weight) {
				#if defined(__AVX2__)
				if constexpr (STATES == 32) {
					const int low_mask = row & 7;
					const int high_mask = row >> 3;
					const __m256i permutation = _mm256_setr_epi32(
						0 ^ low_mask, 1 ^ low_mask, 2 ^ low_mask,
						3 ^ low_mask, 4 ^ low_mask, 5 ^ low_mask,
						6 ^ low_mask, 7 ^ low_mask
					);
					const __m256i transition = _mm256_set1_epi32(
						lambda[static_cast<std::size_t>(group * K + rank)]
					);
					for (int block = 0; block < 4; ++block) {
						const int *skip_pointer = dual_dp_.data() + dual_index(
							rank + 1, weight, block * 8
						);
						__m256i value = _mm256_loadu_si256(
							reinterpret_cast<const __m256i *>(skip_pointer)
						);
						if (weight) {
							const int *take_pointer =
								dual_dp_.data() + dual_index(
									rank + 1, weight - 1,
									(block ^ high_mask) * 8
								);
							const __m256i source = _mm256_loadu_si256(
								reinterpret_cast<const __m256i *>(take_pointer)
							);
							const __m256i take = _mm256_add_epi32(
								_mm256_permutevar8x32_epi32(
									source, permutation
								),
								transition
							);
							value = _mm256_min_epi32(value, take);
						}
						_mm256_storeu_si256(
							reinterpret_cast<__m256i *>(
								dual_dp_.data() + dual_index(
									rank, weight, block * 8
								)
							),
							value
						);
					}
					dp_transitions += STATES;
					continue;
				}
				#endif
				for (int state = 0; state < STATES; ++state) {
					int value = dual_dp_[dual_index(
						rank + 1, weight, state
					)];
					if (weight) {
						const int child = dual_dp_[dual_index(
							rank + 1, weight - 1, state ^ row
						)];
						if (child < INFINITY_COST)
							value = std::min(
								value,
								lambda[static_cast<std::size_t>(
									group * K + rank
								)] + child
							);
					}
					dual_dp_[dual_index(rank, weight, state)] = value;
					++dp_transitions;
				}
			}
		}

		uint64_t selected = 0;
		State state = initial_state;
		int weight = target_weight;
		for (int rank = suffix; rank < K && weight; ++rank) {
			const State row = this->row_mask(group, rank);
			const int skip = dual_dp_[dual_index(
				rank + 1, weight, state
			)];
			const int child = dual_dp_[dual_index(
				rank + 1, weight - 1, state ^ row
			)];
			const int take = child >= INFINITY_COST
				? INFINITY_COST
				: lambda[static_cast<std::size_t>(group * K + rank)] +
					child;
			if (take <= skip) {
				selected |= uint64_t{1} << rank;
				state ^= row;
				--weight;
			}
		}
		assert(weight == 0);
		return {
			dual_dp_[dual_index(suffix, target_weight, initial_state)],
			selected
		};
	}

	int exact_completion_cost(
		uint64_t future,
		int current_information_cost,
		const ExactStates &current_exact
	) const
	{
		ExactStates exact = current_exact;
		int value = current_information_cost;
		while (future) {
			const int rank = __builtin_ctzll(future);
			future &= future - 1;
			value += this->information_cost(rank);
			for (int word = 0; word < EXACT_WORDS; ++word)
				exact[static_cast<std::size_t>(word)] ^=
					this->exact_row_mask(rank, word);
		}
		return value + this->exact_parity_cost(exact);
	}

public:
	ReconstructedTwoBlockPatternDatabase(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		Base(matrix, base, soft),
		reachable_(
			static_cast<std::size_t>(GROUPS) * (K + 1) * (O + 1)
		),
		dual_dp_(
			static_cast<std::size_t>(K + 1) * (O + 1) * STATES,
			INFINITY_COST
		)
	{
		static_assert(K <= 64);
		static_assert(STATES <= 64);
		for (int group = 0; group < GROUPS; ++group) {
			for (int suffix = SPLIT; suffix <= K; ++suffix)
				reachable_[reachable_index(group, suffix, 0)] = 1;
			for (int suffix = SPLIT - 1; suffix >= 0; --suffix) {
				const State row = this->row_mask(group, suffix);
				reachable_[reachable_index(group, suffix, 0)] = 1;
				for (int weight = 1; weight <= O; ++weight) {
					const uint64_t skip = reachable_[reachable_index(
						group, suffix + 1, weight
					)];
					const uint64_t source = reachable_[reachable_index(
						group, suffix + 1, weight - 1
					)];
					uint64_t take = 0;
					uint64_t effects = source;
					while (effects) {
						const int effect = __builtin_ctzll(effects);
						effects &= effects - 1;
						take |= uint64_t{1} << (effect ^ row);
					}
					reachable_[reachable_index(group, suffix, weight)] =
						skip | take;
				}
			}
		}
	}

	int normal_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const PackedStates &states,
		uint64_t &table_lookups
	) const
	{
		int best = INFINITY_COST;
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			int value = current_information_cost +
				this->minimum_information_cost(suffix, weight);
			for (int group = 0; group < GROUPS; ++group) {
				++table_lookups;
				value += this->minimum(
					group, suffix, weight,
					this->packed_group_state(states, group)
				);
			}
			best = std::min(best, value);
		}
		return best;
	}

	int compact_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const PackedStates &states,
		uint64_t &table_lookups
	) const
	{
		if (suffix >= SPLIT) {
			const int weights = std::min(budget, K - suffix);
			// The existing normal CAP engine requires one issue and one
			// accumulate cycle per residual cardinality.
			for (auto &cycles : compact_parallel_cycles_)
				cycles += static_cast<uint64_t>(2 * weights);
			return normal_lower_bound(
				suffix, budget, current_information_cost, states,
				table_lookups
			);
		}

		int best = INFINITY_COST;
		static constexpr std::array<int, 5> LANES{1, 2, 4, 8, 16};
		for (int weight = 1; weight <= budget && suffix + weight <= K;
			++weight) {
			for (int left = 0; left <= weight; ++left) {
				const int right = weight - left;
				const int left_information = information_range_minimum(
					suffix, SPLIT, left
				);
				const int right_information = information_range_minimum(
					SPLIT, K, right
				);
				if (
					left_information >= INFINITY_COST ||
					right_information >= INFINITY_COST
				)
					continue;
				int value = current_information_cost +
					left_information + right_information;
				bool valid = true;
				int maximum_population = 0;
				for (int group = 0; group < GROUPS; ++group) {
					uint64_t effects = reachable_[reachable_index(
						group, suffix, left
					)];
					maximum_population = std::max(
						maximum_population, __builtin_popcountll(effects)
					);
					int group_minimum = INFINITY_COST;
					const State current =
						this->packed_group_state(states, group);
					while (effects) {
						const int effect = __builtin_ctzll(effects);
						effects &= effects - 1;
						++table_lookups;
						group_minimum = std::min(
							group_minimum,
							this->minimum(
								group, SPLIT, right,
								static_cast<State>(current ^ effect)
							)
						);
					}
					if (group_minimum >= INFINITY_COST) {
						valid = false;
						break;
					}
					value += group_minimum;
				}
				if (valid) {
					++compact_parallel_allocations_;
					for (std::size_t lane_index = 0;
						lane_index < LANES.size(); ++lane_index) {
						// One synchronous state-wide table read followed by
						// ceil(maximum reachable population / lanes) scan
						// cycles.  The 13 parity groups operate in parallel.
						compact_parallel_cycles_[lane_index] +=
							1 + static_cast<uint64_t>(
								(maximum_population +
								 LANES[lane_index] - 1) /
								LANES[lane_index]
							);
					}
					best = std::min(best, value);
				}
			}
		}
		return best;
	}

	int dual_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const PackedStates &states,
		const ExactStates &current_exact,
		uint64_t &dp_transitions
	) const
	{
		int overall = INFINITY_COST;
		for (int target = 1; target <= budget && suffix + target <= K;
			++target) {
			std::vector<int> lambda(
				static_cast<std::size_t>(GROUPS * K)
			);
			for (int group = 0; group < GROUPS; ++group)
				for (int rank = suffix; rank < K; ++rank)
					lambda[static_cast<std::size_t>(group * K + rank)] =
						this->information_cost(rank);

			int best_target = -INFINITY_COST;
			for (int update = 0; update < DUAL_UPDATES; ++update) {
				std::array<uint64_t, GROUPS> selections{};
				int dual = GROUPS * current_information_cost;
				int primal = INFINITY_COST;
				for (int group = 0; group < GROUPS; ++group) {
					const auto solution = solve_dual_group(
						group, suffix, target,
						this->packed_group_state(states, group),
						lambda, dp_transitions
					);
					dual += solution.first;
					selections[static_cast<std::size_t>(group)] =
						solution.second;
					primal = std::min(
						primal,
						exact_completion_cost(
							solution.second, current_information_cost,
							current_exact
						)
					);
				}
				best_target = std::max(
					best_target, ceil_divide(dual, GROUPS)
				);

				std::array<int, K> counts{};
				for (int group = 0; group < GROUPS; ++group) {
					uint64_t selected =
						selections[static_cast<std::size_t>(group)];
					while (selected) {
						const int rank = __builtin_ctzll(selected);
						selected &= selected - 1;
						++counts[static_cast<std::size_t>(rank)];
					}
				}
				long long norm = 0;
				for (int group = 0; group < GROUPS; ++group)
					for (int rank = suffix; rank < K; ++rank) {
						const int chosen =
							(selections[static_cast<std::size_t>(group)] >>
							 rank) & uint64_t{1};
						const int direction =
							GROUPS * chosen -
							counts[static_cast<std::size_t>(rank)];
						norm += static_cast<long long>(direction) * direction;
					}
				if (!norm)
					break;
				const long long gap = std::max<long long>(
					0, static_cast<long long>(GROUPS) * primal - dual
				);
				if (!gap)
					break;
				const int step = static_cast<int>(std::min<long long>(
					DUAL_MAX_STEP,
					std::max<long long>(
						1, 3 * gap * GROUPS / (2 * norm)
					)
				));
				for (int group = 0; group < GROUPS; ++group)
					for (int rank = suffix; rank < K; ++rank) {
						const int chosen =
							(selections[static_cast<std::size_t>(group)] >>
							 rank) & uint64_t{1};
						lambda[static_cast<std::size_t>(group * K + rank)] +=
							step * (
								GROUPS * chosen -
								counts[static_cast<std::size_t>(rank)]
							);
					}
			}
			overall = std::min(overall, best_target);
		}
		return overall;
	}

	int compiled_dual_lower_bound(
		int suffix,
		int budget,
		int current_information_cost,
		const PackedStates &states,
		uint64_t &table_lookups
	) const
	{
		ensure_compiled_dual();
		int overall = INFINITY_COST;
		for (int target = 1;
			target <= budget && suffix + target <= K; ++target) {
			int best_target = -INFINITY_COST;
			for (int bank = 0; bank < COMPILED_BANKS; ++bank) {
				int dual = GROUPS * current_information_cost;
				for (int group = 0; group < GROUPS; ++group) {
					dual += compiled_dual_[compiled_index(
						bank, group, suffix, target,
						this->packed_group_state(states, group)
					)];
					++table_lookups;
				}
				best_target = std::max(
					best_target, ceil_divide(dual, GROUPS)
				);
			}
			overall = std::min(overall, best_target);
		}
		return overall;
	}

	uint64_t compiled_build_transitions() const
	{
		return compiled_build_transitions_;
	}

	const std::array<uint64_t, 5> &compact_parallel_cycles() const
	{
		return compact_parallel_cycles_;
	}

	uint64_t compact_parallel_allocations() const
	{
		return compact_parallel_allocations_;
	}

	std::size_t reconstructed_bytes() const
	{
		return this->bytes() + reachable_.size() * sizeof(uint64_t) +
			compiled_dual_.size() * sizeof(int);
	}
};

template <int N, int K, int O, int BOUND_KIND>
class ReconstructedPdbCertificationSearch {
	using Pdb = ReconstructedTwoBlockPatternDatabase<N, K, O>;
	using PackedStates = typename Pdb::PackedStates;
	using ExactStates = typename Pdb::ExactStates;
	static constexpr int W = Pdb::W;
	static constexpr int PACKED_WORDS = Pdb::PACKED_WORDS;
	static constexpr int EXACT_WORDS = Pdb::EXACT_WORDS;

	const int8_t *base_;
	int8_t *output_;
	const int8_t *soft_;
	const Pdb &pdb_;
	PackedStates states_;
	ExactStates exact_;
	std::array<uint16_t, O> selected_{};
	std::array<uint16_t, O> seed_selected_{};
	int seed_weight_ = 0;
	int absolute_metric_ = 0;
	int information_cost_ = 0;
	int best_distance_;
	int next_distance_;
	uint64_t evaluated_ = 0;
	uint64_t scoring_calls_ = 0;
	uint64_t considered_ = 1;
	uint64_t pruned_ = 0;
	uint64_t checks_ = 0;
	uint64_t pdb_lookups_ = 0;
	uint64_t compact_lookups_ = 0;
	uint64_t dual_queries_ = 0;
	uint64_t dual_transitions_ = 0;
	uint64_t compiled_dual_lookups_ = 0;
	std::vector<uint64_t> evaluated_masks_;

	uint64_t selected_mask(int weight) const
	{
		uint64_t mask = 0;
		for (int index = 0; index < weight; ++index)
			mask |= uint64_t{1} << selected_[static_cast<std::size_t>(index)];
		return mask;
	}

	bool is_seed(int weight) const
	{
		return weight == seed_weight_ && std::equal(
			selected_.begin(), selected_.begin() + weight,
			seed_selected_.begin()
		);
	}

	void copy_candidate(int weight)
	{
		pdb_.materialize_pattern(base_, soft_, selected_, weight, output_);
	}

	void flip(int rank)
	{
		information_cost_ += pdb_.information_cost(rank);
		for (int word = 0; word < PACKED_WORDS; ++word)
			states_[static_cast<std::size_t>(word)] ^=
				pdb_.packed_row_mask(rank, word);
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_[static_cast<std::size_t>(word)] ^=
				pdb_.exact_row_mask(rank, word);
	}

	void unflip(int rank)
	{
		for (int word = 0; word < PACKED_WORDS; ++word)
			states_[static_cast<std::size_t>(word)] ^=
				pdb_.packed_row_mask(rank, word);
		for (int word = 0; word < EXACT_WORDS; ++word)
			exact_[static_cast<std::size_t>(word)] ^=
				pdb_.exact_row_mask(rank, word);
		information_cost_ -= pdb_.information_cost(rank);
	}

	int evaluate(int weight)
	{
		++evaluated_;
		evaluated_masks_.push_back(selected_mask(weight));
		const int parity = pdb_.exact_parity_cost(exact_);
		++scoring_calls_;
		if (is_seed(weight))
			return parity;
		const int distance = information_cost_ + parity;
		if (distance < best_distance_) {
			next_distance_ = best_distance_;
			best_distance_ = distance;
			copy_candidate(weight);
		} else if (distance < next_distance_) {
			next_distance_ = distance;
		}
		return parity;
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		for (int rank = start; rank < K; ++rank) {
			if (
				information_cost_ + pdb_.information_cost(rank) >
				best_distance_
			)
				break;
			flip(rank);
			selected_[static_cast<std::size_t>(DEPTH)] =
				static_cast<uint16_t>(rank);
			++considered_;
			const int current_parity = evaluate(DEPTH + 1);
			if constexpr (DEPTH + 1 < O) {
				const int suffix = rank + 1;
				constexpr int budget = O - (DEPTH + 1);
				if (suffix < K) {
					++checks_;
					int lower = pdb_.descendant_effect_norm_lower_bound(
						suffix, budget, information_cost_, current_parity,
						exact_
					);
					if (lower <= best_distance_) {
						if constexpr (BOUND_KIND == 0) {
							lower = pdb_.normal_lower_bound(
								suffix, budget, information_cost_, states_,
								pdb_lookups_
							);
						} else {
							lower = pdb_.compact_lower_bound(
								suffix, budget, information_cost_, states_,
								compact_lookups_
							);
							if constexpr (BOUND_KIND == 2) {
								++dual_queries_;
								const int dual = pdb_.dual_lower_bound(
									suffix, budget, information_cost_, states_,
									exact_, dual_transitions_
								);
								lower = std::max(lower, dual);
							} else if constexpr (BOUND_KIND == 3) {
								const int compiled =
									pdb_.compiled_dual_lower_bound(
										suffix, budget, information_cost_,
										states_, compiled_dual_lookups_
									);
								lower = std::max(lower, compiled);
							}
						}
					}
					if (lower > best_distance_) {
						pruned_ += descendant_count(K, suffix, budget);
					} else {
						traverse<DEPTH + 1>(suffix);
					}
				}
			}
			unflip(rank);
		}
	}

public:
	ReconstructedPdbCertificationSearch(
		const int8_t *base,
		int8_t *output,
		const int8_t *soft,
		const Pdb &pdb,
		int seed_best_metric,
		int seed_next_metric
	):
		base_(base),
		output_(output),
		soft_(soft),
		pdb_(pdb),
		states_(pdb.initial_packed_states()),
		exact_(pdb.initial_exact_states())
	{
		for (int index = 0; index < W; ++index)
			absolute_metric_ += std::abs(static_cast<int>(soft[index]));
		best_distance_ = (absolute_metric_ - seed_best_metric) / 2;
		next_distance_ = seed_next_metric == -1
			? std::numeric_limits<int>::max()
			: (absolute_metric_ - seed_next_metric) / 2;
		for (int rank = 0; rank < K; ++rank) {
			const int row = pdb.coordinate(rank);
			if (base[row] != output[row])
				seed_selected_[static_cast<std::size_t>(seed_weight_++)] =
					static_cast<uint16_t>(rank);
		}
	}

	void run()
	{
		traverse();
		assert(information_cost_ == 0);
		assert(states_ == pdb_.initial_packed_states());
		assert(exact_ == pdb_.initial_exact_states());
	}
	int best_metric() const { return absolute_metric_ - 2 * best_distance_; }
	int next_metric() const
	{
		return next_distance_ == std::numeric_limits<int>::max()
			? -1
			: absolute_metric_ - 2 * next_distance_;
	}
	uint64_t evaluated() const { return evaluated_; }
	uint64_t scoring_calls() const { return scoring_calls_; }
	uint64_t considered() const { return considered_; }
	uint64_t pruned() const { return pruned_; }
	uint64_t checks() const { return checks_; }
	uint64_t pdb_lookups() const { return pdb_lookups_; }
	uint64_t compact_lookups() const { return compact_lookups_; }
	uint64_t dual_queries() const { return dual_queries_; }
	uint64_t dual_transitions() const { return dual_transitions_; }
	uint64_t compiled_dual_lookups() const
	{
		return compiled_dual_lookups_;
	}
	const std::vector<uint64_t> &evaluated_masks() const
	{
		return evaluated_masks_;
	}
};

// Exact fixed-list OSD kernel with a bit-sliced weighted-Hamming metric.
// The dominant deepest frontier is generated directly from packed parity-row
// XORs and scored in AVX-512 batches.  No candidate, metric term, or OSD list
// element is approximated or omitted.
template <int N, int K, int O, bool TRANSPOSED_FRONTIER = false>
class BitplaneFrontierExhaustiveSearch {
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int PARITY_WORDS = (PARITY_LENGTH + 63) / 64;
	static constexpr int PARITY_BYTES = (PARITY_LENGTH + 7) / 8;
	static constexpr int FRONTIER_LANES = 16;
	using State = std::array<uint64_t, PARITY_WORDS>;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<int, K> information_costs{};
	std::array<uint64_t, K * PARITY_WORDS> row_masks{};
	std::array<uint64_t, SOFT_MAGNITUDE_BITS * PARITY_WORDS> weight_planes{};
	std::array<uint64_t, SOFT_MAGNITUDE_BITS * 8> vector_weight_planes{};
	std::array<int, PARITY_BYTES * 256> byte_costs{};
	std::array<uint8_t, PARITY_BYTES * FRONTIER_LANES> frontier_bytes{};
	std::array<int, FRONTIER_LANES> frontier_information_costs{};
	std::array<std::array<uint16_t, O>, FRONTIER_LANES>
		frontier_selected{};
	int frontier_size = 0;
	State state{};
	std::array<uint16_t, O> selected{};
	int information_cost = 0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t materialized_candidates = 0;

	uint64_t row_mask(int rank, int word) const
	{
		return row_masks[
			static_cast<std::size_t>(rank) * PARITY_WORDS + word
		];
	}

	uint64_t weight_plane(int bit, int word) const
	{
		return weight_planes[
			static_cast<std::size_t>(bit) * PARITY_WORDS + word
		];
	}

	int parity_cost(const State &candidate) const
	{
		int value = 0;
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const auto state_byte = static_cast<uint8_t>(
				candidate[static_cast<std::size_t>(byte / 8)] >>
				(8 * (byte % 8))
			);
			value += byte_costs[
				static_cast<std::size_t>(byte) * 256 + state_byte
			];
		}
		return value;
	}

	void copy_candidate(int weight, int pending_rank = -1)
	{
		std::copy(base, base + W, output);
		for (int index = 0; index < weight; ++index)
			output[selected[static_cast<std::size_t>(index)]] ^= int8_t{1};
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			uint64_t mismatch = state[static_cast<std::size_t>(offset / 64)];
			if (pending_rank >= 0)
				mismatch ^= row_mask(pending_rank, offset / 64);
			output[K + offset] = static_cast<int8_t>(
				(soft[K + offset] < 0) ^
				((mismatch >> (offset % 64)) & uint64_t{1})
			);
		}
		for (int index = N; index < W; ++index)
			output[index] = 0;
		++materialized_candidates;
	}

	void consider(int distance, int weight, int pending_rank = -1)
	{
		++evaluated_candidates;
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			copy_candidate(weight, pending_rank);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
	}

	void flip(int rank)
	{
		information_cost += information_costs[static_cast<std::size_t>(rank)];
		for (int word = 0; word < PARITY_WORDS; ++word)
			state[static_cast<std::size_t>(word)] ^= row_mask(rank, word);
	}

	void unflip(int rank)
	{
		for (int word = 0; word < PARITY_WORDS; ++word)
			state[static_cast<std::size_t>(word)] ^= row_mask(rank, word);
		information_cost -= information_costs[static_cast<std::size_t>(rank)];
	}

	int deepest_distance(int rank) const
	{
		State candidate = state;
		for (int word = 0; word < PARITY_WORDS; ++word)
			candidate[static_cast<std::size_t>(word)] ^= row_mask(rank, word);
		return information_cost +
			information_costs[static_cast<std::size_t>(rank)] +
			parity_cost(candidate);
	}

	void copy_frontier_candidate(int lane)
	{
		std::copy(base, base + W, output);
		for (int index = 0; index < O; ++index)
			output[frontier_selected[
				static_cast<std::size_t>(lane)
			][static_cast<std::size_t>(index)]] ^= int8_t{1};
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			const uint8_t byte = frontier_bytes[
				static_cast<std::size_t>(offset / 8) * FRONTIER_LANES + lane
			];
			output[K + offset] = static_cast<int8_t>(
				(soft[K + offset] < 0) ^ ((byte >> (offset % 8)) & 1)
			);
		}
		for (int index = N; index < W; ++index)
			output[index] = 0;
		++materialized_candidates;
	}

	void process_frontier_candidate(int lane, int distance)
	{
		++evaluated_candidates;
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			copy_frontier_candidate(lane);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
	}

	void flush_frontier()
	{
		if (frontier_size == 0)
			return;
		#if defined(__AVX512F__)
		__m512i distances = _mm512_loadu_si512(
			frontier_information_costs.data()
		);
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const __m128i packed = _mm_loadu_si128(
				reinterpret_cast<const __m128i *>(
					frontier_bytes.data() +
					static_cast<std::size_t>(byte) * FRONTIER_LANES
				)
			);
			const __m512i states = _mm512_cvtepu8_epi32(packed);
			const __m512i costs = _mm512_i32gather_epi32(
				states,
				byte_costs.data() + static_cast<std::size_t>(byte) * 256,
				4
			);
			distances = _mm512_add_epi32(distances, costs);
		}
		alignas(64) std::array<int, FRONTIER_LANES> lanes{};
		_mm512_store_si512(lanes.data(), distances);
		for (int lane = 0; lane < frontier_size; ++lane)
			process_frontier_candidate(
				lane, lanes[static_cast<std::size_t>(lane)]
			);
		#else
		for (int lane = 0; lane < frontier_size; ++lane) {
			int distance = frontier_information_costs[
				static_cast<std::size_t>(lane)
			];
			for (int byte = 0; byte < PARITY_BYTES; ++byte)
				distance += byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					frontier_bytes[
						static_cast<std::size_t>(byte) * FRONTIER_LANES + lane
					]
				];
			process_frontier_candidate(lane, distance);
		}
		#endif
		frontier_size = 0;
	}

	void enqueue_deepest(int rank)
	{
		selected[static_cast<std::size_t>(O - 1)] =
			static_cast<uint16_t>(rank);
		const int lane = frontier_size++;
		frontier_information_costs[static_cast<std::size_t>(lane)] =
			information_cost +
			information_costs[static_cast<std::size_t>(rank)];
		frontier_selected[static_cast<std::size_t>(lane)] = selected;
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const uint8_t current = static_cast<uint8_t>(
				state[static_cast<std::size_t>(byte / 8)] >>
				(8 * (byte % 8))
			);
			const uint8_t row = static_cast<uint8_t>(
				row_mask(rank, byte / 8) >> (8 * (byte % 8))
			);
			frontier_bytes[
				static_cast<std::size_t>(byte) * FRONTIER_LANES + lane
			] = current ^ row;
		}
		if (frontier_size == FRONTIER_LANES)
			flush_frontier();
	}

	#if defined(__AVX512VPOPCNTDQ__)
	template <int BIT = 0>
	__m512i vector_parity_cost(__m512i candidates) const
	{
		if constexpr (BIT == SOFT_MAGNITUDE_BITS) {
			return _mm512_setzero_si512();
		} else {
			const __m512i planes = _mm512_loadu_si512(
				vector_weight_planes.data() +
				static_cast<std::size_t>(BIT) * 8
			);
			const __m512i counts = _mm512_popcnt_epi64(
				_mm512_and_si512(candidates, planes)
			);
			return _mm512_add_epi64(
				_mm512_slli_epi64(counts, BIT),
				vector_parity_cost<BIT + 1>(candidates)
			);
		}
	}
	#endif

	void process_deepest(int rank, int distance)
	{
		selected[static_cast<std::size_t>(O - 1)] =
			static_cast<uint16_t>(rank);
		consider(distance, O, rank);
	}

	void traverse_deepest(int start)
	{
		if constexpr (TRANSPOSED_FRONTIER) {
			for (int rank = start; rank < K; ++rank)
				enqueue_deepest(rank);
		} else for (int rank = start; rank < K;) {
			#if defined(__AVX512VPOPCNTDQ__)
			if constexpr (PARITY_WORDS == 1) {
				if (rank + 8 <= K) {
					const __m512i rows = _mm512_loadu_si512(
						row_masks.data() + rank
					);
					const __m512i candidates = _mm512_xor_si512(
						rows, _mm512_set1_epi64(
							static_cast<long long>(state[0])
						)
					);
					const __m512i costs = vector_parity_cost(candidates);
					alignas(64) std::array<uint64_t, 8> lanes{};
					_mm512_store_si512(lanes.data(), costs);
					for (int lane = 0; lane < 8; ++lane)
						process_deepest(
							rank + lane,
							information_cost + information_costs[
								static_cast<std::size_t>(rank + lane)
							] + static_cast<int>(lanes[
								static_cast<std::size_t>(lane)
							])
						);
					rank += 8;
					continue;
				}
			} else if constexpr (PARITY_WORDS == 2) {
				if (rank + 4 <= K) {
					const __m512i rows = _mm512_loadu_si512(
						row_masks.data() +
							static_cast<std::size_t>(rank) * 2
					);
					const __m512i current = _mm512_setr_epi64(
						static_cast<long long>(state[0]),
						static_cast<long long>(state[1]),
						static_cast<long long>(state[0]),
						static_cast<long long>(state[1]),
						static_cast<long long>(state[0]),
						static_cast<long long>(state[1]),
						static_cast<long long>(state[0]),
						static_cast<long long>(state[1])
					);
					const __m512i costs = vector_parity_cost(
						_mm512_xor_si512(rows, current)
					);
					alignas(64) std::array<uint64_t, 8> words{};
					_mm512_store_si512(words.data(), costs);
					for (int lane = 0; lane < 4; ++lane)
						process_deepest(
							rank + lane,
							information_cost + information_costs[
								static_cast<std::size_t>(rank + lane)
							] + static_cast<int>(
								words[static_cast<std::size_t>(2 * lane)] +
								words[static_cast<std::size_t>(2 * lane + 1)]
							)
						);
					rank += 4;
					continue;
				}
			}
			#endif
			process_deepest(rank, deepest_distance(rank));
			++rank;
		}
	}

	template <int DEPTH = 0>
	void traverse(int start = 0)
	{
		if constexpr (DEPTH + 1 == O) {
			traverse_deepest(start);
		} else {
			for (int rank = start; rank < K; ++rank) {
				flip(rank);
				selected[static_cast<std::size_t>(DEPTH)] =
					static_cast<uint16_t>(rank);
				consider(information_cost + parity_cost(state), DEPTH + 1);
				traverse<DEPTH + 1>(rank + 1);
				unflip(rank);
			}
		}
	}

public:
	BitplaneFrontierExhaustiveSearch(
		const int8_t *matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft
	):
		base(input_base), output(input_output), soft(input_soft)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(O > 0);
		for (int index = 0; index < N; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		for (int rank = 0; rank < K; ++rank) {
			information_costs[static_cast<std::size_t>(rank)] =
				std::abs(static_cast<int>(soft[rank]));
			for (int offset = 0; offset < PARITY_LENGTH; ++offset)
				if (matrix[rank * W + K + offset])
					row_masks[
						static_cast<std::size_t>(rank) * PARITY_WORDS +
						static_cast<std::size_t>(offset / 64)
					] |= uint64_t{1} << (offset % 64);
		}
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			if (base[K + offset] != (soft[K + offset] < 0))
				state[static_cast<std::size_t>(offset / 64)] |=
					uint64_t{1} << (offset % 64);
			if constexpr (!TRANSPOSED_FRONTIER) {
				const int cost = std::abs(static_cast<int>(soft[K + offset]));
				for (int bit = 0; bit < SOFT_MAGNITUDE_BITS; ++bit)
					if ((cost >> bit) & 1)
						weight_planes[
							static_cast<std::size_t>(bit) * PARITY_WORDS +
							static_cast<std::size_t>(offset / 64)
						] |= uint64_t{1} << (offset % 64);
			}
		}
		for (int byte = 0; byte < PARITY_BYTES; ++byte)
			for (int value = 1; value < 256; ++value) {
				const int bit = __builtin_ctz(
					static_cast<unsigned int>(value)
				);
				const int offset = byte * 8 + bit;
				byte_costs[
					static_cast<std::size_t>(byte) * 256 + value
				] = byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value & (value - 1))
				] + (offset < PARITY_LENGTH
					? std::abs(static_cast<int>(soft[K + offset]))
					: 0);
			}
		if constexpr (!TRANSPOSED_FRONTIER) {
			for (int bit = 0; bit < SOFT_MAGNITUDE_BITS; ++bit)
				for (int lane = 0; lane < 8; ++lane)
					vector_weight_planes[
						static_cast<std::size_t>(bit) * 8 + lane
					] = weight_plane(bit, lane % PARITY_WORDS);
		}
	}

	void run()
	{
		consider(parity_cost(state), 0);
		traverse();
		if constexpr (TRANSPOSED_FRONTIER)
			flush_frontier();
		assert(information_cost == 0);
		assert(evaluated_candidates == candidate_count(K, O));
	}

	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t materializations() const { return materialized_candidates; }
	std::size_t bytes() const
	{
		return information_costs.size() * sizeof(int) +
			row_masks.size() * sizeof(uint64_t) +
			weight_planes.size() * sizeof(uint64_t) +
			vector_weight_planes.size() * sizeof(uint64_t) +
			byte_costs.size() * sizeof(int) +
			frontier_bytes.size() * sizeof(uint8_t) +
			frontier_information_costs.size() * sizeof(int) +
			frontier_selected.size() * sizeof(frontier_selected[0]) +
			state.size() * sizeof(uint64_t) +
			selected.size() * sizeof(uint16_t);
	}
};

template <int N, int K, int O, bool TRANSPOSED_FRONTIER = false>
bool execute_bitplane_frontier_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next
)
{
	const auto build_start = Clock::now();
	BitplaneFrontierExhaustiveSearch<N, K, O, TRANSPOSED_FRONTIER> search(
		matrix, base, candidate, soft
	);
	const auto build_end = Clock::now();
	search.run();
	const auto search_end = Clock::now();
	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - build_end
		).count()
	);
	override_stats.candidates = search.candidates();
	override_stats.materializations = search.materializations();
	override_stats.patterns_considered = search.candidates();
	override_stats.dp_bytes = search.bytes();
	best = search.best_metric();
	next = search.next_metric();
	return true;
}

template <int N, int K, int O>
struct SystematicOrbSeedResult {
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	std::array<int8_t, W> candidate{};
	bool found = false;
	uint64_t queries = 0;
	int metric = std::numeric_limits<int>::min();
};

// A short ORBGRAND pass is used only as an incumbent proposal.  A syndrome
// hit is accepted when its systematic part differs from the order-zero OSD
// word in at most O positions, proving that the proposal belongs to the
// unchanged fixed-order OSD list.  Certification remains mandatory.
template <int N, int K, int O>
SystematicOrbSeedResult<N, K, O> systematic_orb_seed(
	const int8_t *matrix,
	const int8_t *base,
	const int8_t *soft,
	uint64_t budget
)
{
	constexpr int W = SystematicOrbSeedResult<N, K, O>::W;
	constexpr int REDUNDANCY = N - K;
	constexpr int SYNDROME_WORDS = (REDUNDANCY + 63) / 64;
	SystematicOrbSeedResult<N, K, O> result;
	std::array<uint64_t, N * SYNDROME_WORDS> columns{};
	for (int row = 0; row < K; ++row) {
		for (int column = 0; column < K; ++column)
			assert(matrix[row * W + column] == (row == column));
		for (int offset = 0; offset < REDUNDANCY; ++offset)
			if (matrix[row * W + K + offset])
				columns[
					static_cast<std::size_t>(row) * SYNDROME_WORDS +
					static_cast<std::size_t>(offset / 64)
				] |= uint64_t{1} << (offset % 64);
	}
	for (int offset = 0; offset < REDUNDANCY; ++offset)
		columns[
			static_cast<std::size_t>(K + offset) * SYNDROME_WORDS +
			static_cast<std::size_t>(offset / 64)
		] |= uint64_t{1} << (offset % 64);

	std::array<int, N> reliability_order{};
	std::array<uint64_t, SYNDROME_WORDS> hard_syndrome{};
	int absolute = 0;
	for (int coordinate = 0; coordinate < N; ++coordinate) {
		reliability_order[static_cast<std::size_t>(coordinate)] = coordinate;
		absolute += std::abs(static_cast<int>(soft[coordinate]));
		if (soft[coordinate] >= 0)
			continue;
		for (int word = 0; word < SYNDROME_WORDS; ++word)
			hard_syndrome[static_cast<std::size_t>(word)] ^=
				columns[
					static_cast<std::size_t>(coordinate) * SYNDROME_WORDS + word
				];
	}
	std::stable_sort(
		reliability_order.begin(), reliability_order.end(),
		[soft](int left, int right) {
			const int left_cost = std::abs(static_cast<int>(soft[left]));
			const int right_cost = std::abs(static_cast<int>(soft[right]));
			if (left_cost != right_cost)
				return left_cost < right_cost;
			return left < right;
		}
	);

	auto visitor = [&result, &columns, &hard_syndrome, &reliability_order,
		base, soft, absolute](
		const uint16_t *ranks,
		int count,
		int,
		uint64_t query
	) {
		std::array<uint64_t, SYNDROME_WORDS> noise_syndrome{};
		int distance = 0;
		for (int index = 0; index < count; ++index) {
			const int coordinate = reliability_order[
				static_cast<std::size_t>(ranks[index] - 1)
			];
			distance += std::abs(static_cast<int>(soft[coordinate]));
			for (int word = 0; word < SYNDROME_WORDS; ++word)
				noise_syndrome[static_cast<std::size_t>(word)] ^=
					columns[
						static_cast<std::size_t>(coordinate) * SYNDROME_WORDS + word
					];
		}
		if (noise_syndrome != hard_syndrome)
			return false;
		for (int coordinate = 0; coordinate < N; ++coordinate)
			result.candidate[static_cast<std::size_t>(coordinate)] =
				soft[coordinate] < 0;
		for (int index = 0; index < count; ++index) {
			const int coordinate = reliability_order[
				static_cast<std::size_t>(ranks[index] - 1)
			];
			result.candidate[static_cast<std::size_t>(coordinate)] ^= int8_t{1};
		}
		int information_weight = 0;
		for (int coordinate = 0; coordinate < K; ++coordinate)
			information_weight +=
				result.candidate[static_cast<std::size_t>(coordinate)] !=
				base[coordinate];
		if (information_weight > O)
			return false;
		result.found = true;
		result.queries = query;
		result.metric = absolute - 2 * distance;
		return true;
	};
	result.queries = BasicOrbgrandSchedule<N>::run(budget, visitor);
	return result;
}

template <int N, int K, int O>
bool execute_projected_screen_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	const auto certify_start = Clock::now();
	ProjectedCertificationSearch<N, K, O> certify(
		matrix, base, candidate, soft,
		screen.best_metric(), screen.next_metric()
	);
	certify.run();
	const auto certify_end = Clock::now();
	const uint64_t combined_candidates =
		screen.candidates() + certify.candidates();
	override_stats.bounded_search_ns = screen_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		screen.patterns() + certify.patterns();
	override_stats.pruned_candidates = combined_candidates < total
		? total - combined_candidates
		: 0;
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.dp_bytes = std::max(screen.bytes(), certify.bytes());
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O>
bool execute_packed_parity_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft,
		std::min(candidate_budget, PACKED_DISCOVERY_BUDGET)
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.materializations = screen.materializations();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	const auto certify_start = Clock::now();
	PackedParityCertificationSearch<N, K, O> certify(
		matrix, base, candidate, soft,
		screen.best_metric(), screen.next_metric()
	);
	certify.run();
	const auto certify_end = Clock::now();
	const uint64_t combined_candidates =
		screen.candidates() + certify.candidates();
	override_stats.bounded_search_ns = screen_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	override_stats.candidates = combined_candidates;
	override_stats.materializations =
		screen.materializations() + certify.materializations();
	override_stats.patterns_considered =
		screen.patterns() + certify.patterns();
	override_stats.pruned_candidates = combined_candidates < total
		? total - combined_candidates
		: 0;
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.dp_bytes = std::max(screen.bytes(), certify.bytes());
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <
	int N, int K, int O,
	bool QUERY_LOCAL = false,
	bool PACKED_STATE = false,
	bool ORB_SEED = false,
	bool WITNESS_PROPOSALS = false,
	bool FUSED_SUPPORT = (PDB_GROUPING == 3),
	int FUSED_GROUP_LIMIT = 2,
	bool DISCOVER_SELECTIVE_ORACLE = false,
	bool REPLAY_SELECTIVE_ORACLE = false
>
bool execute_additive_pdb_discover_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget,
	SelectiveCouplingOracleSchedule<K> *oracle_schedule = nullptr
)
{
	constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}
	int seed_best = screen.best_metric();
	int seed_next = screen.next_metric();
	uint64_t seed_ns = 0;
	uint64_t seed_queries = 0;
	if constexpr (ORB_SEED) {
		const auto seed_start = Clock::now();
		const auto orb = systematic_orb_seed<N, K, O>(
			matrix, base, soft, ORB_SEED_QUERIES
		);
		const auto seed_end = Clock::now();
		seed_ns = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				seed_end - seed_start
			).count()
		);
		seed_queries = orb.queries;
		if (orb.found) {
			const bool same = std::equal(
				orb.candidate.begin(), orb.candidate.begin() + W, candidate
			);
			if (!same && orb.metric > seed_best) {
				seed_next = std::max(seed_next, seed_best);
				seed_best = orb.metric;
				std::copy(
					orb.candidate.begin(), orb.candidate.begin() + W, candidate
				);
			} else if (!same && orb.metric > seed_next) {
				seed_next = orb.metric;
			}
		}
	}

	const auto build_start = Clock::now();
	AdditiveParityPatternDatabase<
		N, K, O, QUERY_LOCAL, PACKED_STATE,
		FUSED_SUPPORT, FUSED_GROUP_LIMIT
	> pdb(
		matrix, base, soft
	);
	const auto build_end = Clock::now();
	uint64_t witness_proposals = 0;
	if constexpr (
		PACKED_STATE && (WITNESS_PROPOSALS || PACKED_PDB_WITNESS_PROPOSALS)
	) {
		int absolute_metric = 0;
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		const int seed_distance = (absolute_metric - seed_best) / 2;
		if (
			1000LL * seed_distance >=
			static_cast<long long>(
				PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI
			) * absolute_metric
		) {
			const auto witness_start = Clock::now();
			witness_proposals = pdb.improve_seed_with_group_witnesses(
				base, candidate, soft, seed_best, seed_next
			);
			const auto witness_end = Clock::now();
			seed_ns += static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					witness_end - witness_start
				).count()
			);
		}
	}
	static_assert(!PACKED_STATE || !QUERY_LOCAL);
	using Certification = std::conditional_t<
		PACKED_STATE,
		PackedStateAdditivePdbCertificationSearch<
			N, K, O,
			PACKED_PDB_CONFLICT_LEARNING && WITNESS_PROPOSALS,
			FUSED_SUPPORT, FUSED_GROUP_LIMIT,
			DISCOVER_SELECTIVE_ORACLE, REPLAY_SELECTIVE_ORACLE
		>,
		AdditivePdbCertificationSearch<
			N, K, O, false, false, false, QUERY_LOCAL
		>
	>;
	Certification certify = [&]() -> Certification {
		if constexpr (PACKED_STATE)
			return Certification(
				base, candidate, soft, pdb,
				seed_best, seed_next, oracle_schedule
			);
		else
			return Certification(
				base, candidate, soft, pdb,
				seed_best, seed_next
			);
	}();
	const auto certify_start = Clock::now();
	certify.run();
	const auto certify_end = Clock::now();

	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = screen_ns + seed_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	const uint64_t combined_candidates =
		screen.candidates() + witness_proposals + certify.candidates();
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		screen.patterns() + witness_proposals + certify.patterns();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.bound_checks =
		screen.checks() + certify.checks() + seed_queries;
	if constexpr (PACKED_STATE && PACKED_PDB_CONFLICT_LEARNING) {
		override_stats.learned_conflicts = certify.conflicts();
		override_stats.learned_conflict_hits = certify.conflict_hits();
		override_stats.learned_conflict_derivations =
			certify.conflict_derivations();
	}
	if constexpr (DISCOVER_SELECTIVE_ORACLE || REPLAY_SELECTIVE_ORACLE) {
		override_stats.oracle_coupled_prunes = certify.oracle_prunes();
		override_stats.oracle_coupled_candidates = certify.oracle_candidates();
		override_stats.oracle_schedule_lookups = certify.oracle_lookups();
	}
	// The screen object remains live while the PDB certificate executes.
	override_stats.dp_bytes = screen.bytes() + pdb.bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O, int BOUND_KIND>
bool execute_reconstructed_pdb_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.candidates = screen.candidates();
		override_stats.scoring_calls = screen.candidates();
		override_stats.unique_teps_evaluated = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	int seed_best = screen.best_metric();
	int seed_next = screen.next_metric();
	ReconstructedTwoBlockPatternDatabase<N, K, O> pdb(
		matrix, base, soft
	);
	std::vector<uint64_t> all_masks = screen.evaluated_masks();
	const uint64_t witness_proposals = pdb.improve_seed_with_group_witnesses(
		base, candidate, soft, seed_best, seed_next, &all_masks
	);
	ReconstructedPdbCertificationSearch<N, K, O, BOUND_KIND> certify(
		base, candidate, soft, pdb, seed_best, seed_next
	);
	certify.run();
	all_masks.insert(
		all_masks.end(),
		certify.evaluated_masks().begin(),
		certify.evaluated_masks().end()
	);
	std::sort(all_masks.begin(), all_masks.end());
	all_masks.erase(
		std::unique(all_masks.begin(), all_masks.end()), all_masks.end()
	);

	override_stats.candidates =
		screen.candidates() + witness_proposals + certify.evaluated();
	override_stats.scoring_calls =
		screen.candidates() + witness_proposals + certify.scoring_calls();
	override_stats.unique_teps_evaluated = all_masks.size();
	override_stats.patterns_considered =
		screen.patterns() + witness_proposals + certify.considered();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.pdb_table_lookups = certify.pdb_lookups();
	override_stats.compact_table_lookups = certify.compact_lookups();
	override_stats.compact_parallel_cycles = pdb.compact_parallel_cycles();
	override_stats.compact_parallel_allocations =
		pdb.compact_parallel_allocations();
	override_stats.dual_queries = certify.dual_queries();
	override_stats.dual_dp_transitions = certify.dual_transitions();
	override_stats.compiled_dual_lookups = certify.compiled_dual_lookups();
	override_stats.compiled_dual_build_transitions =
		pdb.compiled_build_transitions();
	override_stats.dp_bytes = screen.bytes() + pdb.reconstructed_bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O>
void prepare_perfect_subtree_oracle(
	const int8_t *matrix,
	const int8_t *base,
	const int8_t *soft,
	PerfectSubtreeOracleSchedule<K, O> &schedule
)
{
	using Pdb = AdditiveParityPatternDatabase<
		N, K, O, false, true, false, 2
	>;
	Pdb pdb(matrix, base, soft);
	PerfectSubtreeOracleBuilder<N, K, O> builder(pdb, schedule);
	builder.run();
}

template <int N, int K, int O, bool PERFECT_SEED>
bool execute_perfect_subtree_oracle_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget,
	const PerfectSubtreeOracleSchedule<K, O> &schedule
)
{
	using Pdb = AdditiveParityPatternDatabase<
		N, K, O, false, true, false, 2
	>;
	constexpr int W = Pdb::W;
	const uint64_t total = candidate_count(K, O);
	int seed_best = 0;
	int seed_next = -1;
	uint64_t screen_ns = 0;
	uint64_t screen_candidates = 0;
	uint64_t screen_patterns = 0;
	uint64_t screen_checks = 0;

	if constexpr (!PERFECT_SEED) {
		const auto screen_start = Clock::now();
		ProjectedSoftWeightScreenSearch<N, K, O> screen(
			matrix, base, candidate, soft, candidate_budget
		);
		const bool screen_completed = screen.run();
		const auto screen_end = Clock::now();
		screen_ns = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				screen_end - screen_start
			).count()
		);
		screen_candidates = screen.candidates();
		screen_patterns = screen.patterns();
		screen_checks = screen.checks();
		seed_best = screen.best_metric();
		seed_next = screen.next_metric();
		if (screen_completed) {
			override_stats.bounded_search_ns = screen_ns;
			override_stats.candidates = screen_candidates;
			override_stats.patterns_considered = screen_patterns;
			override_stats.pruned_candidates =
				total - screen_candidates;
			override_stats.bound_checks = screen_checks;
			override_stats.dp_bytes = screen.bytes();
			best = seed_best;
			next = seed_next;
			return true;
		}
	}

	const auto build_start = Clock::now();
	Pdb pdb(matrix, base, soft);
	const auto build_end = Clock::now();
	uint64_t seed_ns = 0;
	uint64_t seed_proposals = 0;
	if constexpr (PERFECT_SEED) {
		const auto seed_start = Clock::now();
		pdb.materialize_pattern(
			base, soft, schedule.best_ranks,
			schedule.best_weight, candidate
		);
		int absolute_metric = 0;
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		seed_best = absolute_metric - 2 * schedule.best_distance;
		seed_next = -1;
		seed_proposals = 1;
		const auto seed_end = Clock::now();
		seed_ns = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				seed_end - seed_start
			).count()
		);
	} else {
		int absolute_metric = 0;
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		const int seed_distance = (absolute_metric - seed_best) / 2;
		if (
			1000LL * seed_distance >=
			static_cast<long long>(
				PACKED_PDB_WITNESS_MIN_DISTANCE_MILLI
			) * absolute_metric
		) {
			const auto seed_start = Clock::now();
			seed_proposals = pdb.improve_seed_with_group_witnesses(
				base, candidate, soft, seed_best, seed_next
			);
			const auto seed_end = Clock::now();
			seed_ns = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					seed_end - seed_start
				).count()
			);
		}
	}

	PerfectSubtreeOracleCertificationSearch<N, K, O> certify(
		base, candidate, soft, pdb, schedule,
		seed_best, seed_next
	);
	const auto certify_start = Clock::now();
	certify.run();
	const auto certify_end = Clock::now();
	const uint64_t certify_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);

	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = screen_ns + seed_ns + certify_ns;
	override_stats.candidates =
		screen_candidates + seed_proposals + certify.candidates();
	override_stats.patterns_considered =
		screen_patterns + seed_proposals + certify.patterns();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.pruned_subtrees = certify.subtrees();
	override_stats.bound_checks = screen_checks + certify.checks();
	override_stats.perfect_subtree_prunes = certify.subtrees();
	override_stats.perfect_subtree_candidates = certify.pruned();
	override_stats.perfect_subtree_lookups = certify.checks();
	override_stats.dp_bytes = pdb.bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O>
bool execute_fingerprint_pdb_discover_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	const auto build_start = Clock::now();
	FingerprintSynchronizedParityPatternDatabase<N, K, O> pdb(
		matrix, base, soft
	);
	const auto build_end = Clock::now();
	AdditivePdbCertificationSearch<N, K, O, false, true> certify(
		base, candidate, soft, pdb,
		screen.best_metric(), screen.next_metric()
	);
	const auto certify_start = Clock::now();
	certify.run();
	const auto certify_end = Clock::now();

	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = screen_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	const uint64_t combined_candidates =
		screen.candidates() + certify.candidates();
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		screen.patterns() + certify.patterns();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.dp_bytes = screen.bytes() + pdb.bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O>
bool execute_quotient_pdb_discover_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	const auto build_start = Clock::now();
	SyndromeQuotientPatternDatabase<N, K, O> pdb(matrix, base, soft);
	const auto build_end = Clock::now();
	AdditivePdbCertificationSearch<N, K, O, false, false, true> certify(
		base, candidate, soft, pdb,
		screen.best_metric(), screen.next_metric()
	);
	const auto certify_start = Clock::now();
	certify.run();
	const auto certify_end = Clock::now();

	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = screen_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	const uint64_t combined_candidates =
		screen.candidates() + certify.candidates();
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		screen.patterns() + certify.patterns();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.dp_bytes = screen.bytes() + pdb.bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

template <int N, int K, int O>
bool execute_cost_partition_pdb_discover_certify_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto screen_start = Clock::now();
	ProjectedSoftWeightScreenSearch<N, K, O> screen(
		matrix, base, candidate, soft, candidate_budget
	);
	const bool screen_completed = screen.run();
	const auto screen_end = Clock::now();
	const uint64_t screen_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			screen_end - screen_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (screen_completed) {
		override_stats.bounded_search_ns = screen_ns;
		override_stats.candidates = screen.candidates();
		override_stats.patterns_considered = screen.patterns();
		override_stats.pruned_candidates = total - screen.candidates();
		override_stats.bound_checks = screen.checks();
		override_stats.dp_bytes = screen.bytes();
		best = screen.best_metric();
		next = screen.next_metric();
		return true;
	}

	const auto build_start = Clock::now();
	CostPartitionedAdditiveParityPatternDatabase<N, K, O> pdb(
		matrix, base, soft
	);
	const auto build_end = Clock::now();
	AdditivePdbCertificationSearch<N, K, O, true> certify(
		base, candidate, soft, pdb,
		screen.best_metric(), screen.next_metric()
	);
	const auto certify_start = Clock::now();
	certify.run();
	const auto certify_end = Clock::now();

	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = screen_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			certify_end - certify_start
		).count()
	);
	const uint64_t combined_candidates =
		screen.candidates() + certify.candidates();
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		screen.patterns() + certify.patterns();
	override_stats.pruned_candidates = certify.pruned();
	override_stats.bound_checks = screen.checks() + certify.checks();
	override_stats.dp_bytes = screen.bytes() + pdb.bytes();
	override_stats.fallback = true;
	best = certify.best_metric();
	next = certify.next_metric();
	return true;
}

// Exact projected-parity A* comparator.  The trellis keeps only a small set
// of high-reliability parity coordinates.  Its dynamic program gives the
// minimum information cost plus projected-parity mismatch cost attainable by
// every remaining subtree.  This is an admissible lower bound on the complete
// weighted Hamming distance, so A* may stop only when the smallest live bound
// is strictly greater than the incumbent.  The full candidate is still
// re-encoded and scored before it can become the incumbent.
template <int N, int K, int O>
class ProjectedParityTrellis {
public:
	using State = uint16_t;

private:
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int BITS =
		PARITY_LENGTH < WINDOW_BITS ? PARITY_LENGTH : WINDOW_BITS;
	static constexpr std::size_t STATES = std::size_t{1} << BITS;
	std::array<int, K> coordinates_{};
	std::array<int, K> costs_{};
	std::array<State, K> row_masks_{};
	std::array<int, BITS> offsets_{};
	std::vector<int> minimum_;
	State initial_state_ = 0;

	static std::size_t index(int rank, int budget, State state)
	{
		return (
			static_cast<std::size_t>(rank * (O + 1) + budget) * STATES
		) + state;
	}

public:
	ProjectedParityTrellis(
		const int8_t *matrix,
		const int8_t *base,
		const int8_t *soft
	):
		minimum_(
			static_cast<std::size_t>((K + 1) * (O + 1)) * STATES
		)
	{
		static_assert(N > K);
		static_assert(O > 0 && O <= K);
		static_assert(BITS > 0 && BITS <= 16);
		constexpr int W =
			(N + static_cast<int>(sizeof(std::size_t)) - 1) &
			~(static_cast<int>(sizeof(std::size_t)) - 1);

		std::iota(coordinates_.begin(), coordinates_.end(), 0);
		std::stable_sort(
			coordinates_.begin(), coordinates_.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(static_cast<int>(soft[left]));
				const int right_cost = std::abs(static_cast<int>(soft[right]));
				if (left_cost != right_cost)
					return left_cost < right_cost;
				return left < right;
			}
		);
		for (int rank = 0; rank < K; ++rank)
			costs_[static_cast<std::size_t>(rank)] = std::abs(
				static_cast<int>(soft[
					coordinates_[static_cast<std::size_t>(rank)]
				])
			);

		std::array<int, PARITY_LENGTH> parity_offsets{};
		std::iota(parity_offsets.begin(), parity_offsets.end(), 0);
		std::stable_sort(
			parity_offsets.begin(), parity_offsets.end(),
			[soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		std::copy_n(parity_offsets.begin(), BITS, offsets_.begin());

		for (int bit = 0; bit < BITS; ++bit) {
			const int offset = offsets_[static_cast<std::size_t>(bit)];
			if (base[K + offset] != (soft[K + offset] < 0))
				initial_state_ |= static_cast<State>(State{1} << bit);
		}
		for (int rank = 0; rank < K; ++rank) {
			const int row = coordinates_[static_cast<std::size_t>(rank)];
			State mask = 0;
			for (int bit = 0; bit < BITS; ++bit) {
				const int offset = offsets_[static_cast<std::size_t>(bit)];
				if (matrix[row * W + K + offset])
					mask |= static_cast<State>(State{1} << bit);
			}
			row_masks_[static_cast<std::size_t>(rank)] = mask;
		}

		std::array<int, STATES> terminal{};
		for (std::size_t state = 1; state < STATES; ++state) {
			const int bit = __builtin_ctzll(
				static_cast<unsigned long long>(state)
			);
			terminal[state] = terminal[state & (state - 1)] + std::abs(
				static_cast<int>(soft[
					K + offsets_[static_cast<std::size_t>(bit)]
				])
			);
		}
		for (int budget = 0; budget <= O; ++budget)
			std::copy(
				terminal.begin(), terminal.end(),
				minimum_.begin() + static_cast<std::ptrdiff_t>(
					index(K, budget, 0)
				)
			);

		for (int rank = K - 1; rank >= 0; --rank) {
			const State row_mask =
				row_masks_[static_cast<std::size_t>(rank)];
			const int row_cost = costs_[static_cast<std::size_t>(rank)];
			for (std::size_t state = 0; state < STATES; ++state)
				minimum_[index(rank, 0, static_cast<State>(state))] =
					minimum_[index(rank + 1, 0, static_cast<State>(state))];
			for (int budget = 1; budget <= O; ++budget) {
				for (std::size_t state = 0; state < STATES; ++state) {
					const State current = static_cast<State>(state);
					const int skip = minimum_[
						index(rank + 1, budget, current)
					];
					const int take = row_cost + minimum_[index(
						rank + 1, budget - 1,
						static_cast<State>(current ^ row_mask)
					)];
					minimum_[index(rank, budget, current)] =
						std::min(skip, take);
				}
			}
		}
	}

	State initial_state() const { return initial_state_; }
	int coordinate(int rank) const
	{
		return coordinates_[static_cast<std::size_t>(rank)];
	}
	int cost(int rank) const
	{
		return costs_[static_cast<std::size_t>(rank)];
	}
	State row_mask(int rank) const
	{
		return row_masks_[static_cast<std::size_t>(rank)];
	}
	int minimum(int rank, int budget, State state) const
	{
		return minimum_[index(rank, budget, state)];
	}
	std::size_t bytes() const
	{
		return minimum_.size() * sizeof(int) +
			coordinates_.size() * sizeof(int) +
			costs_.size() * sizeof(int) +
			row_masks_.size() * sizeof(State) +
			offsets_.size() * sizeof(int);
	}
};

template <int N, int K, int O>
class ProjectedParityAStarSearch {
	using Trellis = ProjectedParityTrellis<N, K, O>;
	using State = typename Trellis::State;
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);

	struct Node {
		int lower_bound = 0;
		int information_cost = 0;
		uint16_t rank = 0;
		uint16_t weight = 0;
		State state = 0;
		std::array<uint16_t, O> selected{};
		uint64_t serial = 0;
	};
	struct Greater {
		bool operator()(const Node &left, const Node &right) const
		{
			if (left.lower_bound != right.lower_bound)
				return left.lower_bound > right.lower_bound;
			if (left.rank != right.rank)
				return left.rank < right.rank;
			return left.serial > right.serial;
		}
	};

	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	const Trellis &trellis;
	const uint64_t candidate_budget;
	const uint64_t node_budget;
	std::priority_queue<Node, std::vector<Node>, Greater> queue_;
	std::array<int8_t, W> current{};
	uint64_t next_serial = 0;
	uint64_t evaluated_candidates = 0;
	uint64_t leaves_considered = 0;
	uint64_t bound_checks = 0;
	uint64_t expanded_nodes = 0;
	std::size_t peak_nodes = 0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	bool seeded = false;
	uint16_t seed_weight = 0;
	std::array<uint16_t, O> seed_selected{};
	bool aborted = false;

	void push(Node node)
	{
		node.serial = next_serial++;
		queue_.push(node);
		peak_nodes = std::max(peak_nodes, queue_.size());
	}

	void expand(const Node &node)
	{
		assert(node.rank < K);
		const int rank = node.rank;
		Node skip = node;
		++skip.rank;
		skip.lower_bound = skip.information_cost + trellis.minimum(
			skip.rank, O - skip.weight, skip.state
		);
		push(skip);

		if (node.weight < O) {
			Node take = node;
			take.selected[static_cast<std::size_t>(take.weight)] =
				static_cast<uint16_t>(rank);
			++take.weight;
			++take.rank;
			take.information_cost += trellis.cost(rank);
			take.state = static_cast<State>(
				take.state ^ trellis.row_mask(rank)
			);
			take.lower_bound = take.information_cost + trellis.minimum(
				take.rank, O - take.weight, take.state
			);
			push(take);
		}
	}

	bool next_leaf(Node &leaf)
	{
		while (!queue_.empty()) {
			Node node = queue_.top();
			queue_.pop();
			if (node.rank == K) {
				leaf = node;
				return true;
			}
			if (expanded_nodes >= node_budget) {
				aborted = true;
				return false;
			}
			++expanded_nodes;
			expand(node);
		}
		return false;
	}

	int weighted_distance() const
	{
		int metric = 0;
		for (int index = 0; index < W; ++index)
			metric +=
				(1 - 2 * current[static_cast<std::size_t>(index)]) *
				soft[index];
		return (absolute_metric - metric) / 2;
	}

	void evaluate_current()
	{
		const int distance = weighted_distance();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			std::copy(current.begin(), current.end(), output);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

	bool is_seed_pattern(const Node &leaf) const
	{
		if (!seeded || leaf.weight != seed_weight)
			return false;
		return std::equal(
			leaf.selected.begin(),
			leaf.selected.begin() + leaf.weight,
			seed_selected.begin()
		);
	}

public:
	ProjectedParityAStarSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft,
		const Trellis &input_trellis,
		uint64_t input_candidate_budget,
		bool input_seeded = false,
		int input_best_metric = 0,
		int input_next_metric = -1
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft),
		trellis(input_trellis),
		candidate_budget(input_candidate_budget),
		node_budget(input_candidate_budget * static_cast<uint64_t>(K + 1) * 4),
		seeded(input_seeded)
	{
		assert(candidate_budget > 0);
		std::copy(base, base + W, current.begin());
		for (int index = 0; index < W; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		if (seeded) {
			best_distance = (absolute_metric - input_best_metric) / 2;
			next_distance = input_next_metric == -1
				? std::numeric_limits<int>::max()
				: (absolute_metric - input_next_metric) / 2;
			for (int rank = 0; rank < K; ++rank) {
				const int coordinate = trellis.coordinate(rank);
				if (base[coordinate] != output[coordinate]) {
					assert(seed_weight < O);
					seed_selected[static_cast<std::size_t>(seed_weight++)] =
						static_cast<uint16_t>(rank);
				}
			}
		}

		Node root;
		root.state = trellis.initial_state();
		root.lower_bound = trellis.minimum(0, O, root.state);
		push(root);
	}

	bool run()
	{
		// A direct run seeds the incumbent with the order-zero re-encoding.
		// An adaptive run receives a real incumbent from its soft-weight prefix.
		if (!seeded)
			evaluate_current();
		while (!queue_.empty()) {
			++bound_checks;
			if (queue_.top().lower_bound > best_distance)
				break;
			if (evaluated_candidates >= candidate_budget) {
				aborted = true;
				break;
			}
			Node leaf;
			if (!next_leaf(leaf))
				break;
			++leaves_considered;
			if (is_seed_pattern(leaf))
				continue;
			if (!leaf.weight)
				continue;
			std::copy(base, base + W, current.begin());
			for (uint16_t index = 0; index < leaf.weight; ++index) {
				const int row = trellis.coordinate(
					leaf.selected[static_cast<std::size_t>(index)]
				);
				const int8_t *matrix_row = matrix + row * W;
				for (int column = 0; column < W; ++column)
					current[static_cast<std::size_t>(column)] ^=
						matrix_row[column];
			}
			evaluate_current();
		}
		assert(evaluated_candidates <= candidate_count(K, O));
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
		return !aborted;
	}

	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return leaves_considered + !seeded; }
	uint64_t checks() const { return bound_checks; }
	std::size_t bytes() const
	{
		return trellis.bytes() + peak_nodes * sizeof(Node);
	}
};

template <int N, int K, int O>
bool execute_projected_astar_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const auto build_start = Clock::now();
	ProjectedParityTrellis<N, K, O> trellis(matrix, base, soft);
	const auto build_end = Clock::now();
	const auto search_start = Clock::now();
	ProjectedParityAStarSearch<N, K, O> search(
		matrix, base, candidate, soft, trellis, candidate_budget
	);
	const bool completed = search.run();
	const auto search_end = Clock::now();
	const uint64_t total = candidate_count(K, O);
	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - search_start
		).count()
	);
	override_stats.candidates = search.candidates();
	override_stats.patterns_considered = search.patterns();
	override_stats.pruned_candidates = total - search.candidates();
	override_stats.bound_checks = search.checks();
	override_stats.dp_bytes = search.bytes();
	best = search.best_metric();
	next = search.next_metric();
	return completed;
}

template <int N, int K, int O>
bool execute_adaptive_projected_astar_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next,
	uint64_t candidate_budget
)
{
	const uint64_t warm_budget = std::min(
		candidate_budget, PROJECTED_WARM_CANDIDATES
	);
	const auto prefix_start = Clock::now();
	SoftWeightExactSearch<N, K, O> prefix(
		matrix, base, candidate, soft, warm_budget
	);
	const bool prefix_completed = prefix.run();
	const auto prefix_end = Clock::now();
	const uint64_t prefix_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			prefix_end - prefix_start
		).count()
	);
	const uint64_t total = candidate_count(K, O);
	if (prefix_completed) {
		override_stats.bounded_search_ns = prefix_ns;
		override_stats.candidates = prefix.candidates();
		override_stats.patterns_considered = prefix.candidates();
		override_stats.pruned_candidates = total - prefix.candidates();
		override_stats.bound_checks = prefix.checks();
		override_stats.dp_bytes = prefix.bytes();
		best = prefix.best_metric();
		next = prefix.next_metric();
		return true;
	}

	const uint64_t prefix_candidates = prefix.candidates();
	if (prefix_candidates >= candidate_budget) {
		override_stats.bounded_search_ns = prefix_ns;
		override_stats.candidates = prefix_candidates;
		override_stats.patterns_considered = prefix_candidates;
		override_stats.pruned_candidates = 0;
		override_stats.bound_checks = prefix.checks();
		override_stats.dp_bytes = prefix.bytes();
		best = prefix.best_metric();
		next = prefix.next_metric();
		return false;
	}

	const int prefix_best = prefix.best_metric();
	const int prefix_next = prefix.next_metric();
	const auto build_start = Clock::now();
	ProjectedParityTrellis<N, K, O> trellis(matrix, base, soft);
	const auto build_end = Clock::now();
	const auto astar_start = Clock::now();
	ProjectedParityAStarSearch<N, K, O> search(
		matrix, base, candidate, soft, trellis,
		candidate_budget - prefix_candidates,
		true, prefix_best, prefix_next
	);
	const bool completed = search.run();
	const auto astar_end = Clock::now();
	const uint64_t combined_candidates =
		prefix_candidates + search.candidates();
	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.bounded_search_ns = prefix_ns + static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			astar_end - astar_start
		).count()
	);
	override_stats.candidates = combined_candidates;
	override_stats.patterns_considered =
		prefix_candidates + search.patterns();
	override_stats.pruned_candidates = combined_candidates < total
		? total - combined_candidates
		: 0;
	override_stats.bound_checks = prefix.checks() + search.checks();
	override_stats.dp_bytes = std::max(prefix.bytes(), search.bytes());
	best = search.best_metric();
	next = search.next_metric();
	return completed;
}

enum class PublishedPolicy {
	trivial,
	dai,
	extra_parity,
	joint_dai_extra_parity
};

// Straightforward full-codeword implementation retained as a differential
// oracle for the optimized implementation below.  It follows the published
// TEP schedule and criteria literally, apart from suppressing duplicate
// re-encodings when e_L is zero (which cannot change the decoded codeword).
template <int N, int K, int O, PublishedPolicy POLICY>
class ReferencePublishedSkippingSearch {
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int DELTA = N - K < 4 ? N - K : 4;
	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<int8_t, W> current{};
	std::array<int, DELTA> extra_offsets{};
	int selected_information = 0;
	uint8_t selected_extra = 0;
	int information_cost = 0;
	int extra_cost = 0;
	double dai_compensation = 0.0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t considered_patterns = 0;
	uint64_t discarded_patterns = 0;

	static constexpr bool USES_EXTRA_PARITY =
		POLICY == PublishedPolicy::extra_parity ||
		POLICY == PublishedPolicy::joint_dai_extra_parity;
	static constexpr bool USES_DAI =
		POLICY == PublishedPolicy::dai ||
		POLICY == PublishedPolicy::joint_dai_extra_parity;

	int weighted_distance() const
	{
		int metric = 0;
		for (int index = 0; index < W; ++index)
			metric +=
				(1 - 2 * current[static_cast<std::size_t>(index)]) *
				soft[index];
		return (absolute_metric - metric) / 2;
	}

	void evaluate_current()
	{
		const int distance = weighted_distance();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			std::copy(current.begin(), current.end(), output);
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

	void consider_pattern()
	{
		++considered_patterns;
		if constexpr (USES_EXTRA_PARITY) {
			// The order-zero re-encoding is already the incumbent.  Extended
			// patterns containing only the delta coordinates reproduce it.
			if (!selected_information) {
				++discarded_patterns;
				return;
			}
		}

		if constexpr (
			POLICY == PublishedPolicy::trivial ||
			POLICY == PublishedPolicy::dai ||
			POLICY == PublishedPolicy::joint_dai_extra_parity
		) {
			const int lower_bound = information_cost +
				(USES_EXTRA_PARITY ? extra_cost : 0);
			const double test_value = static_cast<double>(lower_bound) +
				(USES_DAI ? dai_compensation : 0.0);
			if (test_value > best_distance) {
				++discarded_patterns;
				return;
			}
		}

		if constexpr (USES_EXTRA_PARITY) {
			for (int rank = 0; rank < DELTA; ++rank) {
				const int offset = extra_offsets[static_cast<std::size_t>(rank)];
				const bool predicted_error =
					current[static_cast<std::size_t>(K + offset)] !=
					(soft[K + offset] < 0);
				const bool proposed_error =
					(selected_extra >> rank) & uint8_t{1};
				if (predicted_error != proposed_error) {
					++discarded_patterns;
					return;
				}
			}
		}
		evaluate_current();
	}

	void toggle(int coordinate, bool selecting)
	{
		if (coordinate < K) {
			selected_information += selecting ? 1 : -1;
			information_cost += (selecting ? 1 : -1) *
				std::abs(static_cast<int>(soft[coordinate]));
			const int8_t *row = matrix + coordinate * W;
			for (int index = 0; index < W; ++index)
				current[static_cast<std::size_t>(index)] ^= row[index];
			return;
		}
		const int rank = coordinate - K;
		assert(rank >= 0 && rank < DELTA);
		const int offset = extra_offsets[static_cast<std::size_t>(rank)];
		selected_extra ^= static_cast<uint8_t>(uint8_t{1} << rank);
		extra_cost += (selecting ? 1 : -1) *
			std::abs(static_cast<int>(soft[K + offset]));
	}

	template <int REMAINING>
	void enumerate_exact_weight(int start = 0)
	{
		constexpr int dimensions = K + (USES_EXTRA_PARITY ? DELTA : 0);
		static_assert(REMAINING > 0);
		for (int coordinate = start;
			coordinate <= dimensions - REMAINING; ++coordinate) {
			toggle(coordinate, true);
			if constexpr (REMAINING == 1)
				consider_pattern();
			else
				enumerate_exact_weight<REMAINING - 1>(coordinate + 1);
			toggle(coordinate, false);
		}
	}

public:
	ReferencePublishedSkippingSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft)
	{
		static_assert(DELTA > 0);
		std::copy(base, base + W, current.begin());
		for (int index = 0; index < N; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));
		std::array<int, N - K> parity_offsets{};
		std::iota(parity_offsets.begin(), parity_offsets.end(), 0);
		std::stable_sort(
			parity_offsets.begin(), parity_offsets.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		std::copy_n(parity_offsets.begin(), DELTA, extra_offsets.begin());
		if constexpr (USES_DAI) {
			for (int offset = 0; offset < N - K; ++offset) {
				const bool is_extra = USES_EXTRA_PARITY && std::find(
					extra_offsets.begin(), extra_offsets.end(), offset
				) != extra_offsets.end();
				if (is_extra)
					continue;
				const int index = K + offset;
				const double reliability =
					std::abs(static_cast<int>(soft[index]));
				dai_compensation += reliability /
					(1.0 + std::exp(reliability * active_llr_scale));
			}
		}
	}

	void run()
	{
		// All four algorithms initialize the order-zero re-encoding before
		// applying their per-pattern skipping tests.
		++considered_patterns;
		evaluate_current();
		static_assert(O >= 1 && O <= 6);
		enumerate_exact_weight<1>();
		if constexpr (O >= 2) enumerate_exact_weight<2>();
		if constexpr (O >= 3) enumerate_exact_weight<3>();
		if constexpr (O >= 4) enumerate_exact_weight<4>();
		if constexpr (O >= 5) enumerate_exact_weight<5>();
		if constexpr (O >= 6) enumerate_exact_weight<6>();
		assert(selected_information == 0);
		assert(selected_extra == 0);
		assert(information_cost == 0);
		assert(extra_cost == 0);
		assert(std::equal(current.begin(), current.end(), base));
	}

	int best_metric() const
	{
		return absolute_metric - 2 * best_distance;
	}
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t discarded() const { return discarded_patterns; }
};

// Fair-runtime implementation of the same published rules.  Every method in
// the publication comparison now receives the same packed-parity metric path:
// a TEP updates a packed parity mismatch state, exact soft weight is obtained
// through frame-local byte tables, and a full codeword is constructed only
// when it improves the incumbent.  The screening rules and TEP schedules—not
// avoidable implementation overhead—are therefore what differ between modes.
template <int N, int K, int O, PublishedPolicy POLICY>
class PackedPublishedSkippingSearch {
	static constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	static constexpr int PARITY_LENGTH = N - K;
	static constexpr int PARITY_WORDS = (PARITY_LENGTH + 63) / 64;
	static constexpr int PARITY_BYTES = (PARITY_LENGTH + 7) / 8;
	static constexpr int DELTA = PARITY_LENGTH < 4 ? PARITY_LENGTH : 4;
	static constexpr bool USES_EXTRA_PARITY =
		POLICY == PublishedPolicy::extra_parity ||
		POLICY == PublishedPolicy::joint_dai_extra_parity;
	static constexpr bool USES_DAI =
		POLICY == PublishedPolicy::dai ||
		POLICY == PublishedPolicy::joint_dai_extra_parity;

	const int8_t *matrix;
	const int8_t *base;
	int8_t *output;
	const int8_t *soft;
	std::array<uint64_t, K * PARITY_WORDS> parity_row_masks{};
	std::array<uint64_t, PARITY_WORDS> parity_state{};
	std::array<int, PARITY_BYTES * 256> byte_costs{};
	std::array<int, DELTA> extra_offsets{};
	std::array<uint16_t, O> selected_rows{};
	std::array<int8_t, W> materialized{};
	int selected_information = 0;
	uint8_t selected_extra = 0;
	int information_cost = 0;
	int extra_cost = 0;
	double dai_compensation = 0.0;
	int absolute_metric = 0;
	int best_distance = std::numeric_limits<int>::max();
	int next_distance = std::numeric_limits<int>::max();
	uint64_t evaluated_candidates = 0;
	uint64_t materialized_candidates = 0;
	uint64_t considered_patterns = 0;
	uint64_t discarded_patterns = 0;

	int parity_cost() const
	{
		int cost = 0;
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			const auto value = static_cast<uint8_t>(
				parity_state[static_cast<std::size_t>(byte / 8)] >>
				(8 * (byte % 8))
			);
			cost += byte_costs[
				static_cast<std::size_t>(byte) * 256 + value
			];
		}
		return cost;
	}

	bool extra_parity_consistent() const
	{
		for (int rank = 0; rank < DELTA; ++rank) {
			const int offset = extra_offsets[static_cast<std::size_t>(rank)];
			const bool predicted_error =
				(parity_state[static_cast<std::size_t>(offset / 64)] >>
				 (offset % 64)) & uint64_t{1};
			const bool proposed_error =
				(selected_extra >> rank) & uint8_t{1};
			if (predicted_error != proposed_error)
				return false;
		}
		return true;
	}

	void materialize()
	{
		std::copy(base, base + W, materialized.begin());
		for (int index = 0; index < selected_information; ++index) {
			const int row = selected_rows[static_cast<std::size_t>(index)];
			const int8_t *matrix_row = matrix + row * W;
			for (int column = 0; column < W; ++column)
				materialized[static_cast<std::size_t>(column)] ^=
					matrix_row[column];
		}
		std::copy(materialized.begin(), materialized.end(), output);
		++materialized_candidates;
	}

	void evaluate_current()
	{
		const int distance = information_cost + parity_cost();
		if (distance < best_distance) {
			next_distance = best_distance;
			best_distance = distance;
			materialize();
		} else if (distance < next_distance) {
			next_distance = distance;
		}
		++evaluated_candidates;
	}

	void consider_pattern()
	{
		++considered_patterns;
		if constexpr (USES_EXTRA_PARITY) {
			// An extended TEP with e_L=0 can only reproduce the already
			// evaluated order-zero codeword.  Suppress that duplicate so it
			// cannot manufacture a false metric tie.
			if (!selected_information) {
				++discarded_patterns;
				return;
			}
		}

		// Section 3.3 specifies soft-weight screening before the extra
		// parity test for the joint rule.  Pure extra-parity skipping has
		// no Trivial test; its sole criterion is consistency.
		if constexpr (
			POLICY == PublishedPolicy::trivial ||
			POLICY == PublishedPolicy::dai ||
			POLICY == PublishedPolicy::joint_dai_extra_parity
		) {
			const int lower_bound = information_cost +
				(USES_EXTRA_PARITY ? extra_cost : 0);
			const double test_value = static_cast<double>(lower_bound) +
				(USES_DAI ? dai_compensation : 0.0);
			if (test_value > best_distance) {
				++discarded_patterns;
				return;
			}
		}

		if constexpr (USES_EXTRA_PARITY) {
			if (!extra_parity_consistent()) {
				++discarded_patterns;
				return;
			}
		}
		evaluate_current();
	}

	void toggle(int coordinate, bool selecting)
	{
		if (coordinate < K) {
			if (selecting) {
				assert(selected_information < O);
				selected_rows[
					static_cast<std::size_t>(selected_information++)
				] = static_cast<uint16_t>(coordinate);
			} else {
				assert(selected_information > 0);
				assert(selected_rows[
					static_cast<std::size_t>(selected_information - 1)
				] == coordinate);
				--selected_information;
			}
			information_cost += (selecting ? 1 : -1) *
				std::abs(static_cast<int>(soft[coordinate]));
			for (int word = 0; word < PARITY_WORDS; ++word)
				parity_state[static_cast<std::size_t>(word)] ^=
					parity_row_masks[
						static_cast<std::size_t>(coordinate) * PARITY_WORDS +
						static_cast<std::size_t>(word)
					];
			return;
		}
		const int rank = coordinate - K;
		assert(rank >= 0 && rank < DELTA);
		const int offset = extra_offsets[static_cast<std::size_t>(rank)];
		selected_extra ^= static_cast<uint8_t>(uint8_t{1} << rank);
		extra_cost += (selecting ? 1 : -1) *
			std::abs(static_cast<int>(soft[K + offset]));
	}

	template <int REMAINING>
	void enumerate_exact_weight(int start = 0)
	{
		constexpr int dimensions = K + (USES_EXTRA_PARITY ? DELTA : 0);
		static_assert(REMAINING > 0);
		for (int coordinate = start;
			coordinate <= dimensions - REMAINING; ++coordinate) {
			toggle(coordinate, true);
			if constexpr (REMAINING == 1)
				consider_pattern();
			else
				enumerate_exact_weight<REMAINING - 1>(coordinate + 1);
			toggle(coordinate, false);
		}
	}

public:
	PackedPublishedSkippingSearch(
		const int8_t *input_matrix,
		const int8_t *input_base,
		int8_t *input_output,
		const int8_t *input_soft
	):
		matrix(input_matrix),
		base(input_base),
		output(input_output),
		soft(input_soft)
	{
		static_assert(PARITY_LENGTH > 0);
		static_assert(DELTA > 0);
		for (int index = 0; index < N; ++index)
			absolute_metric += std::abs(static_cast<int>(soft[index]));

		for (int row = 0; row < K; ++row) {
			for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
				if (matrix[row * W + K + offset])
					parity_row_masks[
						static_cast<std::size_t>(row) * PARITY_WORDS +
						static_cast<std::size_t>(offset / 64)
					] |= uint64_t{1} << (offset % 64);
			}
		}
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			if (base[K + offset] != (soft[K + offset] < 0))
				parity_state[static_cast<std::size_t>(offset / 64)] |=
					uint64_t{1} << (offset % 64);
		}
		for (int byte = 0; byte < PARITY_BYTES; ++byte) {
			for (int value = 1; value < 256; ++value) {
				const int bit = __builtin_ctz(
					static_cast<unsigned int>(value)
				);
				const int offset = byte * 8 + bit;
				byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value)
				] = byte_costs[
					static_cast<std::size_t>(byte) * 256 +
					static_cast<std::size_t>(value & (value - 1))
				] + (offset < PARITY_LENGTH
					? std::abs(static_cast<int>(soft[K + offset]))
					: 0);
			}
		}

		std::array<int, PARITY_LENGTH> parity_offsets{};
		std::iota(parity_offsets.begin(), parity_offsets.end(), 0);
		std::stable_sort(
			parity_offsets.begin(), parity_offsets.end(),
			[input_soft](int left, int right) {
				const int left_cost = std::abs(
					static_cast<int>(input_soft[K + left])
				);
				const int right_cost = std::abs(
					static_cast<int>(input_soft[K + right])
				);
				if (left_cost != right_cost)
					return left_cost > right_cost;
				return left < right;
			}
		);
		std::copy_n(parity_offsets.begin(), DELTA, extra_offsets.begin());

		if constexpr (USES_DAI) {
			for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
				const bool is_extra = USES_EXTRA_PARITY && std::find(
					extra_offsets.begin(), extra_offsets.end(), offset
				) != extra_offsets.end();
				if (is_extra)
					continue;
				const double reliability = std::abs(
					static_cast<int>(soft[K + offset])
				);
				dai_compensation += reliability /
					(1.0 + std::exp(reliability * active_llr_scale));
			}
		}

		std::copy(base, base + W, output);
		best_distance = parity_cost();
		considered_patterns = 1;
		evaluated_candidates = 1;
		materialized_candidates = 1;
	}

	void run()
	{
		static_assert(O >= 1 && O <= 6);
		enumerate_exact_weight<1>();
		if constexpr (O >= 2) enumerate_exact_weight<2>();
		if constexpr (O >= 3) enumerate_exact_weight<3>();
		if constexpr (O >= 4) enumerate_exact_weight<4>();
		if constexpr (O >= 5) enumerate_exact_weight<5>();
		if constexpr (O >= 6) enumerate_exact_weight<6>();
		assert(selected_information == 0);
		assert(selected_extra == 0);
		assert(information_cost == 0);
		assert(extra_cost == 0);
		for (int offset = 0; offset < PARITY_LENGTH; ++offset) {
			const bool initial_error =
				base[K + offset] != (soft[K + offset] < 0);
			const bool current_error =
				(parity_state[static_cast<std::size_t>(offset / 64)] >>
				 (offset % 64)) & uint64_t{1};
			assert(initial_error == current_error);
		}
		for (int index = N; index < W; ++index)
			assert(output[index] == 0);
	}

	int best_metric() const { return absolute_metric - 2 * best_distance; }
	int next_metric() const
	{
		return next_distance == std::numeric_limits<int>::max()
			? -1
			: absolute_metric - 2 * next_distance;
	}
	uint64_t candidates() const { return evaluated_candidates; }
	uint64_t materializations() const { return materialized_candidates; }
	uint64_t patterns() const { return considered_patterns; }
	uint64_t discarded() const { return discarded_patterns; }
	std::size_t bytes() const
	{
		return parity_row_masks.size() * sizeof(uint64_t) +
			parity_state.size() * sizeof(uint64_t) +
			byte_costs.size() * sizeof(int) +
			extra_offsets.size() * sizeof(int) +
			selected_rows.size() * sizeof(uint16_t);
	}
};

bool is_published_mode(Mode mode)
{
	return mode == Mode::published_trivial ||
		mode == Mode::published_dai ||
		mode == Mode::published_extra_parity_delta4 ||
		mode == Mode::published_joint_dai_delta4;
}

bool is_orbgrand_mode(Mode mode)
{
	return mode == Mode::orbgrand_queries_1k ||
		mode == Mode::orbgrand_queries_10k ||
		mode == Mode::orbgrand_queries_100k ||
		mode == Mode::orbgrand_queries_1m ||
		mode == Mode::orbgrand_1line_auto_queries_1m;
}

bool is_orbgrand_clm_cascade_mode(Mode mode)
{
	return mode == Mode::orbgrand_1line_then_clm_queries_16 ||
		mode == Mode::orbgrand_1line_then_clm_queries_64 ||
		mode == Mode::orbgrand_1line_then_clm_queries_256 ||
		mode == Mode::orbgrand_1line_then_clm_queries_1k;
}

uint64_t orbgrand_clm_cascade_query_budget(Mode mode)
{
	switch (mode) {
	case Mode::orbgrand_1line_then_clm_queries_16: return 16;
	case Mode::orbgrand_1line_then_clm_queries_64: return 64;
	case Mode::orbgrand_1line_then_clm_queries_256: return 256;
	case Mode::orbgrand_1line_then_clm_queries_1k: return 1000;
	default: assert(false); return 0;
	}
}

uint64_t orbgrand_query_budget(Mode mode)
{
	switch (mode) {
	case Mode::orbgrand_queries_1k: return 1000;
	case Mode::orbgrand_queries_10k: return 10000;
	case Mode::orbgrand_queries_100k: return 100000;
	case Mode::orbgrand_queries_1m: return 1000000;
	case Mode::orbgrand_1line_auto_queries_1m: return 1000000;
	default: assert(false); return 0;
	}
}

bool is_exact_mode(Mode mode)
{
	return mode == Mode::baseline || mode == Mode::exact_stop_only ||
		mode == Mode::soft_weight_exact ||
		mode == Mode::guarded_soft_weight_exact ||
		mode == Mode::guarded_projected_astar_exact ||
		mode == Mode::guarded_adaptive_projected_astar_exact ||
		mode == Mode::guarded_projected_screen_exact ||
		mode == Mode::guarded_hierarchical_projected_screen_exact ||
		mode == Mode::guarded_portfolio_projected_screen_exact ||
		mode == Mode::guarded_syndrome_sketch_screen_exact ||
		mode == Mode::guarded_discover_certify_exact ||
		mode == Mode::guarded_packed_parity_certify_exact ||
		mode == Mode::adaptive_packed_dispatch_exact ||
		mode == Mode::guarded_additive_pdb_exact ||
		mode == Mode::guarded_cost_partition_pdb_exact ||
		mode == Mode::guarded_fingerprint_pdb_exact ||
		mode == Mode::guarded_quotient_pdb_exact ||
		mode == Mode::guarded_query_local_pdb_exact ||
		mode == Mode::guarded_packed_state_pdb_exact ||
		mode == Mode::guarded_witness_packed_state_pdb_exact ||
		mode == Mode::reconstructed_normal_cap_pdb_exact ||
		mode == Mode::reconstructed_compact_two_block_pdb_exact ||
		mode == Mode::reconstructed_dual_two_block_pdb_exact ||
		mode == Mode::oracle_selective_coupling_pdb_exact ||
		mode == Mode::oracle_perfect_subtree_pdb_exact ||
		mode == Mode::oracle_perfect_seed_subtree_pdb_exact ||
		mode == Mode::guarded_orb_seeded_packed_pdb_exact ||
		mode == Mode::bitplane_frontier_exact ||
		mode == Mode::transposed_frontier_exact ||
		mode == Mode::adaptive_packed_state_pdb_exact ||
		mode == Mode::adaptive_additive_pdb_exact ||
		mode == Mode::adaptive_cost_partition_pdb_exact ||
		mode == Mode::published_trivial || mode == Mode::parity_window ||
		mode == Mode::guarded_parity_window;
}

template <PublishedPolicy POLICY, int N, int K, int O>
void execute_published_search(
	const int8_t *matrix,
	const int8_t *base,
	int8_t *candidate,
	const int8_t *soft,
	int &best,
	int &next
)
{
	#ifdef OSD_PUBLISHED_DIFFERENTIAL
	constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	std::array<int8_t, W> reference_candidate{};
	std::copy(candidate, candidate + W, reference_candidate.begin());
	ReferencePublishedSkippingSearch<N, K, O, POLICY> reference(
		matrix, base, reference_candidate.data(), soft
	);
	reference.run();
	#endif

	const auto search_start = Clock::now();
	PackedPublishedSkippingSearch<N, K, O, POLICY> search(
		matrix, base, candidate, soft
	);
	search.run();
	const auto search_end = Clock::now();
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - search_start
		).count()
	);
	override_stats.candidates = search.candidates();
	override_stats.materializations = search.materializations();
	override_stats.patterns_considered = search.patterns();
	override_stats.pruned_candidates = search.discarded();
	override_stats.dp_bytes = search.bytes();
	best = search.best_metric();
	next = search.next_metric();

	#ifdef OSD_PUBLISHED_DIFFERENTIAL
	assert(best == reference.best_metric());
	assert(next == reference.next_metric());
	assert(search.candidates() == reference.candidates());
	assert(search.patterns() == reference.patterns());
	assert(search.discarded() == reference.discarded());
	assert(std::equal(
		candidate, candidate + W, reference_candidate.begin()
	));
	#endif
}

#endif

template <int N, int K, int O>
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
	constexpr int W =
		(N + static_cast<int>(sizeof(std::size_t)) - 1) &
		~(static_cast<int>(sizeof(std::size_t)) - 1);
	assert(length == N);
	assert(dimension == K);
	assert(width == W);
	assert(order == O);
	if (active_mode == Mode::baseline)
		return false;
	const uint64_t exhaustive_candidates = candidate_count(K, O);
	#ifdef OSD_NOVELTY_COMPARISON
	if (
		(active_mode == Mode::guarded_packed_parity_certify_exact ||
			active_mode == Mode::adaptive_packed_dispatch_exact ||
			active_mode == Mode::adaptive_additive_pdb_exact ||
			active_mode == Mode::adaptive_cost_partition_pdb_exact ||
			active_mode == Mode::guarded_fingerprint_pdb_exact ||
			active_mode == Mode::guarded_quotient_pdb_exact ||
			active_mode == Mode::guarded_query_local_pdb_exact ||
			active_mode == Mode::guarded_packed_state_pdb_exact ||
			active_mode == Mode::guarded_witness_packed_state_pdb_exact ||
			active_mode == Mode::reconstructed_normal_cap_pdb_exact ||
			active_mode == Mode::reconstructed_compact_two_block_pdb_exact ||
			active_mode == Mode::reconstructed_dual_two_block_pdb_exact ||
			active_mode == Mode::oracle_selective_coupling_pdb_exact ||
			active_mode == Mode::oracle_perfect_subtree_pdb_exact ||
			active_mode == Mode::oracle_perfect_seed_subtree_pdb_exact ||
			active_mode == Mode::guarded_orb_seeded_packed_pdb_exact ||
			active_mode == Mode::adaptive_packed_state_pdb_exact) &&
			exhaustive_candidates < PACKED_MIN_CANDIDATES
	) {
		// The frame-local packed tables cannot amortize their fixed setup
		// cost on tiny OSD lists.  Static dispatch preserves conventional
		// OSD's latency and exact result in that regime.
		override_stats = {};
		override_stats.candidates = exhaustive_candidates;
		override_stats.materializations = exhaustive_candidates;
		override_stats.patterns_considered = exhaustive_candidates;
		override_stats.fallback = true;
		override_stats.dispatcher_baseline =
			active_mode == Mode::adaptive_packed_dispatch_exact ||
			active_mode == Mode::adaptive_additive_pdb_exact ||
			active_mode == Mode::adaptive_cost_partition_pdb_exact ||
			active_mode == Mode::adaptive_packed_state_pdb_exact;
		return false;
	}
	#endif

	override_stats = {};
	const auto check_start = Clock::now();
	int order0_metric = 0;
	int absolute = 0;
	bool all_nonzero = true;
	for (int index = 0; index < W; ++index) {
		order0_metric += (1 - 2 * base[index]) * soft[index];
		absolute += std::abs(static_cast<int>(soft[index]));
		if (index < N && soft[index] == 0)
			all_nonzero = false;
	}
	const auto check_end = Clock::now();
	override_stats.exact_check_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			check_end - check_start
		).count()
	);

	if (
		(active_mode == Mode::adaptive_additive_pdb_exact ||
			active_mode == Mode::adaptive_packed_state_pdb_exact) &&
		(order != 4 || dimension < 32 || dimension > 48)
	) {
		// Frozen support envelope: outside the combinatorially expensive
		// order-4, medium-K list, preserve the conventional exact path.
		override_stats.candidates = exhaustive_candidates;
		override_stats.materializations = exhaustive_candidates;
		override_stats.patterns_considered = exhaustive_candidates;
		override_stats.fallback = true;
		override_stats.dispatcher_baseline = true;
		return false;
	}

	if (all_nonzero && order0_metric == absolute) {
		std::copy(base, base + W, candidate);
		best = order0_metric;
		next = -1;
		override_stats.candidates = 1;
		override_stats.patterns_considered = 1;
		override_stats.pruned_candidates =
			candidate_count(K, O) - 1;
		override_stats.exact_stop = true;
		return true;
	}
#ifdef OSD_NOVELTY_COMPARISON
	if (active_mode == Mode::transposed_frontier_exact) {
		const bool completed = execute_bitplane_frontier_search<
			N, K, O, true
		>(matrix, base, candidate, soft, best, next);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.materializations += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::bitplane_frontier_exact) {
		return execute_bitplane_frontier_search<N, K, O>(
			matrix, base, candidate, soft, best, next
		);
	}
	if (active_mode == Mode::adaptive_packed_dispatch_exact) {
		if constexpr (K <= 48) {
			const int order0_distance = (absolute - order0_metric) / 2;
			// Frozen on seeds 101/202/303 and calibrated on seed 404.
			// Integer cross-multiplication implements d0/sum|r| > 0.081
			// without floating point or a speculative candidate probe.
			if (1000LL * order0_distance > 81LL * absolute) {
				override_stats.candidates = exhaustive_candidates;
				override_stats.materializations = exhaustive_candidates;
				override_stats.patterns_considered = exhaustive_candidates;
				override_stats.fallback = true;
				override_stats.dispatcher_baseline = true;
				return false;
			}
		}
		const bool completed = execute_packed_parity_certify_search<N, K, O>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.materializations += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::adaptive_cost_partition_pdb_exact) {
		if constexpr (K > 48) {
			// The frozen development scope is low-dimension, order-4 OSD.  A
			// static code-parameter guard makes unsupported dimensions pay no
			// frame-local pattern-database construction cost.
			override_stats.candidates = exhaustive_candidates;
			override_stats.materializations = exhaustive_candidates;
			override_stats.patterns_considered = exhaustive_candidates;
			override_stats.fallback = true;
			override_stats.dispatcher_baseline = true;
			return false;
		} else {
			const int order0_distance = (absolute - order0_metric) / 2;
			// Experimental cost-partition ablation shares the active PDB guard.
			if (
				1000LL * order0_distance >
				static_cast<long long>(ADDITIVE_PDB_DISPATCH_MILLI) * absolute
			) {
				override_stats.candidates = exhaustive_candidates;
				override_stats.materializations = exhaustive_candidates;
				override_stats.patterns_considered = exhaustive_candidates;
				override_stats.fallback = true;
				override_stats.dispatcher_baseline = true;
				return false;
			}
		}
		const bool completed =
			execute_cost_partition_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::adaptive_packed_state_pdb_exact) {
		const int order0_distance = (absolute - order0_metric) / 2;
		if (
			1000LL * order0_distance >
			static_cast<long long>(PACKED_PDB_DISPATCH_MILLI) * absolute
		) {
			override_stats.candidates = exhaustive_candidates;
			override_stats.materializations = exhaustive_candidates;
			override_stats.patterns_considered = exhaustive_candidates;
			override_stats.fallback = true;
			override_stats.dispatcher_baseline = true;
			return false;
		}
		const bool completed =
			execute_additive_pdb_discover_certify_search<
				N, K, O, false, true
			>(
				matrix, base, candidate, soft, best, next,
				ADDITIVE_PDB_DISCOVERY_BUDGET
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::adaptive_additive_pdb_exact) {
		const int order0_distance = (absolute - order0_metric) / 2;
		// Frozen on development data before the precision, family, channel,
		// and publication-comparator holdouts.  Integer cross-multiplication
		// implements d0/sum|r| <= 0.080 without floating point or a
		// speculative candidate probe.
		if (
			1000LL * order0_distance >
			static_cast<long long>(ADDITIVE_PDB_DISPATCH_MILLI) * absolute
		) {
			override_stats.candidates = exhaustive_candidates;
			override_stats.materializations = exhaustive_candidates;
			override_stats.patterns_considered = exhaustive_candidates;
			override_stats.fallback = true;
			override_stats.dispatcher_baseline = true;
			return false;
		}
		const bool completed =
			execute_additive_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				ADDITIVE_PDB_DISCOVERY_BUDGET
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::exact_stop_only) {
		override_stats.candidates = exhaustive_candidates;
		override_stats.patterns_considered = exhaustive_candidates;
		return false;
	}
	if (active_mode == Mode::soft_weight_exact ||
		active_mode == Mode::guarded_soft_weight_exact) {
		const uint64_t budget =
			active_mode == Mode::guarded_soft_weight_exact
			? active_guard_candidate_budget
			: std::numeric_limits<uint64_t>::max();
		const bool completed = execute_soft_weight_search<N, K, O>(
			matrix, base, candidate, soft, best, next, budget
		);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + speculative;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_projected_astar_exact) {
		const bool completed = execute_projected_astar_search<N, K, O>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_adaptive_projected_astar_exact) {
		const bool completed =
			execute_adaptive_projected_astar_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_projected_screen_exact) {
		const bool completed = execute_projected_screen_search<N, K, O>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_hierarchical_projected_screen_exact) {
		const bool completed = execute_projected_screen_search<
			N, K, O, true
		>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_portfolio_projected_screen_exact) {
		bool completed = false;
		if constexpr (K <= 48) {
			completed = execute_soft_weight_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				std::min(
					active_guard_candidate_budget,
					PORTFOLIO_LOW_RATE_BUDGET
				)
			);
		} else {
			completed = execute_projected_screen_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		}
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_syndrome_sketch_screen_exact) {
		const bool completed = execute_projected_screen_search<
			N, K, O, false, true
		>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		if (!completed) {
			const uint64_t speculative = override_stats.candidates;
			override_stats.candidates = exhaustive_candidates + speculative;
			override_stats.patterns_considered =
				exhaustive_candidates + override_stats.patterns_considered;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_discover_certify_exact) {
		if constexpr (K <= 48) {
			// For low-rate codes the projected test is cheap because the
			// parity side is long.  A small bounded screen captures the
			// high-SNR wins; if it does not finish, exhaustive OSD remains
			// cheaper than launching a second certification traversal.
			const bool completed = execute_projected_screen_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				std::min(
					active_guard_candidate_budget,
					PORTFOLIO_LOW_RATE_BUDGET
				)
			);
			if (!completed) {
				const uint64_t speculative = override_stats.candidates;
				override_stats.candidates =
					exhaustive_candidates + speculative;
				override_stats.patterns_considered =
					exhaustive_candidates + speculative;
				override_stats.pruned_candidates = 0;
				override_stats.fallback = true;
				return false;
			}
			if (best == next) {
				override_stats.candidates += exhaustive_candidates;
				override_stats.patterns_considered +=
					exhaustive_candidates;
				override_stats.pruned_candidates = 0;
				override_stats.fallback = true;
				return false;
			}
			return true;
		} else {
			const bool completed =
				execute_projected_screen_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
			assert(completed);
			if (best == next) {
				override_stats.candidates += exhaustive_candidates;
				override_stats.patterns_considered +=
					exhaustive_candidates;
				override_stats.pruned_candidates = 0;
				override_stats.fallback = true;
				return false;
			}
			return true;
		}
	}
	if (active_mode == Mode::guarded_packed_parity_certify_exact) {
		const bool completed = execute_packed_parity_certify_search<N, K, O>(
			matrix, base, candidate, soft, best, next,
			active_guard_candidate_budget
		);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.materializations += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_additive_pdb_exact) {
		const bool completed =
			execute_additive_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_fingerprint_pdb_exact) {
		const bool completed =
			execute_fingerprint_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_quotient_pdb_exact) {
		const bool completed =
			execute_quotient_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_query_local_pdb_exact) {
		const bool completed =
			execute_additive_pdb_discover_certify_search<N, K, O, true>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_packed_state_pdb_exact) {
		const bool completed =
			execute_additive_pdb_discover_certify_search<
				N, K, O, false, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_witness_packed_state_pdb_exact) {
		const bool completed =
			execute_additive_pdb_discover_certify_search<
				N, K, O, false, true, false, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (
		active_mode == Mode::reconstructed_normal_cap_pdb_exact ||
		active_mode == Mode::reconstructed_compact_two_block_pdb_exact ||
		active_mode == Mode::reconstructed_dual_two_block_pdb_exact ||
		active_mode == Mode::reconstructed_compiled_dual_bank_pdb_exact
	) {
		bool completed = false;
		if (active_mode == Mode::reconstructed_normal_cap_pdb_exact)
			completed = execute_reconstructed_pdb_search<N, K, O, 0>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		else if (
			active_mode == Mode::reconstructed_compact_two_block_pdb_exact
		)
			completed = execute_reconstructed_pdb_search<N, K, O, 1>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		else if (active_mode == Mode::reconstructed_dual_two_block_pdb_exact)
			completed = execute_reconstructed_pdb_search<N, K, O, 2>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		else
			completed = execute_reconstructed_pdb_search<N, K, O, 3>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.scoring_calls += exhaustive_candidates;
			override_stats.unique_teps_evaluated = exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	#ifndef OSD_RECONSTRUCTED_SWEEP_ONLY
	if (active_mode == Mode::oracle_selective_coupling_pdb_exact) {
		auto &schedules = selective_oracle_schedules<K>();
		assert(active_selective_oracle_slot >= 0);
		assert(
			static_cast<std::size_t>(active_selective_oracle_slot) <
			schedules.size()
		);
		auto &schedule = schedules[
			static_cast<std::size_t>(active_selective_oracle_slot)
		];
		bool completed = false;
		if (preparing_selective_oracle) {
			schedule = {};
			completed = execute_additive_pdb_discover_certify_search<
				N, K, O,
				false, true, false, true,
				true, SELECTIVE_COUPLING_GROUP_LIMIT, true, false
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget, &schedule
			);
		} else {
			completed = execute_additive_pdb_discover_certify_search<
				N, K, O,
				false, true, false, true,
				false, SELECTIVE_COUPLING_GROUP_LIMIT, false, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget, &schedule
			);
		}
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (
		active_mode == Mode::oracle_perfect_subtree_pdb_exact ||
		active_mode == Mode::oracle_perfect_seed_subtree_pdb_exact
	) {
		auto &schedules = perfect_oracle_schedules<K, O>();
		assert(active_perfect_oracle_slot >= 0);
		assert(
			static_cast<std::size_t>(active_perfect_oracle_slot) <
			schedules.size()
		);
		auto &schedule = schedules[
			static_cast<std::size_t>(active_perfect_oracle_slot)
		];
		bool completed = false;
		if (preparing_perfect_oracle) {
			prepare_perfect_subtree_oracle<N, K, O>(
				matrix, base, soft, schedule
			);
			completed = execute_additive_pdb_discover_certify_search<
				N, K, O, false, true, false, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		} else if (
			active_mode == Mode::oracle_perfect_seed_subtree_pdb_exact
		) {
			completed = execute_perfect_subtree_oracle_search<
				N, K, O, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget, schedule
			);
		} else {
			completed = execute_perfect_subtree_oracle_search<
				N, K, O, false
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget, schedule
			);
		}
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	#endif
	if (active_mode == Mode::guarded_orb_seeded_packed_pdb_exact) {
		const bool completed =
			execute_additive_pdb_discover_certify_search<
				N, K, O, false, true, true
			>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (active_mode == Mode::guarded_cost_partition_pdb_exact) {
		const bool completed =
			execute_cost_partition_pdb_discover_certify_search<N, K, O>(
				matrix, base, candidate, soft, best, next,
				active_guard_candidate_budget
			);
		assert(completed);
		if (best == next) {
			override_stats.candidates += exhaustive_candidates;
			override_stats.patterns_considered += exhaustive_candidates;
			override_stats.pruned_candidates = 0;
			override_stats.fallback = true;
			return false;
		}
		return true;
	}
	if (is_published_mode(active_mode)) {
		switch (active_mode) {
		case Mode::published_trivial:
			execute_published_search<PublishedPolicy::trivial, N, K, O>(
				matrix, base, candidate, soft, best, next
			);
			break;
		case Mode::published_dai:
			execute_published_search<PublishedPolicy::dai, N, K, O>(
				matrix, base, candidate, soft, best, next
			);
			break;
		case Mode::published_extra_parity_delta4:
			execute_published_search<
				PublishedPolicy::extra_parity, N, K, O
			>(matrix, base, candidate, soft, best, next);
			break;
		case Mode::published_joint_dai_delta4:
			execute_published_search<
				PublishedPolicy::joint_dai_extra_parity, N, K, O
			>(matrix, base, candidate, soft, best, next);
			break;
		default: assert(false);
		}
		return true;
	}
#endif
	if (
		active_mode == Mode::guarded_parity_window &&
		exhaustive_candidates <= MIN_BOUND_WORKLOAD
	) {
		override_stats.candidates = exhaustive_candidates;
		override_stats.patterns_considered = exhaustive_candidates;
		override_stats.fallback = true;
		return false;
	}

	const auto build_start = Clock::now();
	ParityWindowDp<N, K, O> dp(matrix, base, soft);
	const auto build_end = Clock::now();
	override_stats.dp_build_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			build_end - build_start
		).count()
	);
	override_stats.dp_bytes = dp.bytes();

	const auto search_start = Clock::now();
	const uint64_t budget = active_mode == Mode::guarded_parity_window
		? active_guard_candidate_budget
		: std::numeric_limits<uint64_t>::max();
	BoundedSearch<N, K, O> search(
		matrix, base, candidate, soft, dp, budget
	);
	const bool completed = search.run();
	const auto search_end = Clock::now();
	override_stats.bounded_search_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			search_end - search_start
		).count()
	);
	override_stats.candidates = search.evaluated();
	override_stats.patterns_considered = exhaustive_candidates;
	override_stats.pruned_candidates = search.pruned();
	override_stats.pruned_subtrees = search.subtrees();
	override_stats.bound_checks = search.checks();
	if (!completed) {
		override_stats.candidates =
			exhaustive_candidates + search.evaluated();
		override_stats.patterns_considered =
			exhaustive_candidates + search.evaluated();
		override_stats.pruned_candidates = 0;
		override_stats.fallback = true;
		return false;
	}
	best = search.best_metric();
	next = search.next_metric();
	return true;
}

template <int N, int K, int O>
struct DecodeResult {
	std::array<uint8_t, (N + 7) / 8> decoded{};
	bool unique = false;
	int best_metric = 0;
	int next_metric = -1;
	uint64_t total_ns = 0;
	std::array<uint64_t, STAGE_COUNT> stages{};
	OverrideStats stats;
};

template <int N, int K, int O>
DecodeResult<N, K, O> decode_once(
	CODE::OrderedStatisticsDecoder<N, K, O> &decoder,
	const std::array<int8_t, N> &soft,
	const std::array<int8_t, N * K> &genmat,
	Mode mode
)
{
	active_mode = mode;
	stage_elapsed.fill(0);
	override_stats = {};
	completed_best = 0;
	completed_next = -1;
	DecodeResult<N, K, O> result;
	const auto start = Clock::now();
	result.unique = decoder(
		result.decoded.data(), soft.data(), genmat.data()
	);
	const auto end = Clock::now();
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			end - start
		).count()
	);
	result.stages = stage_elapsed;
	result.stats = override_stats;
	result.best_metric = completed_best;
	result.next_metric = completed_next;
	if (mode == Mode::baseline) {
		result.stats.candidates = candidate_count(K, O);
		result.stats.patterns_considered = candidate_count(K, O);
	}
	if (result.stats.materializations == 0)
		result.stats.materializations = result.stats.candidates;
	return result;
}

#ifdef OSD_NOVELTY_COMPARISON
template <int N, int K, int O>
DecodeResult<N, K, O> decode_orbgrand_once(
	const BasicOrbgrandDecoder<N, K> &decoder,
	const std::array<int8_t, N> &soft,
	Mode mode
)
{
	assert(is_orbgrand_mode(mode));
	DecodeResult<N, K, O> result;
	const auto start = Clock::now();
	const auto decoded = mode == Mode::orbgrand_1line_auto_queries_1m
		? decoder.decode_one_line(soft, orbgrand_query_budget(mode))
		: decoder.decode(soft, orbgrand_query_budget(mode));
	const auto end = Clock::now();
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	result.decoded = decoded.decoded;
	result.unique = decoded.found;
	result.best_metric = decoded.found ? decoded.metric : 0;
	result.next_metric = -1;
	result.stages[7] = result.total_ns;
	result.stats.candidates = decoded.queries;
	result.stats.materializations = decoded.found ? 1 : 0;
	result.stats.patterns_considered = decoded.queries;
	result.stats.pruned_candidates = decoded.found &&
		decoded.queries < orbgrand_query_budget(mode)
		? orbgrand_query_budget(mode) - decoded.queries
		: 0;
	result.stats.bounded_search_ns = result.total_ns;
	result.stats.fallback = !decoded.found;
	return result;
}

template <int N, int K, int O>
DecodeResult<N, K, O> decode_orbgrand_clm_cascade_once(
	const BasicOrbgrandDecoder<N, K> &orbgrand,
	CODE::OrderedStatisticsDecoder<N, K, O> &clm,
	const std::array<int8_t, N> &soft,
	const std::array<int8_t, N * K> &genmat,
	Mode mode
)
{
	assert(is_orbgrand_clm_cascade_mode(mode));
	const uint64_t query_budget = orbgrand_clm_cascade_query_budget(mode);
	DecodeResult<N, K, O> result;
	const auto start = Clock::now();
	const auto orb_result = orbgrand.decode_one_line(soft, query_budget);
	const auto orb_end = Clock::now();
	const uint64_t orb_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			orb_end - start
		).count()
	);

	if (orb_result.found) {
		result.decoded = orb_result.decoded;
		result.unique = true;
		result.best_metric = orb_result.metric;
		result.next_metric = -1;
		result.total_ns = orb_ns;
		result.stages[7] = orb_ns;
		result.stats.candidates = orb_result.queries;
		result.stats.materializations = 1;
		result.stats.patterns_considered = orb_result.queries;
		result.stats.pruned_candidates = query_budget - orb_result.queries;
		result.stats.bounded_search_ns = orb_ns;
		return result;
	}

	DecodeResult<N, K, O> clm_result = decode_once<N, K, O>(
		clm, soft, genmat, Mode::guarded_packed_parity_certify_exact
	);
	const auto end = Clock::now();
	result = clm_result;
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
	);
	result.stages[7] += orb_ns;
	result.stats.candidates += orb_result.queries;
	result.stats.patterns_considered += orb_result.queries;
	result.stats.bounded_search_ns += orb_ns;
	result.stats.fallback = true;
	return result;
}
#endif

template <int N, int K, int O>
void assert_equal(
	const DecodeResult<N, K, O> &expected,
	const DecodeResult<N, K, O> &actual
)
{
	assert(expected.decoded == actual.decoded);
	assert(expected.unique == actual.unique);
	assert(expected.best_metric == actual.best_metric);
}

uint64_t checksum_bytes(const uint8_t *data, std::size_t size, bool unique)
{
	uint64_t value = 14695981039346656037ULL;
	for (std::size_t index = 0; index < size; ++index) {
		value ^= data[index];
		value *= 1099511628211ULL;
	}
	value ^= unique;
	value *= 1099511628211ULL;
	return value;
}

uint64_t percentile(std::vector<uint64_t> values, double fraction)
{
	assert(!values.empty());
	std::sort(values.begin(), values.end());
	const std::size_t index = static_cast<std::size_t>(
		std::ceil(fraction * values.size()) - 1
	);
	return values[std::min(index, values.size() - 1)];
}

double mean(const std::vector<uint64_t> &values)
{
	return static_cast<double>(
		std::accumulate(values.begin(), values.end(), uint64_t{0})
	) / values.size();
}

struct ModeSamples {
	std::vector<uint64_t> total;
	std::array<std::vector<uint64_t>, STAGE_COUNT> stages;
	uint64_t candidates = 0;
	uint64_t materializations = 0;
	uint64_t pruned_candidates = 0;
	uint64_t exact_stops = 0;
	uint64_t fallbacks = 0;
	uint64_t dp_build_ns = 0;
	uint64_t bounded_search_ns = 0;
	std::size_t maximum_dp_bytes = 0;
};

struct Metrics {
	double mean_ns = 0;
	uint64_t p50_ns = 0;
	uint64_t p95_ns = 0;
	uint64_t p99_ns = 0;
	uint64_t maximum_ns = 0;
};

Metrics metrics(const std::vector<uint64_t> &values)
{
	return {
		mean(values),
		percentile(values, 0.50),
		percentile(values, 0.95),
		percentile(values, 0.99),
		*std::max_element(values.begin(), values.end())
	};
}

template <int N, int K>
struct Frame {
	std::array<int8_t, N> soft{};
	std::array<uint8_t, (N + 7) / 8> encoded{};
	std::array<uint8_t, (N + 7) / 8> reference{};
	bool reference_unique = false;
	int reference_best_metric = 0;
	uint64_t bit_errors = 0;
	bool frame_error = false;
#ifdef OSD_ADAPTIVE_DATASET
	AdaptiveFeatures adaptive_features;
#endif
};

template <int N, int K>
std::array<int8_t, N * K> random_systematic_matrix(uint64_t seed)
{
	std::array<int8_t, N * K> matrix{};
	std::mt19937_64 random(seed);
	for (int row = 0; row < K; ++row) {
		for (int column = 0; column < K; ++column)
			matrix[static_cast<std::size_t>(row * N + column)] =
				row == column;
		for (int column = K; column < N; ++column)
			matrix[static_cast<std::size_t>(row * N + column)] =
				static_cast<int8_t>(random() & 1);
	}
	return matrix;
}

template <int K>
std::array<int8_t, 127 * K> bch127_matrix(
	std::initializer_list<int> minimal_polynomials
)
{
	std::array<int8_t, 127 * K> matrix{};
	CODE::BoseChaudhuriHocquenghemGenerator<127, K>::matrix(
		matrix.data(), true, minimal_polynomials
	);
	return matrix;
}

template <int K>
std::array<int8_t, 128 * K> extend_even_parity(
	const std::array<int8_t, 127 * K> &input
)
{
	std::array<int8_t, 128 * K> output{};
	for (int row = 0; row < K; ++row) {
		int8_t parity = 0;
		for (int column = 0; column < 127; ++column) {
			const int8_t value = input[
				static_cast<std::size_t>(row * 127 + column)
			];
			output[static_cast<std::size_t>(row * 128 + column)] = value;
			parity ^= value;
		}
		output[static_cast<std::size_t>(row * 128 + 127)] = parity;
	}
	return output;
}

template <int N, int K>
std::array<int8_t, N * K> polar_bec_matrix()
{
	static_assert(N > 1 && (N & (N - 1)) == 0);
	static_assert(K > 0 && K < N);
	int levels = 0;
	for (int value = N; value > 1; value >>= 1)
		++levels;
	std::vector<std::pair<double, int>> channels;
	channels.reserve(N);
	for (int index = 0; index < N; ++index) {
		double erasure = 0.5;
		for (int bit = levels - 1; bit >= 0; --bit)
			erasure = (index >> bit) & 1
				? erasure * erasure
				: 2.0 * erasure - erasure * erasure;
		channels.emplace_back(erasure, index);
	}
	std::stable_sort(channels.begin(), channels.end());

	std::array<int8_t, N * K> matrix{};
	for (int row = 0; row < K; ++row) {
		const int channel = channels[static_cast<std::size_t>(row)].second;
		int source = 0;
		for (int bit = 0; bit < levels; ++bit)
			source |= ((channel >> bit) & 1) << (levels - 1 - bit);
		for (int column = 0; column < N; ++column)
			matrix[static_cast<std::size_t>(row * N + column)] =
				(column & ~source) == 0;
	}
	return matrix;
}

template <int N, int K, int O>
void run_configuration(
	const std::string &code_name,
	const std::array<int8_t, N * K> &genmat,
	std::mt19937_64 &random,
	std::ofstream &raw,
	std::ofstream &summary
)
{
	constexpr std::array<double, 4> SNR_VALUES{4.0, 6.0, 8.0, 10.0};
	constexpr std::array<Mode, 3> MODES{
		Mode::baseline,
		Mode::parity_window,
		Mode::guarded_parity_window
	};
	CODE::LinearEncoder<N, K> encoder;
	CODE::OrderedStatisticsDecoder<N, K, O> reference_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> baseline_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> bounded_decoder;
	CODE::OrderedStatisticsDecoder<N, K, O> guarded_decoder;
	std::uniform_int_distribution<int> bit(0, 1);

	for (const double snr_db: SNR_VALUES) {
		const double snr = std::pow(10.0, snr_db / 10.0);
		const double rate = static_cast<double>(K) / N;
		const double sigma = std::sqrt(1.0 / (2.0 * rate * snr));
		std::normal_distribution<double> noise(0.0, sigma);
		std::vector<Frame<N, K>> frames(
			static_cast<std::size_t>(FRAMES_PER_SNR)
		);
		uint64_t frame_errors = 0;
		uint64_t bit_errors = 0;

		for (Frame<N, K> &frame: frames) {
			std::array<uint8_t, (K + 7) / 8> message{};
			for (int index = 0; index < K; ++index)
				CODE::set_be_bit(message.data(), index, bit(random));
			encoder(frame.encoded.data(), message.data(), genmat.data());
			for (int index = 0; index < N; ++index) {
				const double symbol = CODE::get_be_bit(
					frame.encoded.data(), index
				) ? -1.0 : 1.0;
				const double received = symbol + noise(random);
				const int magnitude = std::max(
					1,
					std::min(SOFT_MAGNITUDE_MAX, static_cast<int>(std::lround(
						std::abs(received) * SOFT_QUANTIZATION_SCALE
					)))
				);
				frame.soft[static_cast<std::size_t>(index)] = received < 0
					? static_cast<int8_t>(-magnitude)
					: static_cast<int8_t>(magnitude);
			}
			#ifdef OSD_ADAPTIVE_DATASET
			captured_adaptive_features = {};
			capture_adaptive_enabled = true;
			#endif
			const auto reference = decode_once<N, K, O>(
				reference_decoder, frame.soft, genmat, Mode::baseline
			);
			#ifdef OSD_ADAPTIVE_DATASET
			capture_adaptive_enabled = false;
			frame.adaptive_features = captured_adaptive_features;
			assert(frame.adaptive_features.extraction_ns > 0);
			#endif
			frame.reference = reference.decoded;
			frame.reference_unique = reference.unique;
			frame.reference_best_metric = reference.best_metric;
			for (int index = 0; index < N; ++index) {
				const bool wrong = CODE::get_be_bit(
					frame.reference.data(), index
				) != CODE::get_be_bit(frame.encoded.data(), index);
				frame.bit_errors += wrong;
			}
			frame.frame_error = frame.bit_errors != 0;
			bit_errors += frame.bit_errors;
			frame_errors += frame.frame_error;
		}

		for (int warmup = 0; warmup < 2; ++warmup) {
			const auto &frame = frames[static_cast<std::size_t>(warmup)];
			decode_once<N, K, O>(
				baseline_decoder, frame.soft, genmat, Mode::baseline
			);
			decode_once<N, K, O>(
				bounded_decoder, frame.soft, genmat, Mode::parity_window
			);
			decode_once<N, K, O>(
				guarded_decoder, frame.soft, genmat,
				Mode::guarded_parity_window
			);
		}

		std::array<ModeSamples, 3> samples;
		for (int repeat = 0; repeat < REPEATS; ++repeat) {
			for (int frame_index = 0; frame_index < FRAMES_PER_SNR; ++frame_index) {
				const auto &frame = frames[static_cast<std::size_t>(frame_index)];
				std::array<int, 3> order{0, 1, 2};
				std::rotate(
					order.begin(),
					order.begin() + ((repeat + frame_index) % 3),
					order.end()
				);
				for (const int mode_index: order) {
					const Mode mode = MODES[static_cast<std::size_t>(mode_index)];
					DecodeResult<N, K, O> result;
					if (mode == Mode::baseline)
						result = decode_once<N, K, O>(
							baseline_decoder, frame.soft, genmat, mode
						);
					else if (mode == Mode::parity_window)
						result = decode_once<N, K, O>(
							bounded_decoder, frame.soft, genmat, mode
						);
					else
						result = decode_once<N, K, O>(
							guarded_decoder, frame.soft, genmat, mode
						);
					assert(result.decoded == frame.reference);
					assert(result.unique == frame.reference_unique);
					assert(result.best_metric == frame.reference_best_metric);
					const uint64_t digest = checksum_bytes(
						result.decoded.data(), result.decoded.size(), result.unique
					);
					auto &mode_samples = samples[static_cast<std::size_t>(mode_index)];
					mode_samples.total.push_back(result.total_ns);
					for (int stage = 0; stage < STAGE_COUNT; ++stage)
						mode_samples.stages[static_cast<std::size_t>(stage)]
							.push_back(result.stages[static_cast<std::size_t>(stage)]);
					mode_samples.candidates += result.stats.candidates;
					mode_samples.pruned_candidates +=
						result.stats.pruned_candidates;
					mode_samples.exact_stops += result.stats.exact_stop;
					mode_samples.fallbacks += result.stats.fallback;
					mode_samples.dp_build_ns += result.stats.dp_build_ns;
					mode_samples.bounded_search_ns +=
						result.stats.bounded_search_ns;
					mode_samples.maximum_dp_bytes = std::max(
						mode_samples.maximum_dp_bytes,
						result.stats.dp_bytes
					);

					raw << code_name << ',' << N << ',' << K << ',' << O << ','
						<< std::fixed << std::setprecision(3) << snr_db << ','
						<< repeat + 1 << ',' << frame_index + 1 << ','
						<< mode_name(mode)
						<< ',' << result.total_ns;
					for (const uint64_t value: result.stages)
						raw << ',' << value;
					raw << ',' << result.stats.candidates << ','
						<< result.stats.pruned_candidates << ','
						<< result.stats.exact_stop << ','
						<< result.stats.fallback << ','
						<< result.stats.exact_check_ns << ','
						<< result.stats.dp_build_ns << ','
						<< result.stats.bounded_search_ns << ','
						<< result.stats.dp_bytes << ',' << result.best_metric << ','
						<< result.next_metric << ',' << frame.frame_error << ','
						<< frame.bit_errors << ',' << digest << ",pass\n";
				}
			}
		}

		const Metrics baseline_metrics = metrics(samples[0].total);
		for (int mode_index = 0; mode_index < 3; ++mode_index) {
			const Mode mode = MODES[static_cast<std::size_t>(mode_index)];
			const auto &value = samples[static_cast<std::size_t>(mode_index)];
			const Metrics total_metrics = metrics(value.total);
			const double mean_candidates =
				static_cast<double>(value.candidates) / value.total.size();
			const double reduction = 1.0 - mean_candidates /
				static_cast<double>(candidate_count(K, O));
			const double mean_speedup =
				baseline_metrics.mean_ns / total_metrics.mean_ns;
			const double p95_speedup = static_cast<double>(
				baseline_metrics.p95_ns
			) / total_metrics.p95_ns;
			const double p99_speedup = static_cast<double>(
				baseline_metrics.p99_ns
			) / total_metrics.p99_ns;
			const bool gate = mode == Mode::baseline || (
				mean_speedup >= 1.50 && p95_speedup >= 0.95 &&
				value.maximum_dp_bytes <= 1024 * 1024
			);

			summary << code_name << ',' << N << ',' << K << ',' << O << ','
				<< std::fixed << std::setprecision(6) << snr_db << ','
				<< mode_name(mode)
				<< ',' << FRAMES_PER_SNR << ',' << REPEATS << ','
				<< value.total.size() << ',' << frame_errors << ','
				<< static_cast<double>(frame_errors) / FRAMES_PER_SNR << ','
				<< bit_errors << ','
				<< static_cast<double>(bit_errors) /
					(static_cast<double>(FRAMES_PER_SNR) * N) << ','
				<< value.exact_stops << ','
				<< static_cast<double>(value.exact_stops) / value.total.size()
				<< ',' << value.fallbacks << ','
				<< static_cast<double>(value.fallbacks) / value.total.size()
				<< ',' << mean_candidates << ',' << reduction << ','
				<< total_metrics.mean_ns << ',' << total_metrics.p50_ns << ','
				<< total_metrics.p95_ns << ',' << total_metrics.p99_ns << ','
				<< total_metrics.maximum_ns << ','
				<< 1e9 / total_metrics.mean_ns << ',' << mean_speedup << ','
				<< p95_speedup << ',' << p99_speedup;
			for (int stage = 0; stage < STAGE_COUNT; ++stage)
				summary << ',' << mean(
					value.stages[static_cast<std::size_t>(stage)]
				);
			summary << ',' << static_cast<double>(value.dp_build_ns) /
				value.total.size() << ','
				<< static_cast<double>(value.bounded_search_ns) /
					value.total.size() << ',' << value.maximum_dp_bytes
				<< ",decoded_best_unique_exact,"
				<< (gate ? "pass" : "fail") << ",pass\n";

			if (mode != Mode::baseline)
				std::cout << "PARITY_WINDOW_E2E_RESULT code=" << code_name
					<< " ebn0_db=" << snr_db
					<< " mode=" << mode_name(mode)
					<< " fer=" << std::setprecision(4)
					<< static_cast<double>(frame_errors) / FRAMES_PER_SNR
					<< " baseline_mean_ns=" << static_cast<uint64_t>(
						baseline_metrics.mean_ns
					) << " mode_mean_ns=" << static_cast<uint64_t>(
						total_metrics.mean_ns
					) << " mean_speedup=" << mean_speedup
					<< " p95_speedup=" << p95_speedup
					<< " p99_speedup=" << p99_speedup
					<< " fallback_rate="
					<< static_cast<double>(value.fallbacks) /
						value.total.size() << '\n';
		}
	}
}

#ifdef OSD_NOVELTY_COMPARISON

#ifdef OSD_HYBRID_SCREEN
constexpr std::array<Mode, 7> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_1line_auto_queries_1m,
	Mode::orbgrand_1line_then_clm_queries_16,
	Mode::orbgrand_1line_then_clm_queries_64,
	Mode::orbgrand_1line_then_clm_queries_256,
	Mode::orbgrand_1line_then_clm_queries_1k
};
#elif defined(OSD_HYBRID_HOLDOUT) || \
	defined(OSD_HYBRID_ORB_CHANNEL_AUDIT)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_1line_auto_queries_1m,
	Mode::orbgrand_1line_then_clm_queries_256
};
#elif defined(OSD_HYBRID_CHANNEL_HOLDOUT)
constexpr std::array<Mode, 2> NOVELTY_MODES{
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_1line_then_clm_queries_256
};
#elif defined(OSD_ADAPTIVE_DISPATCH_ONLY)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::adaptive_packed_dispatch_exact
};
#elif defined(OSD_PACKED_EXACT_ONLY)
constexpr std::array<Mode, 2> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact
};
#elif defined(OSD_FINAL_VALIDATION_ONLY)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_parity_certify_exact
};
#elif defined(OSD_PUBLICATION_HOLDOUT) || \
	defined(OSD_PUBLICATION_BCH92_HOLDOUT)
constexpr std::array<Mode, 6> NOVELTY_MODES{
	Mode::baseline,
	Mode::published_trivial,
	Mode::published_dai,
	Mode::published_extra_parity_delta4,
	Mode::published_joint_dai_delta4,
	Mode::guarded_packed_parity_certify_exact
};
#elif defined(OSD_WITNESS_DECODER_COMPARISON)
constexpr std::array<Mode, 5> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::guarded_witness_packed_state_pdb_exact,
	Mode::bitplane_frontier_exact,
	Mode::orbgrand_1line_auto_queries_1m
};
#elif defined(OSD_DECODER_STRONG_HOLDOUT)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_queries_1m,
	Mode::orbgrand_1line_auto_queries_1m
};
#elif defined(OSD_DECODER_PARETO_HOLDOUT)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_queries_1m
};
#elif defined(OSD_DECODER_PARETO_COMPARISON)
constexpr std::array<Mode, 6> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::orbgrand_queries_1k,
	Mode::orbgrand_queries_10k,
	Mode::orbgrand_queries_100k,
	Mode::orbgrand_queries_1m
};
#elif defined(OSD_PUBLICATION_COMPARISON)
constexpr std::array<Mode, 9> NOVELTY_MODES{
	Mode::baseline,
	Mode::published_trivial,
	Mode::published_dai,
	Mode::published_extra_parity_delta4,
	Mode::published_joint_dai_delta4,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_projected_screen_exact,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_parity_certify_exact
};
#elif defined(OSD_ADDITIVE_PDB_PUBLICATION_COMPARISON)
constexpr std::array<Mode, 7> NOVELTY_MODES{
	Mode::baseline,
	Mode::published_trivial,
	Mode::published_dai,
	Mode::published_extra_parity_delta4,
	Mode::published_joint_dai_delta4,
	Mode::guarded_discover_certify_exact,
	Mode::adaptive_additive_pdb_exact
};
#elif defined(OSD_WITNESS_PUBLICATION_COMPARISON)
constexpr std::array<Mode, 9> NOVELTY_MODES{
	Mode::baseline,
	Mode::published_trivial,
	Mode::published_dai,
	Mode::published_extra_parity_delta4,
	Mode::published_joint_dai_delta4,
	Mode::guarded_packed_parity_certify_exact,
	Mode::guarded_witness_packed_state_pdb_exact,
	Mode::bitplane_frontier_exact,
	Mode::guarded_packed_state_pdb_exact
};
#elif defined(OSD_MULTI_REGIME_SCREEN)
constexpr std::array<Mode, 7> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_packed_parity_certify_exact,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_packed_state_pdb_exact,
	Mode::guarded_witness_packed_state_pdb_exact,
	Mode::bitplane_frontier_exact
};
#elif defined(OSD_PACKED_PARITY_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_parity_certify_exact
};
#elif defined(OSD_ADDITIVE_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact
};
#elif defined(OSD_COST_PARTITION_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_cost_partition_pdb_exact
};
#elif defined(OSD_FINGERPRINT_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_fingerprint_pdb_exact
};
#elif defined(OSD_QUOTIENT_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_quotient_pdb_exact
};
#elif defined(OSD_QUERY_LOCAL_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_query_local_pdb_exact
};
#elif defined(OSD_ORB_SEEDED_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_state_pdb_exact,
	Mode::guarded_orb_seeded_packed_pdb_exact
};
#elif defined(OSD_BITPLANE_FRONTIER_KILL_TEST)
constexpr std::array<Mode, 5> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_state_pdb_exact,
	Mode::bitplane_frontier_exact,
	Mode::transposed_frontier_exact
};
#elif defined(OSD_PERFECT_SUBTREE_ORACLE_SCREEN)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_witness_packed_state_pdb_exact,
	Mode::oracle_perfect_subtree_pdb_exact,
	Mode::oracle_perfect_seed_subtree_pdb_exact
};
#elif defined(OSD_SELECTIVE_COUPLING_ORACLE_SCREEN)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_witness_packed_state_pdb_exact,
	Mode::oracle_selective_coupling_pdb_exact
};
#elif defined(OSD_PACKED_STATE_PDB_KILL_TEST)
constexpr std::array<Mode, 5> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_additive_pdb_exact,
	Mode::guarded_packed_state_pdb_exact,
	Mode::guarded_witness_packed_state_pdb_exact
};
#elif defined(OSD_ADAPTIVE_PACKED_PDB_KILL_TEST)
constexpr std::array<Mode, 4> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::guarded_packed_state_pdb_exact,
	Mode::adaptive_packed_state_pdb_exact
};
#elif defined(OSD_ADDITIVE_PDB_FINAL_VALIDATION)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_discover_certify_exact,
	Mode::adaptive_additive_pdb_exact
};
#elif defined(OSD_DISCOVER_CERTIFY_ONLY)
constexpr std::array<Mode, 5> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_projected_screen_exact,
	Mode::guarded_portfolio_projected_screen_exact,
	Mode::guarded_discover_certify_exact
};
#elif defined(OSD_PROJECTED_SCREEN_ONLY)
constexpr std::array<Mode, 6> NOVELTY_MODES{
	Mode::baseline,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_projected_screen_exact,
	Mode::guarded_portfolio_projected_screen_exact,
	Mode::guarded_syndrome_sketch_screen_exact,
	Mode::guarded_discover_certify_exact
};
#elif defined(OSD_PROJECTED_ASTAR_KILL_TEST)
constexpr std::array<Mode, 9> NOVELTY_MODES{
	Mode::baseline,
	Mode::exact_stop_only,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_projected_astar_exact,
	Mode::guarded_adaptive_projected_astar_exact,
	Mode::guarded_projected_screen_exact,
	Mode::guarded_hierarchical_projected_screen_exact,
	Mode::guarded_portfolio_projected_screen_exact,
	Mode::guarded_syndrome_sketch_screen_exact
};
#elif defined(OSD_SOFT_WEIGHT_KILL_TEST)
constexpr std::array<Mode, 5> NOVELTY_MODES{
	Mode::baseline,
	Mode::exact_stop_only,
	Mode::soft_weight_exact,
	Mode::guarded_soft_weight_exact,
	Mode::guarded_parity_window
};
#elif defined(OSD_EXACT_VALIDATION_ONLY)
constexpr std::array<Mode, 3> NOVELTY_MODES{
	Mode::baseline,
	Mode::exact_stop_only,
	Mode::guarded_parity_window
};
#else
constexpr std::array<Mode, 8> NOVELTY_MODES{
	Mode::baseline,
	Mode::exact_stop_only,
	Mode::published_trivial,
	Mode::published_dai,
	Mode::published_extra_parity_delta4,
	Mode::published_joint_dai_delta4,
	Mode::parity_window,
	Mode::guarded_parity_window
};
#endif

const char *algorithm_class(Mode mode)
{
	if (is_orbgrand_clm_cascade_mode(mode))
		return "adaptive_noise_guessing_osd_cascade";
	if (is_orbgrand_mode(mode))
		return "universal_noise_guessing";
	if (mode == Mode::published_dai ||
		mode == Mode::published_extra_parity_delta4 ||
		mode == Mode::published_joint_dai_delta4)
		return "approximate_published";
	return "exact_fixed_order";
}

struct NoveltySamples {
	ModeSamples timing;
	uint64_t patterns_considered = 0;
	uint64_t decoded_mismatch_frames = 0;
	uint64_t best_metric_mismatch_frames = 0;
	uint64_t unique_mismatch_frames = 0;
	uint64_t decoded_frame_errors = 0;
	uint64_t decoded_bit_errors = 0;
};

template <int N>
uint64_t decoded_bit_errors(
	const std::array<uint8_t, (N + 7) / 8> &decoded,
	const std::array<uint8_t, (N + 7) / 8> &encoded
)
{
	uint64_t errors = 0;
	for (int index = 0; index < N; ++index)
		errors += CODE::get_be_bit(decoded.data(), index) !=
			CODE::get_be_bit(encoded.data(), index);
	return errors;
}

template <int N, int K, int O>
void run_novelty_configuration(
	const std::string &code_name,
	const std::array<int8_t, N * K> &genmat,
	std::mt19937_64 &random,
	std::ofstream &raw,
	std::ofstream &summary
)
{
#ifdef OSD_NOVELTY_SANITIZER_SMOKE
	constexpr std::array<double, 1> SNR_VALUES{3.0};
#elif defined(OSD_PUBLICATION_HOLDOUT) || \
	defined(OSD_PUBLICATION_BCH92_HOLDOUT)
	constexpr std::array<double, 4> SNR_VALUES{0.0, 1.0, 2.0, 3.0};
#elif defined(OSD_DECODER_PARETO_HOLDOUT) || \
	defined(OSD_DECODER_STRONG_HOLDOUT) || \
	defined(OSD_WITNESS_DECODER_COMPARISON) || defined(OSD_HYBRID_SCREEN) || \
	defined(OSD_HYBRID_HOLDOUT) || defined(OSD_HYBRID_CHANNEL_HOLDOUT) || \
	defined(OSD_HYBRID_ORB_CHANNEL_AUDIT)
	constexpr std::array<double, 3> SNR_VALUES{4.0, 5.0, 6.0};
#elif defined(OSD_DECODER_PARETO_HIGH_SNR)
	constexpr std::array<double, 4> SNR_VALUES{3.0, 4.0, 5.0, 6.0};
#elif defined(OSD_HIGH_RATE_SCREEN)
	constexpr std::array<double, 2> SNR_VALUES{3.0, 4.0};
#elif defined(OSD_DECODER_PARETO_COMPARISON)
	constexpr std::array<double, 4> SNR_VALUES{0.0, 1.0, 2.0, 3.0};
#elif defined(OSD_SOFT_WEIGHT_KILL_TEST) || \
	defined(OSD_PROJECTED_ASTAR_KILL_TEST) || \
	defined(OSD_ADDITIVE_PDB_KILL_TEST) || \
	defined(OSD_COST_PARTITION_PDB_KILL_TEST) || \
	defined(OSD_FINGERPRINT_PDB_KILL_TEST) || \
	defined(OSD_QUOTIENT_PDB_KILL_TEST) || \
	defined(OSD_QUERY_LOCAL_PDB_KILL_TEST) || \
	defined(OSD_ORB_SEEDED_PDB_KILL_TEST) || \
	defined(OSD_BITPLANE_FRONTIER_KILL_TEST) || \
	defined(OSD_PERFECT_SUBTREE_ORACLE_SCREEN) || \
	defined(OSD_SELECTIVE_COUPLING_ORACLE_SCREEN) || \
	defined(OSD_PACKED_STATE_PDB_KILL_TEST) || \
	defined(OSD_ADAPTIVE_PACKED_PDB_KILL_TEST) || \
	defined(OSD_ADDITIVE_PDB_FINAL_VALIDATION) || \
	defined(OSD_ADDITIVE_PDB_PUBLICATION_COMPARISON) || \
	defined(OSD_WITNESS_PUBLICATION_COMPARISON) || \
	defined(OSD_MULTI_REGIME_SCREEN) || \
	defined(OSD_PDB_DIMENSION_HOLDOUT) || \
	defined(OSD_PACKED_PARITY_KILL_TEST)
constexpr std::array<double, 4> SNR_VALUES{3.0, 4.0, 6.0, 8.0};
#elif defined(OSD_MULTI_CODE_SCREEN)
	constexpr std::array<double, 4> SNR_VALUES{3.0, 4.0, 6.0, 8.0};
#elif defined(OSD_LONG_VALIDATION)
	constexpr std::array<double, 4> SNR_VALUES{4.0, 6.0, 8.0, 10.0};
#elif defined(OSD_PARAMETER_SWEEP)
	constexpr std::array<double, 3> SNR_VALUES{3.0, 4.0, 6.0};
#else
	constexpr std::array<double, 6> SNR_VALUES{
		2.0, 3.0, 4.0, 6.0, 8.0, 10.0
	};
#endif
	CODE::LinearEncoder<N, K> encoder;
	CODE::OrderedStatisticsDecoder<N, K, O> reference_decoder;
	// Reuse one decoder storage address for every timed mode.  Separate
	// per-mode objects can place the hot G/codeword arrays in different cache
	// sets and create a large address-layout bias even when two modes execute
	// the identical fallback path.  Rotating mode order below still balances
	// immediate predecessor/cache-warmth effects.
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	BasicOrbgrandDecoder<N, K> orbgrand_decoder(genmat);
	std::uniform_int_distribution<int> bit(0, 1);
	#ifdef OSD_RANDOM_SOFT_FUZZ
	std::uniform_int_distribution<int> fuzz_soft(
		-SOFT_MAGNITUDE_MAX, SOFT_MAGNITUDE_MAX
	);
	#endif

	#ifdef OSD_CHANNEL_VALIDATION
	for (const ValidationChannel validation_channel: VALIDATION_CHANNELS) {
		const std::string result_code_name = code_name + "__" +
			validation_channel_name(validation_channel);
	#else
	{
		const std::string &result_code_name = code_name;
	#endif
	for (const double snr_db: SNR_VALUES) {
		const double snr = std::pow(10.0, snr_db / 10.0);
		const double rate = static_cast<double>(K) / N;
		const double sigma = std::sqrt(1.0 / (2.0 * rate * snr));
		// Convert the quantized magnitude back to the AWGN LLR scale used by
		// the published DAI expression.
		active_llr_scale = 2.0 / (sigma * sigma * SOFT_QUANTIZATION_SCALE);
		#ifndef OSD_CHANNEL_VALIDATION
		std::normal_distribution<double> noise(0.0, sigma);
		#endif
		std::vector<Frame<N, K>> frames(
			static_cast<std::size_t>(FRAMES_PER_SNR)
		);
		uint64_t reference_frame_errors = 0;
		uint64_t reference_bit_errors = 0;

		for (Frame<N, K> &frame: frames) {
			#ifdef OSD_CHANNEL_VALIDATION
			ValidationChannelState channel_state;
			#endif
			std::array<uint8_t, (K + 7) / 8> message{};
			for (int index = 0; index < K; ++index)
				CODE::set_be_bit(message.data(), index, bit(random));
			encoder(frame.encoded.data(), message.data(), genmat.data());
			for (int index = 0; index < N; ++index) {
				#ifdef OSD_RANDOM_SOFT_FUZZ
				frame.soft[static_cast<std::size_t>(index)] =
					static_cast<int8_t>(fuzz_soft(random));
				#else
				const double symbol = CODE::get_be_bit(
					frame.encoded.data(), index
				) ? -1.0 : 1.0;
				#ifdef OSD_CHANNEL_VALIDATION
				const double received = validation_soft_observation(
					validation_channel, symbol, sigma, random, channel_state
				);
				#else
				const double received = symbol + noise(random);
				#endif
				const int magnitude = std::max(
					1,
					std::min(SOFT_MAGNITUDE_MAX, static_cast<int>(std::lround(
						std::abs(received) * SOFT_QUANTIZATION_SCALE
					)))
				);
				frame.soft[static_cast<std::size_t>(index)] = received < 0
					? static_cast<int8_t>(-magnitude)
					: static_cast<int8_t>(magnitude);
				#endif
			}
			#ifdef OSD_ADAPTIVE_DATASET
			captured_adaptive_features = {};
			capture_adaptive_enabled = true;
			#endif
			const auto reference = decode_once<N, K, O>(
				reference_decoder, frame.soft, genmat,
			#if defined(OSD_HYBRID_CHANNEL_HOLDOUT) || \
				defined(OSD_HYBRID_ORB_CHANNEL_AUDIT)
				Mode::guarded_packed_parity_certify_exact
			#else
				Mode::baseline
			#endif
			);
			#ifdef OSD_ADAPTIVE_DATASET
			capture_adaptive_enabled = false;
			frame.adaptive_features = captured_adaptive_features;
			assert(frame.adaptive_features.extraction_ns > 0);
			#endif
			frame.reference = reference.decoded;
			frame.reference_unique = reference.unique;
			frame.reference_best_metric = reference.best_metric;
			frame.bit_errors = decoded_bit_errors<N>(
				frame.reference, frame.encoded
			);
			frame.frame_error = frame.bit_errors != 0;
			reference_bit_errors += frame.bit_errors;
			reference_frame_errors += frame.frame_error;
		}

		#ifdef OSD_SELECTIVE_COUPLING_ORACLE_SCREEN
		// Give selective coupling its strongest possible screening condition:
		// discover every useful pair-coupled subtree proof off the clock, then
		// time only a replay of those perfect decisions with the ordinary PDB.
		// This is an oracle upper bound, not a deployable decoder.
		auto &oracle_schedules = selective_oracle_schedules<K>();
		oracle_schedules.assign(
			static_cast<std::size_t>(FRAMES_PER_SNR), {}
		);
		preparing_selective_oracle = true;
		for (int frame_index = 0;
			frame_index < FRAMES_PER_SNR; ++frame_index) {
			active_selective_oracle_slot = frame_index;
			const auto &frame = frames[static_cast<std::size_t>(frame_index)];
			const auto prepared = decode_once<N, K, O>(
				decoder, frame.soft, genmat,
				Mode::oracle_selective_coupling_pdb_exact
			);
			assert(prepared.best_metric == frame.reference_best_metric);
			assert(prepared.unique == frame.reference_unique);
			if (frame.reference_unique)
				assert(prepared.decoded == frame.reference);
		}
		preparing_selective_oracle = false;
		#endif

		#ifdef OSD_PERFECT_SUBTREE_ORACLE_SCREEN
		auto &perfect_schedules = perfect_oracle_schedules<K, O>();
		perfect_schedules.assign(
			static_cast<std::size_t>(FRAMES_PER_SNR), {}
		);
		preparing_perfect_oracle = true;
		for (int frame_index = 0;
			frame_index < FRAMES_PER_SNR; ++frame_index) {
			active_perfect_oracle_slot = frame_index;
			const auto &frame = frames[static_cast<std::size_t>(frame_index)];
			const auto prepared = decode_once<N, K, O>(
				decoder, frame.soft, genmat,
				Mode::oracle_perfect_subtree_pdb_exact
			);
			assert(prepared.best_metric == frame.reference_best_metric);
			assert(prepared.unique == frame.reference_unique);
			if (frame.reference_unique)
				assert(prepared.decoded == frame.reference);
		}
		preparing_perfect_oracle = false;
		#endif

		for (std::size_t mode_index = 0;
			mode_index < NOVELTY_MODES.size(); ++mode_index) {
			for (int warmup = 0;
				warmup < std::min(2, FRAMES_PER_SNR); ++warmup) {
				const auto &frame = frames[static_cast<std::size_t>(warmup)];
				const Mode mode = NOVELTY_MODES[mode_index];
				#ifdef OSD_SELECTIVE_COUPLING_ORACLE_SCREEN
				if (mode == Mode::oracle_selective_coupling_pdb_exact)
					active_selective_oracle_slot = warmup;
				#endif
				#ifdef OSD_PERFECT_SUBTREE_ORACLE_SCREEN
				if (
					mode == Mode::oracle_perfect_subtree_pdb_exact ||
					mode == Mode::oracle_perfect_seed_subtree_pdb_exact
				)
					active_perfect_oracle_slot = warmup;
				#endif
				if (is_orbgrand_clm_cascade_mode(mode))
					decode_orbgrand_clm_cascade_once<N, K, O>(
						orbgrand_decoder, decoder,
						frame.soft, genmat, mode
					);
				else if (is_orbgrand_mode(mode))
					decode_orbgrand_once<N, K, O>(
						orbgrand_decoder, frame.soft, mode
					);
				else
					decode_once<N, K, O>(
						decoder, frame.soft, genmat, mode
					);
			}
		}

		std::array<NoveltySamples, NOVELTY_MODES.size()> samples;
		for (int repeat = 0; repeat < REPEATS; ++repeat) {
			for (int frame_index = 0;
				frame_index < FRAMES_PER_SNR; ++frame_index) {
				const auto &frame = frames[static_cast<std::size_t>(frame_index)];
				std::array<std::size_t, NOVELTY_MODES.size()> run_order{};
				std::iota(run_order.begin(), run_order.end(), std::size_t{0});
				std::rotate(
					run_order.begin(),
					run_order.begin() +
						((static_cast<std::size_t>(repeat) + frame_index) %
						NOVELTY_MODES.size()),
					run_order.end()
				);
				for (const std::size_t mode_index: run_order) {
					const Mode mode = NOVELTY_MODES[mode_index];
					#ifdef OSD_SELECTIVE_COUPLING_ORACLE_SCREEN
					if (mode == Mode::oracle_selective_coupling_pdb_exact)
						active_selective_oracle_slot = frame_index;
					#endif
					#ifdef OSD_PERFECT_SUBTREE_ORACLE_SCREEN
					if (
						mode == Mode::oracle_perfect_subtree_pdb_exact ||
						mode == Mode::oracle_perfect_seed_subtree_pdb_exact
					)
						active_perfect_oracle_slot = frame_index;
					#endif
					DecodeResult<N, K, O> result;
					if (is_orbgrand_clm_cascade_mode(mode))
						result = decode_orbgrand_clm_cascade_once<N, K, O>(
							orbgrand_decoder, decoder,
							frame.soft, genmat, mode
						);
					else if (is_orbgrand_mode(mode))
						result = decode_orbgrand_once<N, K, O>(
							orbgrand_decoder, frame.soft, mode
						);
					else
						result = decode_once<N, K, O>(
							decoder, frame.soft, genmat, mode
						);
					const bool decoded_mismatch =
						result.decoded != frame.reference;
					const bool best_mismatch =
						result.best_metric != frame.reference_best_metric;
					const bool unique_mismatch =
						result.unique != frame.reference_unique;
					if (is_exact_mode(mode)) {
						if (best_mismatch || unique_mismatch ||
							(frame.reference_unique && decoded_mismatch))
							std::cerr << "exact-mode diagnostic mode=" << mode_name(mode)
								<< " frame=" << frame_index << " repeat=" << repeat
								<< " reference_best=" << frame.reference_best_metric
								<< " result_best=" << result.best_metric
								<< " reference_unique=" << frame.reference_unique
								<< " result_unique=" << result.unique << '\n';
						assert(!best_mismatch);
						assert(!unique_mismatch);
						if (frame.reference_unique)
							assert(!decoded_mismatch);
					}
					const uint64_t errors = decoded_bit_errors<N>(
						result.decoded, frame.encoded
					);
					const uint64_t digest = checksum_bytes(
						result.decoded.data(), result.decoded.size(), result.unique
					);
					auto &value = samples[mode_index];
					value.timing.total.push_back(result.total_ns);
					for (int stage = 0; stage < STAGE_COUNT; ++stage)
						value.timing.stages[static_cast<std::size_t>(stage)]
							.push_back(result.stages[static_cast<std::size_t>(stage)]);
					value.timing.candidates += result.stats.candidates;
					value.timing.materializations +=
						result.stats.materializations;
					value.patterns_considered +=
						result.stats.patterns_considered;
					value.timing.pruned_candidates +=
						result.stats.pruned_candidates;
					value.timing.exact_stops += result.stats.exact_stop;
					value.timing.fallbacks += result.stats.fallback;
					value.timing.dp_build_ns += result.stats.dp_build_ns;
					value.timing.bounded_search_ns +=
						result.stats.bounded_search_ns;
					value.timing.maximum_dp_bytes = std::max(
						value.timing.maximum_dp_bytes, result.stats.dp_bytes
					);
					if (repeat == 0) {
						value.decoded_mismatch_frames += decoded_mismatch;
						value.best_metric_mismatch_frames += best_mismatch;
						value.unique_mismatch_frames += unique_mismatch;
						value.decoded_frame_errors += errors != 0;
						value.decoded_bit_errors += errors;
					}

					raw << result_code_name << ',' << N << ',' << K << ',' << O << ','
						<< std::fixed << std::setprecision(6) << snr_db << ','
						<< active_llr_scale << ',' << repeat + 1 << ','
						<< frame_index + 1 << ',' << mode_name(mode) << ','
						<< algorithm_class(mode) << ',' << result.total_ns;
					for (const uint64_t stage: result.stages)
						raw << ',' << stage;
					raw << ',' << result.stats.candidates << ','
						<< result.stats.materializations << ','
						<< result.stats.patterns_considered << ','
						<< result.stats.pruned_candidates << ','
						<< result.stats.exact_stop << ','
						<< result.stats.fallback << ','
						<< result.stats.exact_check_ns << ','
						<< result.stats.bounded_search_ns << ','
						<< result.stats.dp_build_ns << ','
						<< result.stats.dp_bytes << ',' << WINDOW_BITS << ','
						<< active_guard_candidate_budget << ','
						<< frame.reference_best_metric << ','
						<< result.best_metric << ',' << result.next_metric << ','
						<< frame.reference_unique << ',' << result.unique << ','
						<< decoded_mismatch << ',' << best_mismatch << ','
						<< unique_mismatch << ',' << frame.frame_error << ','
						<< (errors != 0) << ',' << errors << ',' << digest;
					#ifdef OSD_ADAPTIVE_DATASET
					write_adaptive_features(raw, frame.adaptive_features);
					#endif
					#if defined(OSD_ADAPTIVE_DISPATCH_ONLY) || \
						defined(OSD_ADDITIVE_PDB_FINAL_VALIDATION) || \
						defined(OSD_ADDITIVE_PDB_PUBLICATION_COMPARISON)
					raw << ',' << result.stats.dispatcher_baseline;
					#endif
					#if OSD_PACKED_PDB_CONFLICT_LEARNING
					raw << ',' << result.stats.learned_conflicts << ','
						<< result.stats.learned_conflict_hits << ','
						<< result.stats.learned_conflict_derivations;
					#endif
					#ifdef OSD_SELECTIVE_COUPLING_ORACLE_SCREEN
					raw << ',' << result.stats.oracle_coupled_prunes << ','
						<< result.stats.oracle_coupled_candidates << ','
						<< result.stats.oracle_schedule_lookups;
					#endif
					#ifdef OSD_PERFECT_SUBTREE_ORACLE_SCREEN
					raw << ',' << result.stats.perfect_subtree_prunes << ','
						<< result.stats.perfect_subtree_candidates << ','
						<< result.stats.perfect_subtree_lookups;
					#endif
					raw << ',' << (is_exact_mode(mode) ? "exact_validated" :
							(best_mismatch ? "approximate_difference" :
							 "approximate_match")) << '\n';
				}
			}
		}

		const Metrics baseline_metrics = metrics(samples[0].timing.total);
		for (std::size_t mode_index = 0;
			mode_index < NOVELTY_MODES.size(); ++mode_index) {
			const Mode mode = NOVELTY_MODES[mode_index];
			const auto &value = samples[mode_index];
			const Metrics total_metrics = metrics(value.timing.total);
			const double sample_count = value.timing.total.size();
			const double mean_patterns =
				static_cast<double>(value.patterns_considered) / sample_count;
			const double mean_candidates =
				static_cast<double>(value.timing.candidates) / sample_count;
			const double mean_materializations = static_cast<double>(
				value.timing.materializations
			) / sample_count;
			const double work_budget = is_orbgrand_mode(mode)
				? static_cast<double>(orbgrand_query_budget(mode))
				: is_orbgrand_clm_cascade_mode(mode)
					? static_cast<double>(
						orbgrand_clm_cascade_query_budget(mode) +
						candidate_count(K, O)
					)
					: static_cast<double>(candidate_count(K, O));
			const double reduction = 1.0 - mean_candidates / work_budget;
			const double mean_speedup =
				baseline_metrics.mean_ns / total_metrics.mean_ns;
			const double p95_speedup = static_cast<double>(
				baseline_metrics.p95_ns
			) / total_metrics.p95_ns;
			const double p99_speedup = static_cast<double>(
				baseline_metrics.p99_ns
			) / total_metrics.p99_ns;
			const bool validation_pass = !is_exact_mode(mode) || (
				value.best_metric_mismatch_frames == 0 &&
				value.unique_mismatch_frames == 0
			);

			summary << result_code_name << ',' << N << ',' << K << ',' << O << ','
				<< std::fixed << std::setprecision(6) << snr_db << ','
				<< active_llr_scale << ',' << mode_name(mode) << ','
				<< algorithm_class(mode) << ',' << FRAMES_PER_SNR << ','
				<< REPEATS << ',' << value.timing.total.size() << ','
				<< reference_frame_errors << ','
				<< static_cast<double>(reference_frame_errors) /
					FRAMES_PER_SNR << ',' << reference_bit_errors << ','
				<< value.decoded_frame_errors << ','
				<< static_cast<double>(value.decoded_frame_errors) /
					FRAMES_PER_SNR << ','
				<< static_cast<double>(
					static_cast<int64_t>(value.decoded_frame_errors) -
					static_cast<int64_t>(reference_frame_errors)
				) /
					FRAMES_PER_SNR << ',' << value.decoded_bit_errors << ','
				<< static_cast<double>(value.decoded_bit_errors) /
					(static_cast<double>(FRAMES_PER_SNR) * N) << ','
				<< value.decoded_mismatch_frames << ','
				<< value.best_metric_mismatch_frames << ','
				<< value.unique_mismatch_frames << ','
				<< value.timing.exact_stops << ','
				<< static_cast<double>(value.timing.exact_stops) / sample_count
				<< ',' << value.timing.fallbacks << ','
				<< static_cast<double>(value.timing.fallbacks) / sample_count
				<< ',' << mean_patterns << ',' << mean_candidates << ','
				<< mean_materializations << ','
				<< reduction << ',' << total_metrics.mean_ns << ','
				<< total_metrics.p50_ns << ',' << total_metrics.p95_ns << ','
				<< total_metrics.p99_ns << ',' << total_metrics.maximum_ns << ','
				<< 1e9 / total_metrics.mean_ns << ',' << mean_speedup << ','
				<< p95_speedup << ',' << p99_speedup;
			for (int stage = 0; stage < STAGE_COUNT; ++stage)
				summary << ',' << mean(
					value.timing.stages[static_cast<std::size_t>(stage)]
				);
			summary << ',' << static_cast<double>(
				value.timing.bounded_search_ns
			) / sample_count << ',' << static_cast<double>(
				value.timing.dp_build_ns
			) / sample_count << ',' << value.timing.maximum_dp_bytes << ','
				<< WINDOW_BITS << ',' << active_guard_candidate_budget << ','
				<< (is_exact_mode(mode) ? "best_and_uniqueness_exact" :
					"approximate_reported_not_asserted") << ','
				<< (validation_pass ? "pass" : "fail") << '\n';

			if (mode != Mode::baseline)
				std::cout << "OSD_NOVELTY_RESULT code=" << result_code_name
					<< " ebn0_db=" << snr_db
					<< " mode=" << mode_name(mode)
					<< " class=" << algorithm_class(mode)
					<< " mean_candidates=" << mean_candidates
					<< " mean_materializations=" << mean_materializations
					<< " mean_speedup=" << mean_speedup
					<< " p95_speedup=" << p95_speedup
					<< " reference_disagreements="
					<< value.best_metric_mismatch_frames
					<< " fer=" << static_cast<double>(
						value.decoded_frame_errors
					) / FRAMES_PER_SNR << '\n';
		}
	}
	}
}

#endif

}

int main(int argc, char **argv)
{
	using namespace experiment;
	uint64_t experiment_seed = OSD_EXPERIMENT_SEED;
	if (argc > 3) {
		active_guard_candidate_budget = std::stoull(argv[3]);
		assert(active_guard_candidate_budget > 0);
	}
	if (argc > 4)
		experiment_seed = std::stoull(argv[4], nullptr, 0);
#ifdef OSD_NOVELTY_COMPARISON
	#if defined(OSD_SOFT_WEIGHT_KILL_TEST) || \
		defined(OSD_PROJECTED_ASTAR_KILL_TEST)
	validate_soft_weight_pattern_queue();
	#endif
	const std::string raw_path = argc > 1
		? argv[1]
		: "results/osd_novelty_comparison_raw.csv";
	const std::string summary_path = argc > 2
		? argv[2]
		: "results/osd_novelty_comparison_summary.csv";

	std::ofstream raw(raw_path);
	std::ofstream summary(summary_path);
	assert(raw.good());
	assert(summary.good());
	raw << "code,n,k,order,ebn0_db,llr_scale,repeat,frame,mode,algorithm_class,"
		"total_ns,stage0_ns,stage1_ns,stage2_ns,stage3_ns,stage4_ns,stage5_ns,"
		"stage6_ns,stage7_ns,stage8_ns,candidates,materializations,"
		"patterns_considered,"
		"discarded_patterns,exact_stop,fallback,exact_check_ns,search_ns,"
		"dp_build_ns,dp_bytes,window_bits,guard_candidate_budget,"
		"reference_best_metric,best_metric,next_metric,"
		"reference_unique,unique,decoded_mismatch,best_metric_mismatch,"
		"unique_mismatch,reference_frame_error,decoded_frame_error,"
		"decoded_bit_errors,checksum";
	#ifdef OSD_ADAPTIVE_DATASET
	raw << ",feature_ns,feat_total_abs,feat_order0_distance,"
		"feat_order0_distance_norm,feat_order0_mismatch_fraction,"
		"feat_information_mean_norm,feat_information_std_norm,"
		"feat_information_min_norm,feat_information_q10_norm,"
		"feat_information_q25_norm,feat_information_median_norm,"
		"feat_parity_mean_norm,feat_parity_std_norm,feat_parity_q10_norm,"
		"feat_parity_q25_norm,feat_parity_median_norm,feat_parity_max_norm,"
		"feat_parity_mismatch_fraction,feat_parity_mismatch_cost_norm,"
		"feat_projected_mismatch_fraction,feat_projected_mismatch_cost_norm,"
		"feat_information_min4_cost_norm,feat_saturation_fraction,"
		"feat_original_adjacent_variation,"
		"feat_original_abs_lag1_correlation,"
		"feat_original_sign_transition_fraction,"
		"feat_original_low_reliability_run_fraction";
	#endif
	#if defined(OSD_ADAPTIVE_DISPATCH_ONLY) || \
		defined(OSD_ADDITIVE_PDB_FINAL_VALIDATION) || \
		defined(OSD_ADDITIVE_PDB_PUBLICATION_COMPARISON)
	raw << ",dispatcher_baseline";
	#endif
	#if OSD_PACKED_PDB_CONFLICT_LEARNING
	raw << ",learned_conflicts,learned_conflict_hits,"
		"learned_conflict_derivations";
	#endif
	#ifdef OSD_SELECTIVE_COUPLING_ORACLE_SCREEN
	raw << ",oracle_coupled_prunes,oracle_coupled_candidates,"
		"oracle_schedule_lookups";
	#endif
	#ifdef OSD_PERFECT_SUBTREE_ORACLE_SCREEN
	raw << ",perfect_subtree_prunes,perfect_subtree_candidates,"
		"perfect_subtree_lookups";
	#endif
	raw << ",result\n";
	summary << "code,n,k,order,ebn0_db,llr_scale,mode,algorithm_class,frames,"
		"repeats,samples,reference_frame_errors,reference_fer,"
		"reference_bit_errors,decoded_frame_errors,fer,fer_delta,"
		"decoded_bit_errors,ber,decoded_mismatch_frames,"
		"best_metric_mismatch_frames,unique_mismatch_frames,exact_stops,"
		"exact_stop_rate,fallbacks,fallback_rate,mean_patterns_considered,"
		"mean_candidates,mean_materializations,candidate_reduction,"
		"mean_total_ns,p50_total_ns,"
		"p95_total_ns,p99_total_ns,max_total_ns,blocks_per_second,"
		"mean_speedup,p95_speedup,p99_speedup,mean_stage0_ns,mean_stage1_ns,"
		"mean_stage2_ns,mean_stage3_ns,mean_stage4_ns,mean_stage5_ns,"
		"mean_stage6_ns,mean_stage7_ns,mean_stage8_ns,mean_search_ns,"
		"mean_dp_build_ns,dp_bytes,window_bits,guard_candidate_budget,"
		"validation,result\n";

	std::mt19937_64 random(experiment_seed);
	#ifdef OSD_PUBLICATION_BCH92_HOLDOUT
	const auto bch92_publication = bch127_matrix<92>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111
	});
	run_novelty_configuration<127, 92, 4>(
		"bch127_92_o4", bch92_publication, random, raw, summary
	);
	#elif defined(OSD_PUBLICATION_HOLDOUT)
	const auto bch36_publication = bch127_matrix<36>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011,
		0b11100101, 0b11000001, 0b11010011, 0b10101011
	});
	run_novelty_configuration<127, 36, 4>(
		"bch127_36_o4", bch36_publication, random, raw, summary
	);
	const auto bch64_publication = bch127_matrix<64>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011
	});
	run_novelty_configuration<127, 64, 4>(
		"bch127_64_o4", bch64_publication, random, raw, summary
	);
	const auto bch92_publication = bch127_matrix<92>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111
	});
	run_novelty_configuration<127, 92, 4>(
		"bch127_92_o4", bch92_publication, random, raw, summary
	);
	#elif defined(OSD_DECODER_PARETO_HOLDOUT) || \
		defined(OSD_DECODER_STRONG_HOLDOUT) || \
		defined(OSD_WITNESS_DECODER_COMPARISON) || defined(OSD_HYBRID_SCREEN) || \
		defined(OSD_HYBRID_HOLDOUT) || defined(OSD_HYBRID_CHANNEL_HOLDOUT) || \
		defined(OSD_HYBRID_ORB_CHANNEL_AUDIT)
	const auto bch92_holdout = bch127_matrix<92>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111
	});
	run_novelty_configuration<127, 92, 4>(
		"bch127_92_o4", bch92_holdout, random, raw, summary
	);
	const auto ebch92_holdout = extend_even_parity<92>(bch92_holdout);
	run_novelty_configuration<128, 92, 4>(
		"ebch128_92_o4", ebch92_holdout, random, raw, summary
	);
	const auto random92_holdout = random_systematic_matrix<127, 92>(
		0x52414e444f4d3932ULL
	);
	run_novelty_configuration<127, 92, 4>(
		"random127_92_o4", random92_holdout, random, raw, summary
	);
	const auto random106_holdout = random_systematic_matrix<127, 106>(
		0x52414e444f4d3136ULL
	);
	run_novelty_configuration<127, 106, 4>(
		"random127_106_o4", random106_holdout, random, raw, summary
	);
	#elif defined(OSD_ORDER_LENGTH_SCREEN)
	const auto random63 = random_systematic_matrix<63, 32>(
		0x52414e444f4d3633ULL
	);
	run_novelty_configuration<63, 32, 2>(
		"random63_32_o2", random63, random, raw, summary
	);
	run_novelty_configuration<63, 32, 3>(
		"random63_32_o3", random63, random, raw, summary
	);

	const auto random127 = random_systematic_matrix<127, 64>(
		0x52414e444f4d3132ULL
	);
	run_novelty_configuration<127, 64, 2>(
		"random127_64_o2", random127, random, raw, summary
	);
	run_novelty_configuration<127, 64, 3>(
		"random127_64_o3", random127, random, raw, summary
	);
	run_novelty_configuration<127, 64, 5>(
		"random127_64_o5", random127, random, raw, summary
	);

	const auto random255 = random_systematic_matrix<255, 128>(
		0x52414e444f4d3235ULL
	);
	run_novelty_configuration<255, 128, 3>(
		"random255_128_o3", random255, random, raw, summary
	);
	const auto polar256 = polar_bec_matrix<256, 128>();
	run_novelty_configuration<256, 128, 3>(
		"polar_bec256_128_o3", polar256, random, raw, summary
	);
	#elif defined(OSD_HIGH_RATE_SCREEN)
	const auto bch92_high = bch127_matrix<92>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111
	});
	run_novelty_configuration<127, 92, 4>(
		"bch127_92_o4", bch92_high, random, raw, summary
	);
	const auto ebch92_high = extend_even_parity<92>(bch92_high);
	run_novelty_configuration<128, 92, 4>(
		"ebch128_92_o4", ebch92_high, random, raw, summary
	);
	const auto bch99_high = bch127_matrix<99>({
		0b10001001, 0b10001111, 0b10011101, 0b11110111
	});
	run_novelty_configuration<127, 99, 4>(
		"bch127_99_o4", bch99_high, random, raw, summary
	);
	const auto polar96_high = polar_bec_matrix<128, 96>();
	run_novelty_configuration<128, 96, 4>(
		"polar_bec128_96_o4", polar96_high, random, raw, summary
	);
	const auto random92_high = random_systematic_matrix<127, 92>(
		0x52414e444f4d3932ULL
	);
	run_novelty_configuration<127, 92, 4>(
		"random127_92_o4", random92_high, random, raw, summary
	);
	const auto random106_high = random_systematic_matrix<127, 106>(
		0x52414e444f4d3136ULL
	);
	run_novelty_configuration<127, 106, 4>(
		"random127_106_o4", random106_high, random, raw, summary
	);
	#elif defined(OSD_PDB_DIMENSION_HOLDOUT)
	// Untouched CAP-PDB external-validity grid.  All dispatcher and PDB
	// parameters were frozen on K=36, O=4 before these templates were run.
	const auto random63_dimension = random_systematic_matrix<63, 24>(
		0x50444244494d3633ULL
	);
	run_novelty_configuration<63, 24, 2>(
		"random63_24_o2", random63_dimension, random, raw, summary
	);
	run_novelty_configuration<63, 24, 3>(
		"random63_24_o3", random63_dimension, random, raw, summary
	);
	run_novelty_configuration<63, 24, 4>(
		"random63_24_o4", random63_dimension, random, raw, summary
	);

	const auto random95_dimension = random_systematic_matrix<95, 42>(
		0x50444244494d3935ULL
	);
	run_novelty_configuration<95, 42, 2>(
		"random95_42_o2", random95_dimension, random, raw, summary
	);
	run_novelty_configuration<95, 42, 3>(
		"random95_42_o3", random95_dimension, random, raw, summary
	);
	run_novelty_configuration<95, 42, 4>(
		"random95_42_o4", random95_dimension, random, raw, summary
	);

	const auto random127_dimension = random_systematic_matrix<127, 48>(
		0x50444244494d3132ULL
	);
	run_novelty_configuration<127, 48, 2>(
		"random127_48_o2", random127_dimension, random, raw, summary
	);
	run_novelty_configuration<127, 48, 3>(
		"random127_48_o3", random127_dimension, random, raw, summary
	);
	run_novelty_configuration<127, 48, 4>(
		"random127_48_o4", random127_dimension, random, raw, summary
	);
	#else
	#ifdef OSD_LOW_RATE_ONLY
	const auto bch36_only = bch127_matrix<36>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011,
		0b11100101, 0b11000001, 0b11010011, 0b10101011
	});
	run_novelty_configuration<127, 36, 4>(
		"bch127_36_o4", bch36_only, random, raw, summary
	);
	#ifdef OSD_PDB_FAMILY_HOLDOUT
	const auto ebch36_only = extend_even_parity<36>(bch36_only);
	run_novelty_configuration<128, 36, 4>(
		"ebch128_36_o4", ebch36_only, random, raw, summary
	);
	const auto polar36_only = polar_bec_matrix<128, 36>();
	run_novelty_configuration<128, 36, 4>(
		"polar_bec128_36_o4", polar36_only, random, raw, summary
	);
	const auto random36_only = random_systematic_matrix<127, 36>(
		0x50444252414e4436ULL
	);
	run_novelty_configuration<127, 36, 4>(
		"random127_36_o4", random36_only, random, raw, summary
	);
	#endif
	#else
	const auto bch127 = bch127_matrix<64>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011
	});
	run_novelty_configuration<127, 64, 4>(
		"bch127_64_o4", bch127, random, raw, summary
	);
#ifdef OSD_MULTI_CODE_SCREEN
	const auto bch36 = bch127_matrix<36>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011,
		0b11100101, 0b11000001, 0b11010011, 0b10101011
	});
	run_novelty_configuration<127, 36, 4>(
		"bch127_36_o4", bch36, random, raw, summary
	);

	const auto bch92 = bch127_matrix<92>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111
	});
	run_novelty_configuration<127, 92, 4>(
		"bch127_92_o4", bch92, random, raw, summary
	);

	const auto ebch128 = extend_even_parity<64>(bch127);
	run_novelty_configuration<128, 64, 4>(
		"ebch128_64_o4", ebch128, random, raw, summary
	);

	const auto polar128 = polar_bec_matrix<128, 64>();
	run_novelty_configuration<128, 64, 4>(
		"polar_bec128_64_o4", polar128, random, raw, summary
	);

	const auto random127 = random_systematic_matrix<127, 64>(
		0x52414e444f4d3132ULL
	);
	run_novelty_configuration<127, 64, 4>(
		"random127_64_o4", random127, random, raw, summary
	);
	#endif
	#endif
	#endif
	std::cout << "OSD_NOVELTY_COMPARISON_PASS frames_per_snr="
		<< FRAMES_PER_SNR << " repeats=" << REPEATS
		<< " seed=" << experiment_seed << '\n';
	return 0;
#else
	const std::string raw_path = argc > 1
		? argv[1]
		: "results/parity_window_end_to_end_raw.csv";
	const std::string summary_path = argc > 2
		? argv[2]
		: "results/parity_window_end_to_end_summary.csv";

	std::ofstream raw(raw_path);
	std::ofstream summary(summary_path);
	assert(raw.good());
	assert(summary.good());
	raw << "code,n,k,order,ebn0_db,repeat,frame,mode,total_ns,"
		"stage0_ns,stage1_ns,stage2_ns,stage3_ns,stage4_ns,stage5_ns,"
		"stage6_ns,stage7_ns,stage8_ns,candidates,pruned_candidates,"
		"exact_stop,fallback,exact_check_ns,dp_build_ns,bounded_search_ns,dp_bytes,"
		"best_metric,next_metric,"
		"frame_error,bit_errors,checksum,result\n";
	summary << "code,n,k,order,ebn0_db,mode,frames,repeats,samples,"
		"frame_errors,fer,bit_errors,ber,exact_stops,exact_stop_rate,"
		"fallbacks,fallback_rate,"
		"mean_candidates,candidate_reduction,mean_total_ns,p50_total_ns,"
		"p95_total_ns,p99_total_ns,max_total_ns,blocks_per_second,"
		"mean_speedup,p95_speedup,p99_speedup,mean_stage0_ns,mean_stage1_ns,"
		"mean_stage2_ns,mean_stage3_ns,mean_stage4_ns,mean_stage5_ns,"
		"mean_stage6_ns,mean_stage7_ns,mean_stage8_ns,mean_dp_build_ns,"
		"mean_bounded_search_ns,dp_bytes,exactness,gate,result\n";

	std::mt19937_64 random(0x454e445f544f5f45ULL);
	std::array<int8_t, 127 * 64> bch127{};
	CODE::BoseChaudhuriHocquenghemGenerator<127, 64>::matrix(
		bch127.data(),
		true,
		{
			0b10001001, 0b10001111, 0b10011101,
			0b11110111, 0b10111111, 0b11010101,
			0b10000011, 0b11101111, 0b11001011
		}
	);
	run_configuration<127, 64, 4>(
		"bch127_64_o4", bch127, random, raw, summary
	);

	const auto random63 = random_systematic_matrix<63, 32>(
		0x52414e444f4d3633ULL
	);
	run_configuration<63, 32, 4>(
		"random63_32_o4", random63, random, raw, summary
	);

	std::array<int8_t, 15 * 5> bch15{};
	CODE::BoseChaudhuriHocquenghemGenerator<15, 5>::matrix(
		bch15.data(), true, {0b10011, 0b11111, 0b00111}
	);
	run_configuration<15, 5, 3>(
		"bch15_5_o3", bch15, random, raw, summary
	);

	std::cout << "PARITY_WINDOW_END_TO_END_PASS frames_per_snr="
		<< FRAMES_PER_SNR << " repeats=" << REPEATS
		<< " guarded_candidate_budget=" << active_guard_candidate_budget
		<< '\n';
	return 0;
#endif
}
