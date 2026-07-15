#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {

bool padding_checked = false;

void check_padding(
	const int8_t *matrix,
	const int8_t *hard,
	const int8_t *soft,
	const int16_t *,
	int length,
	int dimension,
	int width
)
{
	assert(width >= length);
	for (int row = 0; row < dimension; ++row)
		for (int i = length; i < width; ++i)
			assert(matrix[width * row + i] == 0);
	for (int i = length; i < width; ++i) {
		assert(hard[i] == 0);
		assert(soft[i] == 0);
	}
	padding_checked = true;
}

}

#define CODE_OSD_TRACE_READY(matrix, hard, soft, permutation, length, dimension, width) \
	check_padding(matrix, hard, soft, permutation, length, dimension, width)

#include "bitman.hh"
#include "osd.hh"

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
	CODE::OrderedStatisticsDecoder<N, K, O> decoder;
	padding_checked = false;
	decoder(decoded, soft, genmat);
	assert(padding_checked);
}

int main()
{
	run_case<15, 5, 1>({0b10011, 0b11111, 0b00111});
	run_case<127, 64, 1>({
		0b10001001, 0b10001111, 0b10011101,
		0b11110111, 0b10111111, 0b11010101,
		0b10000011, 0b11101111, 0b11001011
	});
	std::cout << "PADDING_STATE_PASS\n";
	return 0;
}
