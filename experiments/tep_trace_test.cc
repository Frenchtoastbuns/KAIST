#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

struct TraceState {
	uint64_t mask = 0;
	uint64_t candidate_count = 0;
	uint64_t mask_hash = FNV_OFFSET;
	uint64_t candidate_hash = FNV_OFFSET;
};

TraceState trace;
bool capture_masks = false;
std::vector<uint64_t> captured_masks;

void mix_byte(uint64_t &hash, uint8_t value)
{
	hash ^= value;
	hash *= FNV_PRIME;
}

void mix_u64(uint64_t &hash, uint64_t value)
{
	for (int byte = 0; byte < 8; ++byte)
		mix_byte(hash, static_cast<uint8_t>(value >> (8 * byte)));
}

void osd_trace_reset()
{
	trace = {};
	trace.mask_hash = FNV_OFFSET;
	trace.candidate_hash = FNV_OFFSET;
	captured_masks.clear();
}

void osd_trace_flip(int index)
{
	assert(index >= 0);
	assert(index < 64);
	trace.mask ^= uint64_t{1} << index;
}

void osd_trace_candidate(
	const int8_t *hard,
	const int8_t *soft,
	int length,
	int,
	int metric
)
{
	int recomputed = 0;
	for (int i = 0; i < length; ++i)
		recomputed += (1 - 2 * hard[i]) * soft[i];
	assert(recomputed == metric);

	++trace.candidate_count;
	if (capture_masks)
		captured_masks.push_back(trace.mask);
	mix_u64(trace.mask_hash, trace.mask);
	mix_u64(trace.candidate_hash, trace.mask);
	for (int i = 0; i < length; ++i)
		mix_byte(trace.candidate_hash, static_cast<uint8_t>(hard[i]));
	mix_u64(trace.candidate_hash, static_cast<uint32_t>(metric));
}

}

#define CODE_OSD_TRACE_RESET() osd_trace_reset()
#define CODE_OSD_TRACE_FLIP(index) osd_trace_flip(index)
#define CODE_OSD_TRACE_CANDIDATE(hard, soft, length, width, value) \
	osd_trace_candidate(hard, soft, length, width, value)

#include "bitman.hh"
#include "osd.hh"

namespace {

uint64_t choose(int n, int r)
{
	if (r < 0 || r > n)
		return 0;
	if (r > n - r)
		r = n - r;
	uint64_t result = 1;
	for (int i = 1; i <= r; ++i)
		result = result * static_cast<uint64_t>(n - r + i) /
			static_cast<uint64_t>(i);
	return result;
}

uint64_t expected_count(int k, int order)
{
	uint64_t count = 0;
	for (int weight = 0; weight <= order; ++weight)
		count += choose(k, weight);
	return count;
}

template <int N, int K, int O>
TraceState run_case(
	const std::initializer_list<int> minimal_polynomials
)
{
	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		minimal_polynomials
	);

	int8_t soft[N];
	for (int i = 0; i < N; ++i)
		soft[i] = 64;

	uint8_t decoded[(N + 7) / 8] = {};
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	capture_masks = K <= 8;
	const bool unique = decoder(decoded, soft, genmat);

	assert(unique);
	assert(trace.mask == 0);
	assert(trace.candidate_count == expected_count(K, O));
	for (int i = 0; i < N; ++i)
		assert(!CODE::get_be_bit(decoded, i));

	return trace;
}

void print_summary(int k, int order, const TraceState &summary)
{
	std::cout << "TRACE "
		<< k << ' '
		<< order << ' '
		<< summary.candidate_count << ' '
		<< std::hex << std::setw(16) << std::setfill('0')
		<< summary.mask_hash << ' '
		<< std::setw(16) << summary.candidate_hash
		<< std::dec << '\n';
}

}

int main()
{
	const auto small = run_case<15, 5, 3>(
		{0b10011, 0b11111, 0b00111}
	);
	const auto small_masks = captured_masks;
	print_summary(5, 3, small);
	std::cout << "MASKS 5 3";
	for (const uint64_t mask: small_masks)
		std::cout << ' ' << std::hex << mask;
	std::cout << std::dec << '\n';

	const auto production = run_case<127, 64, 4>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011
	});
	print_summary(64, 4, production);

	return 0;
}
