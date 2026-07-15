#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {

struct ParityModel {
	const int8_t *matrix = nullptr;
	const int8_t *soft = nullptr;
	int length = 0;
	int dimension = 0;
	int width = 0;
	uint64_t mask = 0;
	std::vector<int8_t> base;
	std::vector<int8_t> parity;
	std::vector<int16_t> permutation;
	int systematic_metric = 0;
	uint64_t candidates = 0;
	uint64_t flips = 0;
	uint64_t parity_metric_terms = 0;
	uint64_t parity_flip_terms = 0;
	int best = std::numeric_limits<int>::min();
	int next = -1;
	uint64_t best_mask = 0;
	std::vector<int8_t> best_parity;
};

ParityModel model;

void parity_ready(
	const int8_t *matrix,
	const int8_t *hard,
	const int8_t *soft,
	const int16_t *permutation,
	int length,
	int dimension,
	int width
)
{
	assert(dimension <= 64);
	model = {};
	model.matrix = matrix;
	model.soft = soft;
	model.length = length;
	model.dimension = dimension;
	model.width = width;
	model.next = -1;
	model.base.assign(hard, hard + length);
	model.parity.assign(hard + dimension, hard + length);
	model.permutation.assign(permutation, permutation + length);
	model.best_parity = model.parity;
	for (int i = 0; i < dimension; ++i)
		model.systematic_metric += (1 - 2 * hard[i]) * soft[i];
}

void parity_flip(int index)
{
	assert(index >= 0 && index < model.dimension);
	const bool old_tep_bit = (model.mask >> index) & 1;
	const int old_bit = model.base[static_cast<std::size_t>(index)] ^
		static_cast<int>(old_tep_bit);
	const int old_term = (1 - 2 * old_bit) * model.soft[index];
	model.systematic_metric -= 2 * old_term;
	model.mask ^= uint64_t{1} << index;

	for (int i = model.dimension; i < model.length; ++i)
		model.parity[static_cast<std::size_t>(i - model.dimension)] ^=
			model.matrix[model.width * index + i];
	++model.flips;
	model.parity_flip_terms +=
		static_cast<uint64_t>(model.length - model.dimension);
}

void parity_candidate(
	const int8_t *hard,
	const int8_t *,
	int length,
	int,
	int metric
)
{
	assert(length == model.length);
	int parity_metric = 0;
	for (int i = model.dimension; i < model.length; ++i) {
		const int8_t parity_bit =
			model.parity[static_cast<std::size_t>(i - model.dimension)];
		assert(hard[i] == parity_bit);
		parity_metric += (1 - 2 * parity_bit) * model.soft[i];
	}
	model.parity_metric_terms +=
		static_cast<uint64_t>(model.length - model.dimension);

	for (int i = 0; i < model.dimension; ++i) {
		const int expected = model.base[static_cast<std::size_t>(i)] ^
			static_cast<int>((model.mask >> i) & 1);
		assert(hard[i] == expected);
	}

	const int decomposed_metric = model.systematic_metric + parity_metric;
	assert(decomposed_metric == metric);

	if (!model.candidates) {
		model.best = metric;
		model.best_mask = model.mask;
		model.best_parity = model.parity;
	} else if (metric > model.best) {
		model.next = model.best;
		model.best = metric;
		model.best_mask = model.mask;
		model.best_parity = model.parity;
	} else if (metric > model.next) {
		model.next = metric;
	}
	++model.candidates;
}

}

#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) \
	parity_ready(matrix, hard, soft, permutation, length, dimension, width)
#define CODE_OSD_TRACE_FLIP(index) parity_flip(index)
#define CODE_OSD_TRACE_CANDIDATE(hard, soft, length, width, value) \
	parity_candidate(hard, soft, length, width, value)

#include "bitman.hh"
#include "osd.hh"

namespace {

uint64_t next_random(uint64_t &state)
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

template <int N, int K, int O>
void check_decoded(
	const uint8_t *decoded,
	bool unique
)
{
	assert(model.mask == 0);
	assert(model.parity.size() == model.base.size() - K);
	for (int i = K; i < N; ++i)
		assert(
			model.parity[static_cast<std::size_t>(i - K)] ==
			model.base[static_cast<std::size_t>(i)]
		);
	assert(unique == (model.best != model.next));

	std::vector<int8_t> expected(N);
	for (int i = 0; i < K; ++i)
		expected[static_cast<std::size_t>(i)] =
			model.base[static_cast<std::size_t>(i)] ^
			static_cast<int>((model.best_mask >> i) & 1);
	for (int i = K; i < N; ++i)
		expected[static_cast<std::size_t>(i)] =
			model.best_parity[static_cast<std::size_t>(i - K)];

	for (int i = 0; i < N; ++i) {
		const int original_index =
			model.permutation[static_cast<std::size_t>(i)];
		assert(
			CODE::get_be_bit(decoded, original_index) ==
			expected[static_cast<std::size_t>(i)]
		);
	}
}

template <int N, int K, int O>
void run_exhaustive_signs(
	const std::initializer_list<int> minimal_polynomials
)
{
	static_assert(N < 20, "exhaustive sign test is only for small codes");
	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		minimal_polynomials
	);
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;

	const uint64_t patterns = uint64_t{1} << N;
	for (uint64_t pattern = 0; pattern < patterns; ++pattern) {
		int8_t soft[N];
		for (int i = 0; i < N; ++i) {
			const int magnitude = i + 1;
			soft[i] = (pattern >> i) & 1
				? static_cast<int8_t>(-magnitude)
				: static_cast<int8_t>(magnitude);
		}
		uint8_t decoded[(N + 7) / 8] = {};
		const bool unique = decoder(decoded, soft, genmat);
		check_decoded<N, K, O>(decoded, unique);
	}
}

template <int N, int K, int O>
void run_random_frames(
	const std::initializer_list<int> minimal_polynomials,
	int frames,
	uint64_t seed
)
{
	int8_t genmat[N * K];
	CODE::BoseChaudhuriHocquenghemGenerator<N, K>::matrix(
		genmat,
		true,
		minimal_polynomials
	);
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	uint64_t random_state = seed;

	for (int frame = 0; frame < frames; ++frame) {
		int8_t soft[N];
		for (int i = 0; i < N; ++i) {
			const uint64_t value = next_random(random_state);
			const int magnitude = 1 + static_cast<int>(value % 127);
			soft[i] = value & (uint64_t{1} << 63)
				? static_cast<int8_t>(-magnitude)
				: static_cast<int8_t>(magnitude);
		}
		uint8_t decoded[(N + 7) / 8] = {};
		const bool unique = decoder(decoded, soft, genmat);
		check_decoded<N, K, O>(decoded, unique);
	}
}

}

int main()
{
	run_exhaustive_signs<15, 5, 3>(
		{0b10011, 0b11111, 0b00111}
	);

	run_random_frames<127, 64, 4>(
		{
			0b10001001, 0b10001111, 0b10011101,
			0b11110111, 0b10111111, 0b11010101,
			0b10000011, 0b11101111, 0b11001011
		},
		3,
		0x5041524954595f44ULL
	);

	std::cout << "PARITY_DELTA_PASS "
		<< "candidates=" << model.candidates << ' '
		<< "flips=" << model.flips << ' '
		<< "parity_metric_terms=" << model.parity_metric_terms << ' '
		<< "parity_flip_terms=" << model.parity_flip_terms << '\n';
	return 0;
}
