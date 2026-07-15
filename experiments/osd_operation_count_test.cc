#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {

struct OperationCounts {
	uint64_t flip_calls = 0;
	uint64_t flip_xor_terms = 0;
	uint64_t metric_calls = 0;
	uint64_t metric_terms = 0;
	uint64_t update_calls = 0;
	std::array<uint64_t, 3> update_outcomes = {};
};

OperationCounts counts;

void profile_flip(int width)
{
	++counts.flip_calls;
	counts.flip_xor_terms += static_cast<uint64_t>(width);
}

void profile_metric(int width)
{
	++counts.metric_calls;
	counts.metric_terms += static_cast<uint64_t>(width);
}

void profile_update(int outcome)
{
	assert(outcome >= 0 && outcome < 3);
	++counts.update_calls;
	++counts.update_outcomes[static_cast<std::size_t>(outcome)];
}

}

#define CODE_OSD_PROFILE_FLIP(width) profile_flip(width)
#define CODE_OSD_PROFILE_METRIC(width) profile_metric(width)
#define CODE_OSD_PROFILE_UPDATE(outcome) profile_update(outcome)

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

uint64_t candidate_count(int k, int order)
{
	uint64_t result = 0;
	for (int weight = 0; weight <= order; ++weight)
		result += choose(k, weight);
	return result;
}

template <int N, int K, int O>
void run_case(const std::initializer_list<int> minimal_polynomials)
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

	counts = {};
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	const bool unique = decoder(decoded, soft, genmat);
	assert(unique);

	const uint64_t candidates = candidate_count(K, O);
	const uint64_t width =
		static_cast<uint64_t>((N + sizeof(std::size_t) - 1) &
			~(sizeof(std::size_t) - 1));
	assert(counts.metric_calls == candidates);
	assert(counts.update_calls == candidates - 1);
	assert(counts.flip_calls == 2 * (candidates - 1));
	assert(counts.metric_terms == candidates * width);
	assert(counts.flip_xor_terms == counts.flip_calls * width);
	assert(
		counts.update_outcomes[0] +
		counts.update_outcomes[1] +
		counts.update_outcomes[2] == counts.update_calls
	);

	std::cout << "COUNTS "
		<< K << ' '
		<< O << ' '
		<< candidates << ' '
		<< counts.metric_calls << ' '
		<< counts.metric_terms << ' '
		<< counts.flip_calls << ' '
		<< counts.flip_xor_terms << ' '
		<< counts.update_outcomes[0] << ' '
		<< counts.update_outcomes[1] << ' '
		<< counts.update_outcomes[2] << '\n';
}

}

int main()
{
	run_case<15, 5, 3>({0b10011, 0b11111, 0b00111});
	run_case<127, 64, 4>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011
	});
	return 0;
}
