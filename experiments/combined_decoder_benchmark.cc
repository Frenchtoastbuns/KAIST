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
#include <string>
#include <utility>
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
constexpr int P = 8;
constexpr int R = N - K;
constexpr uint64_t EXPECTED_CANDIDATES = 679121;
constexpr uint64_t EXPECTED_MASK_HASH = 0x9717451b3bb8a575ULL;
constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

bool combined_enabled = false;
std::vector<uint64_t> tep_cache;
uint64_t cache_build_ns = 0;

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
	uint64_t pages = 0;
	std::size_t last_page_size = 0;
	std::size_t cache_bytes = 0;
	std::size_t page_payload_bytes = 0;
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

template <int DEPTH = 0, typename Consumer>
inline void enumerate_nonzero(
	uint64_t prefix,
	int start,
	Consumer &consumer
)
{
	for (int index = start; index < K; ++index) {
		const uint64_t mask = prefix | (uint64_t{1} << index);
		consumer(mask);
		if constexpr (DEPTH + 1 < O)
			enumerate_nonzero<DEPTH + 1>(mask, index + 1, consumer);
	}
}

std::vector<uint64_t> build_tep_cache()
{
	std::vector<uint64_t> result;
	result.reserve(static_cast<std::size_t>(EXPECTED_CANDIDATES));
	result.push_back(0);
	auto append = [&result](uint64_t mask) { result.push_back(mask); };
	enumerate_nonzero(0, 0, append);
	assert(result.size() == EXPECTED_CANDIDATES);
	return result;
}

uint64_t mask_hash(const std::vector<uint64_t> &masks)
{
	uint64_t hash = FNV_OFFSET;
	for (const uint64_t mask: masks) {
		for (int byte = 0; byte < 8; ++byte) {
			hash ^= static_cast<uint8_t>(mask >> (8 * byte));
			hash *= FNV_PRIME;
		}
	}
	return hash;
}

void build_and_validate_cache()
{
	std::array<uint64_t, 5> samples{};
	for (std::size_t repeat = 0; repeat < samples.size(); ++repeat) {
		const auto start = Clock::now();
		auto schedule = build_tep_cache();
		const auto end = Clock::now();
		samples[repeat] = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				end - start
			).count()
		);
		if (repeat + 1 == samples.size())
			tep_cache = std::move(schedule);
	}
	std::sort(samples.begin(), samples.end());
	cache_build_ns = samples[samples.size() / 2];
	assert(mask_hash(tep_cache) == EXPECTED_MASK_HASH);
}

inline void xor_parity_rows(
	const int8_t *matrix,
	int width,
	uint64_t selected_rows,
	int8_t *destination
)
{
	while (selected_rows) {
		const int row = __builtin_ctzll(selected_rows);
		for (int offset = 0; offset < R; ++offset)
			destination[offset] ^=
				matrix[row * width + K + offset];
		selected_rows &= selected_rows - 1;
	}
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
	if (!combined_enabled)
		return false;

	assert(length == N);
	assert(dimension == K);
	assert(order == O);
	assert(!tep_cache.empty());

	std::array<int, K> systematic_delta{};
	int base_systematic_metric = 0;
	for (int index = 0; index < K; ++index) {
		const int base_term =
			(1 - 2 * base[index]) * soft[index];
		base_systematic_metric += base_term;
		systematic_delta[static_cast<std::size_t>(index)] =
			-2 * base_term;
	}

	std::array<int8_t, R> boundary{};
	std::copy(base + K, base + N, boundary.begin());
	uint64_t boundary_mask = 0;

	std::array<std::array<int8_t, R>, P> edge_deltas{};
	std::array<std::array<int8_t, R>, P> parity_candidates{};
	std::array<int8_t, R> prefix{};

	search_stats = {};
	search_stats.cache_bytes = tep_cache.size() * sizeof(uint64_t);
	search_stats.page_payload_bytes =
		P * sizeof(uint64_t) +
		2 * P * R * sizeof(int8_t) +
		2 * R * sizeof(int8_t);
	best = 0;
	next = -1;

	for (std::size_t page_start = 0;
		page_start < tep_cache.size();
		page_start += P) {
		const std::size_t valid = std::min<std::size_t>(
			P,
			tep_cache.size() - page_start
		);
		uint64_t previous_mask = boundary_mask;
		for (std::size_t lane = 0; lane < valid; ++lane) {
			auto &edge = edge_deltas[lane];
			edge.fill(0);
			const uint64_t mask = tep_cache[page_start + lane];
			xor_parity_rows(
				matrix,
				width,
				previous_mask ^ mask,
				edge.data()
			);
			previous_mask = mask;
		}

		prefix.fill(0);
		for (std::size_t lane = 0; lane < valid; ++lane) {
			auto &parity = parity_candidates[lane];
			for (int offset = 0; offset < R; ++offset) {
				prefix[static_cast<std::size_t>(offset)] ^=
					edge_deltas[lane][static_cast<std::size_t>(offset)];
				parity[static_cast<std::size_t>(offset)] =
					boundary[static_cast<std::size_t>(offset)] ^
					prefix[static_cast<std::size_t>(offset)];
			}

			const uint64_t mask = tep_cache[page_start + lane];
			int metric = base_systematic_metric;
			uint64_t selected = mask;
			while (selected) {
				const int index = __builtin_ctzll(selected);
				metric += systematic_delta[
					static_cast<std::size_t>(index)
				];
				selected &= selected - 1;
			}
			for (int offset = 0; offset < R; ++offset)
				metric +=
					(1 - 2 * parity[static_cast<std::size_t>(offset)]) *
					soft[K + offset];

			const bool first = search_stats.candidates == 0;
			if (first || metric > best) {
				if (!first)
					next = best;
				best = metric;
				search_stats.best_mask = mask;
				for (int index = 0; index < K; ++index)
					candidate[index] = base[index] ^
						static_cast<int8_t>((mask >> index) & 1);
				std::copy(parity.begin(), parity.end(), candidate + K);
				for (int index = N; index < width; ++index)
					candidate[index] = 0;
			} else if (metric > next) {
				next = metric;
			}
			++search_stats.candidates;
		}

		boundary_mask = tep_cache[page_start + valid - 1];
		boundary = parity_candidates[valid - 1];
		++search_stats.pages;
		search_stats.last_page_size = valid;
	}

	search_stats.best = best;
	search_stats.next = next;
	assert(search_stats.candidates == EXPECTED_CANDIDATES);
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
	bool use_combined
)
{
	combined_enabled = use_combined;
	profile.reset();
	DecodeResult result;
	const auto start = Clock::now();
	result.unique = decoder(result.decoded.data(), soft.data(), genmat);
	const auto end = Clock::now();
	result.total_ns = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			end - start
		).count()
	);
	result.candidate_ns = profile.elapsed[7];
	result.completion_state = completion;
	return result;
}

double percentile(std::vector<uint64_t> values, double fraction)
{
	std::sort(values.begin(), values.end());
	const std::size_t index = std::max<std::size_t>(
		0,
		static_cast<std::size_t>(
			std::ceil(values.size() * fraction)
		) - 1
	);
	return static_cast<double>(values[index]);
}

double median(std::vector<uint64_t> values)
{
	std::sort(values.begin(), values.end());
	return static_cast<double>(values[values.size() / 2]);
}

struct ModeSummary {
	std::string name;
	double median_total_ns = 0;
	double p95_total_ns = 0;
	double median_candidate_ns = 0;
	double blocks_per_second = 0;
};

}

int main(int argc, char **argv)
{
	using namespace experiment;
	constexpr int FRAME_COUNT = 9;
	constexpr int WARMUP_ROUNDS = 2;
	constexpr int REPEATS = 9;

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
	uint64_t random_state = 0x434f4d42494e4544ULL;
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

	build_and_validate_cache();
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	for (const auto &frame: frames) {
		const DecodeResult baseline =
			decode_once(decoder, frame, genmat, false);
		const DecodeResult combined =
			decode_once(decoder, frame, genmat, true);
		assert(baseline.unique == combined.unique);
		assert(baseline.decoded == combined.decoded);
		assert(
			baseline.completion_state.best ==
			combined.completion_state.best
		);
		assert(
			baseline.completion_state.next ==
			combined.completion_state.next
		);
		assert(
			baseline.completion_state.candidate ==
			combined.completion_state.candidate
		);
		assert(search_stats.best == combined.completion_state.best);
		assert(search_stats.next == combined.completion_state.next);
		assert(search_stats.pages == 84891);
		assert(search_stats.last_page_size == 1);
	}

	volatile uint64_t output_checksum = 0;
	for (int warmup = 0; warmup < WARMUP_ROUNDS; ++warmup) {
		for (const bool use_combined: {false, true}) {
			for (const auto &frame: frames) {
				const DecodeResult result =
					decode_once(decoder, frame, genmat, use_combined);
				output_checksum ^= result.decoded[0];
			}
		}
	}

	std::array<std::vector<uint64_t>, 2> total_samples;
	std::array<std::vector<uint64_t>, 2> candidate_samples;
	std::ofstream raw;
	if (argc > 2) {
		raw.open(argv[2]);
		raw << "repeat,mode,frames,total_ns,ns_per_block,"
			"candidate_ns_per_block,checksum\n";
	}

	for (int repeat = 0; repeat < REPEATS; ++repeat) {
		const std::array<int, 2> order =
			repeat & 1 ? std::array<int, 2>{1, 0}
				: std::array<int, 2>{0, 1};
		for (const int mode: order) {
			uint64_t total_ns = 0;
			uint64_t candidate_ns = 0;
			uint64_t checksum = 0;
			for (const auto &frame: frames) {
				const DecodeResult result =
					decode_once(decoder, frame, genmat, mode == 1);
				total_ns += result.total_ns;
				candidate_ns += result.candidate_ns;
				for (const uint8_t byte: result.decoded)
					checksum = checksum * FNV_PRIME ^ byte;
				checksum ^= static_cast<uint64_t>(result.unique);
			}
			output_checksum ^= checksum;
			const uint64_t total_per_block = total_ns / FRAME_COUNT;
			const uint64_t candidate_per_block =
				candidate_ns / FRAME_COUNT;
			total_samples[static_cast<std::size_t>(mode)].push_back(
				total_per_block
			);
			candidate_samples[static_cast<std::size_t>(mode)].push_back(
				candidate_per_block
			);
			if (raw)
				raw << repeat + 1 << ','
					<< (mode ? "combined" : "baseline") << ','
					<< FRAME_COUNT << ',' << total_ns << ','
					<< total_per_block << ','
					<< candidate_per_block << ','
					<< std::hex << checksum << std::dec << '\n';
		}
	}

	std::array<ModeSummary, 2> summaries;
	for (int mode = 0; mode < 2; ++mode) {
		auto &summary = summaries[static_cast<std::size_t>(mode)];
		summary.name = mode ? "combined" : "baseline";
		summary.median_total_ns = median(total_samples[mode]);
		summary.p95_total_ns = percentile(total_samples[mode], 0.95);
		summary.median_candidate_ns = median(candidate_samples[mode]);
		summary.blocks_per_second = 1e9 / summary.median_total_ns;
	}
	const double total_speedup =
		summaries[0].median_total_ns / summaries[1].median_total_ns;
	const double candidate_speedup =
		summaries[0].median_candidate_ns /
		summaries[1].median_candidate_ns;

	if (argc > 1) {
		std::ofstream summary(argv[1]);
		summary << "mode,frames_per_repeat,repeats,median_total_ns,"
			"p95_total_ns,median_candidate_ns,blocks_per_second,"
			"speedup_vs_baseline,candidates,page_width,pages,"
			"cache_bytes,page_payload_bytes,cache_build_ns,result\n";
		for (int mode = 0; mode < 2; ++mode) {
			const auto &value = summaries[static_cast<std::size_t>(mode)];
			summary << value.name << ',' << FRAME_COUNT << ',' << REPEATS
				<< ',' << static_cast<uint64_t>(value.median_total_ns)
				<< ',' << static_cast<uint64_t>(value.p95_total_ns)
				<< ',' << static_cast<uint64_t>(value.median_candidate_ns)
				<< ',' << std::fixed << std::setprecision(6)
				<< value.blocks_per_second << ','
				<< (mode ? total_speedup : 1.0) << ','
				<< EXPECTED_CANDIDATES << ','
				<< (mode ? P : 0) << ','
				<< (mode ? search_stats.pages : 0) << ','
				<< (mode ? search_stats.cache_bytes : 0) << ','
				<< (mode ? search_stats.page_payload_bytes : 0) << ','
				<< (mode ? cache_build_ns : 0) << ",pass\n";
		}
	}

	std::cout << "COMBINED_PASS "
		<< "frames=" << FRAME_COUNT << ' '
		<< "candidates=" << search_stats.candidates << ' '
		<< "pages=" << search_stats.pages << ' '
		<< "last_page=" << search_stats.last_page_size << ' '
		<< "cache_bytes=" << search_stats.cache_bytes << ' '
		<< "page_payload_bytes=" << search_stats.page_payload_bytes << ' '
		<< "cache_build_ns=" << cache_build_ns << '\n';
	for (const auto &summary: summaries)
		std::cout << "COMBINED_BENCH " << summary.name << ' '
			<< "median_total_ns="
			<< static_cast<uint64_t>(summary.median_total_ns) << ' '
			<< "p95_total_ns="
			<< static_cast<uint64_t>(summary.p95_total_ns) << ' '
			<< "median_candidate_ns="
			<< static_cast<uint64_t>(summary.median_candidate_ns) << ' '
			<< "blocks_per_second=" << std::fixed
			<< std::setprecision(3) << summary.blocks_per_second << '\n';
	std::cout << "COMBINED_SPEEDUP "
		<< "overall=" << std::fixed << std::setprecision(5)
		<< total_speedup << ' '
		<< "candidate=" << candidate_speedup << ' '
		<< "checksum=" << std::hex << output_checksum << std::dec << '\n';
	return 0;
}
